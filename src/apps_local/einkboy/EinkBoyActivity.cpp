#include "EinkBoyActivity.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#include <esp_timer.h>
#endif

#include <strings.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "../../MappedInputManager.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxTheme.h"
#include "CgbDither.h"

namespace {

constexpr char TAG[] = "EINKBOY";
constexpr char kLibraryStatePath[] = "/.crosspoint/einkboy_library.txt";
constexpr char kLibraryTempPath[] = "/.crosspoint/einkboy_library.tmp";
constexpr char kDmgSettingsPath[] = "/.crosspoint/einkboy_settings.bin";
constexpr char kDmgSettingsTempPath[] = "/.crosspoint/einkboy_settings.tmp";
constexpr char kCgbSettingsPath[] = "/.crosspoint/einkboy_settings_gbc.bin";
constexpr char kCgbSettingsTempPath[] = "/.crosspoint/einkboy_settings_gbc.tmp";

uint64_t nowMicros() {
#if defined(ARDUINO_ARCH_ESP32)
  return static_cast<uint64_t>(esp_timer_get_time());
#else
  return static_cast<uint64_t>(micros());
#endif
}

bool containsPath(const std::vector<std::string>& paths, const std::string& path) {
  return std::find(paths.begin(), paths.end(), path) != paths.end();
}

bool hitRect(const int x, const int y, const int rx, const int ry, const int rw, const int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

#if defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)
uint8_t* allocPsramBytes(const size_t bytes) {
#if defined(ARDUINO_ARCH_ESP32)
  return static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
  return static_cast<uint8_t*>(std::malloc(bytes));
#endif
}

void freePsramBytes(void* ptr) {
  if (!ptr) return;
#if defined(ARDUINO_ARCH_ESP32)
  heap_caps_free(ptr);
#else
  std::free(ptr);
#endif
}

const char* initErrorString(const int errorCode) {
  switch (errorCode) {
    case 1:
      return "Unsupported cartridge type";
    case 2:
      return "Invalid ROM checksum";
    default:
      return "ROM initialization failed";
  }
}
#endif

}  // namespace

std::unique_ptr<Activity> EinkBoyActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<EinkBoyActivity>(renderer, mappedInput);
}

EinkBoyActivity::EinkBoyActivity(GfxRenderer& rendererIn, MappedInputManager& mappedInputIn)
    : Activity("EinkBoy", rendererIn, mappedInputIn) {}

EinkBoyActivity::~EinkBoyActivity() {
#if defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)
  stopEmuTask();
  resetDisplayTuning();
  cleanup();
#endif
}

void EinkBoyActivity::onEnter() {
  Activity::onEnter();
  games.clear();
  visibleGames.clear();
  favorites.clear();
  recents.clear();
  filter = einkboyui::LibraryFilter::All;
  libraryPage = 0;
  selectedRomGlobal = -1;
  activeGameIndex = 0;
  gameActionConfirmDelete = false;
  gameActionSelected = 0;
  pauseMenuSelected = 0;
  settingsSelected = 0;
  settingsFromGame = false;
  statusMessage[0] = '\0';
  noticeHeadline[0] = '\0';
  noticeDetail[0] = '\0';
  interactionsReady = false;

  renderer.setOrientation(GfxRenderer::Portrait);

#if !defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR)
  view = View::Notice;
  noticeReturnView = View::Library;
  std::snprintf(noticeHeadline, sizeof(noticeHeadline), "PSRAM REQUIRED");
  std::snprintf(noticeDetail, sizeof(noticeDetail), "EinkBoy requires an ESP32-S3 board with PSRAM.");
  requestUpdate();
  return;
#else
  cleanup();
  loadSettingsProfile(false);
  loadLibraryState();
  scanDirectory("/", 0);
  std::sort(games.begin(), games.end(),
            [](const GameEntry& a, const GameEntry& b) { return strcasecmp(a.title.c_str(), b.title.c_str()) < 0; });
  rebuildVisibleGames();
  view = View::Library;
  requestUpdate();
#endif
}

void EinkBoyActivity::onExit() {
  stopAndUnloadGame();
  if (renderer.supportsAsyncRefresh()) {
    renderer.waitRefreshComplete();
  }
  renderer.setOrientation(GfxRenderer::Portrait);
  Activity::onExit();
}

void EinkBoyActivity::prepareForSleep() {
  if (view == View::Playing) {
    pauseToMenu();
  } else {
#if defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)
    if (romInitialized.load(std::memory_order_acquire)) {
      stopEmuTask();
      (void)saveCartRam();
    }
#endif
  }
}

void EinkBoyActivity::loop() {
  // Static exit/pause handler at the top of the per-frame path (never remappable, never uses Power).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (view) {
      case View::Playing:
        pauseToMenu();
        return;
      case View::InGameMenu:
        resumeFromPause();
        return;
      case View::GameActions:
        view = View::Library;
        statusMessage[0] = '\0';
        requestUpdate();
        return;
      case View::Settings:
        view = settingsFromGame ? View::InGameMenu : View::Library;
        requestUpdate();
        return;
      case View::Notice:
        if (noticeReturnView == View::Notice) {
          shelf::leave(renderer, mappedInput);
        } else {
          view = noticeReturnView;
          requestUpdate();
        }
        return;
      case View::Library:
        shelf::leave(renderer, mappedInput);
        return;
    }
  }

#if defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)
  if (view == View::Playing) {
    routePlaying();
    return;
  }
#endif

  routeToyboxView();
}

void EinkBoyActivity::render(RenderLock&&) {
#if defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)
  if (view == View::Playing) {
    if (!displayDue.exchange(false, std::memory_order_acq_rel)) return;
    if (!romInitialized.load(std::memory_order_acquire)) return;

    const bool cleaning = fullRefreshDue.exchange(false, std::memory_order_acq_rel);
    const uint16_t batteryPercent = powerManager.getBatteryPercentage();
    const uint32_t chrome = static_cast<uint32_t>(physicalJoypadMask | touchJoypadMask) |
                            (static_cast<uint32_t>(activeTouch) << 8) | (portrait ? 0x10000u : 0u) |
                            (settings.touchControls ? 0x20000u : 0u) | (settings.showGameTitle ? 0x40000u : 0u) |
                            (static_cast<uint32_t>(batteryPercent & 0x7FU) << 20);
    const bool redrawChrome = cleaning || chrome != chromeKey;
    const bool overlapped = !cleaning && settings.refresh <= 1;
    if (!overlapped || redrawChrome) {
      display.finishOverlapped();
      if (renderer.supportsAsyncRefresh()) {
        renderer.waitRefreshComplete();
      }
    }
    if (redrawChrome) renderer.clearScreen(0xFF);
    const uint64_t tDraw0 = nowMicros();
    drawGameFrame();
    perfDrawUsTotal += static_cast<uint32_t>(nowMicros() - tDraw0);
    ++perfDrawCount;
    releaseRenderedFrame();
    if (redrawChrome) {
      drawGameControls();
      chromeKey = chrome;
    }

    if (cleaning) {
      display.finishOverlapped();
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    } else if (overlapped) {
      display.displayBufferOverlapped(HalDisplay::FAST_REFRESH);
    } else if (renderer.supportsAsyncRefresh()) {
      renderer.displayBufferAsync(HalDisplay::FAST_REFRESH);
    } else {
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    }
    return;
  }
  display.finishOverlapped();
#endif
  if (renderer.supportsAsyncRefresh()) {
    renderer.waitRefreshComplete();
  }

  renderer.setOrientation(GfxRenderer::Portrait);
  renderer.clearScreen(0xFF);

  namespace fui = freeink::ui;
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);

  switch (view) {
    case View::Library: {
      einkboyui::LibraryModel model;
      model.filter = filter;
      model.totalGames = static_cast<int>(games.size());
      model.favoriteCount = static_cast<int>(favorites.size());
      model.recentCount = static_cast<int>(recents.size());
      model.visibleCount = static_cast<int>(visibleGames.size());
      const int perPage = einkboyui::libraryRowsPerPage();
      model.pageCount = std::max(1, (model.visibleCount + perPage - 1) / perPage);
      if (libraryPage >= model.pageCount) libraryPage = model.pageCount - 1;
      if (libraryPage < 0) libraryPage = 0;
      model.page = libraryPage;
      model.pageStartIndex = libraryPage * perPage;
      const int rem = std::max(0, model.visibleCount - model.pageStartIndex);
      model.pageItemCount = std::min({perPage, einkboyui::kMaxPageRoms, rem});
      char badgeBuf[einkboyui::kMaxPageRoms][12];
      for (int i = 0; i < model.pageItemCount; ++i) {
        const size_t gIdx = visibleGames[static_cast<size_t>(model.pageStartIndex + i)];
        model.items[i].title = games[gIdx].title.c_str();
        std::snprintf(badgeBuf[i], sizeof(badgeBuf[i]), "%s%s", games[gIdx].isGbc ? "GBC" : "GB",
                      isFavorite(games[gIdx].path) ? " *" : "");
        model.items[i].badge = badgeBuf[i];
        if (selectedRomGlobal == model.pageStartIndex + i) model.selectedOnPage = i;
      }
      if (!recents.empty()) {
        for (const auto& g : games) {
          if (g.path == recents[0]) {
            model.recentTitle = g.title.c_str();
            break;
          }
        }
      }
      model.statusMessage = statusMessage;
      einkboyui::buildLibrary(screen, model);
      break;
    }
    case View::GameActions: {
      einkboyui::GameActionsModel model;
      if (activeGameIndex < games.size()) {
        model.title = games[activeGameIndex].title.c_str();
        model.typeLabel = games[activeGameIndex].isGbc ? "GBC" : "GB";
        model.isFavorite = isFavorite(games[activeGameIndex].path);
        model.hasSave = selectedGameHasSave(games[activeGameIndex].path);
      }
      model.confirmDelete = gameActionConfirmDelete;
      model.statusMessage = statusMessage;
      model.selected = gameActionSelected;
      einkboyui::buildGameActions(screen, model);
      break;
    }
    case View::InGameMenu: {
      einkboyui::InGameMenuModel model;
#if defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)
      model.title = gameTitle[0] ? gameTitle : "EINKBOY";
      model.portrait = portrait;
      model.saveStatus = (cartRamSize > 0) ? (saveDirty ? "UNSAVED" : "SAVED") : "NO SAVE";
#else
      model.title = "EINKBOY";
#endif
      model.selected = pauseMenuSelected;
      einkboyui::buildInGameMenu(screen, model);
      break;
    }
    case View::Settings: {
      einkboyui::SettingsModel model;
      model.settings = settings;
      model.asyncSupported = renderer.supportsAsyncRefresh();
      model.fromGame = settingsFromGame;
      model.selected = settingsSelected;
      einkboyui::buildSettings(screen, model);
      break;
    }
    case View::Notice:
    case View::Playing: {
      einkboyui::NoticeModel model;
      model.title = "EINKBOY";
      model.headline = noticeHeadline[0] ? noticeHeadline : "NOTICE";
      model.detail = noticeDetail;
      model.buttonLabel = "BACK";
      einkboyui::buildNotice(screen, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "EinkBoy");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void EinkBoyActivity::routeToyboxView() {
  namespace fui = freeink::ui;

  if (!interactionsReady) return;

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;

  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions.route(input);
  if (event.action == fui::NO_ACTION) return;

  switch (view) {
    case View::Library:
      handleLibraryAction(event);
      break;
    case View::GameActions:
      handleGameAction(event);
      break;
    case View::InGameMenu:
      handlePauseAction(event);
      break;
    case View::Settings:
      handleSettingsAction(event);
      break;
    case View::Notice:
      if (event.action == einkboyui::ActionDismissNotice) {
        if (noticeReturnView == View::Notice) {
          shelf::leave(renderer, mappedInput);
        } else {
          view = noticeReturnView;
          requestUpdate();
        }
      }
      break;
    case View::Playing:
      break;
  }
}

void EinkBoyActivity::handleLibraryAction(const freeink::ui::ActionEvent& event) {
  statusMessage[0] = '\0';
  switch (event.action) {
    case einkboyui::ActionFilterAll:
      filter = einkboyui::LibraryFilter::All;
      libraryPage = 0;
      selectedRomGlobal = -1;
      rebuildVisibleGames();
      requestUpdate();
      break;
    case einkboyui::ActionFilterFav:
      filter = einkboyui::LibraryFilter::Favorites;
      libraryPage = 0;
      selectedRomGlobal = -1;
      rebuildVisibleGames();
      requestUpdate();
      break;
    case einkboyui::ActionFilterRecent:
      filter = einkboyui::LibraryFilter::Recent;
      libraryPage = 0;
      selectedRomGlobal = -1;
      rebuildVisibleGames();
      requestUpdate();
      break;
    case einkboyui::ActionGoToPage:
      libraryPage = event.value;
      selectedRomGlobal = -1;
      requestUpdate();
      break;
    case einkboyui::ActionOpenSettings:
      openSettings(false);
      break;
    case einkboyui::ActionExitApp:
      shelf::leave(renderer, mappedInput);
      break;
    case einkboyui::ActionQuickResume:
      if (!recents.empty()) {
        for (size_t i = 0; i < games.size(); ++i) {
          if (games[i].path == recents[0]) {
            (void)launchRom(i);
            return;
          }
        }
      }
      break;
    case einkboyui::ActionSelectRom:
      if (event.value >= 0 && static_cast<size_t>(event.value) < visibleGames.size()) {
        selectedRomGlobal = event.value;
        activeGameIndex = visibleGames[static_cast<size_t>(event.value)];
        gameActionConfirmDelete = false;
        gameActionSelected = 0;
        view = View::GameActions;
        requestUpdate();
      }
      break;
    default:
      break;
  }
}

void EinkBoyActivity::handleGameAction(const freeink::ui::ActionEvent& event) {
  if (event.action == einkboyui::ActionBackToLibrary) {
    statusMessage[0] = '\0';
    gameActionConfirmDelete = false;
    view = View::Library;
    requestUpdate();
    return;
  }
  if (event.action != einkboyui::ActionPlayRom || activeGameIndex >= games.size()) return;

  gameActionSelected = event.value;
  if (event.value == 0) {
    (void)launchRom(activeGameIndex);
    return;
  }
  if (event.value == 1) {
    gameActionConfirmDelete = false;
    toggleFavorite(games[activeGameIndex].path);
    std::snprintf(statusMessage, sizeof(statusMessage), "%s",
                  isFavorite(games[activeGameIndex].path) ? "Added to favorites" : "Removed from favorites");
    requestUpdate();
    return;
  }
  if (event.value == 2) {
    if (!selectedGameHasSave(games[activeGameIndex].path)) {
      gameActionConfirmDelete = false;
      std::snprintf(statusMessage, sizeof(statusMessage), "No save file to delete");
      requestUpdate();
      return;
    }
    if (!gameActionConfirmDelete) {
      gameActionConfirmDelete = true;
      std::snprintf(statusMessage, sizeof(statusMessage), "Tap again to confirm delete");
      requestUpdate();
      return;
    }
    gameActionConfirmDelete = false;
    std::snprintf(statusMessage, sizeof(statusMessage), "%s",
                  deleteSaveForRom(games[activeGameIndex].path) ? "Save deleted" : "Delete failed");
    requestUpdate();
  }
}

void EinkBoyActivity::handlePauseAction(const freeink::ui::ActionEvent& event) {
  if (event.action == einkboyui::ActionResumeGame) {
    resumeFromPause();
    return;
  }
  if (event.action != einkboyui::ActionPauseRow) return;

  pauseMenuSelected = event.value;
  const auto row = static_cast<einkboyui::PauseRow>(event.value);
  switch (row) {
    case einkboyui::PauseRow::Resume:
      resumeFromPause();
      break;
    case einkboyui::PauseRow::SaveNow:
#if defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)
      (void)saveCartRam();
#endif
      requestUpdate();
      break;
    case einkboyui::PauseRow::Rotate:
#if defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)
      portrait = !portrait;
      settings.layout = portrait ? 0 : 1;
      saveSettingsProfile(settingsForCgb);
#endif
      requestUpdate();
      break;
    case einkboyui::PauseRow::Settings:
      openSettings(true);
      break;
    case einkboyui::PauseRow::RomLibrary:
      stopAndUnloadGame();
      statusMessage[0] = '\0';
      view = View::Library;
      requestUpdate();
      break;
    case einkboyui::PauseRow::ExitEinkBoy:
      stopAndUnloadGame();
      shelf::leave(renderer, mappedInput);
      break;
    case einkboyui::PauseRow::Count:
      break;
  }
}

void EinkBoyActivity::handleSettingsAction(const freeink::ui::ActionEvent& event) {
  if (event.action == einkboyui::ActionCloseSettings) {
    view = settingsFromGame ? View::InGameMenu : View::Library;
    requestUpdate();
    return;
  }
  if (event.action != einkboyui::ActionSettingRow) return;
  settingsSelected = event.value;
  handleSettingToggle(static_cast<einkboyui::SettingRow>(event.value));
}

void EinkBoyActivity::openSettings(const bool fromGame) {
  settingsFromGame = fromGame;
  settingsSelected = 0;
  if (!fromGame) loadSettingsProfile(false);
  view = View::Settings;
  requestUpdate();
}

void EinkBoyActivity::handleSettingToggle(const einkboyui::SettingRow row) {
  switch (row) {
    case einkboyui::SettingRow::Orientation:
      settings.layout = settings.layout == 0 ? 1 : 0;
#if defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)
      if (settingsFromGame) portrait = (settings.layout == 0);
#endif
      break;
    case einkboyui::SettingRow::RefreshMode:
      settings.refresh = static_cast<uint8_t>((settings.refresh + 1U) % 3U);
      break;
    case einkboyui::SettingRow::DitherMode:
      settings.ditherMode = settings.ditherMode == 0 ? 1 : 0;
      break;
    case einkboyui::SettingRow::AutoClean:
      settings.autoClean = settings.autoClean == 0 ? 1 : 0;
      break;
    case einkboyui::SettingRow::TouchControls:
      settings.touchControls = settings.touchControls ? 0 : 1;
      break;
    case einkboyui::SettingRow::ShowTitle:
      settings.showGameTitle = settings.showGameTitle ? 0 : 1;
      break;
    case einkboyui::SettingRow::ButtonUp:
      settings.upAction = einkboy::nextButtonAction(settings.upAction);
      break;
    case einkboyui::SettingRow::ButtonDown:
      settings.downAction = einkboy::nextButtonAction(settings.downAction);
      break;
    case einkboyui::SettingRow::ButtonConfirm:
      settings.confirmAction = einkboy::nextButtonAction(settings.confirmAction);
      break;
    case einkboyui::SettingRow::Count:
      return;
  }
  saveSettingsProfile(settingsForCgb);
  requestUpdate();
}

void EinkBoyActivity::openNotice(const char* headline, const char* detail, const View returnTo) {
  std::snprintf(noticeHeadline, sizeof(noticeHeadline), "%s", headline ? headline : "NOTICE");
  std::snprintf(noticeDetail, sizeof(noticeDetail), "%s", detail ? detail : "");
  noticeReturnView = returnTo;
  view = View::Notice;
  requestUpdate();
}

void EinkBoyActivity::loadSettingsProfile(const bool cgbProfile) {
  settingsForCgb = cgbProfile;
  settings = einkboy::Settings{};
  const char* path = cgbProfile ? kCgbSettingsPath : kDmgSettingsPath;
  HalFile file;
  if (!Storage.openFileForRead(TAG, path, file)) return;
  uint8_t bytes[einkboy::kSettingsBinarySize] = {};
  const int count = file.read(bytes, sizeof(bytes));
  if (count > 0) {
    (void)einkboy::deserializeSettings(bytes, static_cast<size_t>(count), settings);
  }
}

bool EinkBoyActivity::saveSettingsProfile(const bool cgbProfile) const {
  (void)Storage.mkdir("/.crosspoint");
  uint8_t bytes[einkboy::kSettingsBinarySize] = {};
  const size_t n = einkboy::serializeSettings(settings, bytes, sizeof(bytes));
  if (n == 0) return false;

  const char* tempPath = cgbProfile ? kCgbSettingsTempPath : kDmgSettingsTempPath;
  const char* finalPath = cgbProfile ? kCgbSettingsPath : kDmgSettingsPath;
  bool written = false;
  {
    HalFile file;
    if (!Storage.openFileForWrite(TAG, tempPath, file)) return false;
    written = file.write(bytes, n) == n;
    file.flush();
  }
  if (!written) {
    Storage.remove(tempPath);
    return false;
  }
  Storage.remove(finalPath);
  if (!Storage.rename(tempPath, finalPath)) {
    Storage.remove(tempPath);
    return false;
  }
  return true;
}

void EinkBoyActivity::loadLibraryState() {
  HalFile file;
  if (!Storage.openFileForRead(TAG, kLibraryStatePath, file)) return;
  std::string line;
  line.reserve(256);
  bool tooLong = false;
  while (true) {
    const int c = file.read();
    if (c < 0) break;
    if (c != '\n') {
      if (line.size() < 512) {
        line.push_back(static_cast<char>(c));
      } else {
        tooLong = true;
      }
      continue;
    }
    if (!tooLong) {
      bool isFav = false;
      std::string_view p;
      if (einkboy::parseLibraryLine(line, isFav, p)) {
        std::string s(p);
        if (isFav && favorites.size() < MAX_FAVORITES && !containsPath(favorites, s)) favorites.push_back(s);
        if (!isFav && recents.size() < MAX_RECENTS && !containsPath(recents, s)) recents.push_back(s);
      }
    }
    line.clear();
    tooLong = false;
  }
  if (!line.empty() && !tooLong) {
    bool isFav = false;
    std::string_view p;
    if (einkboy::parseLibraryLine(line, isFav, p)) {
      std::string s(p);
      if (isFav && favorites.size() < MAX_FAVORITES && !containsPath(favorites, s)) favorites.push_back(s);
      if (!isFav && recents.size() < MAX_RECENTS && !containsPath(recents, s)) recents.push_back(s);
    }
  }
}

bool EinkBoyActivity::saveLibraryState() {
  (void)Storage.mkdir("/.crosspoint");
  std::string contents;
  contents.reserve(2048);
  for (const auto& p : favorites) {
    contents += "F\t";
    contents += p;
    contents += '\n';
  }
  for (const auto& p : recents) {
    contents += "R\t";
    contents += p;
    contents += '\n';
  }

  bool ok = false;
  {
    HalFile file;
    if (!Storage.openFileForWrite(TAG, kLibraryTempPath, file)) return false;
    ok = file.write(contents.data(), contents.size()) == contents.size();
    file.flush();
  }
  if (!ok) {
    Storage.remove(kLibraryTempPath);
    return false;
  }
  Storage.remove(kLibraryStatePath);
  if (!Storage.rename(kLibraryTempPath, kLibraryStatePath)) {
    Storage.remove(kLibraryTempPath);
    return false;
  }
  return true;
}

void EinkBoyActivity::scanDirectory(const std::string& path, const unsigned depth) {
  if (depth > MAX_SCAN_DEPTH || games.size() >= MAX_GAMES) return;

  std::vector<std::string> subdirectories;
  subdirectories.reserve(12);
  {
    HalFile root = Storage.open(path.c_str());
    if (!root || !root.isDirectory()) return;
    root.rewindDirectory();
    char name[256] = {};
    while (games.size() < MAX_GAMES) {
      HalFile entry = root.openNextFile();
      if (!entry) break;
      std::memset(name, 0, sizeof(name));
      entry.getName(name, sizeof(name));
      const bool isDirectory = entry.isDirectory();
      if (name[0] != '\0') {
        if (isDirectory) {
          if (depth < MAX_SCAN_DEPTH && !einkboy::shouldSkipScanDirectory(name) && subdirectories.size() < 64) {
            subdirectories.push_back(path == "/" ? path + name : path + "/" + name);
          }
        } else if (einkboy::hasSupportedRomExtension(name)) {
          const std::string fullPath = path == "/" ? path + name : path + "/" + name;
          const bool duplicate =
              std::any_of(games.begin(), games.end(), [&fullPath](const GameEntry& g) { return g.path == fullPath; });
          if (!duplicate) {
            char titleBuf[64] = {};
            einkboy::formatTitleFromPath(fullPath, titleBuf, sizeof(titleBuf));
            games.push_back({fullPath, titleBuf, einkboy::hasGbcExtension(fullPath)});
          }
        }
      }
    }
  }

  for (const auto& child : subdirectories) {
    if (games.size() >= MAX_GAMES) break;
    scanDirectory(child, depth + 1);
  }
}

void EinkBoyActivity::rebuildVisibleGames() {
  visibleGames.clear();
  visibleGames.reserve(games.size());
  if (filter == einkboyui::LibraryFilter::Recent) {
    for (const auto& path : recents) {
      for (size_t i = 0; i < games.size(); ++i) {
        if (games[i].path == path) {
          visibleGames.push_back(i);
          break;
        }
      }
    }
    return;
  }
  for (size_t i = 0; i < games.size(); ++i) {
    if (filter == einkboyui::LibraryFilter::Favorites && !isFavorite(games[i].path)) continue;
    visibleGames.push_back(i);
  }
}

bool EinkBoyActivity::isFavorite(const std::string& path) const { return containsPath(favorites, path); }

void EinkBoyActivity::toggleFavorite(const std::string& path) {
  const auto it = std::find(favorites.begin(), favorites.end(), path);
  if (it == favorites.end()) {
    if (favorites.size() < MAX_FAVORITES) favorites.push_back(path);
  } else {
    favorites.erase(it);
  }
  saveLibraryState();
  rebuildVisibleGames();
}

void EinkBoyActivity::recordRecent(const std::string& path) {
  recents.erase(std::remove(recents.begin(), recents.end(), path), recents.end());
  recents.insert(recents.begin(), path);
  if (recents.size() > MAX_RECENTS) recents.resize(MAX_RECENTS);
  saveLibraryState();
  rebuildVisibleGames();
}

bool EinkBoyActivity::selectedGameHasSave(const std::string& path) const {
  char sav[256] = {}, tmp[256] = {}, bak[256] = {};
  if (!einkboy::buildSavePaths(path, sav, sizeof(sav), tmp, sizeof(tmp), bak, sizeof(bak))) return false;
  return Storage.exists(sav);
}

bool EinkBoyActivity::deleteSaveForRom(const std::string& path) {
  char sav[256] = {}, tmp[256] = {}, bak[256] = {};
  if (!einkboy::buildSavePaths(path, sav, sizeof(sav), tmp, sizeof(tmp), bak, sizeof(bak))) return false;
  if (Storage.exists(sav)) Storage.remove(sav);
  if (Storage.exists(tmp)) Storage.remove(tmp);
  if (Storage.exists(bak)) Storage.remove(bak);
  return !Storage.exists(sav) && !Storage.exists(tmp) && !Storage.exists(bak);
}

bool EinkBoyActivity::launchRom(const size_t gameIdx) {
#if !defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR)
  (void)gameIdx;
  openNotice("PSRAM REQUIRED", "EinkBoy requires an ESP32-S3 board with PSRAM.", View::Library);
  return false;
#else
  if (gameIdx >= games.size()) return false;
  stopAndUnloadGame();

  romPath = games[gameIdx].path;
  if (!einkboy::buildSavePaths(romPath, savePath, sizeof(savePath), tempSavePath, sizeof(tempSavePath), backupSavePath,
                               sizeof(backupSavePath))) {
    openNotice("PATH TOO LONG", "Move the ROM to a shorter path on the SD card.", View::Library);
    return false;
  }

  if (!loadRom()) {
    cleanup();
    return false;
  }

  useWalnutCgb = einkboy::isCgbRom(romPath, rom[0x143]);
  loadSettingsProfile(useWalnutCgb);
  portrait = (settings.layout == 0);

  if (!initEmulator() || !loadSave()) {
    cleanup();
    return false;
  }

  time_t rawTime = 0;
  std::time(&rawTime);
  struct tm timeInfo{};
#ifdef _WIN32
  localtime_s(&timeInfo, &rawTime);
#else
  localtime_r(&rawTime, &timeInfo);
#endif
  if (useWalnutCgb) {
    walnut_cgb_set_rtc(cgb, &timeInfo);
    walnut_cgb_get_rom_name(cgb, gameTitle);
  } else {
    gb_set_rtc(gb, &timeInfo);
    gb_get_rom_name(gb, gameTitle);
  }
  gameTitle[16] = '\0';
  if (gameTitle[0] == '\0') {
    std::snprintf(gameTitle, sizeof(gameTitle), "%s", games[gameIdx].title.c_str());
  }

  recordRecent(romPath);

  romInitialized.store(true, std::memory_order_release);
  fatalEmuError.store(false, std::memory_order_release);
  rotateRequested = false;
  pauseRequested = false;
  physicalJoypadMask = 0;
  touchJoypadMask = 0;
  activeTouch = TouchControl::None;
  multiTouchActive = false;

  renderer.setOrientation(portrait ? GfxRenderer::Portrait : GfxRenderer::LandscapeCounterClockwise);
  setGameLayout();
  chromeKey = CHROME_NONE;

  const uint64_t nowUs = nowMicros();
  nextFrameUs = nowUs;
  nextDisplayUs = nowUs;
  nextAutosaveUs = nowUs + AUTOSAVE_PERIOD_US;
  nextCleanUs = nowUs + CLEAN_PERIOD_US;
  nextPerfLogUs = nowUs + 2000000ULL;
  perfEmuFrames.store(0, std::memory_order_relaxed);
  perfDrawCount = 0;
  perfDrawUsTotal = 0;
  displayDue.store(true, std::memory_order_release);
  fullRefreshDue.store(true, std::memory_order_release);

  applyJoypad();
  applyRefreshPreset();
  view = View::Playing;
  startEmuTask();
  requestUpdate(true);
  return true;
#endif
}

void EinkBoyActivity::pauseToMenu() {
#if defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)
  stopEmuTask();
  resetDisplayTuning();
  (void)saveCartRam();
  physicalJoypadMask = 0;
  touchJoypadMask = 0;
  activeTouch = TouchControl::None;
  multiTouchActive = false;
  applyJoypad();
  renderer.setOrientation(GfxRenderer::Portrait);
#endif
  pauseMenuSelected = 0;
  view = View::InGameMenu;
  requestUpdate(true);
}

void EinkBoyActivity::resumeFromPause() {
#if !defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR)
  view = View::Library;
  requestUpdate();
#else
  if (!romInitialized.load(std::memory_order_acquire)) {
    view = View::Library;
    requestUpdate();
    return;
  }
  portrait = (settings.layout == 0);
  renderer.setOrientation(portrait ? GfxRenderer::Portrait : GfxRenderer::LandscapeCounterClockwise);
  setGameLayout();
  chromeKey = CHROME_NONE;
  const uint64_t nowUs = nowMicros();
  nextFrameUs = nowUs;
  nextDisplayUs = nowUs;
  nextCleanUs = nowUs + CLEAN_PERIOD_US;
  nextPerfLogUs = nowUs + 2000000ULL;
  perfEmuFrames.store(0, std::memory_order_relaxed);
  perfDrawCount = 0;
  perfDrawUsTotal = 0;
  displayDue.store(true, std::memory_order_release);
  fullRefreshDue.store(true, std::memory_order_release);
  applyRefreshPreset();
  view = View::Playing;
  startEmuTask();
  requestUpdate(true);
#endif
}

void EinkBoyActivity::stopAndUnloadGame() {
#if defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)
  stopEmuTask();
  resetDisplayTuning();
  if (romInitialized.load(std::memory_order_acquire)) {
    (void)saveCartRam();
  }
  cleanup();
#endif
}

#if defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)

void EinkBoyActivity::startEmuTask() {
#if defined(ARDUINO_ARCH_ESP32)
  if (emuTask) return;
  emuRun.store(true, std::memory_order_release);
  emuStopped.store(false, std::memory_order_release);
  TaskHandle_t handle = nullptr;
  if (xTaskCreatePinnedToCore(&EinkBoyActivity::emuTaskEntry, "EinkBoyEmu", 12288, this, 1, &handle, 0) == pdPASS) {
    emuTask = handle;
  } else if (xTaskCreatePinnedToCoreWithCaps(&EinkBoyActivity::emuTaskEntry, "EinkBoyEmu", 12288, this, 1, &handle, 0,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
    emuTask = handle;
    LOG_INF(TAG, "EinkBoy emu task stack allocated in PSRAM");
  } else {
    emuTask = nullptr;
    emuRun.store(false, std::memory_order_release);
    emuStopped.store(true, std::memory_order_release);
    openNotice("MEMORY LOW", "Could not start emulator task.", View::Library);
  }
#endif
}

void EinkBoyActivity::stopEmuTask() {
#if defined(ARDUINO_ARCH_ESP32)
  if (!emuTask) return;
  emuRun.store(false, std::memory_order_release);
  const unsigned long start = millis();
  while (!emuStopped.load(std::memory_order_acquire) && millis() - start < 1000) {
    delay(1);
  }
  if (!emuStopped.load(std::memory_order_acquire)) {
    LOG_ERR(TAG, "Emulator task did not stop cleanly within timeout");
  }
  emuTask = nullptr;
#endif
}

#if defined(ARDUINO_ARCH_ESP32)
void EinkBoyActivity::emuTaskEntry(void* arg) {
  auto* self = static_cast<EinkBoyActivity*>(arg);
  self->emuTaskLoop();
  self->emuStopped.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

void EinkBoyActivity::emuTaskLoop() {
  while (emuRun.load(std::memory_order_acquire)) {
    const uint64_t nowUs = nowMicros();
    runEmulation(nowUs);
    if (nowUs >= nextCleanUs) {
      nextCleanUs = nowUs + CLEAN_PERIOD_US;
      if (settings.autoClean != 0) {
        fullRefreshDue.store(true, std::memory_order_release);
      }
    }
    if (nowUs >= nextAutosaveUs) {
      nextAutosaveUs = nowUs + AUTOSAVE_PERIOD_US;
      if ((saveDirty || useWalnutCgb) && !saveCartRam()) {
        LOG_ERR(TAG, "Autosave failed: %s", savePath);
      }
    }
    if (nowMicros() >= nextFrameUs && behindStreak < 15) {
      ++behindStreak;
      yield();
    } else {
      behindStreak = 0;
      delay(1);
    }
  }
}
#endif

void EinkBoyActivity::routePlaying() {
  if (fatalEmuError.exchange(false, std::memory_order_acq_rel)) {
    stopAndUnloadGame();
    openNotice("EMULATOR ERROR", "The ROM hit an invalid opcode or unsupported state.", View::Library);
    return;
  }

  handleGameInput();
  if (pauseRequested) {
    pauseRequested = false;
    pauseToMenu();
    return;
  }
  if (rotateRequested) {
    rotateRequested = false;
    rotatePlayingLayout();
  }

  const uint64_t nowUs = nowMicros();
#ifndef ARDUINO_ARCH_ESP32
  // Simulator runs emulation inline in the main loop (no FreeRTOS task needed).
  runEmulation(nowUs);
  if (nowUs >= nextCleanUs) {
    nextCleanUs = nowUs + CLEAN_PERIOD_US;
    if (settings.autoClean != 0) {
      fullRefreshDue.store(true, std::memory_order_release);
    }
  }
  if (nowUs >= nextAutosaveUs) {
    nextAutosaveUs = nowUs + AUTOSAVE_PERIOD_US;
    if (saveDirty || useWalnutCgb) (void)saveCartRam();
  }
#endif

  if (nowUs >= nextDisplayUs && readyFrame.load(std::memory_order_acquire) <= 1) {
    displayDue.store(true, std::memory_order_release);
    const bool fastPreset = settings.refresh <= 1;
    nextDisplayUs = nowUs + (fastPreset ? FAST_DISPLAY_PERIOD_US : DISPLAY_PERIOD_US);
    requestUpdate();
  }

  logPerformanceIfDue(nowUs);

#if defined(ARDUINO_ARCH_ESP32)
  delay(2);
#endif
}

void EinkBoyActivity::handleGameInput() {
  using Button = MappedInputManager::Button;

  uint8_t nextPhysical = 0;
  if (mappedInput.isPressed(Button::Up)) {
    if (settings.upAction == einkboy::ButtonAction::Rotate && mappedInput.wasPressed(Button::Up)) {
      rotateRequested = true;
    } else {
      nextPhysical |= joypadBitForButtonAction(settings.upAction);
    }
  }
  if (mappedInput.isPressed(Button::Down)) {
    if (settings.downAction == einkboy::ButtonAction::Rotate && mappedInput.wasPressed(Button::Down)) {
      rotateRequested = true;
    } else {
      nextPhysical |= joypadBitForButtonAction(settings.downAction);
    }
  }
  if (mappedInput.isPressed(Button::Confirm)) {
    if (settings.confirmAction == einkboy::ButtonAction::Rotate && mappedInput.wasPressed(Button::Confirm)) {
      rotateRequested = true;
    } else {
      nextPhysical |= joypadBitForButtonAction(settings.confirmAction);
    }
  }
  if (nextPhysical != physicalJoypadMask) {
    physicalJoypadMask = nextPhysical;
    applyJoypad();
  }

  if (mappedInput.hasTouch() && settings.touchControls) {
    bool handledMultiTouch = false;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    if (mappedInput.getTwoFingerTouch(x1, y1, x2, y2)) {
      handledMultiTouch = true;
      multiTouchActive = true;
      int ignoredX = 0, ignoredY = 0;
      (void)mappedInput.wasScreenTouchDown(ignoredX, ignoredY);

      const TouchControl first = hitTestTouch(x1, y1);
      const TouchControl second = hitTestTouch(x2, y2);
      if (first == TouchControl::Menu || second == TouchControl::Menu) {
        mappedInput.swallowCurrentTouch();
        pauseRequested = true;
      } else if (first == TouchControl::Rotate || second == TouchControl::Rotate) {
        rotateRequested = true;
      } else {
        const uint8_t nextTouch = static_cast<uint8_t>(joypadBitForTouch(first) | joypadBitForTouch(second));
        const TouchControl primary = first != TouchControl::None ? first : second;
        if (nextTouch != touchJoypadMask || primary != activeTouch) {
          activeTouch = primary;
          touchJoypadMask = nextTouch;
          applyJoypad();
        }
      }
    }

    if (!handledMultiTouch && multiTouchActive) {
      int x = 0, y = 0;
      if (mappedInput.getOneFingerTouch(x, y)) {
        const TouchControl remaining = hitTestTouch(x, y);
        if (remaining == TouchControl::Menu) {
          mappedInput.swallowCurrentTouch();
          pauseRequested = true;
        } else if (remaining == TouchControl::Rotate) {
          rotateRequested = true;
        } else {
          const uint8_t nextTouch = joypadBitForTouch(remaining);
          if (remaining != activeTouch || nextTouch != touchJoypadMask) {
            activeTouch = remaining;
            touchJoypadMask = nextTouch;
            applyJoypad();
          }
        }
      } else {
        multiTouchActive = false;
        activeTouch = TouchControl::None;
        touchJoypadMask = 0;
        applyJoypad();
      }
      handledMultiTouch = true;
    }

    if (!handledMultiTouch) {
      int x = 0, y = 0;
      if (mappedInput.wasScreenTouchDown(x, y)) updateTouch(x, y);
      if (mappedInput.isScreenTouchHeld(x, y)) {
        updateTouch(x, y);
      } else if (mappedInput.wasScreenTouchReleased()) {
        if (activeTouch != TouchControl::None || touchJoypadMask != 0) {
          activeTouch = TouchControl::None;
          touchJoypadMask = 0;
          applyJoypad();
        }
      }
    }
  } else if (touchJoypadMask != 0 || activeTouch != TouchControl::None || multiTouchActive) {
    multiTouchActive = false;
    activeTouch = TouchControl::None;
    touchJoypadMask = 0;
    applyJoypad();
  }
}

void EinkBoyActivity::updateTouch(const int x, const int y) {
  if (!settings.touchControls) return;
  const TouchControl control = hitTestTouch(x, y);
  if (control == TouchControl::Menu) {
    mappedInput.swallowCurrentTouch();
    pauseRequested = true;
    return;
  }
  if (control == TouchControl::Rotate) {
    rotateRequested = true;
    return;
  }
  if (control == activeTouch) return;
  activeTouch = control;
  touchJoypadMask = joypadBitForTouch(control);
  applyJoypad();
}

EinkBoyActivity::TouchControl EinkBoyActivity::hitTestTouch(const int x, const int y) const {
  for (size_t i = 0; i < controlCount; ++i) {
    const ControlButton& c = controls[i];
    if (hitRect(x, y, c.x, c.y, c.w, c.h)) return c.control;
  }
  return TouchControl::None;
}

uint8_t EinkBoyActivity::joypadBitForTouch(const TouchControl control) {
  switch (control) {
    case TouchControl::Up:
      return einkboy::kJoypadUp;
    case TouchControl::Down:
      return einkboy::kJoypadDown;
    case TouchControl::Left:
      return einkboy::kJoypadLeft;
    case TouchControl::Right:
      return einkboy::kJoypadRight;
    case TouchControl::A:
      return einkboy::kJoypadA;
    case TouchControl::B:
      return einkboy::kJoypadB;
    case TouchControl::Select:
      return einkboy::kJoypadSelect;
    case TouchControl::Start:
      return einkboy::kJoypadStart;
    default:
      return 0;
  }
}

uint8_t EinkBoyActivity::joypadBitForButtonAction(const einkboy::ButtonAction action) const {
  return einkboy::joypadBitForAction(action);
}

void EinkBoyActivity::applyJoypad() {
  const uint8_t activeLow = static_cast<uint8_t>(~(physicalJoypadMask | touchJoypadMask));
  if (useWalnutCgb && cgb) {
    walnut_cgb_set_joypad(cgb, activeLow);
  } else if (gb) {
    gb->direct.joypad = activeLow;
  }
}

void EinkBoyActivity::setGameLayout() {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  gameLayout = einkboy::computeGameLayout(screenW, screenH, portrait);
  controlCount = 0;

  if (portrait) {
    const int padY = gameLayout.gameY + gameLayout.gameH + 24;
    const int pad = 64;
    const int padX = 14;
    addControl(TouchControl::Up, padX + pad, padY, pad, pad, "U");
    addControl(TouchControl::Left, padX, padY + pad, pad, pad, "L");
    addControl(TouchControl::Right, padX + 2 * pad, padY + pad, pad, pad, "R");
    addControl(TouchControl::Down, padX + pad, padY + 2 * pad, pad, pad, "D");

    const int ab = 80;
    addControl(TouchControl::A, screenW - 20 - ab, padY + 24, ab, ab, "A");
    addControl(TouchControl::B, screenW - 36 - 2 * ab, padY + 24 + ab * 3 / 4, ab, ab, "B");

    const int rowH = 52;
    const int rowY = screenH - 12 - rowH;
    const int gap = 4;
    const int rowW = (screenW - 8 - 3 * gap) / 4;
    const TouchControl row[] = {TouchControl::Menu, TouchControl::Select, TouchControl::Start, TouchControl::Rotate};
    const char* rowLabels[] = {"MENU", "SEL", "START", "ROTATE"};
    for (int i = 0; i < 4; ++i) {
      addControl(row[i], 4 + i * (rowW + gap), rowY, rowW, rowH, rowLabels[i]);
    }
    return;
  }

  const int leftW = gameLayout.gameX;
  const int panelY = std::max(gameLayout.gameY + 120, screenH * 15 / 100);
  const int centerX = leftW / 2;
  addControl(TouchControl::Up, centerX - 35, panelY, 70, 58, "U");
  addControl(TouchControl::Down, centerX - 35, panelY + 140, 70, 58, "D");
  addControl(TouchControl::Left, 10, panelY + 58, 60, 82, "L");
  addControl(TouchControl::Right, leftW - 70, panelY + 58, 60, 82, "R");
  addControl(TouchControl::Rotate, 4, gameLayout.gameY + gameLayout.gameH - 52, leftW - 8, 48, "ROTATE");

  const int rightX = gameLayout.gameX + gameLayout.gameW;
  const int rightW = screenW - rightX;
  const int gap = 4;
  const int buttonW = std::min(70, std::max(48, (rightW - gap) / 2));
  const int firstX = rightX + 4;
  const int secondX = firstX + buttonW + gap;
  addControl(TouchControl::B, firstX, panelY, buttonW, 70, "B");
  addControl(TouchControl::A, secondX, panelY, buttonW, 70, "A");
  addControl(TouchControl::Select, firstX, gameLayout.gameY + 275, buttonW, 52, "SEL");
  addControl(TouchControl::Start, secondX, gameLayout.gameY + 275, buttonW, 52, "START");
  addControl(TouchControl::Menu, rightX + 4, gameLayout.gameY + gameLayout.gameH - 52, rightW - 8, 48, "MENU");
}

void EinkBoyActivity::addControl(const TouchControl control, const int x, const int y, const int w, const int h,
                                 const char* label) {
  if (controlCount >= MAX_CONTROLS) return;
  controls[controlCount++] = {control, x, y, w, h, label};
}

void EinkBoyActivity::rotatePlayingLayout() {
  mappedInput.swallowCurrentTouch();
  activeTouch = TouchControl::None;
  multiTouchActive = false;
  touchJoypadMask = 0;
  applyJoypad();
  {
    RenderLock lock(*this);
    display.finishOverlapped();
    if (renderer.supportsAsyncRefresh()) renderer.waitRefreshComplete();
    portrait = !portrait;
    renderer.setOrientation(portrait ? GfxRenderer::Portrait : GfxRenderer::LandscapeCounterClockwise);
    setGameLayout();
  }
  settings.layout = portrait ? 0 : 1;
  saveSettingsProfile(settingsForCgb);
  chromeKey = CHROME_NONE;
  fullRefreshDue.store(true, std::memory_order_release);
  displayDue.store(true, std::memory_order_release);
  requestUpdate(true);
}

void EinkBoyActivity::applyRefreshPreset() {
  if (!cpuSpeedLock) {
    cpuSpeedLock = makeUniqueNoThrow<HalPowerManager::Lock>();
  }
  display.finishOverlapped();
  if (renderer.supportsAsyncRefresh()) {
    renderer.waitRefreshComplete();
  }
  if (settings.refresh == 0) {
    // FAST: 40% scaled DU LUT, PLL 0x0F, 20 MHz SPI, partial diff window.
    display.setFastRefreshTuning(0, 0x0F);
    display.setFastRefreshDuScale(40);
    display.setFastRefreshDiffWindow(true);
    display.setDisplaySpiHz(20000000);
  } else if (settings.refresh == 1) {
    // TURBO: 25% scaled DU LUT, PLL 0x0F, 20 MHz SPI, partial diff window.
    display.setFastRefreshTuning(0, 0x0F);
    display.setFastRefreshDuScale(25);
    display.setFastRefreshDiffWindow(true);
    display.setDisplaySpiHz(20000000);
  } else {
    // STANDARD: stock OTP DU waveform, default SPI clock.
    display.setFastRefreshTuning(0, 0);
    display.setFastRefreshDuScale(0);
    display.setFastRefreshDiffWindow(false);
    display.setDisplaySpiHz(0);
  }
  uint32_t timings[8] = {};
  if (display.lastRefreshTimings(timings)) {
    perfRefreshBase = timings[6];
    perfSkipBase = timings[7];
  } else {
    perfRefreshBase = 0;
    perfSkipBase = 0;
  }
}

void EinkBoyActivity::resetDisplayTuning() {
  display.finishOverlapped();
  if (renderer.supportsAsyncRefresh()) {
    renderer.waitRefreshComplete();
  }
  display.setFastRefreshTuning(0, 0);
  display.setFastRefreshDuScale(0);
  display.setFastRefreshDiffWindow(false);
  display.setDisplaySpiHz(0);
  cpuSpeedLock.reset();
}

void EinkBoyActivity::logPerformanceIfDue(const uint64_t nowUs) {
  if (nextPerfLogUs == 0 || nowUs < nextPerfLogUs) return;
  nextPerfLogUs = nowUs + 2000000ULL;

  const uint32_t emuCount = perfEmuFrames.exchange(0, std::memory_order_relaxed);
  const uint32_t drawCount = perfDrawCount;
  const uint32_t drawAvgUs = drawCount > 0 ? (perfDrawUsTotal / drawCount) : 0;
  perfDrawCount = 0;
  perfDrawUsTotal = 0;

  uint32_t timings[8] = {};
  if (display.lastRefreshTimings(timings)) {
    const uint32_t refreshes = timings[6] - perfRefreshBase;
    const uint32_t skipped = timings[7] - perfSkipBase;
    perfRefreshBase = timings[6];
    perfSkipBase = timings[7];
    LOG_INF(
        TAG,
        "perf2s emu=%u ref=%u skip=%u drawAvg=%uus copy=%uus new=%uus setup=%uus wave=%uus old=%uus bytes=%u mode=%u",
        static_cast<unsigned>(emuCount), static_cast<unsigned>(refreshes), static_cast<unsigned>(skipped),
        static_cast<unsigned>(drawAvgUs), static_cast<unsigned>(timings[0]), static_cast<unsigned>(timings[1]),
        static_cast<unsigned>(timings[2]), static_cast<unsigned>(timings[3]), static_cast<unsigned>(timings[4]),
        static_cast<unsigned>(timings[5]), static_cast<unsigned>(settings.refresh));
  } else {
    LOG_INF(TAG, "perf2s emu=%u draws=%u drawAvg=%uus mode=%u", static_cast<unsigned>(emuCount),
            static_cast<unsigned>(drawCount), static_cast<unsigned>(drawAvgUs),
            static_cast<unsigned>(settings.refresh));
  }
}

bool EinkBoyActivity::loadRom() {
  HalFile file;
  if (!Storage.openFileForRead(TAG, romPath, file)) {
    openNotice("OPEN FAILED", "Unable to open ROM file.", View::Library);
    return false;
  }

  const uint64_t fileSize = file.fileSize64();
  if (fileSize < einkboy::kRomHeaderSize || fileSize > static_cast<uint64_t>(SIZE_MAX)) {
    openNotice("INVALID ROM", "ROM size is invalid.", View::Library);
    return false;
  }

  uint8_t romSizeCode = 0xFF;
  if (!file.seekSet(0x148) || file.read(&romSizeCode, 1) != 1) {
    openNotice("INVALID ROM", "ROM header could not be read.", View::Library);
    return false;
  }

  const size_t bankCount = einkboy::romBankCountFromCode(romSizeCode);
  if (bankCount == 0) {
    openNotice("UNSUPPORTED ROM", "Unsupported cartridge size code.", View::Library);
    return false;
  }

  const size_t expectedSize = bankCount * einkboy::kRomBankBytes;
  if (fileSize < expectedSize) {
    openNotice("INCOMPLETE ROM", "ROM file is shorter than its header declares.", View::Library);
    return false;
  }

  rom = allocPsramBytes(expectedSize);
  if (!rom) {
    openNotice("OUT OF PSRAM", "Not enough PSRAM for this ROM.", View::Library);
    return false;
  }

  if (!file.seekSet(0)) {
    openNotice("READ FAILED", "Could not rewind ROM file.", View::Library);
    return false;
  }

  size_t offset = 0;
  while (offset < expectedSize) {
    const size_t chunk = std::min<size_t>(16384, expectedSize - offset);
    const int got = file.read(rom + offset, chunk);
    if (got <= 0) {
      openNotice("READ FAILED", "Error reading ROM data from SD card.", View::Library);
      return false;
    }
    offset += static_cast<size_t>(got);
  }
  romSize = expectedSize;

  if (rom[0x149] > 5) {
    openNotice("UNSUPPORTED CART", "Unsupported cartridge RAM code.", View::Library);
    return false;
  }
  return true;
}

bool EinkBoyActivity::initEmulator() {
  if (useWalnutCgb) {
    int result = WALNUT_CGB_INIT_OUT_OF_MEMORY;
    cgb = walnut_cgb_create(rom, romSize, &EinkBoyActivity::walnutLcdDrawLine, &EinkBoyActivity::walnutFatalError, this,
                            &result);
    if (!cgb) {
      char msg[80];
      std::snprintf(msg, sizeof(msg), "Walnut-CGB init failed (%d).", result);
      openNotice("INIT FAILED", msg, View::Library);
      return false;
    }
    cartRamSize = walnut_cgb_save_size(cgb);
    cartRam = walnut_cgb_save_ram(cgb);
    lastConsumeUs32.store(static_cast<uint32_t>(nowMicros()), std::memory_order_relaxed);
    consumePeriodUs32.store(static_cast<uint32_t>(DISPLAY_PERIOD_US), std::memory_order_relaxed);
  } else {
    gb = static_cast<gb_s*>(std::calloc(1, sizeof(gb_s)));
    if (!gb) {
      openNotice("OUT OF MEMORY", "Could not allocate Peanut-GB state.", View::Library);
      return false;
    }
    const enum gb_init_error_e initResult = gb_init(gb, &EinkBoyActivity::romRead, &EinkBoyActivity::cartRamRead,
                                                    &EinkBoyActivity::cartRamWrite, &EinkBoyActivity::fatalError, this);
    if (initResult != GB_INIT_NO_ERROR) {
      openNotice("INIT FAILED", initErrorString(static_cast<int>(initResult)), View::Library);
      return false;
    }
    size_t saveSize = 0;
    if (gb_get_save_size_s(gb, &saveSize) < 0) {
      openNotice("INIT FAILED", "Unknown cartridge RAM size.", View::Library);
      return false;
    }
    if (saveSize > 0) {
      cartRam = allocPsramBytes(saveSize);
      if (!cartRam) {
        openNotice("OUT OF PSRAM", "Could not allocate cartridge save RAM.", View::Library);
        return false;
      }
      std::memset(cartRam, 0, saveSize);
      cartRamSize = saveSize;
    }
    gb_init_lcd(gb, &EinkBoyActivity::lcdDrawLine);
  }

  frameBuffers[0] = allocPsramBytes(einkboy::kFrameBytes);
  frameBuffers[1] = allocPsramBytes(einkboy::kFrameBytes);
  if (!frameBuffers[0] || !frameBuffers[1]) {
    openNotice("OUT OF PSRAM", "Could not allocate Game Boy framebuffers.", View::Library);
    return false;
  }
  std::memset(frameBuffers[0], 0, einkboy::kFrameBytes);
  std::memset(frameBuffers[1], 0, einkboy::kFrameBytes);
  writeFrame = 0;
  readyFrame.store(0xFF, std::memory_order_release);
  renderFrame.store(0xFF, std::memory_order_release);
  return true;
}

bool EinkBoyActivity::loadSave() {
  if (cartRamSize == 0 || !cartRam || (!useWalnutCgb && !einkboy::isBatteryBackedCartType(rom[0x147]))) {
    return true;
  }
  std::memset(cartRam, 0, cartRamSize);
  HalFile file;
  if (!Storage.openFileForRead(TAG, savePath, file)) return true;

  size_t offset = 0;
  while (offset < cartRamSize) {
    const size_t want = cartRamSize - offset;
    const int got = file.read(cartRam + offset, want);
    if (got <= 0) break;
    offset += static_cast<size_t>(got);
  }
  LOG_INF(TAG, "Loaded save: %u/%u bytes", static_cast<unsigned>(offset), static_cast<unsigned>(cartRamSize));
  return true;
}

bool EinkBoyActivity::saveCartRam() {
  if ((!saveDirty && !useWalnutCgb) || cartRamSize == 0 || !cartRam || !rom ||
      (!useWalnutCgb && !einkboy::isBatteryBackedCartType(rom[0x147]))) {
    return true;
  }

  bool synced = false;
  {
    HalFile file;
    if (!Storage.openFileForWrite(TAG, tempSavePath, file)) {
      LOG_ERR(TAG, "Unable to open save temp file: %s", tempSavePath);
      return false;
    }
    const size_t written = file.write(cartRam, cartRamSize);
    file.flush();
    synced = (written == cartRamSize);
  }
  if (!synced) {
    Storage.remove(tempSavePath);
    LOG_ERR(TAG, "Save write failed: %s", tempSavePath);
    return false;
  }

  const bool hadExisting = Storage.exists(savePath);
  if (hadExisting) {
    Storage.remove(backupSavePath);
    if (!Storage.rename(savePath, backupSavePath)) {
      Storage.remove(tempSavePath);
      LOG_ERR(TAG, "Could not back up previous save: %s", savePath);
      return false;
    }
  }

  if (!Storage.rename(tempSavePath, savePath)) {
    if (hadExisting) (void)Storage.rename(backupSavePath, savePath);
    Storage.remove(tempSavePath);
    LOG_ERR(TAG, "Could not promote save: %s", savePath);
    return false;
  }

  if (hadExisting) Storage.remove(backupSavePath);
  saveDirty = false;
  return true;
}

void EinkBoyActivity::cleanup() {
  romInitialized.store(false, std::memory_order_release);
  if (cgb) {
    walnut_cgb_destroy(cgb);
    cgb = nullptr;
    cartRam = nullptr;
    cartRamSize = 0;
  }
  if (gb) {
    std::free(gb);
    gb = nullptr;
  }
  if (rom) {
    freePsramBytes(rom);
    rom = nullptr;
  }
  if (cartRam) {
    freePsramBytes(cartRam);
    cartRam = nullptr;
  }
  for (auto*& buffer : frameBuffers) {
    freePsramBytes(buffer);
    buffer = nullptr;
  }
  writeFrame = 0;
  readyFrame.store(0xFF, std::memory_order_release);
  renderFrame.store(0xFF, std::memory_order_release);
  romSize = 0;
  cartRamSize = 0;
  physicalJoypadMask = 0;
  touchJoypadMask = 0;
  activeTouch = TouchControl::None;
  multiTouchActive = false;
  saveDirty = false;
  gameTitle[0] = '\0';
}

void EinkBoyActivity::runEmulation(const uint64_t nowUs) {
  if ((!gb && !cgb) || fatalEmuError.load(std::memory_order_acquire)) return;
  if (nextFrameUs == 0) nextFrameUs = nowUs;

  uint8_t ran = 0;
  while (nowUs >= nextFrameUs && ran < MAX_CATCH_UP_FRAMES) {
    const bool draw = shouldDrawFrame(nowUs);
    if (draw) {
      const uint8_t pending = readyFrame.load(std::memory_order_acquire);
      const uint8_t rendering = renderFrame.load(std::memory_order_acquire);
      if (writeFrame == pending || writeFrame == rendering) break;
    }

    if (useWalnutCgb) {
      walnut_cgb_run_frame_ex(cgb, draw ? 1 : 0);
      if (walnut_cgb_has_fatal_error(cgb)) {
        fatalEmuError.store(true, std::memory_order_release);
        break;
      }
    } else {
      hasPeanutJmp = true;
      if (setjmp(peanutFrameJmp) == 0) {
        gb_run_frame(gb);
      }
      hasPeanutJmp = false;
      if (fatalEmuError.load(std::memory_order_acquire)) break;
    }

    if (draw) publishCompletedFrame();
    perfEmuFrames.fetch_add(1, std::memory_order_relaxed);
    nextFrameUs += EMULATION_FRAME_PERIOD_US;
    ++ran;
  }

  if (ran == MAX_CATCH_UP_FRAMES && nowUs >= nextFrameUs) {
    nextFrameUs = nowUs + EMULATION_FRAME_PERIOD_US;
  }
}

bool EinkBoyActivity::shouldDrawFrame(const uint64_t nowUs) const {
  if (!useWalnutCgb) return true;
  if (readyFrame.load(std::memory_order_acquire) == 0xFF) return true;
  const uint32_t sinceConsume = static_cast<uint32_t>(nowUs) - lastConsumeUs32.load(std::memory_order_relaxed);
  return sinceConsume + DRAW_LEAD_US >= consumePeriodUs32.load(std::memory_order_relaxed);
}

void EinkBoyActivity::noteFrameConsumed() {
  const uint32_t nowUs32 = static_cast<uint32_t>(nowMicros());
  const uint32_t interval = nowUs32 - lastConsumeUs32.load(std::memory_order_relaxed);
  if (interval >= MIN_CONSUME_PERIOD_US && interval <= MAX_CONSUME_PERIOD_US) {
    const uint32_t period = consumePeriodUs32.load(std::memory_order_relaxed);
    consumePeriodUs32.store((period * 3U + interval) / 4U, std::memory_order_relaxed);
  }
  lastConsumeUs32.store(nowUs32, std::memory_order_relaxed);
}

void EinkBoyActivity::publishCompletedFrame() {
  readyFrame.store(writeFrame, std::memory_order_release);
  writeFrame ^= 1u;
}

void EinkBoyActivity::releaseRenderedFrame() {
  const uint8_t claimedFrame = renderFrame.load(std::memory_order_acquire);
  if (claimedFrame > 1) return;
  uint8_t expected = claimedFrame;
  readyFrame.compare_exchange_strong(expected, static_cast<uint8_t>(0xFF), std::memory_order_acq_rel,
                                     std::memory_order_acquire);
  renderFrame.store(0xFF, std::memory_order_release);
}

void EinkBoyActivity::drawGameFrame() {
  const uint8_t frameIndex = readyFrame.load(std::memory_order_acquire);
  if (frameIndex > 1) return;

  const uint8_t* source = frameBuffers[frameIndex];
  uint8_t* framebuffer = renderer.getFrameBuffer();
  if (!source || !framebuffer) return;

  renderFrame.store(frameIndex, std::memory_order_release);
  if (useWalnutCgb) noteFrameConsumed();

  const int panelBytes = renderer.getDisplayWidthBytes();
  const int panelW = renderer.getDisplayWidth();
  const int panelH = renderer.getDisplayHeight();

  bool drawn = false;
  const bool stableDither = (settings.ditherMode == 0);
  if (useWalnutCgb) {
    drawn = cgbdither::cgbDitherFrame(source, framebuffer, gameLayout.gameX, gameLayout.gameY, gameLayout.scale,
                                      panelBytes, panelH, portrait, stableDither);
  } else if (portrait) {
    drawn = einkboy::drawDmgPortrait(source, framebuffer, gameLayout.gameX, gameLayout.gameY, gameLayout.gameW,
                                     gameLayout.gameH, gameLayout.scale, panelBytes, panelW, panelH, stableDither);
  } else {
    drawn = einkboy::drawDmgLandscape(source, framebuffer, gameLayout.gameX, gameLayout.gameY, gameLayout.gameW,
                                      gameLayout.scale, panelBytes, panelW, panelH, stableDither);
  }

  if (!drawn) renderFrame.store(0xFF, std::memory_order_release);
}

void EinkBoyActivity::drawGameControls() {
  const uint8_t currentMask = static_cast<uint8_t>(physicalJoypadMask | touchJoypadMask);
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  const int borderLeft = std::max(0, gameLayout.gameX - 1);
  const int borderRight = std::min(screenW - 1, gameLayout.gameX + gameLayout.gameW);
  const int borderTop = std::max(0, gameLayout.gameY - 1);
  const int borderBottom = std::min(screenH - 1, gameLayout.gameY + gameLayout.gameH);
  if (gameLayout.gameY > 0) renderer.drawLine(borderLeft, borderTop, borderRight, borderTop, true);
  if (gameLayout.gameY + gameLayout.gameH < screenH) {
    renderer.drawLine(borderLeft, borderBottom, borderRight, borderBottom, true);
  }
  if (gameLayout.gameX > 0) renderer.drawLine(borderLeft, borderTop, borderLeft, borderBottom, true);
  if (gameLayout.gameX + gameLayout.gameW < screenW) {
    renderer.drawLine(borderRight, borderTop, borderRight, borderBottom, true);
  }

  char batteryLabel[16];
  std::snprintf(batteryLabel, sizeof(batteryLabel), "%u%%", static_cast<unsigned>(powerManager.getBatteryPercentage()));
  const int batteryWidth = renderer.getTextWidth(toybox::kTileFontId, batteryLabel);
  const int batteryX = std::max(0, screenW - batteryWidth - 8);
  if (settings.showGameTitle) {
    const char* titlePtr = gameTitle[0] ? gameTitle : "EINKBOY";
    const int titleAvail = std::max(1, batteryX - gameLayout.titleX - 8);
    const std::string visibleTitle =
        renderer.truncatedText(toybox::kTileFontId, titlePtr, titleAvail, EpdFontFamily::BOLD);
    renderer.drawText(toybox::kTileFontId, gameLayout.titleX, gameLayout.titleY, visibleTitle.c_str(), true,
                      EpdFontFamily::BOLD);
  }
  renderer.drawText(toybox::kTileFontId, batteryX, gameLayout.titleY, batteryLabel, true, EpdFontFamily::REGULAR);

  if (!settings.touchControls) return;
  const int textHeight = renderer.getTextHeight(toybox::kTileFontId);
  for (size_t i = 0; i < controlCount; ++i) {
    const ControlButton& c = controls[i];
    const uint8_t bit = joypadBitForTouch(c.control);
    const bool active = bit != 0 ? (currentMask & bit) != 0 : activeTouch == c.control;
    renderer.fillRect(c.x + 1, c.y + 1, c.w - 2, c.h - 2, active);
    renderer.drawRect(c.x, c.y, c.w, c.h, true);
    const int tw = renderer.getTextWidth(toybox::kTileFontId, c.label);
    renderer.drawText(toybox::kTileFontId, c.x + (c.w - tw) / 2, c.y + (c.h - textHeight) / 2, c.label, !active,
                      EpdFontFamily::BOLD);
  }
}

uint8_t EinkBoyActivity::romRead(gb_s* state, const uint_fast32_t addr) {
  auto* self = state ? static_cast<EinkBoyActivity*>(state->direct.priv) : nullptr;
  if (!self || !self->rom || addr >= self->romSize) return 0xFF;
  return self->rom[addr];
}

uint8_t EinkBoyActivity::cartRamRead(gb_s* state, const uint_fast32_t addr) {
  auto* self = state ? static_cast<EinkBoyActivity*>(state->direct.priv) : nullptr;
  if (!self || !self->cartRam || addr >= self->cartRamSize) return 0xFF;
  return self->cartRam[addr];
}

void EinkBoyActivity::cartRamWrite(gb_s* state, const uint_fast32_t addr, const uint8_t value) {
  auto* self = state ? static_cast<EinkBoyActivity*>(state->direct.priv) : nullptr;
  if (!self || !self->cartRam || addr >= self->cartRamSize) return;
  if (self->cartRam[addr] != value) self->saveDirty = true;
  self->cartRam[addr] = value;
}

void EinkBoyActivity::lcdDrawLine(gb_s* state, const uint8_t* pixels, const uint_fast8_t line) {
  auto* self = state ? static_cast<EinkBoyActivity*>(state->direct.priv) : nullptr;
  if (self) self->writeGameBoyLine(pixels, line);
}

void EinkBoyActivity::walnutLcdDrawLine(void* user, const uint8_t* pixels, const uint_fast8_t line) {
  auto* self = static_cast<EinkBoyActivity*>(user);
  if (self) self->writeGameBoyLine(pixels, line);
}

void EinkBoyActivity::writeGameBoyLine(const uint8_t* pixels, const uint_fast8_t line) {
  if (!pixels || line >= einkboy::kScreenHeight || !frameBuffers[writeFrame]) return;
  std::memcpy(frameBuffers[writeFrame] + static_cast<size_t>(line) * einkboy::kScreenWidth, pixels,
              einkboy::kScreenWidth);
}

void EinkBoyActivity::walnutFatalError(void* user, const int errorCode, const uint16_t address) {
  auto* self = static_cast<EinkBoyActivity*>(user);
  if (!self) return;
  LOG_ERR(TAG, "Walnut-CGB fatal error=%d addr=0x%04X", errorCode, address);
  (void)self->saveCartRam();
  self->fatalEmuError.store(true, std::memory_order_release);
}

void EinkBoyActivity::fatalError(gb_s* state, const enum gb_error_e errorCode, const uint16_t addr) {
  auto* self = state ? static_cast<EinkBoyActivity*>(state->direct.priv) : nullptr;
  if (!self) return;
  LOG_ERR(TAG, "Peanut-GB fatal error=%d addr=0x%04X", static_cast<int>(errorCode), addr);
  (void)self->saveCartRam();
  self->fatalEmuError.store(true, std::memory_order_release);
  if (self->hasPeanutJmp) {
    self->hasPeanutJmp = false;
    std::longjmp(self->peanutFrameJmp, 1);
  }
}

#endif  // defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)
