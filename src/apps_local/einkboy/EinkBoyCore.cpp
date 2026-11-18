#include "EinkBoyCore.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace einkboy {

namespace {

constexpr int kMaxGameRowBytes = 64;

char toLowerAscii(const char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool endsWithIgnoreCase(const std::string_view text, const std::string_view suffix) {
  if (text.size() < suffix.size()) return false;
  const size_t offset = text.size() - suffix.size();
  for (size_t i = 0; i < suffix.size(); ++i) {
    if (toLowerAscii(text[offset + i]) != toLowerAscii(suffix[i])) return false;
  }
  return true;
}

bool equalsIgnoreCase(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    if (toLowerAscii(*a) != toLowerAscii(*b)) return false;
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

ButtonAction sanitizeAction(const uint8_t raw, const ButtonAction fallback) {
  if (raw < static_cast<uint8_t>(ButtonAction::Count)) {
    return static_cast<ButtonAction>(raw);
  }
  return fallback;
}

void gamePatterns(uint8_t (&patterns)[2][4][2], const int scale, const bool alongIsX) {
  for (int fixed = 0; fixed < 2; ++fixed) {
    for (int shade = 0; shade < 4; ++shade) {
      for (int along0 = 0; along0 < 2; ++along0) {
        uint8_t pattern = 0;
        for (int k = 0; k < scale; ++k) {
          const int along = along0 + k;
          const bool black = alongIsX ? shadeIsBlack(static_cast<uint8_t>(shade), along, fixed)
                                      : shadeIsBlack(static_cast<uint8_t>(shade), fixed, along);
          pattern = static_cast<uint8_t>((pattern << 1) | (black ? 1 : 0));
        }
        patterns[fixed][shade][along0] = pattern;
      }
    }
  }
}

void buildGameRow(uint8_t* out, const uint8_t* src, const int step, const int count, const int scale,
                  const uint8_t (&patterns)[4][2]) {
  uint32_t acc = 0;
  int bits = 0;
  for (int g = 0; g < count; ++g) {
    acc = (acc << scale) | patterns[src[g * step] & 0x03][(g * scale) & 1];
    bits += scale;
    if (bits >= 8) {
      bits -= 8;
      *out++ = static_cast<uint8_t>(~(acc >> bits));
      acc &= (1u << bits) - 1u;
    }
  }
}

}  // namespace

const char* buttonActionLabel(const ButtonAction action) {
  switch (action) {
    case ButtonAction::None:
      return "OFF";
    case ButtonAction::GbA:
      return "A";
    case ButtonAction::GbB:
      return "B";
    case ButtonAction::GbStart:
      return "START";
    case ButtonAction::GbSelect:
      return "SELECT";
    case ButtonAction::GbUp:
      return "D-PAD UP";
    case ButtonAction::GbDown:
      return "D-PAD DOWN";
    case ButtonAction::GbLeft:
      return "D-PAD LEFT";
    case ButtonAction::GbRight:
      return "D-PAD RIGHT";
    case ButtonAction::Rotate:
      return "ROTATE";
    case ButtonAction::Count:
      break;
  }
  return "OFF";
}

ButtonAction nextButtonAction(const ButtonAction action) {
  const uint8_t count = static_cast<uint8_t>(ButtonAction::Count);
  const uint8_t next = static_cast<uint8_t>((static_cast<uint8_t>(action) + 1U) % count);
  return static_cast<ButtonAction>(next);
}

uint8_t joypadBitForAction(const ButtonAction action) {
  switch (action) {
    case ButtonAction::GbA:
      return kJoypadA;
    case ButtonAction::GbB:
      return kJoypadB;
    case ButtonAction::GbStart:
      return kJoypadStart;
    case ButtonAction::GbSelect:
      return kJoypadSelect;
    case ButtonAction::GbUp:
      return kJoypadUp;
    case ButtonAction::GbDown:
      return kJoypadDown;
    case ButtonAction::GbLeft:
      return kJoypadLeft;
    case ButtonAction::GbRight:
      return kJoypadRight;
    default:
      return 0;
  }
}

size_t serializeSettings(const Settings& settings, uint8_t* out, const size_t outSize) {
  if (!out || outSize < kSettingsBinarySize) return 0;
  std::memset(out, 0, kSettingsBinarySize);
  out[0] = 'E';
  out[1] = 'K';
  out[2] = 'B';
  out[3] = 'S';
  out[4] = 1;
  out[5] = settings.layout <= 1 ? settings.layout : 0;
  out[6] = settings.refresh <= 1 ? settings.refresh : 0;
  out[7] = settings.touchControls ? 1 : 0;
  out[8] = settings.showGameTitle ? 1 : 0;
  out[9] = static_cast<uint8_t>(settings.upAction);
  out[10] = static_cast<uint8_t>(settings.downAction);
  out[11] = static_cast<uint8_t>(settings.confirmAction);
  return kSettingsBinarySize;
}

bool deserializeSettings(const uint8_t* data, const size_t size, Settings& out) {
  if (!data || size < 12) return false;
  if (data[0] != 'E' || data[1] != 'K' || data[2] != 'B' || data[3] != 'S' || data[4] != 1) return false;
  const Settings defaults;
  out.layout = data[5] <= 1 ? data[5] : defaults.layout;
  out.refresh = data[6] <= 1 ? data[6] : defaults.refresh;
  out.touchControls = data[7] <= 1 ? data[7] : defaults.touchControls;
  out.showGameTitle = data[8] <= 1 ? data[8] : defaults.showGameTitle;
  out.upAction = sanitizeAction(data[9], defaults.upAction);
  out.downAction = sanitizeAction(data[10], defaults.downAction);
  out.confirmAction = sanitizeAction(data[11], defaults.confirmAction);
  return true;
}

size_t romBankCountFromCode(const uint8_t code) {
  if (code > 8) return 0;
  return static_cast<size_t>(2) << code;
}

bool isBatteryBackedCartType(const uint8_t cartType) {
  switch (cartType) {
    case 0x03:  // MBC1 + RAM + BATTERY
    case 0x06:  // MBC2 + BATTERY
    case 0x09:  // ROM + RAM + BATTERY
    case 0x0F:  // MBC3 + TIMER + BATTERY
    case 0x10:  // MBC3 + TIMER + RAM + BATTERY
    case 0x13:  // MBC3 + RAM + BATTERY
    case 0x1B:  // MBC5 + RAM + BATTERY
    case 0x1E:  // MBC7 + RAM + BATTERY
      return true;
    default:
      return false;
  }
}

bool hasGbcExtension(const std::string_view path) { return endsWithIgnoreCase(path, ".gbc"); }

bool hasSupportedRomExtension(const std::string_view name) {
  return endsWithIgnoreCase(name, ".gb") || endsWithIgnoreCase(name, ".gbc");
}

bool isCgbRom(const std::string_view path, const uint8_t header143) {
  return hasGbcExtension(path) || ((header143 & 0x80U) != 0);
}

bool shouldSkipScanDirectory(const char* name) {
  if (!name || !name[0] || name[0] == '.') return true;
  return equalsIgnoreCase(name, "Android") || equalsIgnoreCase(name, "LOST.DIR") ||
         equalsIgnoreCase(name, "System Volume Information") || equalsIgnoreCase(name, "Recycle.Bin");
}

void formatTitleFromPath(const std::string_view path, char* out, const size_t outSize) {
  if (!out || outSize == 0) return;
  const size_t slash = path.find_last_of('/');
  const size_t start = slash == std::string_view::npos ? 0 : slash + 1;
  const size_t dot = path.find_last_of('.');
  const size_t end = (dot != std::string_view::npos && dot > start) ? dot : path.size();
  const size_t len = std::min(outSize - 1, end - start);
  if (len > 0) std::memcpy(out, path.data() + start, len);
  out[len] = '\0';
}

bool buildSavePaths(const std::string_view romPath, char* saveOut, const size_t saveSize, char* tmpOut,
                    const size_t tmpSize, char* bakOut, const size_t bakSize) {
  if (romPath.empty() || !saveOut || !tmpOut || !bakOut) return false;
  const size_t slash = romPath.find_last_of('/');
  const size_t dot = romPath.find_last_of('.');
  const size_t stemEnd = (dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash)) ? dot
                                                                                                             : romPath.size();
  if (stemEnd + 8 >= saveSize || stemEnd + 8 >= tmpSize || stemEnd + 8 >= bakSize) return false;

  const int nSave = std::snprintf(saveOut, saveSize, "%.*s.sav", static_cast<int>(stemEnd), romPath.data());
  const int nTmp = std::snprintf(tmpOut, tmpSize, "%.*s.sav.tmp", static_cast<int>(stemEnd), romPath.data());
  const int nBak = std::snprintf(bakOut, bakSize, "%.*s.sav.bak", static_cast<int>(stemEnd), romPath.data());
  return nSave > 0 && static_cast<size_t>(nSave) < saveSize && nTmp > 0 && static_cast<size_t>(nTmp) < tmpSize &&
         nBak > 0 && static_cast<size_t>(nBak) < bakSize;
}

bool parseLibraryLine(std::string_view line, bool& isFavorite, std::string_view& pathOut) {
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  if (line.size() <= 2 || line[1] != '\t') return false;
  if (line[0] == 'F') {
    isFavorite = true;
  } else if (line[0] == 'R') {
    isFavorite = false;
  } else {
    return false;
  }
  pathOut = line.substr(2);
  return !pathOut.empty();
}

GameLayout computeGameLayout(const int screenW, const int screenH, const bool portrait) {
  GameLayout out;
  if (portrait) {
    out.scale = std::max(1, std::min(kMaxScale, screenW / kScreenWidth));
    out.gameW = kScreenWidth * out.scale;
    out.gameH = kScreenHeight * out.scale;
    out.gameX = std::max(0, (screenW - out.gameW) / 2);
    out.gameY = 40;
    if (out.gameY + out.gameH > screenH) {
      out.gameY = std::max(0, ((screenH - out.gameH) / 2) & ~7);
    }
    out.titleX = 8;
    out.titleY = 10;
    return out;
  }

  const int availH = std::max(kScreenHeight, screenH - 48);
  out.scale = std::max(1, std::min({kMaxScale, std::max(kScreenWidth, screenW) / kScreenWidth, availH / kScreenHeight}));
  out.gameW = kScreenWidth * out.scale;
  out.gameH = kScreenHeight * out.scale;
  out.gameX = std::max(0, ((screenW - out.gameW) / 2) & ~7);
  out.gameY = std::max(0, (screenH - out.gameH) / 2);
  out.titleX = 8;
  out.titleY = 18;
  return out;
}

bool shadeIsBlack(const uint8_t shade, const int x, const int y) {
  switch (shade) {
    case 3:
      return true;
    case 2:
      return ((x ^ y) & 1) == 0 || (x & 1) == 0;
    case 1:
      return (x & 1) == 0 && (y & 1) == 0;
    default:
      return false;
  }
}

bool drawDmgLandscape(const uint8_t* source, uint8_t* framebuffer, const int gameX, const int gameY, const int gameW,
                      const int scale, const int panelBytes, const int panelW, const int panelH) {
  const int gameH = kScreenHeight * scale;
  const int rowBytes = gameW / 8;
  if (!source || !framebuffer || scale < 1 || scale > kMaxScale || panelBytes <= 0 || panelW <= 0 || panelH <= 0 ||
      gameX < 0 || gameY < 0 || (gameX & 7) != 0 || (gameW & 7) != 0 || gameX + gameW > panelW ||
      gameY + gameH > panelH || rowBytes > kMaxGameRowBytes) {
    return false;
  }
  uint8_t patterns[2][4][2];
  gamePatterns(patterns, scale, /*alongIsX=*/true);
  uint8_t rows[2][kMaxGameRowBytes];
  for (int gy = 0; gy < kScreenHeight; ++gy) {
    const uint8_t* line = source + gy * kScreenWidth;
    for (int parity = 0; parity < 2; ++parity) {
      buildGameRow(rows[parity], line, 1, kScreenWidth, scale, patterns[parity]);
    }
    for (int sy = 0; sy < scale; ++sy) {
      const int panelY = gy * scale + sy;
      uint8_t* out = framebuffer + static_cast<size_t>(gameY + panelY) * static_cast<size_t>(panelBytes) + (gameX >> 3);
      std::memcpy(out, rows[panelY & 1], static_cast<size_t>(rowBytes));
    }
  }
  return true;
}

bool drawDmgPortrait(const uint8_t* source, uint8_t* framebuffer, const int gameX, const int gameY, const int gameW,
                     const int gameH, const int scale, const int panelBytes, const int panelW, const int panelH) {
  const int rowBytes = gameH / 8;
  if (!source || !framebuffer || scale < 1 || scale > kMaxScale || panelBytes <= 0 || panelW <= 0 || panelH <= 0 ||
      gameX < 0 || gameY < 0 || (gameY & 7) != 0 || (gameH & 7) != 0 || gameX + gameW > panelH ||
      gameY + gameH > panelW || rowBytes > kMaxGameRowBytes) {
    return false;
  }
  uint8_t patterns[2][4][2];
  gamePatterns(patterns, scale, /*alongIsX=*/false);
  uint8_t rows[2][kMaxGameRowBytes];
  for (int gx = 0; gx < kScreenWidth; ++gx) {
    for (int parity = 0; parity < 2; ++parity) {
      buildGameRow(rows[parity], source + gx, kScreenWidth, kScreenHeight, scale, patterns[parity]);
    }
    for (int sx = 0; sx < scale; ++sx) {
      const int dstX = gx * scale + sx;
      uint8_t* out = framebuffer + static_cast<size_t>(panelH - 1 - (gameX + dstX)) * static_cast<size_t>(panelBytes) +
                     (gameY >> 3);
      std::memcpy(out, rows[dstX & 1], static_cast<size_t>(rowBytes));
    }
  }
  return true;
}

}  // namespace einkboy
