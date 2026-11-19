# XMarginalia Architecture & Upstream Differences

**XMarginalia** is a fork built by unifying three related e-reader firmware projects:

- [**CrossPoint Reader**](https://github.com/crosspoint-reader/crosspoint-reader) (upstream core EPUB/XTC reader, SD plugin host, sync, and core HAL/FS)
- [**CrossInk**](https://github.com/uxjulia/CrossInk) (UI themes, 5-slot modular status bar customization, Focus Reading / Phase 4 typography, reading stats, Nearby CIFT file transfer, and Controls 2.0)
- [**CrossPlay**](https://github.com/ma-r-s/crossplay) (`src/apps_local/` shelf of games, Study, Notes, Wikipedia, Wallpapers, Instapaper, Hacker News, xkcd, Calculator, and its host-test & simulator harness)

This document itemizes **every feature, board target, subsystem, build script, and SDK change in XMarginalia that differs from upstream CrossPoint, CrossInk, and CrossPlay**. See [LOCAL_SCOPE.md](../LOCAL_SCOPE.md) for the code-seam table linking these features to upstream files.

---

## 1. Hardware Targets, Partitions, and Tooling

XMarginalia targets ESP32-S3 e-ink readers (`platformio.ini` + `freeink-sdk`) and preserves CrossPlay's `x4pro`, `sticky`, and `papermono` profiles while extending and hardening Picco & EEGO A4 support:

| Feature / File | What XMarginalia Does | Upstream Difference |
|---|---|---|
| **Onyx BOOX Picco** (`picco`, `gh_release_picco`, `partitions_picco.csv`) | Native 480x800 portrait profile with TMA525C touch, Up/Down side keys, warm/cool frontlight, hardware toggle switch, and app-only `0xE0000` (`ota_0`) writes leaving vendor `nvs` + `safeboot` untouched. | Uses CrossPoint/CrossPlay `#4510` `scripts/app_only_upload.py` paired with `scripts_local/app_slot_guard.py` (refuses `erase`, `uploadfs`, wrong offsets, removes `firmware.factory.bin`), plus `src/network/UpdateSlot.h` and `scripts_local/check-partitions.sh`. See [picco.md](picco.md). |
| **EEGO Reader A4 / A4 Lite** (`eego_a4`, `gh_release_eego_a4`) | ESP32-S3 N16R8 profile (`FREEINK_DEVICE_EEGO_A4=1`, UC8279C 768x552 + GSLX680 touch) with `500 ms` controller idle (`idleDisplayWhileAwake()`), `400 ms` power-button wake verification + `+400 ms` hold duration, `HALF_REFRESH` cleanup on settings/Home return from reader, factory MAC UID in About (`HalSystem::getEegoUid()`), and slim bottom progress bar tucked into the rounded-corner bottom inset (`UITheme::getReaderStatusBarHeight()`, `BaseTheme::drawModularStatusBar`). | Not shipped in CrossPlay `v1.15.1` default release workflows; XMarginalia ships `gh_release_eego_a4` in `build-release.sh` alongside `gh_release_x4pro` and `gh_release_picco`, with `#4362`, `#4494`, and `#4520` panel/UID fixes applied. |
| **Release Builder** (`scripts_local/build-release.sh`) | Builds `gh_release_x4pro`, `gh_release_picco`, and `gh_release_eego_a4` (or `--dev` variants) in a single PlatformIO invocation, verifies Picco partition guards via `scripts_local/check-partitions.sh`, and packages `dist/xmarginalia-audiobookshelf-plugin.zip` via `scripts_local/package-plugins.sh`. | Specific to XMarginalia (`xmarginalia` version `1.1.0` in `platformio.ini`). |
| **Zero-Rebuild Flasher** (`scripts_local/flash-device.sh`) | Linux USB flasher that reads `0x8000` partition tables to auto-detect X4 Pro vs Picco vs EEGO A4 (`auto | x4pro | picco | eego_a4`), enforces `0xE0000` app-only flashing on Picco, and defaults X4 Pro to factory/app-only modes without invoking a slow PlatformIO rebuild. | Unique to XMarginalia (coexists with CrossPlay's `usb-flash.sh` and `wifi-flash.sh`). |
| **Installer Safeguards** (`site/assets/install.js`, `host-tests/site/install_fn.js`) | Reads flash partition table fail-open (`.catch(() => null)`) to guard Picco `safeboot` layouts before writing, while retaining XMarginalia's full partition write checks. | Synced with CrossPlay `111b5c76`. |

---

## 2. Submodule `freeink-sdk` Customizations (`xmarginalia/sdk-20261117`)

XMarginalia rebases its SDK changes cleanly onto CrossPlay's `freeink-sdk` (`64f79e1`, `origin/fix/chrome-margins-1151`):

1. **All-PSRAM wolfSSL Allocators (`libs/network/SecureNet/src/SecureClient.cpp`)**:
   - Registers custom `XMALLOC`/`XFREE`/`XREALLOC` hooks via `ensureWolfAllocators()` using `psramFound()` and `heap_caps_malloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` for **all** `BOARD_HAS_PSRAM` devices (X4 Pro, Sticky, PaperMono, Picco, EEGO A4), rather than CrossPlay's Picco-only `src/network/WolfsslPsramAllocators.h`.
   - Keeps ~40–60 KB of internal DRAM free for Wi-Fi DMA RX/TX buffers during concurrent TLS sessions (OPDS, Audiobookshelf plugin, AI Companion, Study sync).
2. **Display `_asyncShadow` in PSRAM (`f484c09` / `df3ceb0`)**:
   - Allocates `_asyncShadow = allocFrameBufferStorage()` with `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`, preventing a 48 KB DRAM hit during async display refreshes.
3. **OPDS Parser & Client Improvements (`libs/network/Opds/`)**:
   - XMarginalia enhancements in `OpdsParser.{h,cpp}`, `Opds2Parser.cpp`, `OpdsClient.cpp`, and `OpenSearchDescParser.cpp` for feed parsing, Basic Auth / OPDS acquisition handling, and PSRAM Expat friendliness.

---

## 3. UI, Themes, Typography, and 5-Slot Modular Status Bar

- **CrossInk Themes**:
  - Adds **Minimal** (`src/components/themes/minimal/MinimalTheme.{h,cpp}`) and **Dashboard** (`src/components/themes/dashboard/DashboardTheme.{h,cpp}`) themes alongside CrossPoint/CrossPlay's **Lyra**, **Classic**, **Lyra Extended**, and **Rounded Raff**.
- **5-Slot Modular Status Bar** (`src/CrossPointSettings.{h,cpp}`, `src/components/themes/BaseTheme.cpp`, `src/activities/settings/StatusBarSettingsActivity.{h,cpp}`, `src/components/UITheme.{h,cpp}`):
  - Extends status-bar customization with configurable **Top** (`topLeft`, `topCenter`, `topRight`) and **Bottom** (`bottomLeft`, `bottomMidLeft`, `bottomCenter`, `bottomMidRight`, `bottomRight`) lanes plus selectable `progressBarPosition` (Top/Bottom).
  - Restores **`bottomMidLeft`** and **`bottomMidRight`** slots explicitly so clocks, battery, or page readouts can share the bottom bar without colliding with center chapter titles (validated by `host-tests/statusbar/`).
  - Combines this modular renderer (`BaseTheme::drawModularStatusBar`) with `#4362`'s EEGO A4 bottom-inset slim progress bar and `UITheme::getReaderStatusBarHeight()`.
- **Per-Book Settings (`BookSettings.{h,cpp}`, `TextSettingsActivity.{h,cpp}`)**:
  - Persists per-book typography overrides (margins via `effectiveScreenMargin()`, font family/size, spacing, guide dots, focus reading) independently of global reader defaults.
- **Focus Reading & Phase 4 Typography (`lib/Epub/`, `src/CrossPointSettings.h`, `docs/focus-reading.md`)**:
  - Adds `focusReadingEnabled` (bionic-style bold emphasis on word prefixes), **Guide Dots**, word/character spacing refinements, and shortened `Custom` / `Global` formatting status indicators.
- **Home Shelf & Navigation Integration**:
  - Moves **OPDS Browser** under **File Transfer / Network Mode Selection** (`NetworkModeSelectionActivity`), keeping Home focused on recent books, File Browser, Library, File Transfer, Settings, Games, and Apps (with `ActivityManager::goHome` routing `OpdsBookBrowser`/`PluginCatalog` back cleanly).

---

## 4. Controls, Touch & Hardware Inputs (CrossInk 2.0 Parity)

- **Master Reader Touch Controls & Gestures**:
  - `touchReaderControls` master toggle (`CrossPointSettings.h`), configurable forward/previous gesture filters (`pageTurnGesture`, `previousPageGesture`), reader menu trigger (`showReaderMenu`), and Picco **Up + Down** hardware recovery shortcut outside the reader to re-enable touch if disabled.
- **Touch Edge Gestures**:
  - `edgeGesturesEnabled`, `edgeLeftAction`, `edgeRightAction` (e.g., independent Warm Light / Cool Light vertical edge slides in the reader).
- **Tilt Page Turning Sensitivity**:
  - Configurable `tiltSensitivity` (`HalTiltSensor`) alongside `tiltPageTurn`.
- **Side-Button Long-Press & Combo Actions**:
  - `sideButtonLongPress` and `sideButtonComboAction` for button-first shortcuts in addition to CrossPlay/CrossPoint bindings.
- **Preserved CrossInk Power-Button Behavior**:
  - Retains `doubleClickPwrLight` and CrossInk `SHORT_PWRBTN` actions (`PWR_CONFIRM`, `PWR_BOOKMARK`, `PWR_SYNC`, `PWR_READER_MENU`) in `MappedInputManager.cpp` and `main.cpp` rather than CrossPoint `#4407`'s `doublePwrBtn` rewrite, while incorporating `#4494`'s EEGO A4 `400 ms` wake hold, `+400 ms` hold duration, `500 ms` panel idle, and automatic `!Frontlight.present()` double-click suppression, plus `#4512` orientation-aware side buttons in `DictionaryDefinitionActivity`.

---

## 5. Reading Statistics, Streaks, and Multi-Device Sync

Implemented across `BookReadingStats.{h,cpp}`, `GlobalReadingStats.{h,cpp}`, `ReadingSessionStats.{h,cpp}`, `ReadingStatsUtils.{h,cpp}`, `BookStatsActivity.{h,cpp}`, `BookStatsTracking.{h,cpp}`, and `BookStatsView.{h,cpp}`:

- Tracks daily/total reading time, pages turned, speed, and active reading streaks per book and globally.
- Renders dedicated stats dashboards (`BookStatsActivity` / `BookStatsView`) accessible from Library / Home / Reader menus, backed by host tests in `host-tests/stats/`.
- Supports multi-device stats merging/syncing over the web/sync endpoints.

---

## 6. Official Audiobookshelf SD Plugin (`plugins/audiobookshelf/`)

Bundled at `plugins/audiobookshelf/` (`manifest.json`, `plugin.js`, `device.json`, `README.md`) and packaged into `dist/xmarginalia-audiobookshelf-plugin.zip`:

- **Direct ABS Browsing & Search**: Connects directly to an Audiobookshelf server from **Home → Plugins → Audiobookshelf** (Continue Reading, Recently Added, By Title, By Author, Search Books, Search by Author with drill-down) — no external OPDS bridge required.
- **EPUB Downloads & Sidecars**: Saves EPUBs to `/Books/Audiobookshelf/` (configurable) alongside `<book>.epub.meta.json` (`abs_item_id`, metadata).
- **Existing Library Matching**: Web UI tool on `/plugins` (**Sync Existing SD Card Books**) scans EPUBs already on SD, matches them against Audiobookshelf library items, and writes `.meta.json` sidecars without re-downloading.
- **Offline Progress Outbox & Two-Way In-Reader Sync**: Queues progress pushes (`ebookProgress`, `isFinished`) when offline and syncs two-way inside the reader via **Reader Menu → Sync Progress** (`ebookProgress` + `ebookLocation` EPUB CFI), prompting between **Audiobookshelf** and **KOReader Sync** when both are configured. Credentials can be stored device-bound obfuscated (`password_obf`) or via API Token and are never echoed back over HTTP.

---

## 7. Nearby File Transfer (CIFT over ESP-NOW)

Implemented in `src/activities/network/NearbyBookTransferActivity.{h,cpp}` (`docs/` + `host-tests/nearby/`):

- Uses CrossPlay/CrossInk ESP-NOW peer discovery (`CrossInk File Transfer` / CIFT protocol) to send EPUB books and wallpapers directly between readers with no Wi-Fi router or IP setup.
- Exposed in book options menus (**Send to Nearby Device**) in Library and File Browser and under File Transfer (**Receive File**), compatible across **BOOX Picco**, **Xteink X4 Pro**, and **CrossInk** devices.

---

## 8. Spoiler-Free AI Companion (`src/apps_local/companion/`, `server/companion-bridge/`)

See [companion.md](companion.md) and [server/companion-bridge/README.md](../server/companion-bridge/README.md):

- **On-Device Client (`CompanionCore`, `CompanionClient`)**: Adds **Who is this?**, **What is this?**, **Explain** on word/clipping selection and **Where was I?** / **Recap chapter so far** in the Reader menu, rendered in a paginated viewer honoring the user's reader font and line spacing.
- **Spoiler Guarding**: Sends position anchors (`anchor_exact` → `chapter_progress` → `book_progress`) to a self-hosted Python/Docker `companion-bridge` server (`app.py`, `epub_indexer.py`, `retriever.py`, `abs_sync.py`, `llm.py`) backed by an OpenAI-compatible LLM.

---

## 9. Games, Apps, and the 11-Mode Tabletop Suite

XMarginalia carries CrossPlay's **25 standalone games** and **8 productivity/reference apps** (`Chess`, `Battleship`, `Connections`, `Solitaire`, `Hearts`, `D&Diagrams`, `Insider`, `Jaipur`, `Murdle`, `Checkers`, `Connect Four`, `Yahtzee`, `Knucklebones`, `Minesweeper`, `Sudoku`, `Picross`, `Sea Salt`, `Toy Battle`, `Forehead`, `Trivia`, `Wavelength`, `Go`, `Hex`, `Underhand`, `Wordle`; plus `Study`, `Hacker News`, `xkcd`, `Instapaper`, `Wallpapers`, `Wikipedia`, `Calculator`, `Notes`), and adds:

- **Tabletop Suite (`src/apps_local/tabletop/`, [tabletop.md](apps/tabletop.md))**: A paginated e-ink game lobby adding **10 full competitive games** — **Reversi / Othello**, **Mancala / Kalah**, **Nine Men's Morris**, **Onitama**, **Liar's Dice / Perudo**, **Dobutsu Shogi**, **Backgammon**, **Santorini**, **Quoridor**, and **Dominoes** (each with Solo AI, 2-player pass-and-play with 180° flip or `[ CONCEAL ]`/`[ PEEK ]`, and ESP-NOW **PLAY NEARBY** via `LinkPlay.h` IDs `0x0C01`–`0x0C0A`) — plus a **Tabletop Sandbox** (Dice Cup, 52-card Playing Cards, 4-player Life/Point Counters, Freeform 8x8 Token Board).
- **Deliberate Exclusion**: XMarginalia omits CrossPlay's experimental `apps_local/rss/` feed reader to preserve flash headroom and avoid network-parser bloat alongside ABS/OPDS/Companion.

---

## 10. Upstream Fixes Captured from CrossPlay `v1.15.1` (`93faa497`)

XMarginalia selectively ported all high-value upstream reliability and hardware fixes from CrossPlay `v1.15.1` without disturbing the features above:

- **Networking & OPDS (`#4487`, `#4362`, `2c0671e4`, `76dbc1a3`)**:
  - Unified `HttpDownloader` door (`openRequest`, `applyCommonClientSetup` keeping `XMarginalia-ESP32-` UA, `closeRequest`, `AbortPoll`), `www-authenticate` capture + redirect detection, 2-attempt 401 auth retry while preserving `heap_caps_malloc_extmem_enable(64)` for Expat in PSRAM, verified by `host-tests/httpdoor/`.
  - `src/apps_local/ClockFloor.{h,cpp}` build-date clock floor across Wi-Fi/NTP sync and daily/time apps (`Connections`, `Instapaper`, `Live`, `Study`, `Wallpapers`, `Wavelength`, `Wordle`), verified by `host-tests/clockfloor/`.
- **Panel, Refresh, Reader & UI Polish (`#4362`, `#4444`, `#4469`, `#4460`, `#4473`, `#4512`, `#4450`, `#4509`, `#4494`, `#4520`)**:
  - `HalDisplay::controllerIdle()`, `HalDisplay::setReaderRefresh()`, `ReaderRefreshScope` (EPUB + XTC), `GfxRenderer::displayRetainedGrayscaleOverlay()`, `SleepActivity` pre-white clean & rounded-corner MoonIcon placement, 5-second throttling in `CalibreConnectActivity` and `BleSettingsActivity`, Picco absolute grayscale image mode (`0xFF` clear / `displayGrayBuffer` order), and `CrossPointSettings::fadingFixSupported()`.
  - `Section::finalizeBuild()` SD font-cache release & earlier `discardOverlayPage()` (`#4469`); reader bottom margin preservation (`#4444`); `OptionPopup` touch/button viewport scrolling (`#4460`); `PluginCatalogActivity` `<` prev-page chevron (`#4473`); dictionary side-button layout/orientation adherence (`#4512`); **Show File Extensions** toggle (`#4450`); **Hide New Files in Recent** + Settings **Refresh Library** action + `my clippings.txt`/`crash_report.txt` index exclusions (`#4509`); `HomeCoverCache` night-mode polarity invalidation & `HalGPIO::rawInputActive()` digital pin scan (`#4494`); dev-bridge no-SD / USB-boot stay-up (`eed4239e`) & `DevInputCommands.cpp` include fix.
- **Study, Notes, Wikipedia & Simulator (`9e2736f3`, `601de28d`, `4029baed`, `7a73be1e`, `8232b7ec`, `3c0f2816`, `921c8800`, `c8115979`, `a4266971`, `6b6371b7`, `80413952`, `d392673b`)**:
  - Study card paging (`StudyPaging.h`, `StudyText.h`), deck/NTP clock check (`StudyStats.h`), `HeaderLayoutRecord.h` (`BaseTheme::drawHeader` alignment), `.lst` glyph lists (`tools_local/wasm/sdcard/study/mandarin/*.lst` + regenerated `site/study/tools.zip`), Notes `EDIT ON YOUR PHONE`, Wikipedia built-in install face + Toybox/Wikipedia margins, simulator `84c3421da` pin + `CROSSPOINT_SIM_ENV=simulator_picco`, and `header_row_check.py` in `sim-shot.sh`.
