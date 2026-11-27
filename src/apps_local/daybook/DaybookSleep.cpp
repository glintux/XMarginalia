#include "DaybookSleep.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "../../CrossPointSettings.h"
#include "../ClockFloor.h"
#include "../ui/ToyboxFonts.h"
#include "DaybookCore.h"
#include "DaybookDraw.h"

namespace cal = daybook;

namespace {

constexpr const char* kTag = "DBK";
constexpr const char* kEventsPath = "/.crosspoint/daybook/events.txt";
constexpr const char* kTasksPath = "/.crosspoint/daybook/tasks.txt";
constexpr const char* kSettingsPath = "/.crosspoint/daybook/settings.txt";

constexpr const char* kDow[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
constexpr const char* kMon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

bool readText(const char* path, size_t maxBytes, std::string& out) {
  out.clear();
  if (Storage.exists(path)) return Storage.readFileToString(kTag, path, maxBytes, out);
  const std::string tmp = std::string(path) + ".tmp";
  return Storage.exists(tmp.c_str()) && Storage.readFileToString(kTag, tmp, maxBytes, out);
}

// What the settings file says; the defaults when there is none.
cal::Settings readSettings() {
  std::string text;
  readText(kSettingsPath, cal::kMaxSettingsBytes, text);
  return cal::parseSettings(text);
}

}  // namespace

namespace daybook {

bool drawAgenda(GfxRenderer& renderer) {
  const std::time_t now = std::time(nullptr);
  if (!clockfloor::trusted(static_cast<int64_t>(now))) return false;
  std::tm t{};
  localtime_r(&now, &t);
  const cal::Day today = cal::daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  const int nowMin = t.tm_hour * 60 + t.tm_min;

  std::string text;
  std::vector<cal::Event> events;
  std::vector<cal::Task> tasks;
  if (readText(kEventsPath, cal::kMaxFileBytes, text)) cal::parse(text, events);
  if (readText(kTasksPath, cal::kMaxTaskFileBytes, text)) cal::parseTasks(text, tasks);

  const bool twelve = SETTINGS.clockFormat == 1;
  cal::Agenda agenda = cal::buildAgenda(events, tasks, today, nowMin, twelve);
  cal::AgendaOptions opt;
  const cal::Settings settings = readSettings();
  opt.size = static_cast<cal::AgendaSize>(settings.agendaSize);
  opt.tomorrow = settings.tomorrow;
  opt.evening = nowMin >= cal::kEveningMinute;
  if (!opt.tomorrow) agenda.tomorrow.clear();

  toybox::ensureFonts(renderer);
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight();
  const int fBody = toybox::kReadingFontId;
  const int fSmall = toybox::kReadingSmallFontId;
  const int fBold = toybox::kReadingBoldFontId;
  const int fBoldSmall = toybox::kReadingBoldSmallFontId;
  const bool large = opt.size == cal::AgendaSize::Large;

  // Large: the full-width panel in the reading font. Normal and Small: nothing
  // bigger than the "as of" font (the bold headings are the next cut up), and
  // the panel is only as wide as what it holds.
  const int fDate = large ? fBold : fBoldSmall;
  const int fItem = large ? fBody : fSmall;
  const int fStrong = large ? fBold : fBoldSmall;
  const int margin = 16;
  const int pad = large ? 12 : (opt.size == cal::AgendaSize::Normal ? 10 : 8);
  const int fullW = W - 2 * margin;
  const int lhHead = renderer.getLineHeight(fDate) + (large ? 6 : 5);
  const int maxH = H - 2 * margin;

  cal::AgendaMetrics m;
  m.item = renderer.getLineHeight(fItem) + (large ? 4 : 3);
  m.strong = renderer.getLineHeight(fStrong) + (large ? 4 : 3);
  m.summary = renderer.getLineHeight(fSmall) + 3;
  m.rule = large ? 14 : 10;
  m.tomHead = renderer.getLineHeight(fBoldSmall) + (large ? 4 : 3);
  m.tomItem = renderer.getLineHeight(fSmall) + 3;
  m.more = renderer.getLineHeight(fSmall) + 3;
  m.avail = maxH - 2 * pad - lhHead;

  const std::string tomorrowTitle = [&] {
    const cal::Date d = cal::civilFromDays(today + 1);
    char buf[48];
    std::snprintf(buf, sizeof buf, "Tomorrow, %s %d %s", kDow[cal::weekday(today + 1)], d.d, kMon[d.m - 1]);
    return std::string(buf);
  }();
  const std::vector<cal::AgendaLine> lines = cal::layoutAgenda(agenda, m, tomorrowTitle, opt);

  const auto fontOf = [&](const cal::AgendaStyle s) {
    switch (s) {
      case cal::AgendaStyle::Event:
      case cal::AgendaStyle::Task:
        return fItem;
      case cal::AgendaStyle::Strong:
        return fStrong;
      case cal::AgendaStyle::TomHead:
        return fBoldSmall;
      default:
        return fSmall;  // summary, tomorrow's rows, "+N more"
    }
  };
  const auto heightOf = [&](const cal::AgendaStyle s) {
    switch (s) {
      case cal::AgendaStyle::Event:
      case cal::AgendaStyle::Task:
        return m.item;
      case cal::AgendaStyle::Strong:
        return m.strong;
      case cal::AgendaStyle::Summary:
        return m.summary;
      case cal::AgendaStyle::Rule:
        return m.rule;
      case cal::AgendaStyle::TomHead:
        return m.tomHead;
      case cal::AgendaStyle::TomEvent:
      case cal::AgendaStyle::TomTask:
        return m.tomItem;
      case cal::AgendaStyle::More:
        return m.more;
    }
    return 0;
  };
  const auto isTask = [](const cal::AgendaStyle s) {
    return s == cal::AgendaStyle::Task || s == cal::AgendaStyle::TomTask;
  };
  // An empty box before a to-do; events have nothing before them.
  const auto boxSide = [&](const int font) { return std::max(8, renderer.getLineHeight(font) * 7 / 10); };
  const auto indentOf = [&](const cal::AgendaStyle s) { return isTask(s) ? boxSide(fontOf(s)) + 6 : 0; };

  const std::string stamp = "as of " + cal::formatMinutes(nowMin, twelve);
  const std::string date = [&] {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%s, %d %s", kDow[cal::weekday(today)], t.tm_mday, kMon[t.tm_mon]);
    return std::string(buf);
  }();

  int boxW = fullW;
  if (!large) {  // as wide as the widest line
    int widest = renderer.getTextWidth(fDate, date.c_str()) + 16 + renderer.getTextWidth(fSmall, stamp.c_str());
    for (const cal::AgendaLine& l : lines) {
      if (l.style == cal::AgendaStyle::Rule) continue;
      widest = std::max(widest, indentOf(l.style) + renderer.getTextWidth(fontOf(l.style), l.text.c_str()));
    }
    boxW = std::min(fullW, widest + 2 * pad);
  }
  const int textW = boxW - 2 * pad;

  int used = pad * 2 + lhHead;
  for (const cal::AgendaLine& l : lines) used += heightOf(l.style);
  const int boxH = std::min(used + (large ? pad : 0), maxH);
  const int boxX = margin;
  const int boxY = settings.agendaBottom ? H - margin - boxH : margin;

  // A white panel with small rounded corners and a black border.
  const int radius = 5;
  cal::fillRoundRect(renderer, boxX, boxY, boxW, boxH, radius + 1, false);
  renderer.drawRoundedRect(boxX, boxY, boxW, boxH, 2, radius, true);

  const int x = boxX + pad;
  int y = boxY + pad;
  renderer.drawText(fDate, x, y, date.c_str(), true, EpdFontFamily::BOLD);
  // When it was drawn: the screen does not change while the device sleeps.
  renderer.drawText(fSmall, boxX + boxW - pad - renderer.getTextWidth(fSmall, stamp.c_str()),
                    y + renderer.getLineHeight(fDate) - renderer.getLineHeight(fSmall), stamp.c_str(), true);
  y += lhHead;
  renderer.drawLine(x, y - 3, boxX + boxW - pad, y - 3, true);

  for (const cal::AgendaLine& l : lines) {
    const int h = heightOf(l.style);
    if (l.style == cal::AgendaStyle::Rule) {
      renderer.drawLine(x, y + h / 2, boxX + boxW - pad, y + h / 2, true);
    } else {
      const int font = fontOf(l.style);
      const int indent = indentOf(l.style);
      if (isTask(l.style)) {
        const int side = boxSide(font);
        renderer.drawRoundedRect(x, y + (renderer.getLineHeight(font) - side) / 2 + 1, side, side, 1, 2, true);
      }
      const std::string fit = renderer.truncatedText(font, l.text.c_str(), textW - indent);
      const bool bold = l.style == cal::AgendaStyle::Strong || l.style == cal::AgendaStyle::TomHead;
      if (bold) {
        renderer.drawText(font, x, y, fit.c_str(), true, EpdFontFamily::BOLD);
      } else {
        renderer.drawText(font, x + indent, y, fit.c_str(), true);
      }
    }
    y += h;
  }
  LOG_INF(kTag, "sleep agenda: %u line(s), size %d, %u for tomorrow", static_cast<unsigned>(lines.size()),
          settings.agendaSize, static_cast<unsigned>(agenda.tomorrow.size()));
  return true;
}

bool agendaEnabled() { return readSettings().agenda; }

}  // namespace daybook
