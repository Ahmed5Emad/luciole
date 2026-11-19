#include "fs/file_icon_source.h"
#include "fs/folder_icon.h"
#include "fs/gio_interop.h"
#include "system/icon_resolver.h"
#include "test_check.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <print>

namespace {
  namespace fs = std::filesystem;
  using luciole::FolderCustomIcon;
  using luciole::GioPtr;

  GioPtr<GFileInfo> directoryInfo(const char* metadataIcon = nullptr) {
    GioPtr<GFileInfo> info(g_file_info_new());
    g_file_info_set_name(info.get(), "child");
    g_file_info_set_file_type(info.get(), G_FILE_TYPE_DIRECTORY);
    GioPtr<GIcon> themed(g_themed_icon_new("folder"));
    g_file_info_set_icon(info.get(), themed.get());
    if (metadataIcon != nullptr) {
      g_file_info_set_attribute_string(info.get(), luciole::kFolderCustomIconAttribute, metadataIcon);
    }
    return info;
  }

  void directoryFileRoundtrip(const fs::path& root) {
    const auto folder = root / "styled";
    fs::create_directory(folder);
    const auto location = luciole::Location::fromPath(folder.native());

    // Nothing set yet.
    TEST_CHECK(!luciole::readFolderCustomIcon(location, nullptr).has_value());
    TEST_CHECK(!luciole::folderIconFromDirectoryFile(folder.native()).has_value());

    // Local folders always take the .directory entry (for the fast listing
    // path) plus the metadata store when it is available.
    std::string error;
    TEST_CHECK(luciole::setFolderCustomIcon(location, "folder-red", error));
    const auto stored = luciole::readFolderCustomIcon(location, nullptr);
    TEST_CHECK(stored && *stored == "folder-red");
    const auto direct = luciole::folderIconFromDirectoryFile(folder.native());
    TEST_CHECK(direct && direct->name == "folder-red" && !direct->isPath);
    TEST_CHECK(direct->origin == luciole::FolderIconOrigin::DirectoryFile);

    // The fast listing path picks it up without GFileInfo.
    const auto local = luciole::folderIconForLocal(root.native(), "styled");
    TEST_CHECK(local && local->name == "folder-red");

    TEST_CHECK(luciole::clearFolderCustomIcon(location, error));
    TEST_CHECK(!luciole::readFolderCustomIcon(location, nullptr).has_value());
    TEST_CHECK(!fs::exists(folder / ".directory"));
  }

  void dolphinFileIsRead(const fs::path& root) {
    const auto folder = root / "dolphin";
    fs::create_directory(folder);
    std::ofstream(folder / ".directory") << "[Desktop Entry]\nIcon=folder-blue\n";
    const auto location = luciole::Location::fromPath(folder.native());
    const auto stored = luciole::readFolderCustomIcon(location, nullptr);
    TEST_CHECK(stored && *stored == "folder-blue");

    // Clearing removes a hand-written .directory Icon too, deleting the file
    // when nothing else remains.
    std::string error;
    TEST_CHECK(luciole::clearFolderCustomIcon(location, error));
    TEST_CHECK(!luciole::readFolderCustomIcon(location, nullptr).has_value());
    TEST_CHECK(!fs::exists(folder / ".directory"));

    // Applying an absolute path stores it as filePath, not as a themed name.
    luciole::FileIconSource source;
    source.names = {"folder"};
    luciole::applyFolderCustomIcon(
        source,
        FolderCustomIcon{.name = "/tmp/custom.png", .origin = luciole::FolderIconOrigin::DirectoryFile, .isPath = true}
    );
    TEST_CHECK(source.filePath == "/tmp/custom.png");
    TEST_CHECK(source.names.size() == 1 && source.names.front() == "folder");

    luciole::FileIconSource themed;
    themed.names = {"folder", "inode-directory"};
    luciole::applyFolderCustomIcon(
        themed, FolderCustomIcon{.name = "folder-green", .origin = luciole::FolderIconOrigin::Metadata, .isPath = false}
    );
    TEST_CHECK(themed.names.size() == 3 && themed.names.front() == "folder-green");
    // Applying twice does not duplicate.
    luciole::applyFolderCustomIcon(
        themed, FolderCustomIcon{.name = "folder-green", .origin = luciole::FolderIconOrigin::Metadata, .isPath = false}
    );
    TEST_CHECK(themed.names.size() == 3);
  }

  void metadataBeatsDirectoryFile(const fs::path& root) {
    const auto folder = root / "both";
    fs::create_directory(folder);
    std::ofstream(folder / ".directory") << "[Desktop Entry]\nIcon=folder-blue\n";
    const auto location = luciole::Location::fromPath(folder.native());

    auto info = directoryInfo("folder-red");
    const auto direct = luciole::folderIconFromInfo(info.get());
    TEST_CHECK(direct && direct->name == "folder-red");
    TEST_CHECK(direct->origin == luciole::FolderIconOrigin::Metadata);

    auto resolved = luciole::folderIconFor(info.get(), location);
    TEST_CHECK(resolved && resolved->name == "folder-red");

    // Without the metadata attribute the .directory file wins.
    auto plain = directoryInfo();
    resolved = luciole::folderIconFor(plain.get(), location);
    TEST_CHECK(resolved && resolved->name == "folder-blue");

    // The pooled GIO path prepends the override instead of sharing the generic icon.
    luciole::FileIconPool pool;
    const auto shared = pool.get(info.get(), luciole::Location::fromPath(root.native()));
    TEST_CHECK(!shared->names.empty() && shared->names.front() == "folder-red");
    const auto generic = pool.get(plain.get(), luciole::Location::fromPath(root.native()));
    TEST_CHECK(generic->names.front() != "folder-red");

    // The pooled local path does the same from the parent path alone.
    const auto local =
        pool.getLocal("both", G_FILE_TYPE_DIRECTORY, "inode/directory", luciole::Location::fromPath(root.native()));
    TEST_CHECK(!local->names.empty() && local->names.front() == "folder-blue");
    fs::create_directory(root / "plain");
    const auto localGeneric =
        pool.getLocal("plain", G_FILE_TYPE_DIRECTORY, "inode/directory", luciole::Location::fromPath(root.native()));
    TEST_CHECK(localGeneric->names.front() != "folder-blue");
  }

  void themeListingIsSane() {
    const auto names = IconResolver::listThemeIcons();
    for (const auto& name : names) {
      // Double extensions (foo.png.svg) are legitimate resolvable names.
      TEST_CHECK(!name.empty() && !name.contains('/'));
    }
    TEST_CHECK(std::ranges::is_sorted(names));
    TEST_CHECK(std::ranges::adjacent_find(names) == names.end());
  }
} // namespace

int main() {
  const luciole::GString temporary(g_dir_make_tmp("luciole-folder-icon-XXXXXX", nullptr));
  TEST_CHECK(temporary);
  const fs::path root(temporary.get());
  directoryFileRoundtrip(root);
  dolphinFileIsRead(root);
  metadataBeatsDirectoryFile(root);
  themeListingIsSane();
  fs::remove_all(root);
  std::println("folder icon checks passed");
}
