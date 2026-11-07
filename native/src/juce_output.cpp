#include "coreaudio_output_safety.hpp"
#include <algorithm>
#include <atlas/playback.hpp>
#include <atomic>
#include <cmath>
#include <exception>
#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <map>
#include <set>
#include <utility>

namespace atlas {
namespace {
static_assert(std::atomic<PlaybackRuntime *>::is_always_lock_free);
// All device objects and metadata belong to JUCE's message thread.
void onMessageThread(std::function<void()> operation) {
    auto *manager = juce::MessageManager::getInstance();
    if (manager->isThisTheMessageThread()) {
        operation();
        return;
    }
    struct Invocation {
        std::function<void()> &operation;
        std::exception_ptr failure;
    } invocation{operation, {}};
    manager->callFunctionOnMessageThread(
        [](void *context) -> void * {
            auto &call = *static_cast<Invocation *>(context);
            try {
                call.operation();
            } catch (...) {
                call.failure = std::current_exception();
            }
            return nullptr;
        },
        &invocation);
    if (invocation.failure)
        std::rethrow_exception(invocation.failure);
}
std::string deviceId(const std::string &type, const std::string &name) {
    std::uint64_t hash = 14695981039346656037ull;
    const auto append = [&](unsigned char byte) {
        hash ^= byte;
        hash *= 1099511628211ull;
    };
    for (char byte : type)
        append(static_cast<unsigned char>(byte));
    append(0);
    for (char byte : name)
        append(static_cast<unsigned char>(byte));
    constexpr char digits[] = "0123456789abcdef";
    std::string id = "juce-";
    for (int shift = 60; shift >= 0; shift -= 4)
        id += digits[(hash >> shift) & 15];
    return id;
}
#if JUCE_MAC
// Match the pinned JUCE CoreAudio scanner's device order and duplicate-name
// numbering, so one broken aggregate cannot hide unrelated virtual outputs.
std::optional<std::map<std::string, AudioObjectID>> coreAudioOutputs() {
    const AudioObjectPropertyAddress devicesAddress{
        kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain};
    UInt32 size{};
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &devicesAddress,
                                       0, nullptr, &size) != noErr ||
        size % sizeof(AudioObjectID) != 0 || size / sizeof(AudioObjectID) > 1024)
        return {};
    std::vector<AudioObjectID> devices(size / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &devicesAddress,
                                   0, nullptr, &size, devices.data()) != noErr)
        return {};
    if (size % sizeof(AudioObjectID) != 0 ||
        size / sizeof(AudioObjectID) > devices.size())
        return {};
    devices.resize(size / sizeof(AudioObjectID));
    juce::StringArray names;
    std::vector<AudioObjectID> outputs;
    for (const auto device : devices) {
        const auto name = detail::coreAudioString(device, kAudioDevicePropertyDeviceNameCFString);
        if (!name || juce::String(*name).startsWith("_JucePrivateAggregateDevice_"))
            continue;
        const AudioObjectPropertyAddress streams{
            kAudioDevicePropertyStreamConfiguration, kAudioObjectPropertyScopeOutput,
            kAudioObjectPropertyElementMain};
        UInt32 bytes{};
        if (AudioObjectGetPropertyDataSize(device, &streams, 0, nullptr, &bytes) != noErr ||
            bytes < offsetof(AudioBufferList, mBuffers) || bytes > 65536)
            continue;
        std::vector<std::uint64_t> storage((bytes + sizeof(std::uint64_t) - 1) /
                                            sizeof(std::uint64_t));
        if (AudioObjectGetPropertyData(device, &streams, 0, nullptr, &bytes,
                                       storage.data()) != noErr)
            continue;
        if (bytes < offsetof(AudioBufferList, mBuffers) ||
            bytes > storage.size() * sizeof(std::uint64_t))
            continue;
        const auto *buffers = reinterpret_cast<const AudioBufferList *>(storage.data());
        if (buffers->mNumberBuffers >
            (bytes - offsetof(AudioBufferList, mBuffers)) / sizeof(AudioBuffer))
            continue;
        bool hasOutput{};
        for (UInt32 i = 0; i < buffers->mNumberBuffers; ++i)
            hasOutput = hasOutput || buffers->mBuffers[i].mNumberChannels > 0;
        if (hasOutput) {
            names.add(juce::String(*name));
            outputs.push_back(device);
        }
    }
    names.appendNumbersToDuplicates(false, true);
    std::map<std::string, AudioObjectID> result;
    for (int i = 0; i < names.size(); ++i)
        result.emplace(names[i].toStdString(), outputs[static_cast<std::size_t>(i)]);
    return result;
}
#endif
class JuceOutput final : public OutputDriver,
                         private juce::AudioIODeviceCallback,
                         private juce::AudioIODeviceType::Listener {
  public:
    ~JuceOutput() override {
        stop();
        onMessageThread([this] { types_.clear(); });
    }
    void
    configureOutput(const AudioDeviceConfiguration &configuration) override {
        onMessageThread([&] { configuration_ = configuration; });
    }
    AudioDeviceCatalog audioDevices() override {
        AudioDeviceCatalog catalog;
        onMessageThread([&] { catalog = discover(); });
        return catalog;
    }
    std::optional<ActiveAudioDevice> activeDevice() override {
        std::optional<ActiveAudioDevice> result;
        onMessageThread([&] { result = active_; });
        return result;
    }
    std::string start(PlaybackRuntime &runtime, std::uint32_t rate) override {
        std::string error;
        onMessageThread([&] {
            close();
            const auto catalog = discover();
            if (!catalog.error.empty()) {
                error = catalog.error;
                return;
            }
            const auto selected = std::find_if(
                catalog.devices.begin(), catalog.devices.end(),
                [&](const AudioDeviceInfo &info) {
                    return configuration_.mode == AudioDeviceMode::systemDefault
                               ? info.isDefault
                               : info.id == configuration_.deviceId;
                });
            if (selected == catalog.devices.end()) {
                error = "selected output unavailable";
                return;
            }
            for (auto *type : types_)
                if (type->getTypeName().toStdString() == selected->type) {
                    selectedType_ = type;
                    break;
                }
#if JUCE_MAC
            if (selectedType_ && selected->type == "CoreAudio") {
                const auto outputs = coreAudioOutputs();
                const auto found = outputs ? outputs->find(selected->name)
                                           : std::map<std::string, AudioObjectID>::const_iterator{};
                if (!outputs || found == outputs->end() ||
                    !detail::juceAggregateMetadataReadable(found->second)) {
                    error = "selected output metadata changed or aggregate clock is unavailable";
                    close();
                    return;
                }
            }
#endif
            if (selectedType_)
                device_.reset(selectedType_->createDevice(
                    juce::String(selected->name), {}));
            if (!device_) {
                error = "selected output could not be created";
                close();
                return;
            }
            chosenOutput_ = juce::String(selected->name);
            const auto block = configuration_.bufferFrames
                                   ? configuration_.bufferFrames
                                   : device_->getDefaultBufferSize();
            if (device_->getOutputChannelNames().size() < 2 ||
                !device_->getAvailableSampleRates().contains(
                    static_cast<double>(rate)) ||
                block <= 0 ||
                block > static_cast<int>(AudioRenderer::maxBlockFrames) ||
                (configuration_.bufferFrames &&
                 !device_->getAvailableBufferSizes().contains(block))) {
                runtime.deviceFailed(PlaybackError::unsupportedDevice);
                error = "selected output does not support the prepared stereo "
                        "rate or buffer";
                close();
                return;
            }
            runtime_.store(&runtime, std::memory_order_release);
            intentionalStop_.store(false, std::memory_order_release);
            juce::BigInteger outputs;
            outputs.setRange(0, 2, true);
            error = device_->open({}, outputs, static_cast<double>(rate), block)
                        .toStdString();
            if (!error.empty()) {
                close();
                return;
            }
            selectedType_->addListener(this);
            listening_ = true;
            device_->start(this);
            if (!device_->isPlaying()) {
                error = "selected output did not start";
                close();
                return;
            }
            if (device_->getCurrentBufferSizeSamples() != block) {
                runtime.deviceFailed(PlaybackError::unsupportedDevice);
                error = "selected output changed the requested buffer size";
                close();
                return;
            }
            active_ = ActiveAudioDevice{
                selected->id,
                selected->name,
                selected->type,
                static_cast<std::uint32_t>(device_->getCurrentSampleRate()),
                device_->getCurrentBufferSizeSamples(),
                device_->getActiveOutputChannels().countNumberOfSetBits()};
        });
        return error;
    }
    void stop() noexcept override {
        // JUCE flushes all callbacks before stop returns.
        onMessageThread([this] { close(); });
    }

  private:
    juce::OwnedArray<juce::AudioIODeviceType> types_;
    std::unique_ptr<juce::AudioIODevice> device_;
    juce::AudioIODeviceType *selectedType_{};
    juce::String chosenOutput_;
    AudioDeviceConfiguration configuration_;
    std::optional<ActiveAudioDevice> active_;
    std::atomic<PlaybackRuntime *> runtime_{};
    std::atomic<bool> intentionalStop_{true};
    bool listening_{};
    AudioDeviceCatalog discover() {
        AudioDeviceCatalog result;
        result.available = true;
        if (types_.isEmpty()) {
            juce::AudioDeviceManager discovery;
            discovery.createAudioDeviceTypes(types_);
        }
        std::set<std::string> ids;
        bool foundDefault{};
        for (auto *type : types_) {
            type->scanForDevices();
            const auto names = type->getDeviceNames(false);
            const auto defaultIndex = type->getDefaultDeviceIndex(false);
#if JUCE_MAC
            const auto coreOutputs = type->getTypeName() == "CoreAudio"
                                         ? coreAudioOutputs()
                                         : std::optional<std::map<std::string, AudioObjectID>>{};
            if (type->getTypeName() == "CoreAudio" && !coreOutputs) {
                result.error = "CoreAudio output metadata is unavailable";
                return result;
            }
#endif
            for (int index = 0; index < names.size(); ++index) {
#if JUCE_MAC
                if (coreOutputs) {
                    const auto device = coreOutputs->find(names[index].toStdString());
                    if (device == coreOutputs->end() ||
                        !detail::juceAggregateMetadataReadable(device->second))
                        continue;
                }
#endif
                AudioDeviceInfo info;
                info.name = names[index].toStdString();
                info.type = type->getTypeName().toStdString();
                info.id = deviceId(info.type, info.name);
                if (result.devices.size() >= 128 || info.name.size() > 256 ||
                    info.type.size() > 256 || !ids.insert(info.id).second) {
                    result.devices.clear();
                    result.error = "output catalog is too large or has "
                                   "ambiguous identities";
                    return result;
                }
                std::unique_ptr<juce::AudioIODevice> metadata(
                    type->createDevice(names[index], {}));
                if (!metadata) {
                    result.devices.clear();
                    result.error = "output metadata could not be inspected";
                    return result;
                }
                info.isDefault = !foundDefault && index == defaultIndex;
                foundDefault = foundDefault || info.isDefault;
                info.outputChannels = metadata->getOutputChannelNames().size();
                for (double rate : metadata->getAvailableSampleRates()) {
                    if (std::isfinite(rate) && rate >= 8000 && rate <= 384000 &&
                        rate == std::floor(rate))
                        info.sampleRates.push_back(
                            static_cast<std::uint32_t>(rate));
                    if (info.sampleRates.size() > 64)
                        break;
                }
                for (int block : metadata->getAvailableBufferSizes()) {
                    if (block > 0 &&
                        block <=
                            static_cast<int>(AudioRenderer::maxBlockFrames))
                        info.bufferFrames.push_back(block);
                    if (info.bufferFrames.size() > 64)
                        break;
                }
                if (info.sampleRates.size() > 64 ||
                    info.bufferFrames.size() > 64) {
                    result.devices.clear();
                    result.error = "output format catalog exceeds capacity";
                    return result;
                }
                info.defaultBufferFrames = metadata->getDefaultBufferSize();
                result.devices.push_back(std::move(info));
            }
        }
        return result;
    }
    void close() {
        intentionalStop_.store(true, std::memory_order_release);
        if (listening_ && selectedType_)
            selectedType_->removeListener(this);
        listening_ = false;
        if (device_) {
            device_->stop();
            device_->close();
            device_.reset();
        }
        runtime_.store(nullptr, std::memory_order_release);
        selectedType_ = nullptr;
        chosenOutput_.clear();
        active_.reset();
    }
    void audioDeviceIOCallbackWithContext(
        const float *const *, int, float *const *outputs, int channels,
        int frames, const juce::AudioIODeviceCallbackContext &) override {
        if (auto *runtime = runtime_.load(std::memory_order_acquire))
            runtime->process(outputs, channels, frames);
        else if (outputs && frames > 0)
            for (int channel = 0;
                 channel < std::min(channels, OutputDriver::maxOutputChannels);
                 ++channel)
                if (outputs[channel])
                    std::fill_n(
                        outputs[channel],
                        std::min(frames, OutputDriver::maxCallbackFrames),
                        0.0f);
    }
    void audioDeviceAboutToStart(juce::AudioIODevice *device) override {
        if (auto *runtime = runtime_.load(std::memory_order_acquire))
            runtime->deviceStarted(
                device->getCurrentSampleRate(),
                device->getCurrentBufferSizeSamples(),
                device->getActiveOutputChannels().countNumberOfSetBits());
    }
    void audioDeviceStopped() override {
        if (auto *runtime = runtime_.load(std::memory_order_acquire);
            runtime && !intentionalStop_.load(std::memory_order_acquire))
            runtime->deviceFailed();
    }
    void audioDeviceError(const juce::String &) override {
        if (auto *runtime = runtime_.load(std::memory_order_acquire))
            runtime->deviceFailed(PlaybackError::deviceUnavailable);
    }
    void audioDeviceListChanged() override {
        // The pinned CoreAudio implementation dispatches this on the message
        // thread.
        if (!selectedType_ || !device_)
            return;
        const auto names = selectedType_->getDeviceNames(false);
        const auto index = selectedType_->getDefaultDeviceIndex(false);
        const bool changed =
            !names.contains(chosenOutput_) ||
            (configuration_.mode == AudioDeviceMode::systemDefault &&
             (index < 0 || index >= names.size() ||
              names[index] != chosenOutput_));
        if (changed)
            if (auto *runtime = runtime_.load(std::memory_order_acquire))
                runtime->deviceFailed();
    }
};
} // namespace
std::unique_ptr<OutputDriver> makeJuceOutputDriver() {
    return std::make_unique<JuceOutput>();
}
} // namespace atlas
