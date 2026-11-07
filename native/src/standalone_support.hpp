#pragma once
#include <atlas/soundscapes.hpp>
#include <filesystem>
#include <juce_gui_extra/juce_gui_extra.h>
#include <map>
#include <string>
#include <vector>

namespace atlas::standalone {
namespace fs = std::filesystem;
using Assets = std::map<std::string, juce::WebBrowserComponent::Resource>;
struct Settings {
    fs::path library;
    juce::var output;
};
Assets loadAssets(const fs::path &);
// Worker-only chooser boundary: resolve parent aliases, never a final symlink.
fs::path selectedPath(const fs::path &);
void prepareStateDirectory(const fs::path &);
Settings loadSettings(const fs::path &);
void saveSettings(const fs::path &, const Settings &);
std::string importScene(const fs::path &source, const fs::path &directory);
void copyExport(const fs::path &directory, const std::string &artifactId,
                const fs::path &destination);
fs::path managedExportPath(const fs::path &directory,
                           const std::string &artifactId);
struct ManagedScene {
    std::string id, name;
};
std::vector<ManagedScene> managedScenes(const fs::path &directory);
fs::path managedScenePath(const fs::path &directory, const std::string &id);
void copyScene(const fs::path &directory, const std::string &id,
               const fs::path &destination);
bool opaqueId(std::string_view);
juce::String string(const fs::path &);
fs::path path(const juce::String &);
} // namespace atlas::standalone
