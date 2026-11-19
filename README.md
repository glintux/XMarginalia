<h1 align="center">
  <img src="site/assets/logo.svg#gh-light-mode-only" width="72" alt="XMarginalia"><img src="site/assets/logo-dark.svg#gh-dark-mode-only" width="72" alt="XMarginalia"><br>
  XMarginalia
</h1>

<p align="center">
  <strong>Reading in the margins &mdash; on e-ink.</strong><br>
  A personal, non-commercial reading, Audiobookshelf, spoiler-free AI companion, and tabletop firmware for ESP32-S3 e-readers.
</p>

<p align="center">
  <a href="#why-xmarginalia-exists">Why XMarginalia Exists</a> &middot;
  <a href="#spoiler-free-ai-companion-companion-bridge">AI Companion</a> &middot;
  <a href="#audiobookshelf-sd-plugin">Audiobookshelf</a> &middot;
  <a href="#what-is-on-it">What is on it</a> &middot;
  <a href="#install-it">Install it</a> &middot;
  <a href="https://github.com/glintux/XMarginalia/releases">Releases</a>
</p>

## Why XMarginalia Exists

> **Personal Project Notice:** XMarginalia is a **personal, non-commercial** hobby project built for my own e-ink reading workflow and shared publicly as-is under open-source terms. See [Credit and upstream attribution](#credit-and-upstream-attribution) and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for upstream attributions and third-party license details.

Dedicated e-ink readers such as the **Xteink X4 Pro**, **Onyx BOOX Picco**, **EEGO A4 / A4 Lite**, **Seeed reTerminal Sticky**, and **M5Stack PaperMono / Lite** shine when they keep you inside a book or a thoughtful game without phone notifications or glare. Upstream firmware projects each solved part of that puzzle separately: **CrossPoint Reader** built the fast EPUB/XTC engine and SD plugin host, **CrossInk** refined themes, typography, modular status bars, reading stats, and gesture controls, and **CrossPlay** added a full e-ink game/app shelf and BOOX Picco bring-up.

**XMarginalia** unifies all three codebases into one cohesive firmware and adds the reading-companion infrastructure missing from each upstream:

1. **Spoiler-Free AI Companion (`companion-bridge`)**: Look up *"Who is this?"*, *"What is this?"*, *"Explain"*, *"Where was I?"*, and *"Recap chapter so far"* right from your current page—strictly truncated at or before what you have already read so answers never spoil later chapters.
2. **First-Class Self-Hosted Audiobookshelf Integration**: Browse your Audiobookshelf libraries, download EPUBs with metadata sidecars, link existing SD card books without re-downloading, queue offline progress pushes, and perform two-way EPUB CFI sync straight from the reader menu—with zero intermediate OPDS or external sync daemon required.
3. **Unified Reader Experience Across Three Upstreams**: CrossPoint's EPUB/XTC engine and SD plugin runtime, CrossInk's **Minimal** & **Dashboard** themes, restored **5-slot Bottom Status Bar**, **Focus Reading**, **Controls 2.0**, **Reading Statistics**, and **Nearby CIFT Transfer**, alongside CrossPlay's **27 games and 8 apps** (including the **11-mode Tabletop suite**).
4. **Broad Hardware & Safe Flashing Coverage**: Ships verified release images for **X4 Pro**, **BOOX Picco** (preserving vendor `safeboot` via app-only `0xE0000` writes), and **EEGO A4 / A4 Lite**, backed by all-PSRAM wolfSSL TLS buffers, PSRAM display shadow buffers, hardened OPDS parsing, and zero-rebuild USB tooling.

For a file-by-file breakdown of everything XMarginalia adds or changes relative to CrossPoint, CrossInk, and CrossPlay, see [docs/xmarginalia-differences.md](docs/xmarginalia-differences.md) and [docs/picco.md](docs/picco.md).

---

## Companion Features & Major Additions

### Spoiler-Free AI Companion (`companion-bridge`)

Implemented across [`src/apps_local/companion/`](src/apps_local/companion/), [`ClipSelectionActivity`](src/activities/reader/ClipSelectionActivity.cpp), [`EpubReaderActivity`](src/activities/reader/EpubReaderActivity.cpp), and the self-hosted Python/Docker service in [`server/companion-bridge/`](server/companion-bridge/README.md) (full architecture and deployment guide in [docs/companion.md](docs/companion.md)):

- **Five In-Reader Actions**:
  - From word or passage selection: **Who is this?** (`who_is`), **What is this?** (`what_is`), and **Explain** (`explain`).
  - From the Reader menu: **Where was I?** (`recap_story`) and **Recap chapter so far** (`recap_chapter`).
- **Three-Tier Intra-Chapter Spoiler Gate (`CutoffResolver`)**:
  1. **`anchor_exact`**: Captures up to ~140 characters from the end of your visible page (`extractPageAnchorText`), matches that passage inside the indexed book chunks, and truncates any straddling chunk immediately after that sentence—immune to Calibre HTML file splits or EPUB repacks.
  2. **`chapter_progress`**: Falls back to `chapter_title` / `spine_index` scaled by `chapter_progress` minus a **3%** safety margin.
  3. **`book_progress`**: Falls back to whole-book progress minus a **2%** safety margin across cumulative text codepoints.
- **Multi-Tier Book Matching (`BookResolver`)**:
  - Matches `abs_item_id` (from Audiobookshelf `<book>.meta.json` sidecars), `<dc:identifier>` UUID, ISBN, ASIN, and KOReader MD5 (`ko_md5`), falling back to normalized `title + author` / filename fuzzy matching (`>= 82%` via RapidFuzz) and corpus-wide `anchor_text` search when metadata differs.
- **Book-Focused Retrieval & Entity Masking (`SpoilerFreeRetriever`, `EntityMasker`)**:
  - Character and term lookups combine the **first mention**, **most recent pre-cutoff mentions**, **current-page chunks**, and **hybrid BM25 + dense vector search** (`BAAI/bge-small-en-v1.5` via CPU `fastembed_cpu` by default, or `EMBEDDING_PROVIDER=none` for pure BM25 with zero embedding RAM).
  - Optional proper-noun masking (`COMPANION_ENTITY_MASKING=1`, `[ENTITY_N]` placeholders restored after generation) prevents pre-trained LLM memory bleed on well-known novels.
  - Works with any OpenAI-compatible `/v1/chat/completions` endpoint (LocalAI, llama.cpp, vLLM), with `LOCALAI_CHAT_MODEL=auto` auto-selecting from loaded models in `LOCALAI_MODEL_PREFERENCE` and suppressing `<think>` reasoning chains on `qwen3*` models.
- **Device Memory, TLS & Radio Discipline (`CompanionClient`, `EpubReaderActivity`)**:
  - Before waking Wi-Fi, stops any active BLE page turner and releases rebuildable reader state (`section`, `epub`, `CLIPPINGS`, overlay page, image render cache, and SD font caches) to maximize internal SRAM alongside PSRAM-backed wolfSSL buffers.
  - Wakes saved Wi-Fi on demand, sends `POST /api/ereader/v1/query` with `X-API-Key` and `Authorization: Bearer` headers (optional custom CA trust roots at `/.crosspoint/companion-roots.pem`), tears the radio right back down afterward, reloads cached EPUB state, and renders the reply in a paginated reader view using your active font (`SETTINGS.getReaderFontId()`) and line spacing (`SETTINGS.getReaderLineCompression()`).
- **Quick Configuration**:
  - Place `/.crosspoint/companion.json` on the SD card (or set `companion_host` / `companion_url` and `companion_api_key` in the Audiobookshelf plugin config as a fallback):
    ```json
    {
      "host": "companion.example.com",
      "api_key": "replace-with-a-long-random-secret",
      "enabled": true
    }
    ```
  - For plain HTTP on a trusted private LAN (e.g. `192.168.1.50:8095`), add `"use_http": true`.
  - **Reverse Proxy & Hardening**: Container config (`server/companion-bridge/compose.localai.yaml`) uses `cap_drop: [ALL]`, `no-new-privileges:true`, lazy LRU book parsing (`COMPANION_BOOK_CACHE=3`), and per-IP rate limiting / auth lockouts. A Caddy snippet ([`Caddyfile.snippet`](server/companion-bridge/deploy/Caddyfile.snippet)) lets `/api/ereader/*` bypass interactive SSO (such as Authelia) cleanly when validated by `X-API-Key`. Keep `companion-bridge` on your LAN or behind a private VPN (WireGuard/Tailscale) whenever possible.

---

### Audiobookshelf SD Plugin

Bundled at [`plugins/audiobookshelf/`](plugins/audiobookshelf/README.md) and packaged as `xmarginalia-audiobookshelf-plugin.zip` in every release:

- **Direct Library Browsing & Search**: From **Home -> Plugins -> Audiobookshelf**, browse **Continue Reading**, **Recently Added**, **By Title (A-Z)**, and **By Author (A-Z)**, or run **Search Books** and **Search by Author** (with author-to-books drill-down), view descriptions, and download EPUBs to `/Books/Audiobookshelf/` (configurable).
- **Zero-Duplicate Linking for Existing SD Books**: In the local Web UI (`/plugins`), **Sync Existing SD Card Books** scans EPUBs already stored on your SD card, matches them to Audiobookshelf items, and writes `<book>.epub.meta.json` (`abs_item_id` + metadata) without re-downloading.
- **Offline Outbox & Two-Way CFI Progress Sync**: Closing a linked book queues progress (`ebookProgress`, `isFinished`) offline until the next Wi-Fi session. Inside any linked EPUB, **Reader Menu -> Sync Progress** performs two-way sync using both `ebookProgress` and `ebookLocation` (EPUB CFI), prompting between **Audiobookshelf** and **KOReader Sync** if both are enabled.
- **Secure Credential Storage**: Supports username/password sign-in (device-bound obfuscated on SD as `password_obf`, never returned over HTTP) or direct Audiobookshelf API tokens.

---

### CrossInk UI, Themes, Typography, Controls 2.0 & Stats

XMarginalia brings full CrossInk UI/reader parity and CrossPoint plugin compatibility onto the unified S3 firmware:

- **Themes & Restored 5-Slot Modular Status Bar**: Adds **Minimal** and **Dashboard** UI themes alongside **Lyra**, **Classic**, **Lyra Extended**, and **Rounded Raff**. Customizes independent Top (`topLeft`, `topCenter`, `topRight`) and restored **Bottom 5-slot** (`bottomLeft`, `bottomMidLeft`, `bottomCenter`, `bottomMidRight`, `bottomRight`) lanes plus Top/Bottom progress bar placement and EEGO A4 inset progress rendering.
- **Focus Reading & Per-Book Typography**: Adds `focusReadingEnabled` (bionic prefix emphasis), **Guide Dots**, word/character spacing controls, and per-book overrides (`BookSettings`, `effectiveScreenMargin()`) independent of global reader defaults.
- **OPDS Catalog Browser**: Relocated under **File Transfer / Network Mode Selection** so the Home screen stays focused on reading and your shelf. Enhanced in firmware and `freeink-sdk` with unified `HttpDownloader` connection handling, Basic Auth fallback, `www-authenticate` capture, and 2-attempt 401/redirect retries alongside PSRAM Expat parsing.
- **Controls 2.0 (Touch, Edge Slides, Tilt & Hardware Recovery)**: Adds the `touchReaderControls` master toggle, configurable forward/previous gesture filters (`pageTurnGesture`, `previousPageGesture`), **Edge Gestures** (`edgeGesturesEnabled`, `edgeLeftAction`, `edgeRightAction` for Warm/Cool frontlight slides), **Tilt Sensitivity** (`tiltSensitivity`), side-button long-press & combo bindings, Picco **Up + Down** hardware recovery outside the reader to re-enable touch, CrossInk `doubleClickPwrLight` & `SHORT_PWRBTN` mappings (`PWR_CONFIRM`, `PWR_BOOKMARK`, `PWR_SYNC`, `PWR_READER_MENU`), `!Frontlight.present()` double-click suppression, and orientation-aware dictionary side buttons.
- **Reading Statistics & Multi-Device Sync**: Tracks daily/total reading time, pages turned, reading speed, and active reading streaks per book and globally (`BookStatsActivity`), with multi-device stats sync support.

---

### Nearby Book & Wallpaper Transfer (CIFT over ESP-NOW)

In addition to **PLAY NEARBY** multiplayer games, readers can exchange books and wallpapers directly between devices with no router, pairing screen, or IP setup:

- Choose **Send to Nearby Device** from the book options menu in the **Library** or **File Browser**, and **Receive File** under **File Transfer** on the receiving reader.
- Compatible across **Onyx BOOX Picco**, **Xteink X4 Pro**, **EEGO A4**, and **CrossInk** devices.

---

### Hardware Profiles, Panel Safety & `freeink-sdk`

- **Onyx BOOX Picco (`picco` / `gh_release_picco`)**: Native 480×800 portrait layout, TMA525C touch, Up/Down side keys, warm/cool frontlight, hardware sleep toggle switch, `FREEINK_FB_PSRAM=1` framebuffers, absolute grayscale clearing order, and strict `0xE0000` app-only upload/build guards (`scripts/app_only_upload.py` + `scripts_local/app_slot_guard.py`) that keep the factory bootloader, NVS, and `safeboot` recovery partition (`0x20000`) intact. See [docs/picco.md](docs/picco.md).
- **EEGO Reader A4 / A4 Lite (`eego_a4` / `gh_release_eego_a4`)**: 768×552 UC8279C panel + GSLX680 touch with `500 ms` controller idle, `400 ms` wake hold + `+400 ms` hold duration, slim inset bottom progress bar respecting rounded glass corners, automatic `HALF_REFRESH` cleanup when leaving the reader, and factory MAC UID readout (`HalSystem::getEegoUid()`) in **About**.
- **`freeink-sdk` (`glintux/freeink-sdk`, branch `xmarginalia/sdk-20261117`)**: Moves wolfSSL allocations (`ensureWolfAllocators()`) and display `_asyncShadow` buffers into external PSRAM across **all** `BOARD_HAS_PSRAM` boards (freeing ~40–60 KB of internal DRAM for Wi-Fi DMA RX/TX buffers during simultaneous TLS sessions) and adds OPDS feed/auth/error-logging fixes.
- **Upstream Reliability Sync**: Captures CrossPlay `v1.15.1` panel/reader/app fixes (`#4362` retained grayscale overlay & reader refresh scopes, `#4469` SD font-cache release, `#4444` bottom margin, `#4460` `OptionPopup` scroll, `#4473` plugin `<` chevron, `#4450` Show File Extensions, `#4509` Hide New Files in Recent & Settings Refresh Library, `ClockFloor` build-date floor, `HttpDownloader` unified door, Study paging/glyph lists, Notes edit label, Wikipedia install margins).

---

## What is on it

**XMarginalia** preserves upstream CrossPlay's **27 games and 8 apps** shelf alongside **Tabletop**'s 10 competitive games and 4 sandbox tools (experimental `apps_local/rss/` is intentionally omitted to preserve flash headroom):

### Games

|                  |                                                                              |
| ---------------- | ---------------------------------------------------------------------------- |
| **Chess**        | A full engine on the device, or a board between two of them.                 |
| **Battleship**   | Lay out a fleet, then hunt someone else's.                                   |
| **Connections**  | The daily word grid, with an archive of past boards.                         |
| **Solitaire**    | Klondike, turned sideways because that is the shape of a tableau.            |
| **Hearts**       | Duck the tricks. Every heart costs one and the queen of spades costs 13.     |
| **D&Diagrams**   | A nonogram whose clues are a dungeon. 64 of them.                            |
| **Insider**      | A party game for a table and one device.                                     |
| **Jaipur**       | The two-player trading game, solo or nearby.                                 |
| **Murdle**       | A logic grid built through the solver, so you never have to guess.           |
| **Checkers**     | English draughts, where taking is compulsory and the board says so.          |
| **Connect Four** | Drop a disc, get four in a line. Seven columns, one tap each.                |
| **Yahtzee**      | Thirteen boxes, three rolls a turn, and the Joker rules in full.             |
| **Knucklebones** | Cult of the Lamb's dice game. Matching dice multiply; yours destroy theirs.  |
| **Minesweeper**  | Tap to dig, hold to flag, tap a finished number to chord its neighbours.     |
| **Sudoku**       | Generated on the device and graded by the technique it needs, not the clues. |
| **Picross**      | Fill the grid from the clues and a picture appears. Wrong fills lock in.     |
| **Sea Salt**     | Sea Salt & Paper: collect duos, bet on STOP or LAST CHANCE.                  |
| **Toy Battle**   | Nine boards of bases and paths. Hold regions, take medals, solo or nearby.   |
| **Forehead**     | Screen against your forehead, the room shouts clues, sixty seconds.          |
| **Trivia**       | 50,000 questions off 42 years of Jeopardy. Read them out, or play alone.     |
| **Wavelength**   | A hidden point on a spectrum, one clue, and the whole table arguing.         |
| **Go**           | 9x9, 13x13 or 19x19, against the device or someone next to you. It counts.   |
| **Hex**          | Join your two edges before they join theirs. No captures, and no draws.      |
| **Underhand**    | The 2017 cult card game: bribe, sacrifice, dodge the police, summon a god.   |
| **Wordle**       | Six guesses for the day's word, with every past day in an archive.           |
| **Tabletop**     | 10 classic board, tile & dice games plus a virtual sandbox, solo/2P/nearby.  |
| **EinkBoy**      | Game Boy & Game Boy Color emulator for `.gb`/`.gbc` ROMs on your SD card.    |

### Tabletop Suite

**Tabletop** ([`src/apps_local/tabletop/`](src/apps_local/tabletop/), full rules in [docs/apps/tabletop.md](docs/apps/tabletop.md)) packs 10 competitive games and a 4-tool virtual sandbox into a paginated e-ink lobby:

- **Competitive Games** (Solo AI, 2P Pass-and-Play with 180° table flip on board games, and **PLAY NEARBY** ESP-NOW multiplayer): **Reversi / Othello**, **Mancala / Kalah**, **Nine Men's Morris**, **Onitama**, **Liar's Dice / Perudo**, **Dobutsu Shogi / Catch the Lion**, **Backgammon**, **Santorini**, **Quoridor**, and **Dominoes** (with `[ CONCEAL ]` / `[ PEEK ]` hand privacy).
- **Tabletop Sandbox**: **Dice Cup** (1–6 dice, D4–D20, hold/lock), **Playing Cards** (52-card deck with secret hand privacy), **Life & Point Counters** (4-player), and a **Freeform 8×8 Token Board**.

### Apps

|                 |                                                                          |
| --------------- | ------------------------------------------------------------------------ |
| **Study**       | Anki decks with the FSRS scheduler, offline.                             |
| **Hacker News** | The front page in a reading serif, articles kept on the card.            |
| **xkcd**        | The archive, packed for the card and drawn one to one.                   |
| **Instapaper**  | Your read-later queue, synced both ways: reading position and archiving. |
| **Wallpapers**  | Pick an image on the card as the sleep screen, one tap to set it.        |
| **Wikipedia**   | Fifty thousand articles on the card, read like a book, no internet.      |
| **Calculator**  | A calculator with keys the size of a thumb, and the sums you already did. |
| **Notes**       | Lists you tick with one hand, kept as plain text files on the card.      |

Don't want all of them? Tap **GAMES** or **APPS** at the top of the list on your device and untick what you don't play.

### Two devices, nothing to type

Twelve of the games play over **PLAY NEARBY**: Chess, Checkers, Connect Four, Yahtzee, Knucklebones, Battleship, Jaipur, Sea Salt, Toy Battle, Go, Hex and Tabletop. Across those 12 shelf entries, 21 total titles play over **PLAY NEARBY** (including Tabletop Reversi, Tabletop Mancala, Tabletop Morris, Tabletop Onitama, Tabletop Liars Dice, Tabletop Dobutsu, Tabletop Backgammon, Tabletop Santorini, Tabletop Quoridor, and Tabletop Dominoes). Put two devices next to each other and they find one another. No pairing screen, no room code, no account, no router, no internet.

---

### Design & Architecture Notes

- [docs/xmarginalia-differences.md](docs/xmarginalia-differences.md) -- complete breakdown of every XMarginalia feature, hardware target, SDK change, and upstream fix.
- [docs/companion.md](docs/companion.md) -- spoiler-free AI Companion architecture, Docker Compose / Caddy setup, and security model.
- [plugins/audiobookshelf/README.md](plugins/audiobookshelf/README.md) -- Audiobookshelf SD plugin installation and usage.
- [docs/identity.md](docs/identity.md) -- why a screen that holds still is the point rather than a limitation.
- [docs/design-language.md](docs/design-language.md) -- typography and ink budget on a 1-bit panel.
- [docs/buttons.md](docs/buttons.md) -- how the two physical side buttons are used.
- [docs/games-at-scale.md](docs/games-at-scale.md) -- how games are built and reviewed.
- [docs/apps/](docs/apps/) -- per-game rules, state machines, and design notes.

---

## Install it

> **The X4 Pro is not the X4.** XMarginalia targets ESP32-S3 readers: **Xteink X4 Pro**, **Onyx BOOX Picco**, **EEGO A4 / A4 Lite**, **Seeed reTerminal Sticky**, and **M5Stack PaperMono / Lite**. The plain **X4** and the **X3** are ESP32-C3, and writing an S3 image to one of those used to brick it—install [CrossPoint](https://crosspointreader.com/) on those instead. Between supported S3 boards, every image carries its board tag and the updaters reject mismatched hardware.

Download release binaries and the Audiobookshelf plugin from the [XMarginalia Releases](https://github.com/glintux/XMarginalia/releases) page:

- **Xteink X4 Pro**:
  - First USB install: `xmarginalia-<version>-x4pro-full.bin` at `0x0`
  - Existing CrossPoint / CrossPlay / XMarginalia OTA or SD update: `firmware.bin`
  ```bash
  esptool.py --chip esp32s3 --baud 921600 write_flash 0x0 xmarginalia-<version>-x4pro-full.bin
  ```
- **EEGO A4 / A4 Lite**:
  - First USB install: `xmarginalia-<version>-eego_a4-full.bin` at `0x0`
  - OTA or SD update: `firmware-eego_a4.bin`
  ```bash
  esptool.py --chip esp32s3 --baud 921600 write_flash 0x0 xmarginalia-<version>-eego_a4-full.bin
  ```
- **Audiobookshelf SD Plugin**:
  - Extract `xmarginalia-audiobookshelf-plugin.zip` (`audiobookshelf/`) to `/plugins/` or `/.crosspoint/plugins/` on your SD card and enable **Settings -> Connectivity -> Plugins**.

You do not need to have installed CrossPoint first. Flashing replaces the firmware, not the SD card: your library, reading positions, and fonts are left alone, and installing stock CrossPoint over the top restores stock reading behavior.

[**docs/install.md**](docs/install.md) covers updating over Wi-Fi/SD, a **USB-locked X4 Pro** (via the Xteink Unlocker to CrossPoint, then XMarginalia's `firmware.bin` from SD), and Developer Mode (wireless flashing with no cable). If a flash goes wrong, [docs/fix-bricked-xteink.md](docs/fix-bricked-xteink.md) explains recovery. Once running, [USER_GUIDE.md](USER_GUIDE.md) covers device controls, the reader, and the local web portal.

### BOOX Picco

> [!WARNING]
> **Experimental / Use at Your Own Risk**: Support for the BOOX Picco is **for experimental purposes only** and has received **limited production testing**. While verified on physical hardware, flash at your own risk and always keep a full flash backup (`esptool.py --chip esp32s3 read_flash 0 0x1000000 picco-stock.bin`).

> [!NOTE]
> **PSRAM Display & TLS Buffers**: Display framebuffers (`FREEINK_FB_PSRAM=1`) and wolfSSL TLS allocations live in external PSRAM, keeping internal DRAM free for Wi-Fi DMA packet buffers during TLS requests.

The **BOOX Picco** (BOOX Tiles) is an ESP32-S3 e-reader featuring a 480×800 portrait e-ink panel, capacitive touch, physical Up/Down side buttons, and a hardware toggle switch. Unlike the X4 Pro or EEGO A4, the Picco retains its vendor partition table including `safeboot` recovery. XMarginalia installs **strictly into the single application slot at `0xE0000`**—never touching the vendor bootloader (`0x0`), NVS (`0x9000`), or `safeboot` (`0x20000`). **Never flash a `-full.bin` or write to `0x0` on a BOOX Picco.**

#### Installing Pre-Built Picco Firmware

1. Download **`firmware-picco.bin`** from the [XMarginalia Releases](https://github.com/glintux/XMarginalia/releases) page.
2. Put the BOOX Picco into ROM bootloader mode: power off completely, then hold **both Power and Volume Up (+)** while plugging in USB-C (~5–8 seconds, then release).
3. Flash only the application partition at `0xE0000`:
   ```bash
   esptool --chip esp32s3 -p /dev/ttyACM0 -b 921600 write-flash 0xE0000 firmware-picco.bin
   ```

#### Flashing from a Local Clone (No Rebuild)

```bash
./scripts_local/flash-device.sh         # auto-detects X4 Pro, BOOX Picco, or EEGO A4 via 0x8000 partition table
./scripts_local/flash-device.sh picco   # explicitly enforces 0xE0000 app-only write for Picco
```

#### Controls on the BOOX Picco

- **Side Buttons**: Up / Down turn pages and scroll lists; pressing **Up + Down** outside the reader recovers touch if disabled.
- **Toggle Switch**: Physical switch toggles sleep mode.
- **Touch Screen**: Tap left/right third to turn pages, top third for toolbar/frontlight/settings, swipe inward from the left edge to go **Back**, or configure edge slides under Reader Settings.

Please report hardware or device-specific issues on [GitHub Issues](https://github.com/glintux/XMarginalia/issues).

---

## What it does over the network

Most of XMarginalia stays completely offline. Of the features that use Wi-Fi or ESP-NOW:

- **Spoiler-Free AI Companion (`companion-bridge`)**: Off until `/.crosspoint/companion.json` (or plugin fallback config) is present. When triggered inside the reader, it sends book identifiers, your reading position, and visible-page/selected text to your own self-hosted `companion-bridge` server. Prefer running `companion-bridge` on your private LAN or behind a private VPN (WireGuard/Tailscale); if proxied externally, enforce HTTPS and a strong `X-API-Key` ([docs/companion.md](docs/companion.md)).
- **Audiobookshelf Plugin**: Off unless enabled under **Settings -> Connectivity -> Plugins** and configured with your server URL and credentials. Connects directly to your configured Audiobookshelf server for browsing, EPUB download, and CFI progress sync.
- **OPDS Browser**: Connects only to user-configured OPDS catalogs under **File Transfer**.
- **Reading Position Sync (KOSync)**: Uses CrossPoint's default sync endpoint unless pointed at your own KOSync server in Settings, preserving compatibility for users upgrading from stock CrossPoint.
- **Connections, Wordle, xkcd, Hacker News, Trivia, and Instapaper**: Fetch content only when you ask them to. Connections and Wordle fetch published puzzle archives from community GitHub mirrors (plus NYT for the latest unmirrored Wordle day or two); Trivia and xkcd download packs onto the SD card; Hacker News fetches lists from the public [Algolia API](https://hn.algolia.com/api) and proxies opened articles through [r.jina.ai](https://r.jina.ai).
- **Developer Mode**: Off by default; while enabled, the panel indicates that deep sleep is disabled.

---

## Build it

```bash
git submodule update --init --recursive   # freeink-sdk (glintux/freeink-sdk)
pio run -e x4pro                          # X4 Pro development build
pio run -e gh_release_picco               # BOOX Picco release image
pio run -e gh_release_eego_a4             # EEGO A4 release image
pio run -e simulator_x4_pro               # X4 Pro desktop simulator (SDL2 + FreeRTOS shim)
pio run -e simulator_picco                # BOOX Picco desktop simulator (480x800 portrait)
./scripts_local/build-release.sh          # build x4pro, picco, eego_a4 + Audiobookshelf plugin zip
./scripts_local/flash-device.sh           # auto-detect connected reader & flash safely
./scripts_local/check.sh                  # host tests and builds
python3 -m pytest -q server/companion-bridge/tests  # AI companion server unit tests
```

Always pass `-e <env>` to `pio run`: bare `pio run` targets upstream `[env:default]` (ESP32-C3). See [docs/contributing/](docs/contributing/) for toolchain setup, branches, and testing workflows, [LOCAL_SCOPE.md](LOCAL_SCOPE.md) for local ownership boundaries, [docs/shelf.md](docs/shelf.md) for the shelf contract, [docs/building-apps.md](docs/building-apps.md) for app guidelines, and [site/README.md](site/README.md) for local site assets.

---

## Credit and upstream attribution

**XMarginalia** is a personal, non-commercial open-source fork licensed under MIT ([LICENSE](LICENSE)) and built on the work of the open-source e-ink community. Upstream projects are credited here for attribution and reference:

- [**CrossPoint Reader**](https://github.com/crosspoint-reader/crosspoint-reader) — Upstream foundational firmware providing the EPUB/XTC reading engine, SD plugin system, sync engine, web portal, and HAL architecture. CrossPoint's original README is preserved at [docs/crosspoint-readme.md](docs/crosspoint-readme.md).
- [**CrossInk**](https://github.com/uxjulia/CrossInk) — Upstream fork introducing the Minimal & Dashboard UI themes, customizable status bar, Focus Reading & Phase 4 typography, Reading Statistics, Controls 2.0, and Nearby CIFT transfer.
- [**CrossPlay**](https://github.com/ma-r-s/crossplay) ([browser simulator](https://crossplay.ma-r-s.com)) — Upstream fork providing the `src/apps_local/` 27-game / 8-app shelf, Study, Hacker News, xkcd, Instapaper, Wallpapers, Wikipedia, Calculator, Notes, Play Nearby ESP-NOW multiplayer, host-test & simulator framework, and BOOX Picco / EEGO A4 upstream hardware & `v1.15.1` reliability fixes.
- [**FreeInk SDK**](https://freeink.org/) — Low-level ESP32 e-paper HAL/SDK underlying these firmwares (extended in XMarginalia's [`glintux/freeink-sdk`](https://github.com/glintux/freeink-sdk) fork for all-PSRAM wolfSSL/display buffers and OPDS improvements).

### Licensing & personal-use disclosures

- **Permissive project & addition licenses**: XMarginalia's additions—including `companion-bridge` (`FastAPI` MIT, `Uvicorn` BSD-3, `RapidFuzz` MIT, `FastEmbed` Apache-2.0, `BAAI/bge-small-en-v1.5` MIT), the Audiobookshelf SD plugin, Tabletop extensions, and core firmware dependencies (`ArduinoJson`, `SdFat`, `QRCode`, `expat`, `miniz`, `uzlib`, `Lucide` ISC, `decNumber` ICU, `Peanut-GB` / `Walnut-CGB` MIT)—are permissively licensed.
- **Copyleft firmware linkage**: Compiled ESP32 firmware binaries link **Arduino-wolfSSL** (`GPLv2+`, compatible with Apache-2.0 dependencies such as `NimBLE-Arduino`, `JPEGDEC`, `PNGdec`, and `AnimatedGIF` under `GPLv3`), **WebSockets** (`LGPLv2.1`), and **FreeType** (`FTL` / `GPLv2`). Complete corresponding source code is published in this repository and `glintux/freeink-sdk`.
- **Inherited non-commercial / open-item content**: **xkcd** (`CC BY-NC 2.5`) is non-commercial with attribution; **Trivia** uses a community Jeopardy! transcription dataset; **Connections** and **Wordle** fetch puzzles from external mirrors only when requested; and the inherited upstream browser-demo SD card under `tools_local/wasm/sdcard/` still carries a rasterized **KaiTi** font flagged in [docs/open-items.md](docs/open-items.md) (not compiled into device firmware binaries).

### Third-party content & library notices

xkcd comics are by Randall Munroe, [CC BY-NC 2.5](https://xkcd.com/license.html), fetched by the device from [xkcd.com](https://xkcd.com). Connections puzzles are the New York Times'; XMarginalia ships none of them and fetches the archive, when you ask it to, from [Eyefyre/NYT-Connections-Answers](https://github.com/Eyefyre/NYT-Connections-Answers). Wordle answers and accepted guesses are the New York Times' too; XMarginalia ships none of them and fetches them, when asked, from [mfilej/wrdl](https://github.com/mfilej/wrdl) and, for the newest days, the Times' own daily answer. Trivia's questions are built from the community [Jeopardy! clue dataset](https://github.com/jwolle1/jeopardy_clue_dataset). Underhand's cards are the text of Underhand (Spoopy Squad, Cornell GDIAC, 2017), used with its authors' permission; none of its art or sound is included. Type is Jersey 25 and Instrument Serif, both SIL OFL. Calculator arithmetic uses IBM's [decNumber](https://speleotrove.com/decimal/decnumber.html) under the ICU License (vendored at `lib/decNumber`).

Where a game on the shelf carries the name of a published game, that name is its owner's trademark and is used to describe what the game is; game mechanics are not copyrightable. Sea Salt & Paper was designed by Bruno Cathala and Theo Riviere and is published by Bombyx. Toy Battle was designed by Paolo Mori and Alessandro Zucchini and is published by Repos Production. XMarginalia (via the upstream CrossPlay shelf) implements these games; it is not affiliated with, endorsed by, or sponsored by any of their owners.

See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for full notices and [SECURITY.md](SECURITY.md) to report a security issue.
