/*
 * XMarginalia Walnut-CGB bridge (ported from Ink-boy, MIT).
 * License and upstream notices are retained in lib/WalnutCGB/src/walnut_cgb.h.
 */

#if defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)

#if !defined(WALNUT_CGB_KEEP_BUILD_OPT) && defined(__GNUC__) && !defined(__clang__) && defined(__OPTIMIZE_SIZE__)
#pragma GCC optimize("O2")
#endif

#include "walnut_cgb_color.h"
#include "walnut_cgb_port.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#ifndef SIMULATOR
#include <esp_heap_caps.h>
#endif

/* Fixed configuration: CGB emulation enabled; audio omitted; CPU is little endian. */
#define WALNUT_GB_IS_LITTLE_ENDIAN 1
#define WALNUT_GB_RGB565_BIGENDIAN 0
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#define WALNUT_GB_12_COLOUR 1
#define WALNUT_FULL_GBC_SUPPORT 1

/* Namespace Walnut-CGB symbols so Peanut-GB and Walnut-CGB link cleanly in one binary. */
#define __gb_read16 walnut_core_read16
#define __gb_read32 walnut_core_read32
#define __gb_read walnut_core_read
#define __gb_write walnut_core_write
#define __gb_write32 walnut_core_write32
#define __gb_write16 walnut_core_write16
#define __gb_execute_cb walnut_core_execute_cb
#define __gb_draw_line walnut_core_draw_line
#define __gb_step_cpu walnut_core_step_cpu
#define __gb_step_cpu_x walnut_core_step_cpu_x
#define gb_run_frame walnut_core_run_frame
#define gb_run_frame_dualfetch walnut_core_run_frame_dualfetch
#define gb_get_save_size_s walnut_core_get_save_size_s
#define gb_get_save_size walnut_core_get_save_size
#define gb_init_serial walnut_core_init_serial
#define gb_colour_hash walnut_core_colour_hash
#define gb_reset walnut_core_reset
#define gb_init walnut_core_init
#define gb_get_rom_name walnut_core_get_rom_name
#define gb_init_lcd walnut_core_init_lcd
#define gb_set_bootrom walnut_core_set_bootrom
#define gb_tick_rtc walnut_core_tick_rtc
#define gb_set_rtc walnut_core_set_rtc

#include <walnut_cgb.h>

#ifndef WALNUT_CGB_INTERNAL_RESERVE_BYTES
#define WALNUT_CGB_INTERNAL_RESERVE_BYTES (128U * 1024U)
#endif
#ifndef WALNUT_CGB_INTERNAL_BLOCK_MARGIN_BYTES
#define WALNUT_CGB_INTERNAL_BLOCK_MARGIN_BYTES (16U * 1024U)
#endif

struct WalnutCgbContext {
  struct gb_s gb;
  const uint8_t *rom;
  size_t rom_size;
  uint8_t *cart_ram;
  size_t cart_ram_size;
  WalnutCgbLineCallback line_callback;
  WalnutCgbFatalCallback fatal_callback;
  void *user;
  WalnutCgbLuminanceCache luminance;
  int state_in_internal_ram;
  int fatal_occurred;
  int has_frame_jmp;
  jmp_buf frame_jmp;
  uint8_t grayscale_line[160];
};

static struct WalnutCgbContext *context_from_gb(struct gb_s *gb) {
  return gb ? (struct WalnutCgbContext *)gb->direct.priv : NULL;
}

static uint8_t walnut_rom_read(struct gb_s *gb, const uint_fast32_t address) {
  const struct WalnutCgbContext *ctx = (const struct WalnutCgbContext *)gb->direct.priv;
  return address < ctx->rom_size ? ctx->rom[address] : 0xFF;
}

static uint16_t walnut_rom_read16(struct gb_s *gb, const uint_fast32_t address) {
  const struct WalnutCgbContext *ctx = (const struct WalnutCgbContext *)gb->direct.priv;
  if (address + 2U <= ctx->rom_size) {
    const uint8_t *p = ctx->rom + address;
    return (uint16_t)(p[0] | (uint16_t)(p[1] << 8));
  }
  return (uint16_t)(walnut_rom_read(gb, address) | (uint16_t)(walnut_rom_read(gb, address + 1U) << 8));
}

static uint32_t walnut_rom_read32(struct gb_s *gb, const uint_fast32_t address) {
  const struct WalnutCgbContext *ctx = (const struct WalnutCgbContext *)gb->direct.priv;
  if (address + 4U <= ctx->rom_size) {
    const uint8_t *p = ctx->rom + address;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  }
  return (uint32_t)walnut_rom_read(gb, address) | ((uint32_t)walnut_rom_read(gb, address + 1U) << 8) |
         ((uint32_t)walnut_rom_read(gb, address + 2U) << 16) | ((uint32_t)walnut_rom_read(gb, address + 3U) << 24);
}

static uint8_t walnut_cart_ram_read(struct gb_s *gb, const uint_fast32_t address) {
  struct WalnutCgbContext *ctx = context_from_gb(gb);
  if (!ctx || !ctx->cart_ram || address >= ctx->cart_ram_size) return 0xFF;
  return ctx->cart_ram[address];
}

static void walnut_cart_ram_write(struct gb_s *gb, const uint_fast32_t address, const uint8_t value) {
  struct WalnutCgbContext *ctx = context_from_gb(gb);
  if (!ctx || !ctx->cart_ram || address >= ctx->cart_ram_size) return;
  ctx->cart_ram[address] = value;
}

static void walnut_lcd_draw_line(struct gb_s *gb, const uint8_t *pixels, const uint_fast8_t line) {
  struct WalnutCgbContext *ctx = context_from_gb(gb);
  if (!ctx || !pixels || line >= 144U || !ctx->line_callback) return;

  if (gb->cgb.cgbMode) {
    walnut_cgb_luminance_cache_sync(&ctx->luminance, gb->cgb.fixPalette);
    walnut_cgb_convert_cgb_line(&ctx->luminance, pixels, ctx->grayscale_line);
  } else {
    walnut_cgb_convert_dmg_line(pixels, ctx->grayscale_line);
  }

  ctx->line_callback(ctx->user, ctx->grayscale_line, line);
}

static void walnut_core_error(struct gb_s *gb, const enum gb_error_e error, const uint16_t address) {
  struct WalnutCgbContext *ctx = context_from_gb(gb);
  if (!ctx) return;
  ctx->fatal_occurred = 1;
  if (ctx->fatal_callback) ctx->fatal_callback(ctx->user, (int)error, address);
  if (ctx->has_frame_jmp) {
    ctx->has_frame_jmp = 0;
    longjmp(ctx->frame_jmp, 1);
  }
}

static void *walnut_alloc_state(size_t size, int *in_internal_ram) {
  *in_internal_ram = 0;
#ifdef SIMULATOR
  *in_internal_ram = 1;
  return calloc(1, size);
#else
#ifndef WALNUT_CGB_STATE_IN_PSRAM
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= size + WALNUT_CGB_INTERNAL_RESERVE_BYTES &&
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >=
          size + WALNUT_CGB_INTERNAL_BLOCK_MARGIN_BYTES) {
    void *memory = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (memory) {
      *in_internal_ram = 1;
      return memory;
    }
  }
#endif
  return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
}

static void *walnut_alloc_psram(size_t size) {
#ifdef SIMULATOR
  return calloc(1, size);
#else
  return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
}

static void walnut_free_mem(void *ptr) {
  if (!ptr) return;
#ifdef SIMULATOR
  free(ptr);
#else
  heap_caps_free(ptr);
#endif
}

WalnutCgbContext *walnut_cgb_create(const uint8_t *rom, size_t rom_size,
                                     WalnutCgbLineCallback line_callback,
                                     WalnutCgbFatalCallback fatal_callback,
                                     void *user, int *init_result) {
  struct WalnutCgbContext *ctx;
  size_t save_size = 0;
  enum gb_init_error_e result;
  int in_internal_ram = 0;

  if (init_result) *init_result = WALNUT_CGB_INIT_OUT_OF_MEMORY;
  if (!rom || rom_size < 0x150U || !line_callback) return NULL;

  ctx = (struct WalnutCgbContext *)walnut_alloc_state(sizeof(*ctx), &in_internal_ram);
  if (!ctx) return NULL;
  ctx->state_in_internal_ram = in_internal_ram;
  ctx->rom = rom;
  ctx->rom_size = rom_size;
  ctx->line_callback = line_callback;
  ctx->fatal_callback = fatal_callback;
  ctx->user = user;

  result = walnut_core_init(&ctx->gb, walnut_rom_read, walnut_rom_read16, walnut_rom_read32,
                            walnut_cart_ram_read, walnut_cart_ram_write, walnut_core_error, ctx);
  if (result != GB_INIT_NO_ERROR) {
    if (init_result) *init_result = (int)result;
    walnut_free_mem(ctx);
    return NULL;
  }

  if (walnut_core_get_save_size_s(&ctx->gb, &save_size) < 0) {
    if (init_result) *init_result = WALNUT_CGB_INIT_INVALID_SAVE_SIZE;
    walnut_free_mem(ctx);
    return NULL;
  }
  if (save_size > 0) {
    ctx->cart_ram = (uint8_t *)walnut_alloc_psram(save_size);
    if (!ctx->cart_ram) {
      if (init_result) *init_result = WALNUT_CGB_INIT_OUT_OF_MEMORY;
      walnut_free_mem(ctx);
      return NULL;
    }
    ctx->cart_ram_size = save_size;
  }

  /* Prime all 64 converted RGB565 cache entries before frame 0 renders. */
  {
    size_t i;
    for (i = 0; i < 32U; ++i) {
      const uint16_t bg = (uint16_t)(ctx->gb.cgb.BGPalette[2U * i] |
                                     ((uint16_t)ctx->gb.cgb.BGPalette[2U * i + 1U] << 8));
      const uint16_t obj = (uint16_t)(ctx->gb.cgb.OAMPalette[2U * i] |
                                      ((uint16_t)ctx->gb.cgb.OAMPalette[2U * i + 1U] << 8));
      ctx->gb.cgb.fixPalette[i] = bgr555_to_rgb565_accurate(bg);
      ctx->gb.cgb.fixPalette[0x20U + i] = bgr555_to_rgb565_accurate(obj);
    }
  }

  walnut_core_init_lcd(&ctx->gb, walnut_lcd_draw_line);
  if (init_result) *init_result = WALNUT_CGB_INIT_NO_ERROR;
  return ctx;
}

void walnut_cgb_destroy(WalnutCgbContext *context) {
  if (!context) return;
  if (context->cart_ram) walnut_free_mem(context->cart_ram);
  walnut_free_mem(context);
}

size_t walnut_cgb_save_size(const WalnutCgbContext *context) {
  return context ? context->cart_ram_size : 0U;
}

uint8_t *walnut_cgb_save_ram(WalnutCgbContext *context) {
  return context ? context->cart_ram : NULL;
}

int walnut_cgb_state_in_internal_ram(const WalnutCgbContext *context) {
  return context ? context->state_in_internal_ram : 0;
}

int walnut_cgb_has_fatal_error(const WalnutCgbContext *context) {
  return context ? context->fatal_occurred : 0;
}

void walnut_cgb_run_frame_ex(WalnutCgbContext *context, int draw_lines) {
  if (!context || context->fatal_occurred) return;
  context->gb.direct.frame_skip = 1;
  context->gb.display.frame_skip_count = draw_lines ? 1 : 0;
  context->has_frame_jmp = 1;
  if (setjmp(context->frame_jmp) == 0) {
    walnut_core_run_frame(&context->gb);
  }
  context->has_frame_jmp = 0;
}

void walnut_cgb_run_frame(WalnutCgbContext *context) {
  walnut_cgb_run_frame_ex(context, 1);
}

void walnut_cgb_set_joypad(WalnutCgbContext *context, uint8_t active_low_joypad) {
  if (context) context->gb.direct.joypad = active_low_joypad;
}

void walnut_cgb_set_rtc(WalnutCgbContext *context, const struct tm *time_info) {
  if (context && time_info) walnut_core_set_rtc(&context->gb, time_info);
}

void walnut_cgb_get_rom_name(WalnutCgbContext *context, char *title) {
  if (context && title) walnut_core_get_rom_name(&context->gb, title);
}

#endif  /* defined(BOARD_HAS_PSRAM) || defined(SIMULATOR) */
