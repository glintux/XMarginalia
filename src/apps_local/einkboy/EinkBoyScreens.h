#pragma once

#include <FreeInkUI.h>

#include <cstdint>

#include "../ui/ToyboxScreen.h"
#include "EinkBoyCore.h"

namespace einkboyui {

namespace fui = freeink::ui;

enum ActionId : fui::ActionId {
  ActionSelectRom = 1,
  ActionFilterAll = 2,
  ActionFilterFav = 3,
  ActionFilterRecent = 4,
  ActionGoToPage = 5,
  ActionOpenSettings = 6,
  ActionExitApp = 7,
  ActionQuickResume = 8,

  ActionPlayRom = 10,
  ActionToggleFavorite = 11,
  ActionDeleteSave = 12,
  ActionBackToLibrary = 13,

  ActionSettingRow = 20,
  ActionCloseSettings = 21,

  ActionPauseRow = 30,
  ActionResumeGame = 31,

  ActionDismissNotice = 40,
};

enum class LibraryFilter : uint8_t {
  All = 0,
  Favorites = 1,
  Recent = 2,
};

struct RomRowItem {
  const char* title = nullptr;
  const char* badge = nullptr;
};

constexpr int kMaxPageRoms = 7;

struct LibraryModel {
  LibraryFilter filter = LibraryFilter::All;
  int totalGames = 0;
  int favoriteCount = 0;
  int recentCount = 0;
  int visibleCount = 0;
  int page = 0;
  int pageCount = 1;
  int selectedOnPage = -1;
  int pageStartIndex = 0;
  int pageItemCount = 0;
  RomRowItem items[kMaxPageRoms] = {};
  const char* recentTitle = nullptr;
  const char* statusMessage = nullptr;
};

struct GameActionsModel {
  const char* title = nullptr;
  const char* typeLabel = nullptr;
  bool isFavorite = false;
  bool hasSave = false;
  bool confirmDelete = false;
  const char* statusMessage = nullptr;
  int selected = 0;
};

enum class PauseRow : uint8_t {
  Resume = 0,
  SaveNow,
  Rotate,
  Settings,
  RomLibrary,
  ExitEinkBoy,
  Count,
};

struct InGameMenuModel {
  const char* title = nullptr;
  const char* saveStatus = nullptr;
  bool portrait = true;
  int selected = 0;
};

enum class SettingRow : uint8_t {
  Orientation = 0,
  RefreshMode,
  DitherMode,
  AutoClean,
  TouchControls,
  ShowTitle,
  ButtonUp,
  ButtonDown,
  ButtonConfirm,
  Count,
};

struct SettingsModel {
  einkboy::Settings settings;
  bool asyncSupported = true;
  bool fromGame = false;
  int selected = 0;
};

struct NoticeModel {
  const char* title = "EINKBOY";
  const char* headline = nullptr;
  const char* detail = nullptr;
  const char* buttonLabel = "BACK";
};

int libraryRowsPerPage();
void buildLibrary(toybox::Screen& screen, const LibraryModel& model);
void buildGameActions(toybox::Screen& screen, const GameActionsModel& model);
void buildInGameMenu(toybox::Screen& screen, const InGameMenuModel& model);
void buildSettings(toybox::Screen& screen, const SettingsModel& model);
void buildNotice(toybox::Screen& screen, const NoticeModel& model);

const char* settingRowLabel(SettingRow row);
void formatSettingRowValue(const SettingsModel& model, SettingRow row, char* out, size_t outSize);
const char* pauseRowLabel(PauseRow row);
const char* pauseRowValue(const InGameMenuModel& model, PauseRow row);

}  // namespace einkboyui
