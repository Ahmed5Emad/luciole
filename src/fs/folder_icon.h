#pragma once

#include "fs/file_icon_source.h"
#include "fs/location.h"

#include <gio/gio.h>
#include <optional>
#include <string>

namespace luciole {
  // GVFS metadata attribute Nautilus uses for per-folder artwork. Stored in the
  // user's metadata store, so it needs no write access inside the folder and
  // leaves no file behind. Unavailable when GVFS metadata is (tests, minimal
  // installs); the .directory fallback below covers those cases.
  inline constexpr const char* kFolderCustomIconAttribute = "metadata::custom-icon";

  enum class FolderIconOrigin { Metadata, DirectoryFile };

  struct FolderCustomIcon {
    // An icon-theme name, or an absolute path when the stored value is one.
    std::string name;
    FolderIconOrigin origin = FolderIconOrigin::Metadata;
    bool isPath = false;
  };

  // From an enumerated GFileInfo that may carry the metadata attribute.
  std::optional<FolderCustomIcon> folderIconFromInfo(GFileInfo* info);
  // Dolphin-compatible: [Desktop Entry] Icon=... in <folder>/.directory.
  std::optional<FolderCustomIcon> folderIconFromDirectoryFile(const std::string& folderPath);
  // Effective override for a directory entry: metadata first, then .directory
  // when the entry itself is a local folder. Null when neither is set.
  std::optional<FolderCustomIcon> folderIconFor(GFileInfo* info, const Location& entryLocation);
  // Local fast path without GFileInfo: reads <parentPath>/<name>/.directory.
  std::optional<FolderCustomIcon> folderIconForLocal(const std::string& parentPath, const char* name);
  // Prepends the override to the source: themed names go first, absolute
  // paths become filePath (skipped by the loader when missing).
  void applyFolderCustomIcon(FileIconSource& source, const FolderCustomIcon& icon);

  // Effective custom icon name for a folder, for preselecting the picker.
  // Blocking (queries GIO + one small file): call on a worker.
  std::optional<std::string> readFolderCustomIcon(const Location& folder, GCancellable* cancellable);
  // Stores the icon: metadata first, .directory fallback for local folders
  // when metadata is unavailable. Clears a stale .directory key on success so
  // a later reset cannot resurrect it. Blocking: call on a worker.
  bool setFolderCustomIcon(const Location& folder, const std::string& iconName, std::string& error);
  // Clears both stores best-effort; removes a .directory left without keys.
  // Blocking: call on a worker.
  bool clearFolderCustomIcon(const Location& folder, std::string& error);
} // namespace luciole
