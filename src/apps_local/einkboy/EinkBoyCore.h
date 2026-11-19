#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace einkboy {

constexpr int kScreenWidth = 160;
constexpr int kScreenHeight = 144;
constexpr int kMaxScale = 3;
constexpr size_t kRomHeaderSize = 0x150;
constexpr size_t kRomBankBytes = 0x4000;
constexpr size_t kFrameBytes = static_cast<size_t>(kScreenWidth) * static_cast<size_t>(kScreenHeight);

// Active-high joypad bits matching Peanut-GB's JOYPAD_* defines.
constexpr uint8_t kJoypadA = 0x01;
constexpr uint8_t kJoypadB = 0x02;
constexpr uint8_t kJoypadSelect = 0x04;
constexpr uint8_t kJoypadStart = 0x08;
constexpr uint8_t kJoypadRight = 0x10;
constexpr uint8_t kJoypadLeft = 0x20;
constexpr uint8_t kJoypadUp = 0x40;
constexpr uint8_t kJoypadDown = 0x80;

// Selectable actions for physical buttons (Up, Down, Confirm).
// Button::Back is statically bound to Pause/Menu/Exit and is never remapped.
enum class ButtonAction : uint8_t {
  None = 0,
  GbA,
  GbB,
  GbStart,
  GbSelect,
  GbUp,
  GbDown,
  GbLeft,
  GbRight,
  Rotate,
  Count,
};

struct Settings {
  // layout: 0 portrait, 1 landscape.
  uint8_t layout = 0;
  // refresh: 0 FAST (40% DU + PLL 0x0F + 20 MHz SPI + diff window),
  //          1 TURBO (25% DU + PLL 0x0F + 20 MHz SPI + diff window),
  //          2 STANDARD (stock OTP DU waveform, 10 MHz SPI).
  uint8_t refresh = 0;
  uint8_t touchControls = 1;
  uint8_t showGameTitle = 1;
  ButtonAction upAction = ButtonAction::GbA;
  ButtonAction downAction = ButtonAction::GbB;
  ButtonAction confirmAction = ButtonAction::GbStart;
  // ditherMode: 0 STABLE (scroll-invariant block/Bayer dither), 1 SMOOTH (panel-parity / Floyd-Steinberg).
  uint8_t ditherMode = 0;
  // autoClean: 0 ON PAUSE (clean on pause/resume/rotate only), 1 AUTO 60S (periodic 60s mid-gameplay clean).
  uint8_t autoClean = 0;
};

constexpr size_t kSettingsBinarySize = 16;

const char* buttonActionLabel(ButtonAction action);
ButtonAction nextButtonAction(ButtonAction action);
uint8_t joypadBitForAction(ButtonAction action);

size_t serializeSettings(const Settings& settings, uint8_t* out, size_t outSize);
bool deserializeSettings(const uint8_t* data, size_t size, Settings& out);

// Cartridge & ROM metadata helpers.
size_t romBankCountFromCode(uint8_t code);
bool isBatteryBackedCartType(uint8_t cartType);
bool hasGbcExtension(std::string_view path);
bool hasSupportedRomExtension(std::string_view name);
bool isCgbRom(std::string_view path, uint8_t header143);
bool shouldSkipScanDirectory(const char* name);

void formatTitleFromPath(std::string_view path, char* out, size_t outSize);
bool buildSavePaths(std::string_view romPath, char* saveOut, size_t saveSize, char* tmpOut, size_t tmpSize,
                    char* bakOut, size_t bakSize);
bool parseLibraryLine(std::string_view line, bool& isFavorite, std::string_view& pathOut);

// Game area placement in logical renderer coordinates.
struct GameLayout {
  int scale = 3;
  int gameX = 0;
  int gameY = 40;
  int gameW = 480;
  int gameH = 432;
  int titleX = 8;
  int titleY = 10;
};

GameLayout computeGameLayout(int screenW, int screenH, bool portrait);

// DMG 4-shade dithering helpers (0 = white .. 3 = black).
bool shadeIsBlack(uint8_t shade, int x, int y);
bool drawDmgLandscape(const uint8_t* source, uint8_t* framebuffer, int gameX, int gameY, int gameW, int scale,
                      int panelBytes, int panelW, int panelH, bool stableDither = false);
bool drawDmgPortrait(const uint8_t* source, uint8_t* framebuffer, int gameX, int gameY, int gameW, int gameH, int scale,
                     int panelBytes, int panelW, int panelH, bool stableDither = false);

}  // namespace einkboy
