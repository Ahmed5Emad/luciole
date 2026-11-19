#pragma once

#include "fs/location.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace luciole {
  class FileIconCache;
} // namespace luciole

class IconPicker;
class PageContext;
struct KeyboardEvent;

// "Change folder icon…" for a single folder: an icon-theme picker whose choice
// is stored in GVFS metadata (Nautilus-compatible), falling back to the
// folder's .directory file (Dolphin-compatible) when metadata is unavailable.
class FolderIconController {
public:
  struct LoadResult {
    std::vector<std::string> names;
    std::optional<std::string> current;
  };
  struct WriteResult {
    bool ok = false;
    std::string error;
  };

  FolderIconController(PageContext& page, luciole::FileIconCache& icons);

  // Opens the picker for a local, non-trash folder. Loading the theme icons
  // runs on a worker; the dialog appears when it completes. `onReturn` runs
  // when the picker closes without a browser return: cancel re-shows the
  // caller (Properties), and a successful apply does too.
  void open(const luciole::Location& folder, std::function<void()> onReturn = {});
  // Refreshed the Properties dialog after an outside change, when set.
  void setOnChanged(std::function<void(const luciole::Location&)> callback) { m_onChanged = std::move(callback); }
  // Keyboard hook in BrowserPage::keyboard(): Escape cancels the picker through
  // the same path as its Cancel button.
  bool escapeKey(const KeyboardEvent& event);

private:
  void showPicker(LoadResult result, std::uint64_t generation);
  void submitIcon(std::string iconName);
  void resetIcon();
  void finishWrite(const WriteResult& result);
  void cancelPicker();

  PageContext& m_page;
  luciole::FileIconCache& m_icons;
  std::function<void(const luciole::Location&)> m_onChanged;
  std::function<void()> m_returnCallback;

  IconPicker* m_picker = nullptr;
  std::optional<luciole::Location> m_target;
  std::uint64_t m_generation = 0;
  bool m_expectingDialog = false;
};
