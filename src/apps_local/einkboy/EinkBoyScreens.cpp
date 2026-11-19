#include "EinkBoyScreens.h"

#include <algorithm>
#include <cstdio>

#include "../ui/ToyboxFormat.h"

namespace einkboyui {

namespace {

void toyboxChrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

void drawFilterButton(toybox::Screen& screen, const fui::Rect& rect, const char* label, const bool active,
                      const fui::ActionId action) {
  fui::ButtonProps btn;
  btn.label = label;
  btn.action = action;
  btn.styles = fui::StyleSet{};
  btn.styles.explicitlySet = true;
  if (active) {
    btn.styles.normal.background = fui::Paint::solid(fui::Color::Black);
    btn.styles.normal.foreground = fui::Paint::solid(fui::Color::White);
    btn.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    btn.styles.normal.borderWidth = 2;
  } else {
    btn.styles.normal.background = fui::Paint::solid(fui::Color::White);
    btn.styles.normal.foreground = fui::Paint::solid(fui::Color::Black);
    btn.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    btn.styles.normal.borderWidth = 1;
  }
  btn.styles.active = btn.styles.normal;
  btn.styles.selected = btn.styles.normal;
  btn.styles.focused = btn.styles.normal;
  btn.text = fui::TextStyle{};
  btn.text.font = toybox::kTileFont;
  btn.text.align = fui::TextAlign::Center;
  btn.text.color = active ? fui::Color::White : fui::Color::Black;
  btn.borderEdges = fui::EdgesAll;
  screen.button(btn, rect);
}

void drawPagerStrip(toybox::Screen& screen, const fui::Rect& bar, const int page, const int pageCount) {
  if (pageCount <= 1) return;
  constexpr int16_t kPitch = 40;
  const int16_t pitch = kPitch * pageCount <= bar.width ? kPitch : static_cast<int16_t>(bar.width / pageCount);
  const int16_t clusterX = static_cast<int16_t>(bar.x + (bar.width - pitch * pageCount) / 2);

  fui::StyleSet invisible;
  invisible.explicitlySet = true;

  const int16_t inset = 2;
  const int16_t cellH = static_cast<int16_t>(bar.height - 2 * inset);
  for (int p = 0; p < pageCount; ++p) {
    const fui::Rect target =
        fui::makeRect(static_cast<int16_t>(clusterX + p * pitch), bar.y, pitch, static_cast<int16_t>(bar.height));
    fui::ButtonProps jump;
    jump.action = ActionGoToPage;
    jump.value = static_cast<int16_t>(p);
    jump.styles = invisible;
    jump.minTouchSize = 0;
    screen.button(jump, target);

    const fui::Rect cell = fui::makeRect(static_cast<int16_t>(target.x + inset), static_cast<int16_t>(target.y + inset),
                                         static_cast<int16_t>(pitch - 2 * inset), cellH);
    const bool here = (p == page);
    if (here) {
      screen.target().fill(cell, fui::Paint::solid(fui::Color::Black), 10);
    } else {
      screen.target().stroke(cell, fui::Paint::solid(fui::Color::Black), 1, 10);
    }

    char number[toybox::kIntTextChars];
    std::snprintf(number, sizeof(number), "%d", p + 1);
    fui::TextStyle style;
    style.font = toybox::kUiFont;
    style.align = fui::TextAlign::Center;
    style.color = here ? fui::Color::White : fui::Color::Black;
    screen.target().text(toybox::inkCentred(cell, toybox::kUiCut), number, style);
  }
}

void drawEmptyLibraryMessage(toybox::Screen& screen, const LibraryFilter filter) {
  const fui::Rect body = screen.body();
  const int16_t boxH = 120;
  const fui::Rect card =
      fui::makeRect(body.x, static_cast<int16_t>(body.y + std::max(0, (body.height - boxH) / 3)), body.width, boxH);
  screen.target().stroke(card, fui::Paint::solid(fui::Color::Black), 1, 0);

  const char* headline = "NO ROMS FOUND";
  const char* hint = "PLACE .GB OR .GBC FILES ON SD CARD";
  if (filter == LibraryFilter::Favorites) {
    headline = "NO FAVORITE GAMES";
    hint = "SELECT A GAME TO STAR IT";
  } else if (filter == LibraryFilter::Recent) {
    headline = "NO RECENT GAMES";
    hint = "LAUNCH A GAME FROM ALL GAMES";
  }

  fui::TextStyle hStyle;
  hStyle.font = toybox::kUiFont;
  hStyle.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(card.x + 8, card.y + 24, card.width - 16, 32), headline, hStyle);

  fui::TextStyle sStyle;
  sStyle.font = toybox::kTileFont;
  sStyle.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(card.x + 8, card.y + 64, card.width - 16, 28), hint, sStyle);
}

}  // namespace

int libraryRowsPerPage() { return 6; }

void buildLibrary(toybox::Screen& screen, const LibraryModel& model) {
  char headerRight[24];
  if (model.pageCount > 1) {
    std::snprintf(headerRight, sizeof(headerRight), "%d/%d", model.page + 1, model.pageCount);
  } else {
    std::snprintf(headerRight, sizeof(headerRight), "%d ROMS", model.totalGames);
  }
  toyboxChrome(screen, "EINKBOY", headerRight);

  {
    const fui::Rect strip = screen.takeTop(42, toybox::kGutter);
    const int16_t gap = 8;
    const int16_t tabW = static_cast<int16_t>((strip.width - 2 * gap) / 3);
    char allLbl[20], favLbl[20], recLbl[20];
    std::snprintf(allLbl, sizeof(allLbl), "ALL (%d)", model.totalGames);
    std::snprintf(favLbl, sizeof(favLbl), "FAV (%d)", model.favoriteCount);
    std::snprintf(recLbl, sizeof(recLbl), "RECENT (%d)", model.recentCount);
    drawFilterButton(screen, fui::makeRect(strip.x, strip.y, tabW, strip.height), allLbl,
                     model.filter == LibraryFilter::All, ActionFilterAll);
    drawFilterButton(screen, fui::makeRect(static_cast<int16_t>(strip.x + tabW + gap), strip.y, tabW, strip.height),
                     favLbl, model.filter == LibraryFilter::Favorites, ActionFilterFav);
    drawFilterButton(screen,
                     fui::makeRect(static_cast<int16_t>(strip.x + 2 * (tabW + gap)), strip.y,
                                   static_cast<int16_t>(strip.width - 2 * (tabW + gap)), strip.height),
                     recLbl, model.filter == LibraryFilter::Recent, ActionFilterRecent);
  }

  {
    const fui::Rect footer = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
    const int16_t gap = toybox::kGutter;
    const int16_t btnW = static_cast<int16_t>((footer.width - gap) / 2);

    fui::ButtonProps settingsBtn;
    settingsBtn.label = "SETTINGS";
    settingsBtn.action = ActionOpenSettings;
    settingsBtn.borderEdges = fui::EdgesNone;
    screen.button(settingsBtn, fui::makeRect(footer.x, footer.y, btnW, footer.height));

    fui::ButtonProps exitBtn;
    exitBtn.label = "EXIT";
    exitBtn.action = ActionExitApp;
    exitBtn.borderEdges = fui::EdgesNone;
    screen.button(exitBtn, fui::makeRect(static_cast<int16_t>(footer.x + btnW + gap), footer.y,
                                         footer.width - btnW - gap, footer.height));
  }

  if (model.pageCount > 1) {
    const fui::Rect pagerBar = screen.takeBottom(40, toybox::kGutter);
    drawPagerStrip(screen, pagerBar, model.page, model.pageCount);
  }

  if (model.statusMessage && model.statusMessage[0]) {
    const fui::Rect statusLine = screen.takeBottom(24, 4);
    fui::TextStyle st;
    st.font = toybox::kTileFont;
    st.align = fui::TextAlign::Center;
    screen.target().text(statusLine, model.statusMessage, st);
  }

  if (model.pageItemCount <= 0) {
    drawEmptyLibraryMessage(screen, model.filter);
    return;
  }

  fui::ListItem listItems[kMaxPageRoms] = {};
  const int count = model.pageItemCount < kMaxPageRoms ? model.pageItemCount : kMaxPageRoms;
  for (int i = 0; i < count; ++i) {
    listItems[i].label = model.items[i].title;
    listItems[i].value = model.items[i].badge;
    listItems[i].actionValue = static_cast<int16_t>(model.pageStartIndex + i);
  }

  fui::ListProps list;
  list.items = listItems;
  list.count = static_cast<uint16_t>(count);
  list.selectedIndex = static_cast<int16_t>(model.selectedOnPage);
  list.action = ActionSelectRom;
  list.rowHeight = toybox::kRowHeight;
  list.rowGap = screen.theme().listRowGap;
  screen.list(list);
}

void buildGameActions(toybox::Screen& screen, const GameActionsModel& model) {
  toyboxChrome(screen, model.title ? model.title : "GAME", model.typeLabel);

  fui::ButtonProps backBtn;
  backBtn.label = "BACK TO LIBRARY";
  backBtn.action = ActionBackToLibrary;
  backBtn.borderEdges = fui::EdgesNone;
  screen.button(backBtn, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  if (model.statusMessage && model.statusMessage[0]) {
    const fui::Rect note = screen.takeBottom(28, 4);
    fui::TextStyle st;
    st.font = toybox::kTileFont;
    st.align = fui::TextAlign::Center;
    screen.target().text(note, model.statusMessage, st);
  }

  fui::ListItem rows[3] = {};
  rows[0].label = "PLAY GAME";
  rows[0].value = model.hasSave ? "SAVED" : "NEW";
  rows[0].actionValue = 0;

  rows[1].label = "FAVORITE";
  rows[1].value = model.isFavorite ? "ON" : "OFF";
  rows[1].actionValue = 1;

  rows[2].label = model.confirmDelete ? "CONFIRM DELETE" : "DELETE SAVE";
  rows[2].value = model.hasSave ? (model.confirmDelete ? "TAP AGAIN" : ".SAV") : "NONE";
  rows[2].actionValue = 2;

  fui::ListProps list;
  list.items = rows;
  list.count = 3;
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionPlayRom;
  // Map each row to its dedicated action via row selection dispatch in Activity or ActionPlayRom value.
  // Keeping dedicated action buttons below or ActionPlayRom with index 0/1/2 keeps interaction single-pass.
  screen.list(list);
}

const char* pauseRowLabel(const PauseRow row) {
  switch (row) {
    case PauseRow::Resume:
      return "RESUME GAME";
    case PauseRow::SaveNow:
      return "SAVE PROGRESS";
    case PauseRow::Rotate:
      return "ROTATE SCREEN";
    case PauseRow::Settings:
      return "SETTINGS";
    case PauseRow::RomLibrary:
      return "ROM LIBRARY";
    case PauseRow::ExitEinkBoy:
      return "EXIT EINKBOY";
    case PauseRow::Count:
      break;
  }
  return "";
}

const char* pauseRowValue(const InGameMenuModel& model, const PauseRow row) {
  switch (row) {
    case PauseRow::SaveNow:
      return model.saveStatus ? model.saveStatus : "";
    case PauseRow::Rotate:
      return model.portrait ? "PORTRAIT" : "LANDSCAPE";
    default:
      return "";
  }
}

void buildInGameMenu(toybox::Screen& screen, const InGameMenuModel& model) {
  toyboxChrome(screen, "PAUSED", model.title);

  fui::ButtonProps resumePill;
  resumePill.label = "RESUME";
  resumePill.action = ActionResumeGame;
  resumePill.borderEdges = fui::EdgesNone;
  screen.button(resumePill, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  constexpr int kCount = static_cast<int>(PauseRow::Count);
  fui::ListItem rows[kCount] = {};
  for (int i = 0; i < kCount; ++i) {
    const PauseRow row = static_cast<PauseRow>(i);
    rows[i].label = pauseRowLabel(row);
    rows[i].value = pauseRowValue(model, row);
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps list;
  list.items = rows;
  list.count = kCount;
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionPauseRow;
  list.rowHeight = toybox::kRowHeight;
  list.rowGap = screen.theme().listRowGap;
  screen.list(list);
}

const char* settingRowLabel(const SettingRow row) {
  switch (row) {
    case SettingRow::Orientation:
      return "ORIENTATION";
    case SettingRow::RefreshMode:
      return "REFRESH MODE";
    case SettingRow::DitherMode:
      return "DITHER MODE";
    case SettingRow::AutoClean:
      return "CLEANING";
    case SettingRow::TouchControls:
      return "TOUCH CONTROLS";
    case SettingRow::ShowTitle:
      return "GAME TITLE";
    case SettingRow::ButtonUp:
      return "UP BUTTON";
    case SettingRow::ButtonDown:
      return "DOWN BUTTON";
    case SettingRow::ButtonConfirm:
      return "CONFIRM BUTTON";
    case SettingRow::Count:
      break;
  }
  return "";
}

void formatSettingRowValue(const SettingsModel& model, const SettingRow row, char* out, const size_t outSize) {
  if (!out || outSize == 0) return;
  switch (row) {
    case SettingRow::Orientation:
      std::snprintf(out, outSize, "%s", model.settings.layout == 0 ? "PORTRAIT" : "LANDSCAPE");
      return;
    case SettingRow::RefreshMode: {
      const char* label = "FAST";
      if (model.settings.refresh == 1) {
        label = "TURBO";
      } else if (model.settings.refresh == 2) {
        label = model.asyncSupported ? "STANDARD" : "BLOCKING";
      }
      std::snprintf(out, outSize, "%s", label);
      return;
    }
    case SettingRow::DitherMode:
      std::snprintf(out, outSize, "%s", model.settings.ditherMode == 0 ? "STABLE" : "SMOOTH");
      return;
    case SettingRow::AutoClean:
      std::snprintf(out, outSize, "%s", model.settings.autoClean == 0 ? "ON PAUSE" : "AUTO 60S");
      return;
    case SettingRow::TouchControls:
      std::snprintf(out, outSize, "%s", model.settings.touchControls ? "ON" : "OFF");
      return;
    case SettingRow::ShowTitle:
      std::snprintf(out, outSize, "%s", model.settings.showGameTitle ? "ON" : "OFF");
      return;
    case SettingRow::ButtonUp:
      std::snprintf(out, outSize, "%s", einkboy::buttonActionLabel(model.settings.upAction));
      return;
    case SettingRow::ButtonDown:
      std::snprintf(out, outSize, "%s", einkboy::buttonActionLabel(model.settings.downAction));
      return;
    case SettingRow::ButtonConfirm:
      std::snprintf(out, outSize, "%s", einkboy::buttonActionLabel(model.settings.confirmAction));
      return;
    case SettingRow::Count:
      break;
  }
  out[0] = '\0';
}

void buildSettings(toybox::Screen& screen, const SettingsModel& model) {
  toyboxChrome(screen, "SETTINGS");

  fui::ButtonProps done;
  done.label = model.fromGame ? "BACK TO PAUSE MENU" : "BACK TO LIBRARY";
  done.action = ActionCloseSettings;
  done.borderEdges = fui::EdgesNone;
  screen.button(done, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect hint = screen.takeBottom(24, 4);
  fui::TextStyle st;
  st.font = toybox::kTileFont;
  st.align = fui::TextAlign::Center;
  screen.target().text(hint, "BACK ALWAYS PAUSES / EXITS", st);

  constexpr int kCount = static_cast<int>(SettingRow::Count);
  char values[kCount][24] = {};
  fui::ListItem rows[kCount] = {};
  for (int i = 0; i < kCount; ++i) {
    const SettingRow row = static_cast<SettingRow>(i);
    formatSettingRowValue(model, row, values[i], sizeof(values[i]));
    rows[i].label = settingRowLabel(row);
    rows[i].value = values[i];
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps list;
  list.items = rows;
  list.count = kCount;
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionSettingRow;
  list.rowHeight = 52;
  list.rowGap = screen.theme().listRowGap;
  screen.list(list);
}

void buildNotice(toybox::Screen& screen, const NoticeModel& model) {
  toyboxChrome(screen, model.title ? model.title : "EINKBOY");

  fui::ButtonProps okBtn;
  okBtn.label = model.buttonLabel ? model.buttonLabel : "BACK";
  okBtn.action = ActionDismissNotice;
  okBtn.borderEdges = fui::EdgesNone;
  screen.button(okBtn, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect body = screen.body();
  if (model.headline && model.headline[0]) {
    fui::TextStyle hs;
    hs.font = toybox::kUiFont;
    hs.align = fui::TextAlign::Center;
    screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(body.y + body.height / 3 - 20), body.width, 36),
                         model.headline, hs);
  }
  if (model.detail && model.detail[0]) {
    fui::TextStyle ds;
    ds.font = toybox::kTileFont;
    ds.align = fui::TextAlign::Center;
    screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(body.y + body.height / 3 + 28), body.width, 64),
                         model.detail, ds);
  }
}

}  // namespace einkboyui
