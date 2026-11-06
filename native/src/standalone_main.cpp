#include "atlas_build_identity.hpp"
#include "json_text.hpp"
#include "standalone_bridge.hpp"
#include "standalone_support.hpp"
#include <atlas/playback.hpp>
#include <atlas/ui_host.hpp>
#include <atlas/voices.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>

namespace {
namespace fs = std::filesystem;
using atlas::standalone::path;
using atlas::standalone::string;
using Clock = std::chrono::steady_clock;
struct Launch {
    fs::path state, library, smokeReceipt, smokeScene, smokeExport;
    int smokeSeconds{3};
    bool libraryOverride{};
};
Launch parse(const juce::StringArray &arguments) {
    Launch launch;
    launch.state = path(juce::File::getSpecialLocation(
                            juce::File::userApplicationDataDirectory)
                            .getFullPathName()) /
                   "Application Support" / "Animal Atlas";
    for (int i = 0; i < arguments.size(); ++i) {
        const auto option = arguments[i];
        if (i + 1 == arguments.size())
            throw std::invalid_argument("A launch option requires a value.");
        const auto value = arguments[++i];
        if (option == "--state-dir")
            launch.state = path(value);
        else if (option == "--library") {
            launch.library = path(value);
            launch.libraryOverride = true;
        } else if (option == "--smoke-test")
            launch.smokeReceipt = path(value);
        else if (option == "--smoke-scene")
            launch.smokeScene = path(value);
        else if (option == "--smoke-export")
            launch.smokeExport = path(value);
        else if (option == "--smoke-seconds") {
            if (!value.containsOnly("0123456789") || value.isEmpty())
                throw std::invalid_argument(
                    "Smoke seconds must be an integer.");
            launch.smokeSeconds = value.getIntValue();
            if (launch.smokeSeconds < 1 || launch.smokeSeconds > 600)
                throw std::invalid_argument(
                    "Smoke duration is outside supported bounds.");
        } else
            throw std::invalid_argument("Unknown launch option.");
    }
    if (!launch.state.is_absolute() ||
        (!launch.library.empty() && !launch.library.is_absolute()) ||
        (!launch.smokeReceipt.empty() && !launch.smokeReceipt.is_absolute()))
        throw std::invalid_argument("Application paths must be absolute.");
    if ((!launch.smokeScene.empty() || !launch.smokeExport.empty()) &&
        launch.smokeReceipt.empty())
        throw std::invalid_argument(
            "Smoke scene/export requires --smoke-test.");
    return launch;
}
atlas::AudioDeviceConfiguration configuration(const juce::var &json) {
    atlas::AudioDeviceConfiguration result;
    if (!json.isVoid()) {
        result.mode = json["mode"].toString() == "explicitDevice"
                          ? atlas::AudioDeviceMode::explicitDevice
                          : atlas::AudioDeviceMode::systemDefault;
        result.deviceId = json["deviceId"].toString().toStdString();
        result.bufferFrames = static_cast<int>(json["bufferFrames"]);
    }
    return result;
}
juce::var configurationJson(const atlas::AudioDeviceConfiguration &value) {
    juce::DynamicObject::Ptr json = new juce::DynamicObject;
    json->setProperty("mode",
                      value.mode == atlas::AudioDeviceMode::systemDefault
                          ? "systemDefault"
                          : "explicitDevice");
    json->setProperty("deviceId", value.deviceId.empty()
                                      ? juce::var()
                                      : juce::var(value.deviceId));
    json->setProperty("bufferFrames", value.bufferFrames);
    return json.get();
}
std::string failed(std::string_view envelope, std::string_view message) {
    const auto json = juce::JSON::parse(juce::String::fromUTF8(
        envelope.data(), static_cast<int>(envelope.size())));
    return "{\"requestId\":" +
           atlas::detail::quoteJson(
               json["requestId"].toString().toStdString()) +
           ",\"ok\":false,\"error\":{\"code\":\"unavailable\",\"message\":" +
           atlas::detail::quoteJson(message) + "}}";
}
class Shell;
class Browser final : public juce::WebBrowserComponent {
  public:
    explicit Browser(const Options &options) : WebBrowserComponent(options) {}
    std::function<void()> loaded;
    bool pageAboutToLoad(const juce::String &url) override {
        // Only bundled resources may navigate with the native bridge enabled.
        return url.startsWith(getResourceProviderRoot());
    }
    void newWindowAttemptingToLoad(const juce::String &) override {}
    void pageFinishedLoading(const juce::String &) override {
        if (loaded)
            loaded();
    }
};
struct Work {
    enum Kind { request, library, scene, exportWav } kind{};
    juce::int64 token{};
    std::string text;
    fs::path file;
    unsigned epoch{};
};
class Shell final : public juce::Component, private juce::Timer {
  public:
    Shell(Launch launch, std::function<void()> finished)
        : launch_(std::move(launch)), finished_(std::move(finished)) {
        for (auto *button : {&choose_, &sceneOnly_, &import_, &reveal_})
            addAndMakeVisible(button);
        addAndMakeVisible(status_);
        status_.setText("Opening the local instrument…",
                        juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, juce::Colour(0xffe8e5dd));
        choose_.onClick = [this] { chooseFile(false); };
        sceneOnly_.onClick = [this] {
            submitControl({Work::library, 0, {}, {}});
        };
        import_.onClick = [this] { chooseFile(true); };
        reveal_.onClick = [this] {
            juce::File(string(launch_.state)).revealToUser();
        };
        setSize(1400, 920);
        auto safe = juce::Component::SafePointer<Shell>(this);
        worker_ = std::thread([this, safe] { run(safe); });
        if (!launch_.smokeReceipt.empty()) {
            smokeStarted_ = Clock::now();
            startTimer(250);
        }
    }
    ~Shell() override {
        if (worker_.joinable())
            worker_.join();
    }
    void resized() override {
        auto bounds = getLocalBounds();
        auto toolbar = bounds.removeFromTop(46).reduced(8, 6);
        choose_.setBounds(toolbar.removeFromLeft(145));
        toolbar.removeFromLeft(6);
        sceneOnly_.setBounds(toolbar.removeFromLeft(105));
        toolbar.removeFromLeft(6);
        import_.setBounds(toolbar.removeFromLeft(140));
        toolbar.removeFromLeft(6);
        reveal_.setBounds(toolbar.removeFromLeft(130));
        toolbar.removeFromLeft(8);
        status_.setBounds(toolbar);
        if (browser_)
            browser_->setBounds(bounds);
    }
    void paint(juce::Graphics &g) override {
        g.fillAll(juce::Colour(0xff282c2b));
    }
    void close() {
        if (closing_.exchange(true, std::memory_order_acq_rel))
            return;
        setControls(false);
        if (launch_.smokeReceipt.empty())
            status_.setText("Closing audio and saving application state…",
                            juce::dontSendNotification);
        {
            const std::lock_guard lock(mutex_);
            queue_.clear();
        }
        condition_.notify_one();
    }
    bool done() const noexcept { return done_.load(std::memory_order_acquire); }

  private:
    Launch launch_;
    std::function<void()> finished_;
    juce::TextButton choose_{"Choose wildlife library"},
        sceneOnly_{"Scene only"}, import_{"Import soundscape"},
        reveal_{"Show app data"};
    juce::Label status_;
    std::unique_ptr<Browser> browser_;
    std::unique_ptr<juce::FileChooser> chooser_;
    atlas::standalone::Assets assets_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Work> queue_;
    std::atomic<bool> closing_{}, done_{};
    std::atomic<unsigned> requests_{}, rejected_{};
    bool ready_{}, smokeLaunched_{}, smokeEvaluating_{}, smokeComplete_{},
        smokeExported_{}, finishedOnce_{};
    juce::var smokeResult_;
    Clock::time_point smokeStarted_, frontendStarted_;
    std::string stateNotice_;
    unsigned browserEpoch_{};
    void setControls(bool enabled) {
        choose_.setEnabled(enabled);
        sceneOnly_.setEnabled(enabled);
        import_.setEnabled(enabled);
        reveal_.setEnabled(enabled);
    }
    bool enqueue(Work work) {
        const std::lock_guard lock(mutex_);
        if (closing_.load(std::memory_order_acquire) || queue_.size() >= 16) {
            ++rejected_;
            return false;
        }
        queue_.push_back(std::move(work));
        condition_.notify_one();
        return true;
    }
    void submitControl(Work work) {
        if (!ready_)
            return;
        if (enqueue(std::move(work))) {
            ready_ = false;
            setControls(false);
            status_.setText("Updating local instrument…",
                            juce::dontSendNotification);
        } else
            status_.setText("The request queue is full. Try again.",
                            juce::dontSendNotification);
    }
    void chooseFile(bool scene) {
        chooser_ = std::make_unique<juce::FileChooser>(
            scene ? "Import a verified Animal Atlas soundscape"
                  : "Choose a wildlife library (read only)",
            juce::File(), scene ? "*.atlas-scene" : "*.sqlite3;*.sqlite;*.db");
        auto safe = juce::Component::SafePointer<Shell>(this);
        chooser_->launchAsync(
            juce::FileBrowserComponent::openMode |
                juce::FileBrowserComponent::canSelectFiles,
            [safe, scene](const juce::FileChooser &chooser) {
                if (safe && chooser.getResult() != juce::File())
                    safe->submitControl(
                        {scene ? Work::scene : Work::library,
                         0,
                         {},
                         path(chooser.getResult().getFullPathName())});
            });
    }
    void submitExport(const std::string &id, const fs::path &file) {
        if (enqueue({Work::exportWav, 0, id, file}))
            status_.setText("Saving rendered WAV…", juce::dontSendNotification);
        else
            status_.setText("The request queue is full. Try again.",
                            juce::dontSendNotification);
    }
    void exportFile(const std::string &id) {
        if (!ready_ || !atlas::standalone::opaqueId(id))
            return;
        if (!launch_.smokeReceipt.empty() && !launch_.smokeExport.empty()) {
            submitExport(id, launch_.smokeExport);
            return;
        }
        chooser_ = std::make_unique<juce::FileChooser>(
            "Save rendered WAV to a new file",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("Animal Atlas.wav"),
            "*.wav");
        auto safe = juce::Component::SafePointer<Shell>(this);
        chooser_->launchAsync(
            juce::FileBrowserComponent::saveMode |
                juce::FileBrowserComponent::canSelectFiles |
                juce::FileBrowserComponent::warnAboutOverwriting,
            [safe, id](const juce::FileChooser &chooser) {
                if (safe && chooser.getResult() != juce::File())
                    safe->submitExport(
                        id, path(chooser.getResult().getFullPathName()));
            });
    }
    void reply(juce::int64 token, const std::string &json) {
        if (!browser_ || closing_.load(std::memory_order_acquire))
            return;
        juce::DynamicObject::Ptr event = new juce::DynamicObject;
        event->setProperty("token", token);
        event->setProperty("json", juce::String(json));
        browser_->emitEventIfBrowserIsVisible("atlasReply", event.get());
    }
    void createBrowser() {
        if (closing_.load(std::memory_order_acquire))
            return;
        browser_.reset();
        ++browserEpoch_;
        const auto options =
            Browser::Options()
                .withNativeIntegrationEnabled()
                .withUserScript(juce::String::fromUTF8(
                    atlas::standalone::bridgeScript.data(),
                    static_cast<int>(atlas::standalone::bridgeScript.size())))
                .withEventListener(
                    "atlasRequest",
                    [this](juce::var event) {
                        if (!event["json"].isString() ||
                            event["json"].toString().getNumBytesAsUTF8() >
                                16384)
                            return;
                        const auto text =
                            event["json"].toString().toStdString();
                        const auto token =
                            static_cast<juce::int64>(event["token"]);
                        if (!ready_ || token <= 0 ||
                            !enqueue({Work::request,
                                      token,
                                      text,
                                      {},
                                      browserEpoch_}))
                            reply(token,
                                  failed(text,
                                         "Native request was not admitted."));
                    })
                .withEventListener(
                    "atlasExport",
                    [this](juce::var event) {
                        exportFile(
                            event["artifactId"].toString().toStdString());
                    })
                .withResourceProvider([this](const juce::String &requested)
                                          -> std::optional<Browser::Resource> {
                    const auto key = requested == "/"
                                         ? std::string("/index.html")
                                         : requested.toStdString();
                    const auto found = assets_.find(key);
                    if (found == assets_.end())
                        return std::nullopt;
                    return found->second;
                });
        browser_ = std::make_unique<Browser>(options);
        addAndMakeVisible(*browser_);
        browser_->loaded = [this] {
            frontendStarted_ = Clock::now();
            smokeLaunched_ = false;
        };
        resized();
        browser_->goToURL(Browser::getResourceProviderRoot());
    }
    void updateStatus(const std::string &message, bool reload) {
        if (closing_.load(std::memory_order_acquire))
            return;
        ready_ = true;
        setControls(true);
        status_.setText(juce::String(message + stateNotice_),
                        juce::dontSendNotification);
        status_.setTooltip(juce::String(message + stateNotice_));
        if (reload)
            createBrowser();
    }
    void run(juce::Component::SafePointer<Shell> safe) {
        const auto post = [safe](auto callback) {
            juce::MessageManager::callAsync(
                [safe, callback = std::move(callback)]() mutable {
                    if (safe)
                        callback(*safe);
                });
        };
        try {
            const auto resources =
                path(juce::File::getSpecialLocation(
                         juce::File::currentApplicationFile)
                         .getChildFile("Contents/Resources/ui")
                         .getFullPathName());
            auto assets = atlas::standalone::loadAssets(resources);
            atlas::standalone::prepareStateDirectory(launch_.state);
            const auto lockName =
                "animal-atlas-state-" +
                juce::String::toHexString(
                    string(fs::canonical(launch_.state)).hashCode64());
            juce::InterProcessLock stateLock(lockName);
            if (!stateLock.enter(0))
                throw std::runtime_error(
                    "This application data directory is already open in "
                    "another Animal Atlas process.");
            atlas::standalone::Settings settings;
            bool settingsWritable{true};
            std::string notice;
            try {
                settings = atlas::standalone::loadSettings(launch_.state);
            } catch (const std::exception &error) {
                settingsWritable = false;
                notice = std::string(" ") + error.what();
            }
            if (launch_.libraryOverride)
                settings.library = launch_.library;
            atlas::PlaybackRuntime playback(atlas::makeJuceOutputDriver(),
                                            configuration(settings.output));
            atlas::VoiceStore voices(launch_.state / "voices.sqlite3");
            atlas::UiHostStorage storage{launch_.state / "soundscapes",
                                         launch_.state / "exports",
                                         atlas::nativeSourceSha256};
            const auto makeHost = [&](const fs::path &library) {
                if (!library.empty() &&
                    !fs::is_regular_file(fs::symlink_status(library)))
                    throw std::runtime_error(
                        "Wildlife library is unavailable; saved path retained. "
                        "Scene only is available.");
                return std::make_unique<atlas::UiHost>(library, &playback,
                                                       &voices, storage);
            };
            std::unique_ptr<atlas::UiHost> host;
            std::string initial =
                settings.library.empty()
                    ? "Scene only · import a soundscape to begin"
                    : "Library: " + settings.library.filename().string();
            try {
                host = makeHost(settings.library);
            } catch (const std::exception &error) {
                initial = error.what();
                host = makeHost({});
            }
            if (!launch_.smokeScene.empty()) {
                atlas::standalone::importScene(launch_.smokeScene,
                                               storage.soundscapes);
                initial = "Smoke soundscape imported; scene only";
            }
            post([assets = std::move(assets), initial,
                  notice](Shell &shell) mutable {
                shell.assets_ = std::move(assets);
                shell.stateNotice_ = notice;
                shell.updateStatus(initial, true);
            });
            unsigned expectedPageEpoch = 1;
            while (!closing_.load(std::memory_order_acquire)) {
                playback.service();
                // This in-process owner remains alive while WKWebView timers
                // are throttled or the window is hidden. Service first keeps
                // a genuine worker stall/fault observable before renewing.
                playback.heartbeat();
                std::optional<Work> work;
                {
                    std::unique_lock lock(mutex_);
                    condition_.wait_for(
                        lock, std::chrono::milliseconds(100), [this] {
                            return closing_.load(std::memory_order_acquire) ||
                                   !queue_.empty();
                        });
                    if (closing_.load(std::memory_order_acquire))
                        break;
                    if (!queue_.empty()) {
                        work = std::move(queue_.front());
                        queue_.pop_front();
                    }
                }
                if (!work)
                    continue;
                if (work->kind == Work::request) {
                    // A retired page must not mutate its replacement host.
                    if (work->epoch != expectedPageEpoch)
                        continue;
                    ++requests_;
                    auto response = host->request(work->text);
                    if (response.size() > 8 * 1024 * 1024)
                        response = failed(work->text,
                                          "Native reply exceeds capacity.");
                    const auto request =
                        juce::JSON::parse(juce::String(work->text));
                    const auto reply =
                        juce::JSON::parse(juce::String(response));
                    if (request["method"].toString() == "setAudioDevice" &&
                        static_cast<bool>(reply["ok"])) {
                        settings.output =
                            configurationJson(playback.outputConfiguration());
                        if (settingsWritable) {
                            try {
                                atlas::standalone::saveSettings(launch_.state,
                                                                settings);
                            } catch (const std::exception &error) {
                                post([message = std::string(error.what())](
                                         Shell &shell) {
                                    shell.updateStatus(message, false);
                                });
                            }
                        }
                    }
                    post([token = work->token, epoch = work->epoch,
                          response = std::move(response)](Shell &shell) {
                        if (epoch == shell.browserEpoch_)
                            shell.reply(token, response);
                    });
                    continue;
                }
                std::string message;
                bool reload{};
                try {
                    if (work->kind == Work::library) {
                        auto next = makeHost(work->file);
                        playback.enable(false);
                        host = std::move(next);
                        settings.library = work->file;
                        reload = true;
                        if (settingsWritable)
                            atlas::standalone::saveSettings(launch_.state,
                                                            settings);
                        message =
                            work->file.empty()
                                ? "Scene only · import a soundscape to begin"
                                : "Library: " + work->file.filename().string();
                        reload = true;
                    } else if (work->kind == Work::scene) {
                        message = "Imported “" +
                                  atlas::standalone::importScene(
                                      work->file, storage.soundscapes) +
                                  "” · choose it in Soundscapes";
                        playback.enable(false);
                        reload = true;
                    } else {
                        atlas::standalone::copyExport(storage.exports,
                                                      work->text, work->file);
                        message =
                            "WAV saved: " + work->file.filename().string();
                        post([](Shell &shell) { shell.smokeExported_ = true; });
                    }
                } catch (const std::exception &error) {
                    message = error.what();
                }
                if (reload)
                    ++expectedPageEpoch;
                post([message, reload](Shell &shell) {
                    shell.updateStatus(message, reload);
                });
            }
            // UiHost may own an offline renderer. Retire it, then stop the
            // audio runtime while the JUCE message loop is still servicing
            // driver calls.
            host.reset();
            playback.enable(false);
        } catch (const std::exception &error) {
            post([message = std::string(error.what())](Shell &shell) {
                shell.status_.setText(
                    juce::String("Instrument unavailable: " + message),
                    juce::dontSendNotification);
                shell.smokeResult_ = juce::JSON::parse(
                    "{\"ok\":false,\"error\":" +
                    juce::JSON::toString(juce::var(message)) + "}");
                if (!shell.launch_.smokeReceipt.empty())
                    shell.close();
            });
            // A fatal startup failure remains visible in a normal launch, with
            // no live runtime. A close request can quit immediately after done.
        }
        done_.store(true, std::memory_order_release);
        post([](Shell &shell) {
            if (shell.closing_.load(std::memory_order_acquire))
                shell.finish();
        });
    }
    void finish() {
        if (finishedOnce_)
            return;
        finishedOnce_ = true;
        stopTimer();
        if (!launch_.smokeReceipt.empty()) {
            juce::DynamicObject::Ptr receipt = new juce::DynamicObject;
            receipt->setProperty(
                "ok", smokeComplete_ && smokeResult_["ok"] == juce::var(true) &&
                          (launch_.smokeExport.empty() || smokeExported_));
            receipt->setProperty("webview", smokeResult_);
            receipt->setProperty("nativeStatus", status_.getText());
            receipt->setProperty("nativeRequests",
                                 static_cast<int>(requests_.load()));
            receipt->setProperty("rejectedRequests",
                                 static_cast<int>(rejected_.load()));
            receipt->setProperty("audioEnabled", false);
            receipt->setProperty("shutdownComplete", done());
            receipt->setProperty(
                "elapsedSeconds",
                std::chrono::duration<double>(Clock::now() - smokeStarted_)
                    .count());
            // Receipt I/O must be off the message thread too. Worker is now
            // finished, so a final joined I/O worker cannot await JUCE calls.
            const auto file = launch_.smokeReceipt;
            const auto bytes =
                juce::JSON::toString(receipt.get(), true).toStdString();
            std::thread writer([file, bytes] {
                std::ofstream output(file);
                output << bytes;
            });
            writer.join();
        }
        finished_();
    }
    void timerCallback() override {
        if (closing_.load(std::memory_order_acquire)) {
            if (done())
                finish();
            return;
        }
        if (Clock::now() - smokeStarted_ >
            std::chrono::seconds(launch_.smokeSeconds + 60)) {
            close();
            return;
        }
        if (!browser_ || !ready_ || smokeEvaluating_)
            return;
        if (!smokeLaunched_) {
            smokeLaunched_ = true;
            const auto render =
                !launch_.smokeScene.empty() && !launch_.smokeExport.empty();
            const juce::String script = juce::String(R"JS(
(async () => {
 const result = window.__atlasSmoke = {finished: false, ok: false, rounds: 0};
 let serial = 0;
 const call = async (method, args = {}) => {
   const reply = await window.atlasHost.request({protocolVersion: 1, requestId: `shell-smoke-${++serial}`, method, args});
   if (!reply.ok) throw Error(`${method}: ${reply.error?.message}`);
   return reply.result;
 };
 try {
   const waitFor = async (predicate, description) => {
     const until = Date.now() + 10000;
     while (!predicate()) {
       if (Date.now() >= until) throw Error(`Timed out waiting for ${description}.`);
       await new Promise(resolve => setTimeout(resolve, 100));
     }
   };
   result.capabilities = await call('getCapabilities');
   result.audio = await call('getAudioState');
   if (result.audio.status === 'running') throw Error('Smoke must not enable physical output.');
   result.audioDevices = await call('getAudioDevices');
   if (result.audioDevices.available !== true || !Array.isArray(result.audioDevices.devices) || result.audioDevices.devices.length > 128 || result.audioDevices.active !== null) throw Error('Native silent device discovery was invalid.');
   await waitFor(() => document.getElementById('root')?.innerText?.includes('ANIMAL ATLAS') && document.querySelector('button[aria-label="Audio settings"]'), 'the production React UI');
   const audioSettings = document.querySelector('button[aria-label="Audio settings"]');
   audioSettings.click();
   await waitFor(() => {
     const select = document.querySelector('[role="dialog"][aria-label="Audio settings"] select[aria-label="Output device"]');
     return select && !select.disabled && select.options.length >= result.audioDevices.devices.length + 1;
   }, 'the native output catalogue in Audio settings');
   const dialog = document.querySelector('[role="dialog"][aria-label="Audio settings"]');
   result.deviceUI = {visible: !!dialog, text: dialog.innerText, outputs: [...dialog.querySelector('select[aria-label="Output device"]').options].map(option => option.textContent), buffers: [...dialog.querySelector('select[aria-label="Buffer size"]').options].map(option => option.textContent)};
   if (!result.deviceUI.text.includes('Output off') || !result.audioDevices.devices.every(device => result.deviceUI.outputs.some(label => label.includes(device.name)))) throw Error('Visible Audio settings did not show the native quiet catalogue.');
   result.afterDiscoveryAudio = await call('getAudioState');
   if (result.afterDiscoveryAudio.status === 'running') throw Error('Device discovery enabled output.');
   audioSettings.click();
   result.scenes = await call('listSoundscapes', {limit: 100});
   result.dom = {title: document.title, text: document.getElementById('root')?.innerText?.slice(0, 3000) ?? '', buttons: [...document.querySelectorAll('button')].map(button => button.textContent).slice(0, 100)};
   if (!result.dom.text.includes('ATLAS') || !result.dom.buttons.length || result.dom.text.includes('DEMO DATA')) throw Error('Native React UI did not render.');
)JS") +
                                        (render ? juce::String(R"JS(
   const scene = result.scenes.items.find(item => item.valid);
   if (!scene) throw Error('Imported soundscape was not listed.');
   await call('recallSoundscape', {soundscapeId: scene.id});
   let job = await call('startSoundscapeRender', {soundscapeId: scene.id});
   while (job.state === 'queued' || job.state === 'rendering') {
     await new Promise(resolve => setTimeout(resolve, 100)); job = await call('getRenderJob', {jobId: job.jobId});
   }
   if (job.state !== 'complete' || !/^[0-9a-f]{32}$/.test(job.artifactId)) throw Error('Native offline render failed.');
   result.render = job;
   const link = document.createElement('a'); link.dataset.artifactId = job.artifactId; link.href = `/__atlas/exports/${job.artifactId}`; link.download = job.filename; document.body.append(link); link.click(); link.remove();
)JS")
                                                : juce::String()) +
                                        juce::String(R"JS(
   const until = Date.now() + SMOKE_DURATION_MS;
   while (Date.now() < until) {
     const audio = await call('getAudioState');
     if (audio.status === 'running') throw Error('Physical output unexpectedly enabled.');
     await call('getTransport'); result.rounds++;
     await new Promise(resolve => setTimeout(resolve, 100));
   }
   result.ok = true;
 } catch (error) { result.error = String(error); }
 result.finished = true;
})();
)JS");
            browser_->evaluateJavascript(
                script.replace("SMOKE_DURATION_MS",
                               juce::String(launch_.smokeSeconds * 1000)));
            return;
        }
        smokeEvaluating_ = true;
        auto safe = juce::Component::SafePointer<Shell>(this);
        browser_->evaluateJavascript(
            "JSON.stringify(window.__atlasSmoke ?? null)",
            [safe](Browser::EvaluationResult result) {
                if (!safe)
                    return;
                safe->smokeEvaluating_ = false;
                if (const auto *value = result.getResult()) {
                    const auto parsed = juce::JSON::parse(value->toString());
                    if (parsed.isObject() &&
                        static_cast<bool>(parsed["finished"])) {
                        safe->smokeResult_ = parsed;
                        safe->smokeComplete_ = true;
                        if (!static_cast<bool>(parsed["ok"]) ||
                            (Clock::now() - safe->frontendStarted_ >=
                                 std::chrono::seconds(
                                     safe->launch_.smokeSeconds) &&
                             (safe->launch_.smokeExport.empty() ||
                              safe->smokeExported_)))
                            safe->close();
                    }
                }
            });
    }
};
class Window final : public juce::DocumentWindow {
  public:
    Window(Launch launch, std::function<void()> finished)
        : DocumentWindow("Animal Atlas", juce::Colour(0xff282c2b), allButtons) {
        setUsingNativeTitleBar(true);
        shell_ = new Shell(std::move(launch), std::move(finished));
        setContentOwned(shell_, true);
        setResizable(true, true);
        setResizeLimits(1050, 700, 2560, 1800);
        centreWithSize(1400, 920);
        setVisible(true);
    }
    void closeButtonPressed() override {
        shell_->close();
        if (shell_->done())
            juce::JUCEApplicationBase::quit();
    }
    void closeShell() {
        shell_->close();
        if (shell_->done())
            juce::JUCEApplicationBase::quit();
    }

  private:
    Shell *shell_{};
};
class Application final : public juce::JUCEApplication {
  public:
    const juce::String getApplicationName() override { return "Animal Atlas"; }
    const juce::String getApplicationVersion() override { return "0.4.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise(const juce::String &) override {
        try {
            window_ = std::make_unique<Window>(
                parse(getCommandLineParameterArray()),
                [] { juce::JUCEApplicationBase::quit(); });
        } catch (const std::exception &error) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Animal Atlas could not open", juce::String(error.what()));
            setApplicationReturnValue(1);
            quit();
        }
    }
    void shutdown() override { window_.reset(); }
    void systemRequestedQuit() override {
        if (window_)
            window_->closeShell();
        else
            quit();
    }
    void anotherInstanceStarted(const juce::String &) override {
        if (window_)
            window_->toFront(true);
    }

  private:
    std::unique_ptr<Window> window_;
};
} // namespace
START_JUCE_APPLICATION(Application)
