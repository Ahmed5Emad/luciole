#pragma once

#include "fs/file_icon_source.h"
#include "fs/location.h"

#include <gio/gio.h>
#include <optional>
#include <string>

namespace luciole {
  // GVFS metadata keys GNOME Files uses for per-folder artwork: custom-icon
  // holds a file:// URI to an image, custom-icon-name an icon-theme name.
  // Stored in the user's metadata store, so they need no write access inside
  // the folder and leave no file behind. Unavailable when GVFS metadata is
  // (tests, minimal installs); the .directory fallback below covers those.
  inline constexpr const char* kFolderCustomIconAttribute = "metadata::custom-icon";
  inline constexpr const char* kFolderCustomIconNameAttribute = "metadata::custom-icon-name";

  enum class FolderIconOrigin { Metadata, DirectoryFile };

  struct FolderCustomIcon {
    // An icon-theme name, or an absolute path when the stored value is one.
    std::string name;
    FolderIconOrigin origin = FolderIconOrigin::Metadata;
    bool isPath = false;
  };

  // From an enumerated GFileInfo that may carry the metadata keys: a file://
  // image URI in custom-icon first, then a theme name in custom-icon-name.
  std::optional<FolderCustomIcon> folderIconFromInfo(GFileInfo* info);
  // Dolphin-compatible: [Desktop Entry] Icon=... in <folder>/.directory.
  std::optional<FolderCustomIcon> folderIconFromDirectoryFile(const std::string& folderPath);
  // Effective override for a directory entry: the metadata keys first, then
  // .directory when the entry itself is a local folder. Null when none is set.
  std::optional<FolderCustomIcon> folderIconFor(GFileInfo* info, const Location& entryLocation);
  // Local path without GFileInfo: GVFS metadata keys plus .directory, in that
  // order. Blocking (queries GIO + one small file): call on a worker.
  std::optional<FolderCustomIcon> folderIconForLocal(const std::string& parentPath, const char* name);
  // Prepends the override to the source: themed names go first, absolute
  // paths become filePath (skipped by the loader when missing).
  void applyFolderCustomIcon(FileIconSource& source, const FolderCustomIcon& icon);

  // Effective custom icon name for a folder, for preselecting the picker.
  // Blocking (queries GIO + one small file): call on a worker.
  std::optional<std::string> readFolderCustomIcon(const Location& folder, GCancellable* cancellable);
  // Stores the icon: a theme name goes to metadata::custom-icon-name, an image
  // file to metadata::custom-icon as a file:// URI (the other key is cleared
  // so GNOME Files never sees a stale value), plus a .directory entry for
  // local folders. Clears a stale .directory key on success so a later reset
  // cannot resurrect it. Blocking: call on a worker.
  bool setFolderCustomIcon(const Location& folder, const std::string& iconName, std::string& error);
  // Clears both metadata keys best-effort; removes a .directory left without keys.
  // Blocking: call on a worker.
  bool clearFolderCustomIcon(const Location& folder, std::string& error);
} // namespace luciole
