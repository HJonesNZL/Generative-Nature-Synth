#include <algorithm>
#include <atlas/playback.hpp>
#include <atomic>
#include <exception>
#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <utility>

namespace atlas {
namespace {
static_assert(std::atomic<PlaybackRuntime *>::is_always_lock_free);
// Device lifecycle belongs to JUCE's message thread; the serial stdin worker
// waits off audio.
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
class JuceOutput final : public OutputDriver,
                         private juce::AudioIODeviceCallback,
                         private juce::AudioIODeviceType::Listener {
  public:
    ~JuceOutput() override { stop(); }
    std::string start(PlaybackRuntime &runtime, std::uint32_t rate) override {
        std::string error;
        onMessageThread([&] {
            close();
            runtime_.store(&runtime, std::memory_order_release);
            intentionalStop_.store(false, std::memory_order_release);
            // Discovery only: AudioDeviceManager is never inserted into the
            // signal callback path.
            juce::AudioDeviceManager discovery;
            discovery.createAudioDeviceTypes(types_);
            for (auto *type : types_) {
                type->scanForDevices();
                const auto names = type->getDeviceNames(false);
                const auto index = type->getDefaultDeviceIndex(false);
                if (index < 0 || index >= names.size())
                    continue;
                device_.reset(type->createDevice(names[index], {}));
                if (device_) {
                    selectedType_ = type;
                    break;
                }
            }
            if (!device_) {
                error = "default output device unavailable";
                close();
                return;
            }
            if (device_->getOutputChannelNames().size() < 2 ||
                !device_->getAvailableSampleRates().contains(
                    static_cast<double>(rate))) {
                runtime.deviceFailed(PlaybackError::unsupportedDevice);
                error = "default output does not support stereo at the "
                        "prepared sample rate";
                close();
                return;
            }
            auto block = device_->getDefaultBufferSize();
            if (block <= 0 ||
                block > static_cast<int>(AudioRenderer::maxBlockFrames)) {
                runtime.deviceFailed(PlaybackError::unsupportedDevice);
                error = "default output block size unsupported";
                close();
                return;
            }
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
                error = "default output did not start";
                close();
            }
        });
        return error;
    }
    void stop() noexcept override {
        // JUCE AudioIODevice::stop guarantees all pending callback calls are
        // flushed before return.
        onMessageThread([this] { close(); });
    }

  private:
    juce::OwnedArray<juce::AudioIODeviceType> types_;
    std::unique_ptr<juce::AudioIODevice> device_;
    juce::AudioIODeviceType *selectedType_{};
    std::atomic<PlaybackRuntime *> runtime_{};
    std::atomic<bool> intentionalStop_{true};
    bool listening_{};
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
        types_.clear();
    }
    void audioDeviceIOCallbackWithContext(
        const float *const *, int, float *const *outputs, int channels,
        int frames, const juce::AudioIODeviceCallbackContext &) override {
        if (auto *runtime = runtime_.load(std::memory_order_acquire))
            runtime->process(outputs, channels, frames);
        else if (outputs && frames > 0) {
            for (int channel = 0;
                 channel < std::min(channels, OutputDriver::maxOutputChannels);
                 ++channel)
                if (outputs[channel])
                    std::fill_n(
                        outputs[channel],
                        std::min(frames, OutputDriver::maxCallbackFrames),
                        0.0f);
        }
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
        if (auto *runtime = runtime_.load(std::memory_order_acquire))
            runtime->deviceFailed(); // No automatic migration.
    }
};
} // namespace
std::unique_ptr<OutputDriver> makeJuceOutputDriver() {
    return std::make_unique<JuceOutput>();
}
} // namespace atlas
