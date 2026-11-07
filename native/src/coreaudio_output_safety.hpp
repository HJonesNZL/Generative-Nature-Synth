#pragma once
#if defined(__APPLE__)
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <optional>
#include <string>
#include <vector>

namespace atlas::detail {
using AudioPropertyReader = decltype(&AudioObjectGetPropertyData);
using AudioPropertySizer = decltype(&AudioObjectGetPropertyDataSize);
// Message-thread metadata only. Injectable property access reproduces drivers
// returning success with a null CFString without opening an audio device.
inline std::optional<std::string>
coreAudioString(AudioObjectID device, AudioObjectPropertySelector selector,
                AudioPropertyReader read = AudioObjectGetPropertyData) {
    const AudioObjectPropertyAddress address{
        selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    CFStringRef value{};
    UInt32 size = sizeof(value);
    const auto status = read(device, &address, 0, nullptr, &size, &value);
    const bool valid = status == noErr && size == sizeof(value) && value &&
                       CFGetTypeID(value) == CFStringGetTypeID();
    std::optional<std::string> result;
    if (valid) {
        char text[1024]{};
        if (CFStringGetCString(value, text, sizeof(text), kCFStringEncodingUTF8))
            result = text;
    }
    if (value)
        CFRelease(value);
    return result;
}
inline bool
juceAggregateMetadataReadable(AudioObjectID device,
                              AudioPropertyReader read = AudioObjectGetPropertyData,
                              AudioPropertySizer dataSize = AudioObjectGetPropertyDataSize) {
    const AudioObjectPropertyAddress address{
        kAudioObjectPropertyClass, kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain};
    AudioClassID deviceClass{};
    UInt32 size = sizeof(deviceClass);
    if (read(device, &address, 0, nullptr, &size, &deviceClass) != noErr ||
        size != sizeof(deviceClass))
        return false;
    if (deviceClass != kAudioAggregateDeviceClassID)
        return true; // Virtual and physical nonaggregate devices stay available.
    const auto clock = coreAudioString(device, kAudioAggregateDevicePropertyClockDevice, read);
    if (!clock)
        return false; // JUCE 9 calls CFStringGetLength here without a null check.
    if (!clock->empty())
        return true;
    const auto main = coreAudioString(device, kAudioAggregateDevicePropertyMainSubDevice, read);
    if (!main)
        return false;
    if (!main->empty())
        return true;
    // JUCE's final fallback chooses its first active subdevice. Keep valid
    // fallback aggregates, but omit disconnected aggregates with no clock UID.
    const AudioObjectPropertyAddress subdevices{
        kAudioAggregateDevicePropertyActiveSubDeviceList,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 bytes{};
    if (dataSize(device, &subdevices, 0, nullptr, &bytes) != noErr ||
        bytes == 0 || bytes % sizeof(AudioObjectID) != 0 ||
        bytes / sizeof(AudioObjectID) > 1024)
        return false;
    std::vector<AudioObjectID> ids(bytes / sizeof(AudioObjectID));
    const auto capacity = bytes;
    if (read(device, &subdevices, 0, nullptr, &bytes, ids.data()) != noErr ||
        bytes < sizeof(AudioObjectID) || bytes > capacity)
        return false;
    const auto uid = coreAudioString(ids.front(), kAudioDevicePropertyDeviceUID, read);
    return uid && !uid->empty();
}
} // namespace atlas::detail
#endif
