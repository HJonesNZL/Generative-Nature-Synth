#include "../src/coreaudio_output_safety.hpp"
#include <iostream>
#include <stdexcept>
namespace {
struct Properties {
    bool aggregate{true}, clockNull{}, clockEmpty{}, mainNull{}, mainEmpty{};
    bool propertyFailure{}, activeSubdevice{};
    int stringReads{};
} properties;
void check(bool condition) {
    if (!condition)
        throw std::runtime_error("CoreAudio aggregate safety regression");
}
OSStatus read(AudioObjectID, const AudioObjectPropertyAddress *address,
              UInt32, const void *, UInt32 *size, void *data) {
    if (address->mSelector == kAudioObjectPropertyClass) {
        *static_cast<AudioClassID *>(data) = properties.aggregate
            ? static_cast<AudioClassID>(kAudioAggregateDeviceClassID)
            : static_cast<AudioClassID>(kAudioDeviceClassID);
        *size = sizeof(AudioClassID);
        return noErr;
    }
    if (properties.propertyFailure)
        return kAudioHardwareUnspecifiedError;
    if (address->mSelector == kAudioAggregateDevicePropertyActiveSubDeviceList) {
        *static_cast<AudioObjectID *>(data) = 99;
        *size = sizeof(AudioObjectID);
        return noErr;
    }
    ++properties.stringReads;
    const bool clock = address->mSelector == kAudioAggregateDevicePropertyClockDevice;
    const bool main = address->mSelector == kAudioAggregateDevicePropertyMainSubDevice;
    auto &value = *static_cast<CFStringRef *>(data);
    value = nullptr;
    *size = sizeof(CFStringRef);
    if ((clock && properties.clockNull) || (main && properties.mainNull))
        return noErr; // Exact success/null failure from the crash boundary.
    const bool empty = (clock && properties.clockEmpty) || (main && properties.mainEmpty);
    value = static_cast<CFStringRef>(CFRetain(empty ? CFSTR("") : CFSTR("valid-device-uid")));
    return noErr;
}
OSStatus sizeOf(AudioObjectID, const AudioObjectPropertyAddress *, UInt32,
                const void *, UInt32 *size) {
    *size = properties.activeSubdevice ? sizeof(AudioObjectID) : 0;
    return noErr;
}
bool safe() { return atlas::detail::juceAggregateMetadataReadable(42, read, sizeOf); }
}
int main() {
    try {
        properties = {};
        properties.clockNull = true;
        check(!safe());
        properties = {};
        properties.clockEmpty = properties.mainNull = true;
        check(!safe());
        properties = {};
        properties.propertyFailure = true;
        check(!safe());
        properties = {};
        properties.aggregate = false;
        properties.clockNull = properties.mainNull = true;
        check(safe() && properties.stringReads == 0); // Real/virtual hardware unaffected.
        properties = {};
        properties.mainNull = true;
        check(safe() && properties.stringReads == 1); // Nonempty clock needs no main.
        properties = {};
        properties.clockEmpty = true;
        check(safe());
        properties = {};
        properties.clockEmpty = properties.mainEmpty = true;
        check(!safe()); // Disconnected aggregate.
        properties.activeSubdevice = true;
        check(safe()); // Valid aggregate's active-subdevice fallback retained.
        std::cout << "CoreAudio nullable aggregate metadata tests passed (injected only)\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
