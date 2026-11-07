#include "atlas_build_identity.hpp"
#include "json_text.hpp"
#include "juce_collection.hpp"
#include "standalone_bridge.hpp"
#include "standalone_support.hpp"
#include <algorithm>
#include <atlas/playback.hpp>
#include <atlas/projects.hpp>
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
    fs::path state, library, smokeReceipt, smokeScene, smokeExport,
        smokeSceneExport, smokeBackup, smokeRestore;
    int smokeSeconds{3};
    bool libraryOverride{}, stateOverride{}, smokeLive{};
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
        if (option == "--state-dir") {
            launch.state = path(value);
            launch.stateOverride = true;
        } else if (option == "--library") {
            launch.library = path(value);
            launch.libraryOverride = true;
        } else if (option == "--smoke-test")
            launch.smokeReceipt = path(value);
        else if (option == "--smoke-scene")
            launch.smokeScene = path(value);
        else if (option == "--smoke-export")
            launch.smokeExport = path(value);
        else if (option == "--smoke-scene-export")
            launch.smokeSceneExport = path(value);
        else if (option == "--smoke-backup")
            launch.smokeBackup = path(value);
        else if (option == "--smoke-restore")
            launch.smokeRestore = path(value);
        else if (option == "--smoke-live") {
            if (value != "1")
                throw std::invalid_argument(
                    "Live smoke requires explicit value 1.");
            launch.smokeLive = true;
        } else if (option == "--smoke-seconds") {
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
    for (const auto &file :
         {launch.smokeScene, launch.smokeExport, launch.smokeSceneExport,
          launch.smokeBackup, launch.smokeRestore})
        if (!file.empty() && !file.is_absolute())
            throw std::invalid_argument("Smoke paths must be absolute.");
    if ((!launch.smokeScene.empty() || !launch.smokeExport.empty() ||
         !launch.smokeSceneExport.empty() || !launch.smokeBackup.empty() ||
         !launch.smokeRestore.empty()) &&
        launch.smokeReceipt.empty())
        throw std::invalid_argument(
            "Smoke scene/export requires --smoke-test.");
    if (launch.smokeLive &&
        (!launch.stateOverride || launch.smokeReceipt.empty()))
        throw std::invalid_argument(
            "Live smoke requires explicit state and receipt paths.");
    if (launch.smokeBackup.empty() != launch.smokeRestore.empty())
        throw std::invalid_argument("Smoke backup and restore must be paired.");
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
    enum Kind {
        request,
        library,
        scene,
        exportWav,
        exportScene,
        projectScenes,
        backup,
        inspectProject,
        restoreProject,
        revealWav
    } kind{};
    juce::int64 token{};
    std::string text;
    fs::path file;
    unsigned epoch{};
    std::vector<fs::path> files;
    Work(Kind operation, juce::int64 requestToken = 0, std::string value = {},
         fs::path target = {}, unsigned pageEpoch = 0,
         std::vector<fs::path> selected = {})
        : kind(operation), token(requestToken), text(std::move(value)),
          file(std::move(target)), epoch(pageEpoch),
          files(std::move(selected)) {}
};
class Shell final : public juce::Component, private juce::Timer {
  public:
    Shell(Launch launch, std::function<void()> finished)
        : launch_(std::move(launch)), finished_(std::move(finished)) {
        addAndMakeVisible(fileMenu_);
        addAndMakeVisible(status_);
        status_.setText("Opening the local instrument…",
                        juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, juce::Colour(0xffe8e5dd));
        fileMenu_.onClick = [this] { showFileMenu(); };
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
        fileMenu_.setBounds(toolbar.removeFromLeft(125));
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
    juce::TextButton fileMenu_{"File / Project"};
    juce::Label status_;
    std::string libraryStatus_;
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
        smokeExported_{}, smokeSceneExported_{}, smokeProjectDone_{},
        smokeProjectStarted_{}, finishedOnce_{};
    std::vector<atlas::standalone::ManagedScene> backupScenes_;
    std::vector<bool> backupSelection_;
    juce::var smokeResult_;
    Clock::time_point smokeStarted_, frontendStarted_;
    std::string stateNotice_;
    unsigned browserEpoch_{};
    void setControls(bool enabled) { fileMenu_.setEnabled(enabled); }
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
        const auto pausesPlayback =
            work.kind == Work::backup || work.kind == Work::inspectProject ||
            work.kind == Work::restoreProject ||
            work.kind == Work::projectScenes || work.kind == Work::exportScene;
        if (enqueue(std::move(work))) {
            ready_ = false;
            setControls(false);
            status_.setText(
                pausesPlayback ? "Pausing playback and verifying project files…"
                               : "Updating local instrument…",
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
    void showFileMenu() {
        juce::PopupMenu menu;
        menu.addItem(1, "Choose wildlife library…");
        menu.addItem(2, "Scene only");
        menu.addItem(3, "Import soundscape…");
        menu.addSeparator();
        menu.addItem(4, "Back up project…");
        menu.addItem(5, "Restore project into a new folder…");
        menu.addSeparator();
        menu.addItem(6, "Show app storage");
        menu.addItem(7, "Storage and library status…");
        auto safe = juce::Component::SafePointer<Shell>(this);
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(&fileMenu_),
            [safe](int action) {
                if (!safe || !safe->ready_)
                    return;
                if (action == 1)
                    safe->chooseFile(false);
                else if (action == 2)
                    safe->submitControl({Work::library});
                else if (action == 3)
                    safe->chooseFile(true);
                else if (action == 4)
                    safe->submitControl({Work::projectScenes});
                else if (action == 5)
                    safe->chooseProject();
                else if (action == 6)
                    juce::File(string(safe->launch_.state)).revealToUser();
                else if (action == 7)
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::InfoIcon,
                        "App storage and library",
                        "App storage: " + string(safe->launch_.state) + "\n\n" +
                            juce::String(safe->libraryStatus_ +
                                         safe->stateNotice_) +
                            "\n\nLast action: " + safe->status_.getText() +
                            "\n\nScene only works without a wildlife library. "
                            "Project backups include selected verified scenes "
                            "and saved voices. Wildlife libraries, device "
                            "settings and rendered WAVs stay separate.");
            });
    }
    void showBackupSelection() {
        juce::PopupMenu menu;
        menu.addSectionHeader("Scenes to back up (saved voices are included)");
        menu.addItem(1, "Back up selected scenes…");
        menu.addItem(2, "Select all");
        menu.addItem(3, "Select none (voices only)");
        menu.addSeparator();
        for (std::size_t i = 0; i < backupScenes_.size(); ++i)
            menu.addItem(static_cast<int>(100 + i),
                         juce::String(backupScenes_[i].name), true,
                         backupSelection_[i]);
        auto safe = juce::Component::SafePointer<Shell>(this);
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(&fileMenu_),
            [safe](int action) {
                if (!safe || !safe->ready_ || action == 0)
                    return;
                if (action == 1) {
                    safe->chooseBackupDestination();
                    return;
                }
                if (action == 2 || action == 3)
                    std::fill(safe->backupSelection_.begin(),
                              safe->backupSelection_.end(), action == 2);
                else if (action >= 100 &&
                         static_cast<std::size_t>(action - 100) <
                             safe->backupSelection_.size()) {
                    const auto index = static_cast<std::size_t>(action - 100);
                    safe->backupSelection_[index] =
                        !safe->backupSelection_[index];
                }
                safe->showBackupSelection();
            });
    }
    void chooseBackupDestination() {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Back up project to a new file",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("Animal Atlas.atlas-project"),
            "*.atlas-project");
        auto safe = juce::Component::SafePointer<Shell>(this);
        chooser_->launchAsync(
            juce::FileBrowserComponent::saveMode |
                juce::FileBrowserComponent::canSelectFiles,
            [safe](const juce::FileChooser &chooser) {
                if (!safe || chooser.getResult() == juce::File())
                    return;
                std::vector<fs::path> scenes;
                for (std::size_t i = 0; i < safe->backupScenes_.size(); ++i)
                    if (safe->backupSelection_[i])
                        scenes.push_back(
                            safe->launch_.state / "soundscapes" /
                            (safe->backupScenes_[i].id + ".atlas-scene"));
                safe->submitControl(
                    {Work::backup,
                     0,
                     {},
                     path(chooser.getResult().getFullPathName()),
                     0,
                     std::move(scenes)});
            });
    }
    void chooseProject() {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Inspect an Animal Atlas project backup", juce::File(),
            "*.atlas-project");
        auto safe = juce::Component::SafePointer<Shell>(this);
        chooser_->launchAsync(
            juce::FileBrowserComponent::openMode |
                juce::FileBrowserComponent::canSelectFiles,
            [safe](const juce::FileChooser &chooser) {
                if (safe && chooser.getResult() != juce::File())
                    safe->submitControl(
                        {Work::inspectProject,
                         0,
                         {},
                         path(chooser.getResult().getFullPathName())});
            });
    }
    void previewRestore(const fs::path &backup,
                        const atlas::ProjectSummary &summary) {
        juce::String names;
        for (const auto &name : summary.sceneNames)
            names += "\n• " + juce::String(name);
        const auto message =
            juce::String(static_cast<juce::int64>(summary.sceneCount)) +
            " verified scenes, " +
            juce::String(static_cast<juce::int64>(summary.voiceCount)) +
            " saved voices · " +
            juce::String(static_cast<juce::int64>(summary.totalBytes)) +
            " bytes" + names +
            "\n\nNext, choose a NEW app state folder name. An existing folder, "
            "even empty, is a conflict and will be refused. Restore verifies "
            "the backup again and opens a separate app window. Your current "
            "project stays open.";
        auto safe = juce::Component::SafePointer<Shell>(this);
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::InfoIcon, "Restore project preview",
            message, "Choose new folder…", "Cancel", nullptr,
            juce::ModalCallbackFunction::create([safe, backup](int accepted) {
                if (safe && accepted != 0)
                    safe->chooseRestoreDestination(backup);
            }));
    }
    void chooseRestoreDestination(const fs::path &backup) {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Name a new app state folder (existing paths are refused)",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("Animal Atlas Restored"));
        auto safe = juce::Component::SafePointer<Shell>(this);
        chooser_->launchAsync(
            juce::FileBrowserComponent::saveMode |
                juce::FileBrowserComponent::canSelectFiles,
            [safe, backup](const juce::FileChooser &chooser) {
                if (safe && chooser.getResult() != juce::File())
                    safe->submitControl(
                        {Work::restoreProject, 0, backup.string(),
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
    void exportSceneFile(const std::string &id) {
        if (!ready_ || !atlas::standalone::opaqueId(id))
            return;
        if (!launch_.smokeReceipt.empty() &&
            !launch_.smokeSceneExport.empty()) {
            submitControl({Work::exportScene, 0, id, launch_.smokeSceneExport});
            return;
        }
        chooser_ = std::make_unique<juce::FileChooser>(
            "Export verified scene to a new file",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("Animal Atlas.atlas-scene"),
            "*.atlas-scene");
        auto safe = juce::Component::SafePointer<Shell>(this);
        chooser_->launchAsync(
            juce::FileBrowserComponent::saveMode |
                juce::FileBrowserComponent::canSelectFiles,
            [safe, id](const juce::FileChooser &chooser) {
                if (safe && chooser.getResult() != juce::File())
                    safe->submitControl(
                        {Work::exportScene, 0, id,
                         path(chooser.getResult().getFullPathName())});
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
                .withEventListener(
                    "atlasSceneExport",
                    [this](juce::var event) {
                        exportSceneFile(
                            event["sceneId"].toString().toStdString());
                    })
                .withEventListener(
                    "atlasRevealExport",
                    [this](juce::var event) {
                        const auto id =
                            event["artifactId"].toString().toStdString();
                        if (ready_ && atlas::standalone::opaqueId(id))
                            submitControl({Work::revealWav, 0, id});
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
            const auto state = atlas::standalone::selectedPath(launch_.state);
            const auto resources =
                path(juce::File::getSpecialLocation(
                         juce::File::currentApplicationFile)
                         .getChildFile("Contents/Resources/ui")
                         .getFullPathName());
            auto assets = atlas::standalone::loadAssets(resources);
            atlas::standalone::prepareStateDirectory(state);
            const auto lockName =
                "animal-atlas-state-" +
                juce::String::toHexString(
                    string(fs::canonical(state)).hashCode64());
            juce::InterProcessLock stateLock(lockName);
            if (!stateLock.enter(0))
                throw std::runtime_error(
                    "This application data directory is already open in "
                    "another Animal Atlas process.");
            atlas::standalone::Settings settings;
            bool settingsWritable{true};
            std::string notice;
            try {
                settings = atlas::standalone::loadSettings(state);
            } catch (const std::exception &error) {
                settingsWritable = false;
                notice = std::string(" ") + error.what();
            }
            if (launch_.libraryOverride)
                settings.library = launch_.library;
            std::unique_ptr<atlas::standalone::CollectionService> collection;
            try {
                collection =
                    std::make_unique<atlas::standalone::CollectionService>(
                        state / "live.sqlite3");
            } catch (const std::exception &) {
                notice += " Live collection storage is unavailable; existing "
                          "library browsing remains available.";
            }
            atlas::PlaybackRuntime playback(atlas::makeJuceOutputDriver(),
                                            configuration(settings.output));
            atlas::VoiceStore voices(state / "voices.sqlite3");
            atlas::UiHostStorage storage{state / "soundscapes",
                                         state / "exports",
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
            std::string libraryStatus =
                settings.library.empty()
                    ? "Scene only · no wildlife library selected"
                    : "Wildlife library: " + settings.library.string();
            try {
                host = makeHost(settings.library);
            } catch (const std::exception &error) {
                initial = error.what();
                libraryStatus = "Scene only · selected library unavailable: " +
                                settings.library.string() + " · " + initial;
                host = makeHost({});
            }
            if (!launch_.smokeScene.empty()) {
                atlas::standalone::importScene(launch_.smokeScene,
                                               storage.soundscapes);
                initial = "Smoke soundscape imported; scene only";
            }
            post([assets = std::move(assets), initial, notice, state,
                  libraryStatus](Shell &shell) mutable {
                shell.launch_.state = state;
                shell.assets_ = std::move(assets);
                shell.stateNotice_ = notice;
                shell.libraryStatus_ = libraryStatus;
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
                    bool reloadLibrary{};
                    auto response = host->request(work->text);
                    if (response.size() > 8 * 1024 * 1024)
                        response = failed(work->text,
                                          "Native reply exceeds capacity.");
                    const auto request =
                        juce::JSON::parse(juce::String(work->text));
                    auto reply = juce::JSON::parse(juce::String(response));
                    const auto method = request["method"].toString();
                    if (collection && method == "getCapabilities" &&
                        static_cast<bool>(reply["ok"])) {
                        if (auto *methods = reply["result"]["supportedMethods"]
                                                .getArray()) {
                            methods->add("getLiveSources");
                            methods->add("setLiveSources");
                        }
                        response =
                            juce::JSON::toString(reply, true).toStdString();
                    }
                    if (collection &&
                        (method == "getLiveSources" ||
                         method == "setLiveSources") &&
                        reply["error"]["code"].toString() == "unsupported") {
                        try {
                            const auto *arguments =
                                request["args"].getDynamicObject();
                            const auto count =
                                arguments ? arguments->getProperties().size()
                                          : -1;
                            bool libraryChanged{};
                            if (method == "getLiveSources" && count != 0)
                                throw std::invalid_argument(
                                    "Unexpected live source arguments.");
                            if (method == "setLiveSources") {
                                const auto *ids =
                                    request["args"]["sourceIds"].getArray();
                                if (count != 1 || !ids || ids->size() > 2)
                                    throw std::invalid_argument(
                                        "Choose only the available live "
                                        "sources.");
                                std::vector<std::string> selected;
                                for (const auto &id : *ids) {
                                    if (!id.isString() ||
                                        (id.toString() != "birdweather" &&
                                         id.toString() != "nzsharks") ||
                                        std::find(
                                            selected.begin(), selected.end(),
                                            id.toString().toStdString()) !=
                                            selected.end())
                                        throw std::invalid_argument(
                                            "Invalid or duplicate live "
                                            "source.");
                                    selected.push_back(
                                        id.toString().toStdString());
                                }
                                if (!selected.empty() &&
                                    settings.library !=
                                        collection->databasePath()) {
                                    host->setLibrary(
                                        collection->databasePath());
                                    playback.enable(false);
                                    settings.library =
                                        collection->databasePath();
                                    libraryChanged = true;
                                    reloadLibrary = true;
                                    post([file =
                                              settings.library](Shell &shell) {
                                        shell.libraryStatus_ =
                                            "Managed live library: " +
                                            file.string();
                                        shell.updateStatus(
                                            "Live library selected · choose "
                                            "Listening separately",
                                            false);
                                    });
                                    if (settingsWritable) {
                                        try {
                                            atlas::standalone::saveSettings(
                                                state, settings);
                                        } catch (const std::exception &) {
                                            post([](Shell &shell) {
                                                shell.updateStatus(
                                                    "Live library selected; "
                                                    "preference could not be "
                                                    "saved",
                                                    false);
                                            });
                                        }
                                    }
                                }
                                collection->configure(selected);
                            }
                            auto result = juce::JSON::parse(
                                juce::String(collection->stateJson()));
                            result.getDynamicObject()->setProperty(
                                "libraryActive",
                                settings.library == collection->databasePath());
                            result.getDynamicObject()->setProperty(
                                "libraryChanged", libraryChanged);
                            juce::DynamicObject::Ptr envelope =
                                new juce::DynamicObject;
                            envelope->setProperty("requestId",
                                                  request["requestId"]);
                            envelope->setProperty("ok", true);
                            envelope->setProperty("result", result);
                            response = juce::JSON::toString(
                                           juce::var(envelope.get()), true)
                                           .toStdString();
                        } catch (const std::invalid_argument &error) {
                            response =
                                "{\"requestId\":" +
                                atlas::detail::quoteJson(request["requestId"]
                                                             .toString()
                                                             .toStdString()) +
                                ",\"ok\":false,\"error\":{\"code\":\"invalid_"
                                "request\",\"message\":" +
                                atlas::detail::quoteJson(error.what()) + "}}";
                        } catch (const std::exception &) {
                            response = failed(
                                work->text, "Live collection could not start. "
                                            "Check app storage and retry.");
                        }
                        reply = juce::JSON::parse(juce::String(response));
                    }
                    if (request["method"].toString() == "setAudioDevice" &&
                        static_cast<bool>(reply["ok"])) {
                        settings.output =
                            configurationJson(playback.outputConfiguration());
                        if (settingsWritable) {
                            try {
                                atlas::standalone::saveSettings(state,
                                                                settings);
                            } catch (const std::exception &error) {
                                post([message = std::string(error.what())](
                                         Shell &shell) {
                                    shell.updateStatus(message, false);
                                });
                            }
                        }
                    }
                    // Retire requests from the old document before replacing
                    // its view; a queued edit must not cross the library
                    // switch.
                    if (reloadLibrary)
                        ++expectedPageEpoch;
                    post([token = work->token, epoch = work->epoch,
                          response = std::move(response),
                          reloadLibrary](Shell &shell) {
                        if (epoch != shell.browserEpoch_)
                            return;
                        shell.reply(token, response);
                        if (reloadLibrary)
                            shell.updateStatus("Live library selected · choose "
                                               "Listening separately",
                                               true);
                    });
                    continue;
                }
                std::string message;
                bool reload{};
                try {
                    if (work->kind == Work::backup ||
                        work->kind == Work::inspectProject ||
                        work->kind == Work::restoreProject ||
                        work->kind == Work::projectScenes ||
                        work->kind == Work::exportScene)
                        playback.enable(false);
                    if (work->kind == Work::library) {
                        if (!work->file.empty() &&
                            !fs::is_regular_file(
                                fs::symlink_status(work->file)))
                            throw std::runtime_error(
                                "Choose a regular wildlife library file.");
                        host->setLibrary(work->file);
                        if (collection &&
                            work->file != collection->databasePath())
                            collection->configure({});
                        playback.enable(false);
                        settings.library = work->file;
                        post([path = work->file](Shell &shell) {
                            shell.libraryStatus_ =
                                path.empty()
                                    ? "Scene only · no wildlife library "
                                      "selected"
                                    : "Wildlife library: " + path.string();
                        });
                        reload = true;
                        if (settingsWritable)
                            atlas::standalone::saveSettings(state, settings);
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
                    } else if (work->kind == Work::exportWav) {
                        atlas::standalone::copyExport(storage.exports,
                                                      work->text, work->file);
                        message =
                            "WAV saved: " + work->file.filename().string();
                        post([](Shell &shell) { shell.smokeExported_ = true; });
                    } else if (work->kind == Work::exportScene) {
                        atlas::standalone::copyScene(storage.soundscapes,
                                                     work->text, work->file);
                        message = "Verified scene exported: " +
                                  work->file.filename().string();
                        post([](Shell &shell) {
                            shell.smokeSceneExported_ = true;
                        });
                    } else if (work->kind == Work::revealWav) {
                        const auto file = atlas::standalone::managedExportPath(
                            storage.exports, work->text);
                        post([file](Shell &) {
                            juce::File(string(file)).revealToUser();
                        });
                        message = "Rendered WAV shown in Finder";
                    } else if (work->kind == Work::projectScenes) {
                        auto scenes = atlas::standalone::managedScenes(
                            storage.soundscapes);
                        message = "Choose scenes for project backup · playback "
                                  "paused";
                        post([scenes =
                                  std::move(scenes)](Shell &shell) mutable {
                            shell.backupScenes_ = std::move(scenes);
                            shell.backupSelection_.assign(
                                shell.backupScenes_.size(), true);
                            shell.updateStatus("Choose scenes for project "
                                               "backup · playback paused",
                                               false);
                            if (!shell.launch_.smokeReceipt.empty() &&
                                !shell.launch_.smokeBackup.empty()) {
                                std::vector<fs::path> selected;
                                for (const auto &scene : shell.backupScenes_)
                                    selected.push_back(
                                        shell.launch_.state / "soundscapes" /
                                        (scene.id + ".atlas-scene"));
                                shell.submitControl({Work::backup,
                                                     0,
                                                     {},
                                                     shell.launch_.smokeBackup,
                                                     0,
                                                     std::move(selected)});
                            } else
                                shell.showBackupSelection();
                        });
                        continue;
                    } else if (work->kind == Work::backup) {
                        std::vector<fs::path> selected;
                        for (const auto &file : work->files)
                            selected.push_back(
                                atlas::standalone::selectedPath(file));
                        const auto summary = atlas::createProjectBackup(
                            state / "voices.sqlite3", selected,
                            atlas::standalone::selectedPath(work->file));
                        message = "Project backed up: " +
                                  work->file.filename().string() + " · " +
                                  std::to_string(summary.sceneCount) +
                                  " scenes · playback paused";
                        if (!launch_.smokeReceipt.empty() &&
                            work->file == launch_.smokeBackup) {
                            // The smoke follows the same full inspection and
                            // restore worker operations; only native dialog
                            // destinations differ.
                            post([file = work->file](Shell &shell) {
                                shell.updateStatus(
                                    "Smoke backup saved; inspecting restore…",
                                    false);
                                shell.submitControl(
                                    {Work::inspectProject, 0, {}, file});
                            });
                            continue;
                        }
                    } else if (work->kind == Work::inspectProject) {
                        const auto summary = atlas::inspectProjectBackup(
                            atlas::standalone::selectedPath(work->file));
                        post([file = work->file, summary](Shell &shell) {
                            shell.updateStatus(
                                "Backup verified · playback paused", false);
                            if (!shell.launch_.smokeReceipt.empty() &&
                                file == shell.launch_.smokeBackup)
                                shell.submitControl(
                                    {Work::restoreProject, 0, file.string(),
                                     shell.launch_.smokeRestore});
                            else
                                shell.previewRestore(file, summary);
                        });
                        continue;
                    } else if (work->kind == Work::restoreProject) {
                        const auto destination =
                            atlas::standalone::selectedPath(work->file);
                        const auto summary = atlas::restoreProjectBackup(
                            atlas::standalone::selectedPath(work->text),
                            destination);
                        message = "Restored " +
                                  std::to_string(summary.sceneCount) +
                                  " scenes into " + destination.string();
                        if (launch_.smokeReceipt.empty()) {
                            juce::ChildProcess application;
                            const juce::StringArray arguments{
                                juce::File::getSpecialLocation(
                                    juce::File::currentExecutableFile)
                                    .getFullPathName(),
                                "--state-dir", string(destination)};
                            if (!application.start(arguments, 0))
                                message += " · new window could not launch; "
                                           "restored folder retained";
                            else
                                message += " · opened in a new window";
                        }
                        post([](Shell &shell) {
                            shell.smokeProjectDone_ = true;
                        });
                    }
                } catch (const std::exception &error) {
                    message = error.what();
                    if (!launch_.smokeReceipt.empty())
                        post([message](Shell &shell) {
                            shell.smokeResult_ = juce::JSON::parse(
                                "{\"ok\":false,\"error\":" +
                                juce::JSON::toString(juce::var(message)) + "}");
                            shell.close();
                        });
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
                "ok",
                smokeComplete_ && smokeResult_["ok"] == juce::var(true) &&
                    (launch_.smokeExport.empty() || smokeExported_) &&
                    (launch_.smokeSceneExport.empty() || smokeSceneExported_) &&
                    (launch_.smokeBackup.empty() || smokeProjectDone_));
            receipt->setProperty("webview", smokeResult_);
            receipt->setProperty("nativeStatus", status_.getText());
            receipt->setProperty("nativeRequests",
                                 static_cast<int>(requests_.load()));
            receipt->setProperty("rejectedRequests",
                                 static_cast<int>(rejected_.load()));
            receipt->setProperty("audioEnabled", false);
            receipt->setProperty("shutdownComplete", done());
            receipt->setProperty("sceneExported", smokeSceneExported_);
            receipt->setProperty("projectRestored", smokeProjectDone_);
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
            std::chrono::seconds(launch_.smokeSeconds +
                                 (launch_.smokeLive ? 120 : 60))) {
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
   result.dom = {visibility: document.visibilityState, title: document.title, text: document.getElementById('root')?.innerText?.slice(0, 3000) ?? '', buttons: [...document.querySelectorAll('button')].map(button => button.textContent).slice(0, 100)};
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
   LIVE_COLLECTION_SMOKE
   SCENE_EXPORT_SMOKE
   result.ok = true;
 } catch (error) { result.error = String(error); result.failureDom = {visibility: document.visibilityState, text: document.getElementById('root')?.innerText?.slice(0, 9000)}; }
 result.finished = true;
})();
)JS");
            browser_->evaluateJavascript(
                script
                    .replace("SMOKE_DURATION_MS",
                             juce::String(launch_.smokeSeconds * 1000))
                    .replace("LIVE_COLLECTION_SMOKE", launch_.smokeLive
                                                          ? juce::String(R"JS(
   result.liveInitial = await call('getLiveSources');
   if (!result.liveInitial.libraryActive) {
     if (result.liveInitial.collecting) throw Error('Unexpected active source in fresh smoke state.');
     const collect = document.querySelector('button[aria-label="Start live collection"]');
     if (!collect) throw Error('Native Collect control is missing.');
     collect.click();
     const start = () => [...document.querySelectorAll('.live-sources__menu button')].find(button => button.textContent === 'Start live updates');
     await waitFor(() => start() && !start().disabled, 'enabled native collection control');
     start().click();
     // This old-document mutation is deliberately queued after the switch.
     // Native epoch retirement must discard it before it can pause the new view.
     void window.atlasHost.request({protocolVersion: 1, requestId: 'retired-page-pause', method: 'setLiveSources', args: {sourceIds: []}}).catch(() => {});
     return; // The real React action reloads; the shell starts the second phase.
   }
   if (!result.liveInitial.collecting || !result.capabilities.libraryAvailable) throw Error('Collect did not survive the native UI reload.');
   try {
     result.liveStarted = result.liveInitial;
     const deadline = Date.now() + 65000;
     do {
       result.live = await call('getLiveSources');
       if (result.live.sources.every(source => source.lastSuccessUtc || source.error)) break;
       await new Promise(resolve => setTimeout(resolve, 250));
     } while (Date.now() < deadline);
     if (!result.live.sources.every(source => source.lastSuccessUtc && (!source.error || source.error === 'pagination_limit_reached') && BigInt(source.records) > 0n)) throw Error('Native provider collection did not populate both streams: ' + JSON.stringify(result.live));
     const changes = await call('getChanges', {storeId: '', afterSequence: '0', limit: 500});
     result.liveMap = {count: changes.events.length, sources: [...new Set(changes.events.map(item => item.event.sourceId))], baselinePulses: changes.events.filter(item => item.liveEligible).length};
     if (!['birdweather', 'nzsharks'].every(id => result.liveMap.sources.includes(id)) || result.liveMap.baselinePulses) throw Error('Map baseline lost a source or replayed old pings.');
     document.querySelector('button[aria-label="Map details and coverage"]')?.click();
     await waitFor(() => /[1-9][0-9]* records \u00b7 [1-9][0-9]* of [1-9][0-9]* locations shown/.test(document.querySelector('.map-stage')?.textContent ?? ''), 'loaded event counts in map details');
     await waitFor(() => document.querySelector('.grain__ticks path[d]'), 'actual detections in Grain before the catalogue refresh');
     const initialBird = result.live.sources.find(source => source.id === 'birdweather');
     const nextDeadline = Date.now() + 75000;
     do {
       await new Promise(resolve => setTimeout(resolve, 500));
       result.liveNext = await call('getLiveSources');
       const bird = result.liveNext.sources.find(source => source.id === 'birdweather');
       if (bird.lastSuccessUtc !== initialBird.lastSuccessUtc) break;
       if (Date.now() >= nextDeadline) throw Error('No second native bird poll completed.');
     } while (true);
     const incoming = await call('getChanges', {storeId: changes.storeId, afterSequence: changes.nextSequence, limit: 500});
     result.liveIncoming = {count: incoming.events.length, pings: incoming.events.filter(item => item.liveEligible).length, sources: [...new Set(incoming.events.map(item => item.event.sourceId))]};
     if (!result.liveIncoming.pings) throw Error('No genuinely new bird activity arrived during the live smoke window.');
     await waitFor(() => parseInt(document.querySelector('.map-locations > summary')?.textContent ?? '0', 10) > 0 && document.querySelectorAll('.species-row').length > 0, 'real observations in the React map and species list');
     result.liveUI = {mapDetails: document.querySelector('.map-stage').innerText.slice(0, 3000), locations: document.querySelector('.map-locations > summary').textContent, speciesRows: document.querySelectorAll('.species-row').length, sourceText: document.querySelector('[aria-label="Live source status"]').innerText, canvas: !!document.querySelector('.map-stage canvas')};
   } finally {
     result.liveStopped = await call('setLiveSources', {sourceIds: []});
     if (result.liveStopped.collecting) throw Error('Live collection did not pause.');
   }
)JS")
                                                          : juce::String())
                    .replace("SCENE_EXPORT_SMOKE",
                             launch_.smokeSceneExport.empty()
                                 ? juce::String()
                                 : juce::String(R"JS(
   const exportedScene = result.scenes.items.find(item => item.valid);
   if (!exportedScene) throw Error('No verified scene to export.');
   const sceneLink = document.createElement('a'); sceneLink.dataset.sceneId = exportedScene.id;
   sceneLink.href = '#export-scene'; document.body.append(sceneLink); sceneLink.click(); sceneLink.remove();
)JS")));
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
                        if (static_cast<bool>(parsed["ok"]) &&
                            !safe->launch_.smokeBackup.empty() &&
                            !safe->smokeProjectStarted_ &&
                            (safe->launch_.smokeSceneExport.empty() ||
                             safe->smokeSceneExported_)) {
                            safe->smokeProjectStarted_ = true;
                            safe->submitControl({Work::projectScenes});
                            return;
                        }
                        if (!static_cast<bool>(parsed["ok"]) ||
                            (Clock::now() - safe->frontendStarted_ >=
                                 std::chrono::seconds(
                                     safe->launch_.smokeSeconds) &&
                             (safe->launch_.smokeExport.empty() ||
                              safe->smokeExported_) &&
                             (safe->launch_.smokeSceneExport.empty() ||
                              safe->smokeSceneExported_) &&
                             (safe->launch_.smokeBackup.empty() ||
                              safe->smokeProjectDone_)))
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
    const juce::String getApplicationVersion() override { return "0.4.1"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise(const juce::String &) override {
        try {
            auto launch = parse(getCommandLineParameterArray());
            const bool liveSmoke = launch.smokeLive;
            window_ = std::make_unique<Window>(
                std::move(launch), [] { juce::JUCEApplicationBase::quit(); });
            if (liveSmoke) {
                juce::Process::makeForegroundProcess();
                window_->toFront(true);
            }
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
