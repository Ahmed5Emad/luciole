#include "fs/folder_icon.h"

#include "fs/gio_interop.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace {
  std::optional<luciole::FolderCustomIcon> normalize(std::string value, luciole::FolderIconOrigin origin) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
      return std::nullopt;
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    value = value.substr(begin, end - begin + 1);
    if (value.empty()) {
      return std::nullopt;
    }
    const bool isPath = value.front() == '/';
    return luciole::FolderCustomIcon{
        .name = std::move(value),
        .origin = origin,
        .isPath = isPath,
    };
  }

  std::optional<luciole::FolderCustomIcon>
  directoryFileIcon(const std::string& folderPath, luciole::FolderIconOrigin origin) {
    const std::string path = folderPath + "/.directory";
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
      return std::nullopt;
    }
    std::array<char, 256U * 1024U + 1> bytes{};
    std::size_t count = 0;
    while (count < bytes.size()) {
      const auto chunk = ::read(fd, bytes.data() + count, bytes.size() - count);
      if (chunk <= 0) {
        break;
      }
      count += static_cast<std::size_t>(chunk);
    }
    ::close(fd);
    if (count == bytes.size()) {
      return std::nullopt;
    }
    const std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> file(g_key_file_new(), &g_key_file_unref);
    if (!g_key_file_load_from_data(file.get(), bytes.data(), count, G_KEY_FILE_NONE, nullptr)) {
      return std::nullopt;
    }
    const luciole::GString icon(g_key_file_get_string(file.get(), "Desktop Entry", "Icon", nullptr));
    if (!icon) {
      return std::nullopt;
    }
    return normalize(icon.get(), origin);
  }
} // namespace

std::optional<luciole::FolderCustomIcon> luciole::folderIconFromInfo(GFileInfo* info) {
  if (info == nullptr) {
    return std::nullopt;
  }
  if (const char* value = infoString(info, kFolderCustomIconAttribute)) {
    return normalize(value, FolderIconOrigin::Metadata);
  }
  return std::nullopt;
}

std::optional<luciole::FolderCustomIcon> luciole::folderIconFromDirectoryFile(const std::string& folderPath) {
  return directoryFileIcon(folderPath, FolderIconOrigin::DirectoryFile);
}

std::optional<luciole::FolderCustomIcon> luciole::folderIconFor(GFileInfo* info, const Location& entryLocation) {
  if (auto icon = folderIconFromInfo(info)) {
    return icon;
  }
  if (const auto path = entryLocation.nativePath()) {
    return folderIconFromDirectoryFile(*path);
  }
  return std::nullopt;
}

std::optional<luciole::FolderCustomIcon> luciole::folderIconForLocal(const std::string& parentPath, const char* name) {
  if (name == nullptr || *name == '\0') {
    return std::nullopt;
  }
  return directoryFileIcon(parentPath + '/' + name, FolderIconOrigin::DirectoryFile);
}

void luciole::applyFolderCustomIcon(FileIconSource& source, const FolderCustomIcon& icon) {
  if (icon.isPath) {
    source.filePath = icon.name;
    return;
  }
  if (!std::ranges::contains(source.names, icon.name)) {
    source.names.insert(source.names.begin(), icon.name);
  }
}

std::optional<std::string> luciole::readFolderCustomIcon(const Location& folder, GCancellable* cancellable) {
  GError* rawError = nullptr;
  const std::string attributes = std::string(G_FILE_ATTRIBUTE_STANDARD_TYPE ",") + kFolderCustomIconAttribute;
  const GioPtr<GFileInfo> info(g_file_query_info(
      gioFile(folder).get(), attributes.c_str(), G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, &rawError
  ));
  const GErrorPtr error(rawError);
  if (info && g_file_info_get_file_type(info.get()) == G_FILE_TYPE_DIRECTORY) {
    if (auto icon = folderIconFromInfo(info.get())) {
      return icon->name;
    }
  }
  if (const auto path = folder.nativePath()) {
    if (auto icon = folderIconFromDirectoryFile(*path)) {
      return icon->name;
    }
  }
  return std::nullopt;
}

namespace {
  // Best-effort removal of a stale .directory Icon so a metadata write cannot
  // be shadowed by it later (and vice versa on reset).
  bool clearDirectoryFileIcon(const std::string& folderPath) {
    const std::string path = folderPath + "/.directory";
    const std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> file(g_key_file_new(), &g_key_file_unref);
    GError* rawError = nullptr;
    if (!g_key_file_load_from_file(file.get(), path.c_str(), G_KEY_FILE_KEEP_TRANSLATIONS, &rawError)) {
      g_clear_error(&rawError);
      return true; // Nothing stored there.
    }
    GError* removeError = nullptr;
    if (!g_key_file_remove_key(file.get(), "Desktop Entry", "Icon", &removeError)) {
      g_clear_error(&removeError);
      return true; // No Icon key stored.
    }
    gsize groups = 0;
    const std::unique_ptr<gchar*, decltype(&g_strfreev)> names(g_key_file_get_groups(file.get(), &groups), &g_strfreev);
    gsize keys = 0;
    for (gsize i = 0; i < groups; ++i) {
      gsize count = 0;
      const std::unique_ptr<gchar*, decltype(&g_strfreev)> keyNames(
          g_key_file_get_keys(file.get(), names.get()[i], &count, nullptr), &g_strfreev
      );
      keys += count;
    }
    if (keys == 0) {
      // Nothing left: remove the file instead of leaving an empty group behind.
      return ::unlink(path.c_str()) == 0 || errno == ENOENT;
    }
    gsize length = 0;
    GError* dataError = nullptr;
    const std::unique_ptr<gchar, decltype(&g_free)> data(g_key_file_to_data(file.get(), &length, &dataError), &g_free);
    if (!data) {
      g_clear_error(&dataError);
      return false;
    }
    if (length == 0) {
      ::unlink(path.c_str());
      return true;
    }
    GError* saveError = nullptr;
    const bool saved = g_key_file_save_to_file(file.get(), path.c_str(), &saveError);
    g_clear_error(&saveError);
    return saved;
  }

  bool writeDirectoryFileIcon(const std::string& folderPath, const std::string& iconName, std::string& error) {
    const std::string path = folderPath + "/.directory";
    const std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> file(g_key_file_new(), &g_key_file_unref);
    GError* loadError = nullptr;
    (void)g_key_file_load_from_file(file.get(), path.c_str(), G_KEY_FILE_KEEP_TRANSLATIONS, &loadError);
    g_clear_error(&loadError);
    g_key_file_set_string(file.get(), "Desktop Entry", "Icon", iconName.c_str());
    GError* saveError = nullptr;
    const bool saved = g_key_file_save_to_file(file.get(), path.c_str(), &saveError);
    if (!saved) {
      error = saveError != nullptr ? saveError->message : "Could not write the folder's .directory file.";
    }
    g_clear_error(&saveError);
    return saved;
  }
} // namespace

bool luciole::setFolderCustomIcon(const Location& folder, const std::string& iconName, std::string& error) {
  if (iconName.empty()) {
    return clearFolderCustomIcon(folder, error);
  }
  const auto path = folder.nativePath();
  // Local folders always get a .directory entry: the native fast listing path
  // carries no GFileInfo, so metadata alone would never display there.
  // Non-local folders rely on metadata only.
  std::string directoryError;
  const bool directoryOk = !path || writeDirectoryFileIcon(*path, iconName, directoryError);
  GError* rawError = nullptr;
  const bool stored = g_file_set_attribute_string(
                          gioFile(folder).get(), kFolderCustomIconAttribute, iconName.c_str(),
                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, &rawError
                      )
      != FALSE;
  GErrorPtr metadataError(rawError);
  if (path) {
    if (!directoryOk) {
      error = directoryError;
      if (metadataError) {
        error += " (" + std::string(metadataError->message) + ")";
      }
      return false;
    }
    // A metadata failure here only loses Nautilus interop, not the icon itself.
    return true;
  }
  if (stored) {
    return true;
  }
  error = metadataError ? metadataError->message : "Setting a folder icon is not supported here.";
  return false;
}

bool luciole::clearFolderCustomIcon(const Location& folder, std::string& error) {
  GError* rawError = nullptr;
  const bool cleared = g_file_set_attribute(
                           gioFile(folder).get(), kFolderCustomIconAttribute, G_FILE_ATTRIBUTE_TYPE_INVALID, nullptr,
                           G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, &rawError
                       )
      != FALSE;
  GErrorPtr metadataError(rawError);
  bool directoryOk = true;
  if (const auto path = folder.nativePath()) {
    directoryOk = clearDirectoryFileIcon(*path);
  }
  if (cleared || metadataError == nullptr) {
    return directoryOk;
  }
  // Metadata actively refused and no local fallback: report it.
  if (!directoryOk || !folder.nativePath()) {
    error = metadataError->message;
    return false;
  }
  return true;
}
