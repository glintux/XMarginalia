#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "CgbDither.h"
#include "EinkBoyCore.h"
#include "walnut_cgb_port.h"

extern "C" {
#define PEANUT_GB_IS_LITTLE_ENDIAN 1
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#define PEANUT_GB_HEADER_ONLY
#include <peanut_gb.h>
#undef PEANUT_GB_HEADER_ONLY
}

namespace {

int gChecks = 0;
int gFailed = 0;

void check(const bool ok, const char* label, const int line) {
  ++gChecks;
  if (!ok) {
    ++gFailed;
    std::printf("FAIL einkboy (%d): %s\n", line, label);
  }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

// Constructs a minimal valid 32KB DMG or CGB ROM with proper Nintendo logo checksum and global checksum.
std::vector<uint8_t> makeTestRom(const char* title, const bool cgb) {
  std::vector<uint8_t> rom(32768, 0x00);
  // 0x100: NOP; JR -2 (infinite idle loop after PC=0x100).
  rom[0x100] = 0x00;
  rom[0x101] = 0x18;
  rom[0x102] = 0xFE;

  // Nintendo logo bytes required by some loaders.
  static constexpr uint8_t kLogo[48] = {
      0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
      0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E, 0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
      0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
  };
  std::memcpy(&rom[0x104], kLogo, sizeof(kLogo));

  for (size_t i = 0; i < 15 && title && title[i]; ++i) {
    rom[0x134 + i] = static_cast<uint8_t>(title[i]);
  }
  rom[0x143] = cgb ? 0x80 : 0x00;
  rom[0x147] = 0x03;  // MBC1 + RAM + BATTERY
  rom[0x148] = 0x00;  // 32KB (2 banks)
  rom[0x149] = 0x02;  // 8KB RAM

  uint8_t headerSum = 0;
  for (size_t addr = 0x134; addr <= 0x14C; ++addr) {
    headerSum = static_cast<uint8_t>(headerSum - rom[addr] - 1);
  }
  rom[0x14D] = headerSum;
  return rom;
}

struct PeanutBridge {
  const uint8_t* rom = nullptr;
  size_t romSize = 0;
  std::vector<uint8_t> cartRam;
  std::vector<uint8_t> fb;
  int linesDrawn = 0;
  bool fatalCalled = false;
};

uint8_t pRomRead(struct gb_s* gb, const uint_fast32_t addr) {
  auto* b = static_cast<PeanutBridge*>(gb->direct.priv);
  return (b && addr < b->romSize) ? b->rom[addr] : 0xFF;
}

uint8_t pRamRead(struct gb_s* gb, const uint_fast32_t addr) {
  auto* b = static_cast<PeanutBridge*>(gb->direct.priv);
  return (b && addr < b->cartRam.size()) ? b->cartRam[addr] : 0xFF;
}

void pRamWrite(struct gb_s* gb, const uint_fast32_t addr, const uint8_t v) {
  auto* b = static_cast<PeanutBridge*>(gb->direct.priv);
  if (b && addr < b->cartRam.size()) b->cartRam[addr] = v;
}

void pDrawLine(struct gb_s* gb, const uint8_t* pixels, const uint_fast8_t line) {
  auto* b = static_cast<PeanutBridge*>(gb->direct.priv);
  if (!b || line >= einkboy::kScreenHeight) return;
  std::memcpy(&b->fb[static_cast<size_t>(line) * einkboy::kScreenWidth], pixels, einkboy::kScreenWidth);
  ++b->linesDrawn;
}

void pError(struct gb_s* gb, const enum gb_error_e, const uint16_t) {
  auto* b = static_cast<PeanutBridge*>(gb->direct.priv);
  if (b) b->fatalCalled = true;
}

struct WalnutBridge {
  int linesDrawn = 0;
  bool fatalCalled = false;
  std::vector<uint8_t> fb;
};

void wDrawLine(void* user, const uint8_t* lum, const uint_fast8_t line) {
  auto* b = static_cast<WalnutBridge*>(user);
  if (!b || line >= einkboy::kScreenHeight) return;
  std::memcpy(&b->fb[static_cast<size_t>(line) * einkboy::kScreenWidth], lum, einkboy::kScreenWidth);
  ++b->linesDrawn;
}

void wFatal(void* user, int, uint16_t) {
  auto* b = static_cast<WalnutBridge*>(user);
  if (b) b->fatalCalled = true;
}

void testCoreHelpers() {
  using namespace einkboy;

  CHECK(romBankCountFromCode(0) == 2);
  CHECK(romBankCountFromCode(8) == 512);
  CHECK(romBankCountFromCode(9) == 0);

  CHECK(isBatteryBackedCartType(0x03));
  CHECK(isBatteryBackedCartType(0x1B));
  CHECK(!isBatteryBackedCartType(0x00));
  CHECK(!isBatteryBackedCartType(0x01));

  CHECK(hasSupportedRomExtension("tetris.gb"));
  CHECK(hasSupportedRomExtension("Zelda.GBC"));
  CHECK(!hasSupportedRomExtension("readme.txt"));
  CHECK(hasGbcExtension("poke.GBC"));
  CHECK(isCgbRom("game.gb", 0x80));
  CHECK(!isCgbRom("game.gb", 0x00));

  CHECK(shouldSkipScanDirectory("."));
  CHECK(shouldSkipScanDirectory(".crosspoint"));
  CHECK(shouldSkipScanDirectory("Android"));
  CHECK(!shouldSkipScanDirectory("ROMS"));

  char title[32] = {};
  formatTitleFromPath("/roms/gb/Links Awakening.gb", title, sizeof(title));
  CHECK(std::strcmp(title, "Links Awakening") == 0);

  char sav[128] = {}, tmp[128] = {}, bak[128] = {};
  CHECK(buildSavePaths("/roms/Pokemon Red.gb", sav, sizeof(sav), tmp, sizeof(tmp), bak, sizeof(bak)));
  CHECK(std::strcmp(sav, "/roms/Pokemon Red.sav") == 0);
  CHECK(std::strcmp(tmp, "/roms/Pokemon Red.sav.tmp") == 0);
  CHECK(std::strcmp(bak, "/roms/Pokemon Red.sav.bak") == 0);

  bool fav = false;
  std::string_view p;
  CHECK(parseLibraryLine("F\t/roms/tetris.gb\r", fav, p) && fav && p == "/roms/tetris.gb");
  CHECK(parseLibraryLine("R\t/roms/zelda.gbc", fav, p) && !fav && p == "/roms/zelda.gbc");
  CHECK(!parseLibraryLine("X\t/roms/bad.gb", fav, p));
}

void testSettingsAndButtons() {
  using namespace einkboy;

  Settings s;
  s.layout = 1;
  s.refresh = 2;
  s.touchControls = 0;
  s.showGameTitle = 0;
  s.upAction = ButtonAction::GbStart;
  s.downAction = ButtonAction::Rotate;
  s.confirmAction = ButtonAction::GbB;
  s.ditherMode = 1;
  s.autoClean = 1;

  uint8_t buf[kSettingsBinarySize] = {};
  CHECK(serializeSettings(s, buf, sizeof(buf)) == kSettingsBinarySize);

  Settings decoded;
  CHECK(deserializeSettings(buf, sizeof(buf), decoded));
  CHECK(decoded.layout == 1);
  CHECK(decoded.refresh == 2);
  CHECK(decoded.touchControls == 0);
  CHECK(decoded.showGameTitle == 0);
  CHECK(decoded.upAction == ButtonAction::GbStart);
  CHECK(decoded.downAction == ButtonAction::Rotate);
  CHECK(decoded.confirmAction == ButtonAction::GbB);
  CHECK(decoded.ditherMode == 1);
  CHECK(decoded.autoClean == 1);

  // Out-of-range button action falls back cleanly without UB.
  buf[9] = 250;
  CHECK(deserializeSettings(buf, sizeof(buf), decoded));
  CHECK(decoded.upAction == Settings{}.upAction);

  buf[0] = 'X';
  CHECK(!deserializeSettings(buf, sizeof(buf), decoded));

  CHECK(joypadBitForAction(ButtonAction::GbA) == kJoypadA);
  CHECK(joypadBitForAction(ButtonAction::Rotate) == 0);
  CHECK(std::strcmp(buttonActionLabel(ButtonAction::GbSelect), "SELECT") == 0);
  CHECK(nextButtonAction(ButtonAction::Rotate) == ButtonAction::None);
}

void testLayoutAndDither() {
  using namespace einkboy;

  const GameLayout port = computeGameLayout(480, 800, true);
  CHECK(port.scale == 3 && port.gameW == 480 && port.gameH == 432);
  CHECK((port.gameY & 7) == 0);

  const GameLayout land = computeGameLayout(800, 480, false);
  CHECK(land.scale == 3 && land.gameW == 480 && land.gameH == 432);
  CHECK((land.gameX & 7) == 0);

  // DMG dithering fills framebuffer cleanly without out-of-bounds writes.
  std::vector<uint8_t> src(kFrameBytes, 3);  // shade 3 = black -> bits should become 0
  std::vector<uint8_t> panel(100 * 480, 0xFF);
  CHECK(drawDmgLandscape(src.data(), panel.data(), land.gameX, land.gameY, land.gameW, land.scale, 100, 800, 480));
  CHECK(panel[static_cast<size_t>(land.gameY) * 100 + (land.gameX >> 3)] == 0x00);

  std::fill(panel.begin(), panel.end(), 0xFF);
  CHECK(drawDmgPortrait(src.data(), panel.data(), port.gameX, port.gameY, port.gameW, port.gameH, port.scale, 100, 800,
                        480));
  CHECK(panel[static_cast<size_t>(480 - 1 - port.gameX) * 100 + (port.gameY >> 3)] == 0x00);

  // Reject unaligned DMG blit.
  CHECK(!drawDmgLandscape(src.data(), panel.data(), 3, land.gameY, land.gameW, land.scale, 100, 800, 480));

  // CgbDither: all-black luminance produces all-0 bits; all-white produces all-1 bits.
  std::vector<uint8_t> lumBlack(kFrameBytes, 0);
  std::fill(panel.begin(), panel.end(), 0xFF);
  CHECK(cgbdither::cgbDitherFrame(lumBlack.data(), panel.data(), land.gameX, land.gameY, land.scale, 100, 480, false));
  CHECK(panel[static_cast<size_t>(land.gameY) * 100 + (land.gameX >> 3)] == 0x00);

  std::fill(panel.begin(), panel.end(), 0xFF);
  CHECK(cgbdither::cgbDitherFrame(lumBlack.data(), panel.data(), port.gameX, port.gameY, port.scale, 100, 480, true));
  CHECK(panel[static_cast<size_t>(480 - 1 - port.gameX) * 100 + (port.gameY >> 3)] == 0x00);

  // Scroll-stable DMG dithering: shifting a sprite by 1 game pixel across a grey
  // background leaves the background bytes outside the sprite bit-identical.
  std::vector<uint8_t> dmgA(kFrameBytes, 1);  // uniform light grey background
  std::vector<uint8_t> dmgB(kFrameBytes, 1);
  dmgA[10 * kScreenWidth + 10] = 3;
  dmgB[10 * kScreenWidth + 11] = 3;
  std::vector<uint8_t> panelA(100 * 480, 0xFF);
  std::vector<uint8_t> panelB(100 * 480, 0xFF);
  CHECK(drawDmgLandscape(dmgA.data(), panelA.data(), land.gameX, land.gameY, land.gameW, land.scale, 100, 800, 480,
                         /*stableDither=*/true));
  CHECK(drawDmgLandscape(dmgB.data(), panelB.data(), land.gameX, land.gameY, land.gameW, land.scale, 100, 800, 480,
                         /*stableDither=*/true));
  // Far right of the same row is completely unaffected.
  const size_t checkRowOffset = static_cast<size_t>(land.gameY + 10 * land.scale) * 100 + (land.gameX >> 3) + 30;
  CHECK(std::memcmp(&panelA[checkRowOffset], &panelB[checkRowOffset], 20) == 0);

  // Scroll-stable Ordered Bayer CGB dithering: changing one pixel on a grey
  // background stays strictly local (no error-diffusion wake across the row).
  std::vector<uint8_t> cgbA(kFrameBytes, 128);
  std::vector<uint8_t> cgbB(kFrameBytes, 128);
  cgbA[20 * kScreenWidth + 8] = 0;
  cgbB[20 * kScreenWidth + 9] = 0;
  std::fill(panelA.begin(), panelA.end(), 0xFF);
  std::fill(panelB.begin(), panelB.end(), 0xFF);
  CHECK(cgbdither::cgbDitherFrame(cgbA.data(), panelA.data(), land.gameX, land.gameY, land.scale, 100, 480, false,
                                  /*orderedDither=*/true));
  CHECK(cgbdither::cgbDitherFrame(cgbB.data(), panelB.data(), land.gameX, land.gameY, land.scale, 100, 480, false,
                                  /*orderedDither=*/true));
  const size_t cgbTailOffset = static_cast<size_t>(land.gameY + 20 * land.scale) * 100 + (land.gameX >> 3) + 20;
  CHECK(std::memcmp(&panelA[cgbTailOffset], &panelB[cgbTailOffset], 30) == 0);
}

void testEmulatorSmoke() {
  // Peanut-GB smoke test.
  const std::vector<uint8_t> dmgRom = makeTestRom("EINKTEST", false);
  PeanutBridge pb;
  pb.rom = dmgRom.data();
  pb.romSize = dmgRom.size();
  pb.cartRam.resize(8192, 0);
  pb.fb.resize(einkboy::kFrameBytes, 0);

  gb_s gb = {};
  const enum gb_init_error_e initRes = gb_init(&gb, pRomRead, pRamRead, pRamWrite, pError, &pb);
  CHECK(initRes == GB_INIT_NO_ERROR);
  gb_init_lcd(&gb, pDrawLine);
  gb_run_frame(&gb);  // Frame 0 finishes initial VBLANK-mode scanline setup.
  pb.linesDrawn = 0;
  gb_run_frame(&gb);
  CHECK(!pb.fatalCalled);
  CHECK(pb.linesDrawn == einkboy::kScreenHeight);

  char romTitle[17] = {};
  gb_get_rom_name(&gb, romTitle);
  CHECK(std::strcmp(romTitle, "EINKTEST") == 0);

  // Walnut-CGB smoke test (with both draw_lines=1 and draw_lines=0).
  const std::vector<uint8_t> cgbRom = makeTestRom("CGBTEST", true);
  WalnutBridge wb;
  wb.fb.resize(einkboy::kFrameBytes, 0);
  int walnutInit = -99;
  WalnutCgbContext* ctx = walnut_cgb_create(cgbRom.data(), cgbRom.size(), wDrawLine, wFatal, &wb, &walnutInit);
  CHECK(ctx != nullptr && walnutInit == WALNUT_CGB_INIT_NO_ERROR);
  if (ctx) {
    CHECK(walnut_cgb_save_size(ctx) == 8192);
    walnut_cgb_run_frame_ex(ctx, 1);  // Warm up initial scanline after reset.
    wb.linesDrawn = 0;
    walnut_cgb_run_frame_ex(ctx, 1);
    CHECK(wb.linesDrawn == einkboy::kScreenHeight);
    wb.linesDrawn = 0;
    walnut_cgb_run_frame_ex(ctx, 0);
    CHECK(wb.linesDrawn == 0);
    CHECK(walnut_cgb_has_fatal_error(ctx) == 0);

    char cgbTitle[17] = {};
    walnut_cgb_get_rom_name(ctx, cgbTitle);
    CHECK(std::strcmp(cgbTitle, "CGBTEST") == 0);
    walnut_cgb_destroy(ctx);
  }

  // Test Walnut-CGB fatal error unwinding without aborting/restarting.
  std::vector<uint8_t> badRom = makeTestRom("BADOP", true);
  badRom[0x100] = 0xDD;  // Undefined Game Boy opcode triggers GB_INVALID_OPCODE.
  WalnutBridge badWb;
  badWb.fb.resize(einkboy::kFrameBytes, 0);
  int badInit = -99;
  WalnutCgbContext* badCtx = walnut_cgb_create(badRom.data(), badRom.size(), wDrawLine, wFatal, &badWb, &badInit);
  CHECK(badCtx != nullptr);
  if (badCtx) {
    walnut_cgb_run_frame_ex(badCtx, 1);
    CHECK(badWb.fatalCalled);
    CHECK(walnut_cgb_has_fatal_error(badCtx) != 0);
    walnut_cgb_destroy(badCtx);
  }
}

}  // namespace

int main() {
  testCoreHelpers();
  testSettingsAndButtons();
  testLayoutAndDither();
  testEmulatorSmoke();
  std::printf("%d checks, %d failed\n", gChecks, gFailed);
  return gFailed == 0 ? 0 : 1;
}
