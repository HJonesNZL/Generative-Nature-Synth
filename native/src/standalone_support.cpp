#include "standalone_support.hpp"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace atlas::standalone {
juce::String string(const fs::path &value) {
    return juce::String(value.string());
}
fs::path path(const juce::String &value) {
    return fs::path(value.toStdString());
}
bool opaqueId(std::string_view id) {
    return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
namespace {
void regular(const fs::path &file) {
    if (!fs::is_regular_file(fs::symlink_status(file)))
        throw std::runtime_error(
            "A regular file is required; symlinks are not accepted.");
}
std::string read(const fs::path &file, std::uintmax_t maximum) {
    regular(file);
    const auto size = fs::file_size(file);
    if (size > maximum)
        throw std::runtime_error("File exceeds supported capacity.");
    std::ifstream input(file, std::ios::binary);
    std::string result(static_cast<std::size_t>(size), '\0');
    if (!input.read(result.data(), static_cast<std::streamsize>(result.size())))
        throw std::runtime_error("File could not be read.");
    return result;
}
void atomicWrite(const fs::path &destination, const std::string &bytes) {
    if (fs::exists(fs::symlink_status(destination)))
        regular(destination);
    const auto temporary = destination.parent_path() /
                           (".state-" + juce::Uuid().toString().toStdString());
    const int descriptor = ::open(
        temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (descriptor < 0)
        throw std::runtime_error("Could not create application settings.");
    bool closed{};
    try {
        std::size_t position{};
        while (position < bytes.size()) {
            const auto count = ::write(descriptor, bytes.data() + position,
                                       bytes.size() - position);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                throw std::runtime_error(
                    "Could not write application settings.");
            position += static_cast<std::size_t>(count);
        }
        if (::fsync(descriptor) != 0)
            throw std::runtime_error("Could not flush application settings.");
        closed = true;
        if (::close(descriptor) != 0)
            throw std::runtime_error("Could not close application settings.");
        fs::rename(temporary, destination);
    } catch (...) {
        if (!closed)
            ::close(descriptor);
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}
// JUCE's object parser retains only the last duplicate property. Count the
// lexical keys as well so an otherwise valid schema cannot hide duplicates.
std::size_t jsonKeyCount(std::string_view json) {
    std::size_t count{};
    for (std::size_t i = 0; i < json.size(); ++i) {
        if (json[i] != '"')
            continue;
        for (++i; i < json.size(); ++i) {
            if (json[i] == '\\') {
                ++i;
                continue;
            }
            if (json[i] == '"')
                break;
        }
        auto next = i + 1;
        while (next < json.size() && (json[next] == ' ' || json[next] == '\t' ||
                                      json[next] == '\r' || json[next] == '\n'))
            ++next;
        if (next < json.size() && json[next] == ':')
            ++count;
    }
    return count;
}
bool fields(const juce::var &value,
            std::initializer_list<const char *> allowed) {
    const auto *object = value.getDynamicObject();
    if (!object ||
        object->getProperties().size() != static_cast<int>(allowed.size()))
        return false;
    for (const auto &property : object->getProperties()) {
        if (std::none_of(allowed.begin(), allowed.end(), [&](const char *name) {
                return property.name.toString() == juce::String(name);
            }))
            return false;
    }
    return true;
}
} // namespace
void prepareStateDirectory(const fs::path &directory) {
    fs::create_directories(directory);
    if (!fs::is_directory(fs::symlink_status(directory)))
        throw std::runtime_error(
            "Application data must be a directory, not a symlink.");
    for (const auto *child : {"soundscapes", "exports"}) {
        const auto target = directory / child;
        fs::create_directories(target);
        if (!fs::is_directory(fs::symlink_status(target)))
            throw std::runtime_error(
                "Application data child is not a directory.");
    }
    for (const auto *child : {"voices.sqlite3", "settings.json"})
        if (fs::exists(fs::symlink_status(directory / child)))
            regular(directory / child);
}
Settings loadSettings(const fs::path &directory) {
    const auto file = directory / "settings.json";
    if (!fs::exists(fs::symlink_status(file)))
        return {};
    juce::var json;
    const auto bytes = read(file, 8192);
    const auto result = juce::JSON::parse(juce::String(bytes), json);
    if (result.failed() || !fields(json, {"version", "library", "output"}) ||
        !json["version"].isInt() || static_cast<int>(json["version"]) != 1 ||
        !(json["library"].isVoid() || json["library"].isString()) ||
        json["library"].toString().getNumBytesAsUTF8() > 4096)
        throw std::runtime_error(
            "Saved application settings are invalid; the file was retained.");
    Settings settings;
    if (json["library"].isString()) {
        settings.library = path(json["library"].toString());
        if (!settings.library.is_absolute())
            throw std::runtime_error("Saved library path must be absolute.");
    }
    const auto output = json["output"];
    if (jsonKeyCount(bytes) != (output.isVoid() ? 3u : 6u))
        throw std::runtime_error("Saved application settings contain duplicate "
                                 "fields; the file was retained.");
    if (!output.isVoid()) {
        if (!fields(output, {"mode", "deviceId", "bufferFrames"}) ||
            !output["mode"].isString() ||
            !(output["deviceId"].isVoid() || output["deviceId"].isString()) ||
            !output["bufferFrames"].isInt())
            throw std::runtime_error(
                "Saved audio settings are invalid; the file was retained.");
        const auto mode = output["mode"].toString();
        const int frames = output["bufferFrames"];
        if ((mode != "systemDefault" && mode != "explicitDevice") ||
            frames < 0 || frames > 4096 ||
            output["deviceId"].toString().getNumBytesAsUTF8() > 64 ||
            (mode == "explicitDevice" &&
             (!output["deviceId"].isString() ||
              output["deviceId"].toString().isEmpty() ||
              !output["deviceId"].toString().containsOnly(
                  "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567"
                  "89-"))) ||
            (mode == "systemDefault" && !output["deviceId"].isVoid()))
            throw std::runtime_error("Saved audio configuration is "
                                     "unsupported; the file was retained.");
        settings.output = output;
    }
    return settings;
}
void saveSettings(const fs::path &directory, const Settings &settings) {
    juce::DynamicObject::Ptr json = new juce::DynamicObject;
    json->setProperty("version", 1);
    json->setProperty("library", settings.library.empty()
                                     ? juce::var()
                                     : juce::var(string(settings.library)));
    json->setProperty("output", settings.output);
    atomicWrite(directory / "settings.json",
                juce::JSON::toString(json.get(), true).toStdString());
}
Assets loadAssets(const fs::path &directory) {
    if (!fs::is_directory(fs::symlink_status(directory)))
        throw std::runtime_error("Bundled interface is missing.");
    Assets assets;
    std::uintmax_t total{};
    const std::map<std::string, juce::String> mime{
        {".html", "text/html"},    {".js", "text/javascript"},
        {".css", "text/css"},      {".json", "application/json"},
        {".svg", "image/svg+xml"}, {".png", "image/png"},
        {".woff2", "font/woff2"},  {".txt", "text/plain"},
        {".md", "text/plain"}};
    for (const auto &entry : fs::recursive_directory_iterator(directory)) {
        if (entry.is_symlink())
            throw std::runtime_error("Bundled interface contains a symlink.");
        if (entry.is_directory())
            continue;
        if (assets.size() == 512)
            throw std::runtime_error(
                "Bundled interface exceeds file capacity.");
        auto bytes = read(entry.path(), 16 * 1024 * 1024);
        total += bytes.size();
        if (total > 64 * 1024 * 1024)
            throw std::runtime_error(
                "Bundled interface exceeds byte capacity.");
        const auto extension = entry.path().extension().string();
        const auto type = mime.find(extension);
        juce::WebBrowserComponent::Resource resource;
        resource.mimeType =
            type == mime.end() ? "application/octet-stream" : type->second;
        resource.data.resize(bytes.size());
        std::transform(bytes.begin(), bytes.end(), resource.data.begin(),
                       [](char byte) { return static_cast<std::byte>(byte); });
        assets.emplace(
            "/" + entry.path().lexically_relative(directory).generic_string(),
            std::move(resource));
    }
    if (!assets.contains("/index.html"))
        throw std::runtime_error("Bundled interface entry is missing.");
    return assets;
}
std::string importScene(const fs::path &source, const fs::path &directory) {
    regular(source);
    // The full sealed recipe is verified before anything becomes visible in the
    // catalogue.
    const auto scene = loadSoundscape(source);
    const auto id = juce::Uuid().toString().toStdString();
    if (!opaqueId(id))
        throw std::runtime_error("Could not generate a soundscape identifier.");
    saveSoundscape(directory / (id + ".atlas-scene"), scene);
    return scene.capture().name;
}
void copyExport(const fs::path &directory, const std::string &artifactId,
                const fs::path &destination) {
    if (!opaqueId(artifactId))
        throw std::runtime_error("Invalid WAV artifact identifier.");
    const auto source = directory / (artifactId + ".wav");
    regular(source);
    if (fs::weakly_canonical(destination.parent_path()) ==
        fs::weakly_canonical(directory))
        throw std::runtime_error(
            "Choose a destination outside the managed export directory.");
    // copy_file without overwrite uses an exclusive destination. Existing user
    // files survive.
    if (!fs::copy_file(source, destination, fs::copy_options::none))
        throw std::runtime_error("The WAV could not be saved.");
}
} // namespace atlas::standalone
