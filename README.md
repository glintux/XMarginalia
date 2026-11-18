<h1 align="center">
  <img src="site/assets/logo.svg#gh-light-mode-only" width="72" alt="XMarginalia"><img src="site/assets/logo-dark.svg#gh-dark-mode-only" width="72" alt="XMarginalia"><br>
  XMarginalia
</h1>

<p align="center">
  <strong>E-ink is good at waiting.</strong><br>
  So is a chess position, a flashcard, a puzzle you are halfway through.
</p>

<p align="center">
  <a href="https://crossplay.ma-r-s.com">Try it in your browser</a> &middot;
  <a href="#what-is-on-it">What is on it</a> &middot;
  <a href="#install-it">Install it</a> &middot;
  <a href="https://github.com/ma-r-s/crossplay/releases">Releases</a>
</p>

<p align="center">
  <a href="https://github.com/ma-r-s/crossplay/actions/workflows/crossplay-ci.yml"><img src="https://github.com/ma-r-s/crossplay/actions/workflows/crossplay-ci.yml/badge.svg?branch=xteink" alt="Build status"></a>
</p>

![CrossPlay on the Xteink X4 Pro](site/assets/shots/og.png)

The **Xteink X4 Pro**, **Seeed reTerminal Sticky**, **M5Stack PaperMono / Lite**, **Onyx BOOX Picco**, and **EEGO A4** are e-ink
devices with an 800x480 panel (480x800 portrait on Picco), capacitive touch and physical buttons
(three on the Sticky; the design uses the two shared buttons), and
[CrossPoint](https://crosspointreader.com/) already makes them good at reading.
**XMarginalia** (CrossMarginalia) is a new e-reader and tabletop firmware product built as a fork of
**CrossPoint**, **CrossInk**, and **CrossPlay**. It unifies CrossPoint's EPUB engine & SD plugin system, CrossInk's UI themes,
status bar customization (including restored **Bottom Mid-Left / Mid-Right** clock slots), reading stats, and typography controls, CrossPlay's **27 games and 8 apps**,
the **11-mode Tabletop suite**, **BOOX Picco** & **EEGO A4** hardware support, **Nearby File Transfer**, an official **Audiobookshelf SD Plugin** (`plugins/audiobookshelf/`), and an optional spoiler-free **AI Companion**.

## Try it without a device

[**crossplay.ma-r-s.com**](https://crossplay.ma-r-s.com) runs the real firmware
in the browser: the same `src/` and `lib/` the device build compiles, put
through `em++` instead of `g++`, against an SD card of its own. Click the screen
in the hero, or ask for two devices and watch them find each other.

It is the real thing rather than a video, with three things faked, all listed
in [site/README.md](site/README.md): the network answers from a snapshot,
Study's headword font is a smaller cut standing in for the large one, and sleep
is off. The snapshot is also what put Connections in the browser at all: the
build answers its archive fetch from a 40-puzzle slice of the real thing, so
the import lands a real pack instead of failing on an unreachable host.

## What is on it

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
| **Picross**     | Fill the grid from the clues and a picture appears. Wrong fills lock in.     |
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

**Tabletop** (`src/apps_local/tabletop/`, full rules in [docs/apps/tabletop.md](docs/apps/tabletop.md)) packs 10 competitive games and a 4-tool virtual sandbox into a paginated e-ink lobby:

- **Competitive Games** (Solo AI, 2P Pass-and-Play with 180° table flip on board games, and **PLAY NEARBY** ESP-NOW multiplayer): **Reversi / Othello**, **Mancala / Kalah**, **Nine Men's Morris**, **Onitama**, **Liar's Dice / Perudo**, **Dobutsu Shogi / Catch the Lion**, **Backgammon**, **Santorini**, **Quoridor**, and **Dominoes** (with `[ CONCEAL ]` / `[ PEEK ]` hand privacy).
- **Tabletop Sandbox**: **Dice Cup** (1–6 dice, D4–D20, hold/lock), **Playing Cards** (52-card deck with secret hand privacy), **Life & Point Counters** (4-player), and a **Freeform 8×8 Token Board**.

### Reader, CrossInk Parity & BOOX Picco

XMarginalia brings CrossPlay up to parity with **CrossPoint** and **CrossInk** while adding **BOOX Picco** compatibility:

- **CrossInk UI & Reader**: CrossInk UI themes, customizable status bar items (including Left / Mid-Left / Center / Mid-Right / Right bottom lanes), OPDS catalog browser under **File Transfer**, Phase 4 typography/layout and guide dots, tilt sensitivity setting, reading statistics and streaks with multi-device sync, and a unified controls hierarchy.
- **Direct Audiobookshelf SD Plugin (`plugins/audiobookshelf/`)**: Browse libraries, search books, download EPUBs with `.meta.json` sidecars, queue offline progress pushes, and run two-way progress sync directly against Audiobookshelf from **Home → Plugins** and **Reader Menu → Sync Progress** without an external OPDS or KOSync server.
- **Nearby File Transfer (CIFT)**: Send and receive EPUBs and wallpapers directly over ESP-NOW between **Onyx BOOX Picco**, **Xteink X4 Pro**, and **CrossInk** devices (`Send to Nearby Device` / `Receive File`).
- **BOOX Picco & EEGO A4 Support**: Native 480×800 portrait layout, hardware toggle switch sleep control, PSRAM display and all-PSRAM wolfSSL TLS buffers (`freeink-sdk`), app-only `0xE0000` flashing (Picco), and **EEGO A4 / A4 Lite** (`eego_a4` / `gh_release_eego_a4`) builds.

For a full itemized breakdown of everything in XMarginalia that differs from upstream CrossPoint, CrossInk, and CrossPlay, see [docs/xmarginalia-differences.md](docs/xmarginalia-differences.md) and [docs/picco.md](docs/picco.md).

### Spoiler-Free AI Companion

While reading an EPUB, you can select a word/passage for **Who is this?**, **What is this?**, or **Explain**, or open the reader menu for **Where was I?** and **Recap chapter so far**. Queries are strictly gated to text at or before your current page (`anchor_exact` $\rightarrow$ `chapter_progress` $\rightarrow$ `book_progress`), displayed in a paginated viewer using your reader font and line-spacing settings, and backed by the self-hosted [`companion-bridge`](server/companion-bridge/README.md) server and an OpenAI-compatible LLM (such as LocalAI). See [docs/companion.md](docs/companion.md) for how it works, Docker/Caddy setup instructions, and security guidance.

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

Don't want all of them? Tap GAMES or APPS at the top of the list on your device and
untick what you don't play.

And the reader is still CrossPoint's reader: the EPUB engine, sync and the file
browser are theirs and stay theirs.

### Two devices, nothing to type

Twelve of the games play over **PLAY NEARBY**: Chess, Checkers, Connect Four,
Yahtzee, Knucklebones, Battleship, Jaipur, Sea Salt, Toy Battle, Go, Hex and
Tabletop. Across those 12 shelf entries, 21 total titles play over **PLAY NEARBY**
(including Tabletop Reversi, Tabletop Mancala, Tabletop Morris,
Tabletop Onitama, Tabletop Liars Dice, Tabletop Dobutsu, Tabletop Backgammon,
Tabletop Santorini, Tabletop Quoridor, and Tabletop Dominoes). Put two devices
next to each other and they find one another. No pairing screen, no room code,
no account, no router, no internet.

Readers can also exchange books and artwork directly: select **Send to Nearby Device** from the book options menu in the Library or File Browser, and choose **Receive File** under File Transfer on the other reader. Compatible across Onyx BOOX Picco, Xteink X4 Pro, and CrossInk devices.

### Why it is shaped like this

The decisions are written down rather than remembered. Each of these is the
short version of one of them:

- [docs/identity.md](docs/identity.md) -- why a screen that holds still is the
  point rather than a limitation. The rest is downstream of it.
- [docs/design-language.md](docs/design-language.md) -- what that means on a
  1-bit panel, and the ink budget that governs it.
- [docs/buttons.md](docs/buttons.md) -- how the two physical buttons are used,
  and why there are only two.
- [docs/games-at-scale.md](docs/games-at-scale.md) -- how a game gets built, and
  the critic agents that tear it apart before it ships.
- [docs/apps/](docs/apps/) -- per-game rules, state machines and the decisions
  behind each one.

## Install it

> **The X4 Pro is not the X4.** CrossPlay is for the Xteink **X4 Pro** and the
> Seeed reTerminal Sticky and M5Stack PaperMono / Lite, all ESP32-S3. The plain **X4** and the **X3** are
> ESP32-C3, and writing an S3 image to one of those used to brick it. Install
> [CrossPoint](https://crosspointreader.com/) on those instead: it is excellent,
> and it is what this is built on.
>
> **You do not have to work out which you have.** The browser installer below
> reads the chip off the device before it writes anything, and stops with
> "Nothing was written" if it is a C3. Between the supported S3 devices every image
> carries its board name and both updaters refuse an image built for the other
> board.

Open [**crossplay.ma-r-s.com/#get**](https://crossplay.ma-r-s.com/#get) in
Chrome or Edge on a computer, plug the device in, wake it, and press **Install**.
The page downloads the current release and writes it over USB itself; there is
nothing to install first and no command to type. Safari and Firefox have no Web
Serial, and no phone or tablet does either, so the button says so rather than
failing when pressed.

You do not need to have installed CrossPoint first. Flashing replaces the
firmware, not the SD card: your library, your reading positions and your fonts
are left alone, and installing stock CrossPoint over the top puts the device
back where it was.

[**docs/install.md**](docs/install.md) covers the rest: installing by hand with
esptool, updating a device you already flashed, a **USB-locked X4 Pro** (the
Xteink Unlocker to CrossPoint, then our `firmware.bin` from the SD card), and
Developer Mode, which reflashes over Wi-Fi with no cable. If a flash goes wrong,
[docs/fix-bricked-xteink.md](docs/fix-bricked-xteink.md) is the way back.

Once it is running, [USER_GUIDE.md](USER_GUIDE.md) is the guide to the device
itself: the controls, the reader, the web server and what to do when something
goes wrong. It is upstream's guide with this fork's hardware folded in, so it
covers several boards at once: where it names Left or Right, that is the older
X4, which had a front button row the X4 Pro dropped. Back here is a swipe and
Confirm exists only on the Sticky, which
[docs/buttons.md](docs/buttons.md) explains.

### BOOX Picco

> [!WARNING]
> **Experimental / Use at Your Own Risk**: Support for the BOOX Picco in CrossPlay is **for experimental purposes only**. It was developed with AI assistance / generation and has received **limited production testing**. While tested and confirmed working on physical hardware, flash at your own risk and always keep a full flash backup.

> [!NOTE]
> **PSRAM Display Buffers**: Display framebuffers are allocated in external PSRAM (`FREEINK_FB_PSRAM=1`). This keeps internal DRAM free for Wi-Fi MAC DMA packet buffers during TLS downloads (Wordle, XKCD, Hacker News).

The **BOOX Picco** (BOOX Tiles) is an ESP32-S3 e-reader featuring a 480×800 portrait e-ink panel, capacitive touch, physical Up/Down side buttons, and a hardware toggle switch. Unlike other devices, the Picco retains its factory partition table, which includes a vendor `safeboot` recovery partition. CrossPlay completely respects that layout and installs **strictly into the single application partition at `0xE0000`**, never touching the vendor bootloader (`0x0`), NVS (`0x9000`), or `safeboot` recovery (`0x20000`).

#### Installing Pre-Built Firmware

1. Download **`crossplay-boox-picco-firmware.bin`** from the [Releases](https://github.com/glintux/crossplay/releases) page.
2. Put the BOOX Picco into ROM bootloader mode:
   - Power off the device completely.
   - Press and hold **both the Power button and Volume Up (+)** side button simultaneously while connecting the USB-C cable (hold for ~5–8 seconds, then release).
3. Flash the single application slot using `esptool`:
   ```bash
   esptool --chip esp32s3 -p /dev/ttyACM0 -b 921600 write-flash 0xE0000 crossplay-boox-picco-firmware.bin
   ```
   *(On Windows, replace `/dev/ttyACM0` with your COM port, e.g., `COM3`. On older esptool installations, `esptool.py` and `write_flash` syntax also work).*

#### Building and Flashing from Source

```bash
# Build the release image
pio run -e gh_release_picco

# Flash safely to hardware (the custom uploader guarantees only offset 0xE0000 is written)
pio run -e gh_release_picco -t upload
```

#### Running the Desktop Simulator

You can test the full Picco experience (native 480×800 portrait layout, apps, and hardware toggle switch) on your computer without a device:

```bash
pio run -e simulator_picco -t run
```

#### Controls on the BOOX Picco

- **Side Buttons**: Physical Up / Down keys navigate pages in the EPUB reader and scroll lists.
- **Toggle Switch**: Physical switch toggles sleep mode.
- **Touch Screen**:
  - Tap left or right third of the screen to turn pages forward/backward.
  - Tap top third of the screen to reveal the toolbar, library menu, frontlight sliders, and reader settings.
  - Swipe from the left edge inwards to go **Back** to the previous screen.
  - Tap games and apps on the Home shelf to launch them.

#### Restoring Stock / Safe Recovery

Before your first flash, it is recommended to back up your full flash:
```bash
esptool.py --chip esp32s3 read_flash 0 0x1000000 picco-stock.bin
```
Because CrossPlay never overwrites `0x20000` (`safeboot`), the hardware recovery partition is always preserved. Installing BOOX's official firmware or CrossPoint via their web flasher restores the stock state at any time.

Every release is flashed to real hardware (X4 Pro, Sticky, and BOOX Picco) before it ships. BOOX Picco support remains experimental with limited production testing, so if you install it, please [say what happened](https://github.com/glintux/crossplay/issues), either way.

## What it does over the network

Most of the shelf never touches the network. Of the parts that do:

- **Reading positions sync to CrossPoint's own server by default.** That is
  upstream's infrastructure rather than this fork's, inherited so that flashing
  CrossPlay over CrossPoint does not orphan an existing sync. The address is a
  setting and can be pointed at any KOSync server.
- **Connections, Wordle, xkcd, Hacker News, Trivia and Instapaper**
  fetch what you ask them for, when you ask. Connections downloads the
  published puzzle archive in one go when you press the button for it, from a
  GitHub mirror rather than from the New York Times, and CrossPlay ships none
  of the puzzles. Wordle does the same from another mirror, and asks the New
  York Times itself only for the day or two that mirror has not caught up with,
  never for a day after today; Trivia's question pack and xkcd's comics are downloaded once onto
  the card.
- **Opening a Hacker News article sends its URL to a third party.** The story
  list comes from the public [Algolia API](https://hn.algolia.com/api), and
  opening an article proxies it through [r.jina.ai](https://r.jina.ai) to get
  readable text back, so that service learns what you read. It is a choice, not
  an accident, which is why it is written here.
- **AI Companion is off until you configure `/.crosspoint/companion.json`.**
  When enabled, queries send book metadata, your reading position, and the
  selected/anchor text to your own self-hosted `companion-bridge` server.
  **Security note:** exposing `companion-bridge` externally to the public
  internet is risky—prefer keeping it on your LAN or behind a private VPN, and
  if you front it with a reverse proxy such as Caddy, enforce HTTPS and strong
  `X-API-Key` authentication (see [docs/companion.md](docs/companion.md)).
- **Developer Mode is off** until you turn it on. While it is on the device
  will not deep-sleep, and the panel says so.

## Build it

```bash
git submodule update --init --recursive   # freeink-sdk, and nothing builds without it
pio run -e x4pro                          # X4 Pro firmware
pio run -e gh_release_picco               # BOOX Picco firmware
pio run -e gh_release_eego_a4             # EEGO A4 firmware
pio run -e simulator_x4_pro               # X4 Pro simulator (SDL2 + FreeRTOS shim)
pio run -e simulator_picco                # BOOX Picco simulator (480x800 portrait)
./scripts_local/build-release.sh          # build x4pro, picco, eego_a4 + Audiobookshelf plugin zip
./scripts_local/flash-device.sh           # auto-detect connected reader & flash safely
./scripts_local/check.sh                  # host tests and builds
```

Always name an environment. A bare `pio run` builds `[env:default]`, which is
upstream's ESP32-C3 target: the chip the install warning above says not to
write an S3 image to. [docs/contributing/](docs/contributing/) is the long
version of all of this -- prerequisites, the clone, the hooks, what CI checks
and how a branch lands.

The apps live in `src/apps_local/`, which is what keeps the merge with upstream
close to conflict-free. Read [LOCAL_SCOPE.md](LOCAL_SCOPE.md) for what the fork
owns and what it turns down, [docs/shelf.md](docs/shelf.md) for the shelf
contract, and [docs/building-apps.md](docs/building-apps.md) for how an app is
put together. [docs/README.md](docs/README.md) indexes the rest, including
[docs/workflow/](docs/workflow/), which is how the work itself is run.
`site/` is the static website and the browser build;
[site/README.md](site/README.md) says how to serve it.

## Credit and licence

XMarginalia is MIT, like the upstream projects it forks. This repository is a
fork of the upstream firmwares:
[CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader),
**CrossInk**, and **CrossPlay**, and stands on the
[FreeInk SDK](https://freeink.org/); CrossPoint's own README is kept at
[docs/crosspoint-readme.md](docs/crosspoint-readme.md).

xkcd comics are by Randall Munroe, [CC BY-NC 2.5](https://xkcd.com/license.html),
fetched by the device from [xkcd.com](https://xkcd.com). Connections puzzles are
the New York Times'; CrossPlay ships none of them and fetches the archive, when
you ask it to, from the community mirror at
[Eyefyre/NYT-Connections-Answers](https://github.com/Eyefyre/NYT-Connections-Answers).
Wordle's answers and its list of accepted guesses are the New York Times' too;
CrossPlay ships none of them and fetches them, when you ask, from
[mfilej/wrdl](https://github.com/mfilej/wrdl) and, for the newest days, from
the Times' own daily answer.
Trivia's questions are built from the community
[Jeopardy! clue dataset](https://github.com/jwolle1/jeopardy_clue_dataset).
Underhand's cards are the text of Underhand (Spoopy Squad, Cornell GDIAC, 2017),
used with its authors' permission; none of its art or sound is.
Type is Jersey 25 and Instrument Serif, both SIL OFL. The Calculator's
arithmetic is IBM's [decNumber](https://speleotrove.com/decimal/decnumber.html),
under the ICU License (ICU 1.8.1 and later), vendored at `lib/decNumber` with
its notice; it is there so the calculator answers 0.1 + 0.2 - 0.3 with zero,
which a binary one cannot.

Where a game on the shelf carries the name of a published game, that name is
its owner's trademark and is used to say what the thing is; game mechanics are
not copyrightable. Sea Salt & Paper was designed by Bruno Cathala and Theo
Riviere and is published by Bombyx. Toy Battle was designed by Paolo Mori and
Alessandro Zucchini and is published by Repos Production. CrossPlay implements
the games; it is not affiliated with, endorsed by or sponsored by any of their
owners.

Every third-party notice in one place, including what is knowingly outstanding:
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). To report a security problem,
[SECURITY.md](SECURITY.md).
