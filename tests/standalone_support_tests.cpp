#include "standalone_support.hpp"
#include <atlas/projects.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace atlas::standalone;
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class Function> void rejects(Function action, const char *message) {
    bool rejected{};
    try {
        action();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, message);
}
void write(const fs::path &file, std::string_view bytes) {
    std::ofstream output(file);
    output << bytes;
}
} // namespace
int main() {
    const auto temporary =
        fs::temp_directory_path() /
        ("atlas-standalone-support-" + juce::Uuid().toString().toStdString());
    try {
        prepareStateDirectory(temporary);
        const auto canonicalTemporary = fs::canonical(temporary);
        const auto parentAlias = temporary / "parent-alias";
        fs::create_directory_symlink(canonicalTemporary, parentAlias);
        require(
            selectedPath(parentAlias / "new-state") ==
                canonicalTemporary / "new-state",
            "selected parent aliases must resolve before strict project I/O");
        rejects([&] { selectedPath(parentAlias); },
                "a selected final directory symlink must fail");
        const auto backupAlias = parentAlias / "alias.atlas-project";
        atlas::createProjectBackup({}, {}, selectedPath(backupAlias));
        rejects([&] { atlas::inspectProjectBackup(backupAlias); },
                "portable archive validation must continue rejecting parent "
                "symlinks");
        require(
            atlas::inspectProjectBackup(selectedPath(backupAlias)).sceneCount ==
                0,
            "shell-selected backup through a parent alias must inspect");
        atlas::restoreProjectBackup(
            selectedPath(backupAlias),
            selectedPath(parentAlias / "restored-alias"));
        require(
            fs::is_regular_file(canonicalTemporary / "restored-alias" /
                                "voices.sqlite3"),
            "shell-selected restore destination through an alias must work");
        fs::create_symlink(canonicalTemporary / "alias.atlas-project",
                           temporary / "final-link.atlas-project");
        rejects([&] { selectedPath(temporary / "final-link.atlas-project"); },
                "a selected final file symlink must fail");
        fs::create_symlink(canonicalTemporary / "absent.atlas-project",
                           temporary / "dangling-link.atlas-project");
        rejects(
            [&] { selectedPath(temporary / "dangling-link.atlas-project"); },
            "a selected dangling final symlink must fail");
        require(loadSettings(temporary).library.empty(),
                "fresh state must be scene-only");
        Settings settings;
        settings.library = temporary / "missing-library.sqlite3";
        juce::DynamicObject::Ptr output = new juce::DynamicObject;
        output->setProperty("mode", "explicitDevice");
        output->setProperty("deviceId", "disconnected-123");
        output->setProperty("bufferFrames", 512);
        settings.output = output.get();
        saveSettings(temporary, settings);
        const auto restored = loadSettings(temporary);
        require(restored.library == settings.library,
                "missing library preference must survive");
        require(restored.output["deviceId"].toString() == "disconnected-123",
                "explicit unavailable device must survive");
        write(temporary / "settings.json",
              "{\"version\":1,\"library\":null,\"output\":null,\"unexpected\":"
              "true}");
        rejects([&] { loadSettings(temporary); },
                "unknown state fields must fail");
        require(fs::file_size(temporary / "settings.json") > 0,
                "corrupt settings must remain");
        write(temporary / "settings.json",
              R"({"version":1,"library":null,"output":null,"version":1})");
        rejects([&] { loadSettings(temporary); },
                "duplicate state fields must fail");
        fs::remove(temporary / "settings.json");
        fs::create_symlink(temporary / "voices.sqlite3",
                           temporary / "settings.json");
        rejects([&] { loadSettings(temporary); }, "state symlinks must fail");
        fs::remove(temporary / "settings.json");
        const auto ui = temporary / "ui";
        fs::create_directory(ui);
        write(ui / "index.html", "<html>local</html>");
        write(ui / "app.js", "window.local=true;");
        auto assets = loadAssets(ui);
        require(assets.size() == 2 &&
                    assets.at("/app.js").mimeType == "text/javascript",
                "static asset map must retain MIME");
        fs::create_symlink(ui / "app.js", ui / "link.js");
        rejects([&] { loadAssets(ui); }, "resource symlinks must fail");
        fs::remove(ui / "link.js");
        write(temporary / "invalid.atlas-scene", "not a scene");
        rejects(
            [&] {
                importScene(temporary / "invalid.atlas-scene",
                            temporary / "soundscapes");
            },
            "unverified scenes must fail");
        require(fs::is_empty(temporary / "soundscapes"),
                "failed import must publish nothing");
        require(managedScenes(temporary / "soundscapes").empty(),
                "fresh backup selection must be empty");
        rejects(
            [&] { managedScenePath(temporary / "soundscapes", "../invalid"); },
            "managed scene traversal must fail");
        const std::string sceneId(32, 'b');
        write(temporary / "soundscapes" / (sceneId + ".atlas-scene"),
              "corrupt");
        rejects([&] { managedScenes(temporary / "soundscapes"); },
                "backup selection must fully verify scenes");
        rejects(
            [&] {
                copyScene(temporary / "soundscapes", sceneId,
                          temporary / "scene.atlas-scene");
            },
            "corrupt scene export must fail");
        require(!fs::exists(temporary / "scene.atlas-scene"),
                "failed scene export must publish nothing");
        fs::remove(temporary / "soundscapes" / (sceneId + ".atlas-scene"));
        fs::create_symlink(temporary / "invalid.atlas-scene",
                           temporary / "soundscapes" /
                               (sceneId + ".atlas-scene"));
        rejects([&] { managedScenes(temporary / "soundscapes"); },
                "backup selection must reject scene symlinks");
        fs::remove(temporary / "soundscapes" / (sceneId + ".atlas-scene"));
        const std::string id(32, 'a');
        write(temporary / "exports" / (id + ".wav"), "wav artifact");
        const auto destination = temporary / "saved.wav";
        copyExport(temporary / "exports", id, destination);
        require(fs::file_size(destination) == 12, "export bytes must copy");
        rejects([&] { copyExport(temporary / "exports", id, destination); },
                "export must not overwrite");
        rejects(
            [&] {
                copyExport(temporary / "exports", "../invalid",
                           temporary / "bad.wav");
            },
            "artifact traversal must fail");
        rejects(
            [&] {
                copyExport(temporary / "exports", id,
                           temporary / "exports" / "copy.wav");
            },
            "managed artifact destination must fail");
        fs::remove_all(temporary);
        std::cout << "standalone support checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        fs::remove_all(temporary);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
