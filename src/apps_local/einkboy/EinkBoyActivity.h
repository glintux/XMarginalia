#pragma once

#include <atomic>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "EinkBoyCore.h"
#include "EinkBoyScreens.h"

#if defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)
#include "walnut_cgb_port.h"
extern "C" {
#define PEANUT_GB_IS_LITTLE_ENDIAN 1
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#define PEANUT_GB_HEADER_ONLY
#include <peanut_gb.h>
#undef PEANUT_GB_HEADER_ONLY
}
#endif

class EinkBoyActivity final : public Activity {
 public:
  EinkBoyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~EinkBoyActivity() override;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void prepareForSleep() override;
  void loop() override;
  void render(RenderLock&& lock) override;

  bool skipLoopDelay() override { return view == View::Playing; }
  bool preventAutoSleep() override { return view == View::Playing; }

 private:
  enum class View : uint8_t {
    Library = 0,
    GameActions,
    Playing,
    InGameMenu,
    Settings,
    Notice,
  };

  enum class TouchControl : uint8_t {
    None = 0,
    Up,
    Down,
    Left,
    Right,
    A,
    B,
    Select,
    Start,
    Menu,
    Rotate,
  };

  struct ControlButton {
    TouchControl control = TouchControl::None;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    const char* label = nullptr;
  };

  struct GameEntry {
    std::string path;
    std::string title;
    bool isGbc = false;
  };

  static constexpr size_t MAX_GAMES = 256;
  static constexpr size_t MAX_FAVORITES = 128;
  static constexpr size_t MAX_RECENTS = 32;
  static constexpr unsigned MAX_SCAN_DEPTH = 4;
  static constexpr size_t MAX_CONTROLS = 10;

  static constexpr uint64_t EMULATION_FRAME_PERIOD_US = 16743ULL;
  static constexpr uint64_t DISPLAY_PERIOD_US = 250000ULL;
  static constexpr uint64_t FAST_DISPLAY_PERIOD_US = 65000ULL;
  static constexpr uint64_t AUTOSAVE_PERIOD_US = 60000000ULL;
  static constexpr uint64_t CLEAN_PERIOD_US = 60000000ULL;
  static constexpr uint8_t MAX_CATCH_UP_FRAMES = 4;
  static constexpr uint32_t DRAW_LEAD_US = static_cast<uint32_t>(3 * EMULATION_FRAME_PERIOD_US);
  static constexpr uint32_t MIN_CONSUME_PERIOD_US = 30000U;
  static constexpr uint32_t MAX_CONSUME_PERIOD_US = 2000000U;
  static constexpr uint32_t CHROME_NONE = 0xFFFFFFFFu;

  View view = View::Library;
  View noticeReturnView = View::Library;

  // Library state.
  std::vector<GameEntry> games;
  std::vector<size_t> visibleGames;
  std::vector<std::string> favorites;
  std::vector<std::string> recents;
  einkboyui::LibraryFilter filter = einkboyui::LibraryFilter::All;
  int libraryPage = 0;
  int selectedRomGlobal = -1;
  size_t activeGameIndex = 0;
  bool gameActionConfirmDelete = false;
  int gameActionSelected = 0;
  int pauseMenuSelected = 0;
  int settingsSelected = 0;
  bool settingsFromGame = false;
  char statusMessage[96] = {};
  char noticeHeadline[64] = {};
  char noticeDetail[128] = {};

  // Active settings (loaded for the current profile: DMG vs CGB).
  einkboy::Settings settings;
  bool settingsForCgb = false;

  // Toybox screen interaction buffer (used for all non-Playing views).
  toybox::Interactions interactions;
  bool interactionsReady = false;

#if defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)
  gb_s* gb = nullptr;
  WalnutCgbContext* cgb = nullptr;
  bool useWalnutCgb = false;
  uint8_t* rom = nullptr;
  size_t romSize = 0;
  uint8_t* cartRam = nullptr;
  size_t cartRamSize = 0;
  uint8_t* frameBuffers[2] = {nullptr, nullptr};
  uint8_t writeFrame = 0;
  std::atomic<uint8_t> readyFrame{0xFF};
  std::atomic<uint8_t> renderFrame{0xFF};

  std::string romPath;
  char savePath[256] = {};
  char tempSavePath[256] = {};
  char backupSavePath[256] = {};
  char gameTitle[32] = {};

  std::atomic<uint32_t> lastConsumeUs32{0};
  std::atomic<uint32_t> consumePeriodUs32{static_cast<uint32_t>(DISPLAY_PERIOD_US)};
  uint8_t behindStreak = 0;

  uint8_t physicalJoypadMask = 0;
  uint8_t touchJoypadMask = 0;
  bool multiTouchActive = false;
  TouchControl activeTouch = TouchControl::None;

  bool portrait = true;
  einkboy::GameLayout gameLayout;
  ControlButton controls[MAX_CONTROLS] = {};
  size_t controlCount = 0;
  uint32_t chromeKey = CHROME_NONE;

  std::atomic<bool> romInitialized{false};
  std::atomic<bool> displayDue{true};
  std::atomic<bool> fullRefreshDue{false};
  std::atomic<bool> fatalEmuError{false};
  bool rotateRequested = false;
  bool pauseRequested = false;
  bool saveDirty = false;
  uint64_t nextFrameUs = 0;
  uint64_t nextDisplayUs = 0;
  uint64_t nextAutosaveUs = 0;
  uint64_t nextCleanUs = 0;

  std::jmp_buf peanutFrameJmp{};
  bool hasPeanutJmp = false;

#if defined(ARDUINO_ARCH_ESP32)
  TaskHandle_t emuTask = nullptr;
  std::atomic<bool> emuRun{false};
  std::atomic<bool> emuStopped{true};
  static void emuTaskEntry(void* arg);
  void emuTaskLoop();
#endif
  void startEmuTask();
  void stopEmuTask();
#endif  // defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)

  // Navigation & library persistence.
  void loadLibraryState();
  bool saveLibraryState();
  void scanDirectory(const std::string& path, unsigned depth);
  void rebuildVisibleGames();
  void loadSettingsProfile(bool cgbProfile);
  bool saveSettingsProfile(bool cgbProfile) const;
  bool isFavorite(const std::string& path) const;
  void toggleFavorite(const std::string& path);
  void recordRecent(const std::string& path);
  bool selectedGameHasSave(const std::string& path) const;
  bool deleteSaveForRom(const std::string& path);

  void openNotice(const char* headline, const char* detail, View returnTo);
  void openSettings(bool fromGame);
  void handleSettingToggle(einkboyui::SettingRow row);
  void routeToyboxView();
  void handleLibraryAction(const freeink::ui::ActionEvent& event);
  void handleGameAction(const freeink::ui::ActionEvent& event);
  void handlePauseAction(const freeink::ui::ActionEvent& event);
  void handleSettingsAction(const freeink::ui::ActionEvent& event);

  // Emulation lifecycle (no-op / safe fallback when PSRAM is absent).
  bool launchRom(size_t gameIdx);
  void pauseToMenu();
  void resumeFromPause();
  void stopAndUnloadGame();

#if defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)
  void routePlaying();
  void handleGameInput();
  void updateTouch(int x, int y);
  TouchControl hitTestTouch(int x, int y) const;
  static uint8_t joypadBitForTouch(TouchControl control);
  uint8_t joypadBitForButtonAction(einkboy::ButtonAction action) const;
  void applyJoypad();
  void setGameLayout();
  void addControl(TouchControl control, int x, int y, int w, int h, const char* label);
  void rotatePlayingLayout();

  bool loadRom();
  bool initEmulator();
  bool loadSave();
  bool saveCartRam();
  void cleanup();

  void runEmulation(uint64_t nowUs);
  bool shouldDrawFrame(uint64_t nowUs) const;
  void noteFrameConsumed();
  void publishCompletedFrame();
  void releaseRenderedFrame();
  void drawGameFrame();
  void drawGameControls();

  static uint8_t romRead(gb_s* state, uint_fast32_t addr);
  static uint8_t cartRamRead(gb_s* state, uint_fast32_t addr);
  static void cartRamWrite(gb_s* state, uint_fast32_t addr, uint8_t value);
  static void lcdDrawLine(gb_s* state, const uint8_t* pixels, uint_fast8_t line);
  static void fatalError(gb_s* state, enum gb_error_e errorCode, uint16_t addr);
  static void walnutLcdDrawLine(void* user, const uint8_t* pixels, uint_fast8_t line);
  static void walnutFatalError(void* user, int errorCode, uint16_t address);
  void writeGameBoyLine(const uint8_t* pixels, uint_fast8_t line);
#endif
};
