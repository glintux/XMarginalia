#include "DaybookCore.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace daybook {

// --- Dates (Howard Hinnant's civil-days algorithms) -------------------------

Day daysFromCivil(int y, const int m, const int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153u * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2u) / 5u + static_cast<unsigned>(d) - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return static_cast<Day>(era * 146097 + static_cast<int>(doe) - 719468);
}

Date civilFromDays(Day z) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
  const int y = static_cast<int>(yoe) + era * 400;
  const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
  const unsigned mp = (5u * doy + 2u) / 153u;
  Date out;
  out.d = static_cast<int>(doy - (153u * mp + 2u) / 5u + 1u);
  out.m = static_cast<int>(mp < 10u ? mp + 3u : mp - 9u);
  out.y = y + (out.m <= 2);
  return out;
}

int weekday(const Day day) {
  int w = (day + 4) % 7;
  if (w < 0) w += 7;
  return w;
}

int daysInMonth(const int y, const int m) {
  static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return kDays[(m - 1) % 12];
}

int isoWeek(const Day day) {
  const int mondayBased = (weekday(day) + 6) % 7;
  const Day thursday = day - mondayBased + 3;
  const Date t = civilFromDays(thursday);
  return (thursday - daysFromCivil(t.y, 1, 1)) / 7 + 1;
}

Day addMonths(const Day day, const int months) {
  const Date d = civilFromDays(day);
  const int total = d.y * 12 + (d.m - 1) + months;
  const int y = total / 12;
  const int m = total % 12 + 1;
  return daysFromCivil(y, m, std::min(d.d, daysInMonth(y, m)));
}

Day clampDay(const Day day) {
  static const Day last = daysFromCivil(2200, 12, 31);
  return std::clamp<Day>(day, 0, last);
}

// --- Recurrence --------------------------------------------------------------

void occurrencesIn(const std::vector<Event>& events, const Day from, const Day to, std::vector<Occurrence>& out) {
  for (size_t i = 0; i < events.size(); ++i) {
    const Event& ev = events[i];
    const Day s0 = ev.start;
    const Day dur = ev.end > ev.start ? ev.end - ev.start : 0;
    const auto add = [&](const Day s) {
      if (s + dur >= from && s <= to) out.push_back(Occurrence{i, s, s + dur});
    };
    if (ev.repeat == Repeat::None) {
      add(s0);
      continue;
    }
    const int every = ev.every < 1 ? 1 : ev.every;
    const Day limit = ev.hasUntil ? std::min(to, ev.until) : to;
    if (ev.repeat == Repeat::Daily || ev.repeat == Repeat::Weekly) {
      const int per = every * (ev.repeat == Repeat::Weekly ? 7 : 1);
      int64_t k = 0;
      if (s0 < from - dur) k = (static_cast<int64_t>(from) - dur - s0) / per;
      for (int guard = 0; guard < 4000; ++guard, ++k) {
        const Day s = static_cast<Day>(s0 + k * per);
        if (s > limit) break;
        add(s);
      }
    } else {
      const Date p = civilFromDays(s0);
      const int step = ev.repeat == Repeat::Yearly ? 12 * every : every;
      const Date f = civilFromDays(from);
      int64_t k = ((f.y * 12 + f.m) - (p.y * 12 + p.m)) / step - 1;
      if (k < 0) k = 0;
      for (int guard = 0; guard < 4000; ++guard, ++k) {
        const int total = p.y * 12 + (p.m - 1) + static_cast<int>(k) * step;
        const int y = total / 12;
        const int m = total % 12 + 1;
        const Day s = daysFromCivil(y, m, std::min(p.d, daysInMonth(y, m)));  // 31st clamps to the month's end
        if (s > limit) break;
        add(s);
      }
    }
  }
}

namespace {
int sortKey(const Event& ev, const Occurrence& o, const Day day) {
  if (ev.allDay || day != o.start) return -1;
  return ev.startMin;
}
}  // namespace

void occurrencesOn(const std::vector<Event>& events, const Day day, std::vector<Occurrence>& out) {
  out.clear();
  occurrencesIn(events, day, day, out);
  std::sort(out.begin(), out.end(), [&](const Occurrence& a, const Occurrence& b) {
    const int ka = sortKey(events[a.event], a, day);
    const int kb = sortKey(events[b.event], b, day);
    if (ka != kb) return ka < kb;
    if (events[a.event].title != events[b.event].title) return events[a.event].title < events[b.event].title;
    return a.event != b.event ? a.event < b.event : a.start < b.start;  // a total order: same answer as a stable sort
  });
}

uint64_t busyDays(const std::vector<Event>& events, const Day first) {
  std::vector<Occurrence> found;
  occurrencesIn(events, first, first + 41, found);
  uint64_t bits = 0;
  for (const Occurrence& o : found) {
    const Day a = std::max(o.start, first);
    const Day b = std::min(o.end, first + 41);
    for (Day d = a; d <= b; ++d) bits |= uint64_t{1} << (d - first);
  }
  return bits;
}

// --- Words -------------------------------------------------------------------

std::string formatMinutes(const int minutes, const bool twelveHour) {
  const int m = ((minutes % 1440) + 1440) % 1440;
  char buf[16];
  if (!twelveHour) {
    std::snprintf(buf, sizeof buf, "%02d:%02d", m / 60, m % 60);
  } else {
    const int h = m / 60;
    const int h12 = h % 12 == 0 ? 12 : h % 12;
    std::snprintf(buf, sizeof buf, "%d:%02d %s", h12, m % 60, h < 12 ? "AM" : "PM");
  }
  return buf;
}

std::string formatDate(const Day day) {
  const Date d = civilFromDays(day);
  char buf[16];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", d.y, d.m, d.d);
  return buf;
}

std::string formatSpan(const Day start, const Day end) {
  const Date a = civilFromDays(start);
  const Date b = civilFromDays(end);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%02d.%02d-%02d.%02d", a.m, a.d, b.m, b.d);
  return buf;
}

bool parseDate(const std::string& text, Day& out) {
  int y = 0, m = 0, d = 0;
  char tail = 0;
  if (std::sscanf(text.c_str(), "%4d-%2d-%2d%c", &y, &m, &d, &tail) != 3) return false;
  if (y < 1970 || y > 2200 || m < 1 || m > 12 || d < 1 || d > daysInMonth(y, m)) return false;
  out = daysFromCivil(y, m, d);
  return true;
}

std::string foldForDisplay(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size();) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    if (c < 0x80) {
      out += static_cast<char>(c);
      ++i;
      continue;
    }
    int len = 1;
    uint32_t cp = 0xFFFD;
    if ((c & 0xE0) == 0xC0) {
      len = 2;
      cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
      len = 3;
      cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      len = 4;
      cp = c & 0x07;
    }
    if (i + static_cast<size_t>(len) > in.size()) {
      out += '?';
      break;
    }
    for (int k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(in[i + static_cast<size_t>(k)]) & 0x3F);
    i += static_cast<size_t>(len);
    if (cp == 0x150)
      cp = 0xD6;  // O double acute -> O umlaut
    else if (cp == 0x151)
      cp = 0xF6;  // o double acute -> o umlaut
    else if (cp == 0x170)
      cp = 0xDC;  // U double acute -> U umlaut
    else if (cp == 0x171)
      cp = 0xFC;  // u double acute -> u umlaut
    if (cp >= 0xA0 && cp <= 0xFF) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += '?';
    }
  }
  return out;
}

// --- Storage format ----------------------------------------------------------
//
// V1
// E <id> <title> <start> <end> <allday 0|1> <startMin> <endMin> <repeat>
// <every> <until|-> <location> <note> with TAB between fields; \t \n \\ escaped
// inside text.

namespace {

void putEscaped(std::string& out, const std::string& text) {
  for (const char c : text) {
    if (c == '\t')
      out += "\\t";
    else if (c == '\n')
      out += "\\n";
    else if (c == '\r')
      continue;
    else if (c == '\\')
      out += "\\\\";
    else
      out += c;
  }
}

std::string unescape(const std::string& text) {
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\\' && i + 1 < text.size()) {
      const char n = text[++i];
      out += n == 't' ? '\t' : n == 'n' ? '\n' : n;
    } else {
      out += text[i];
    }
  }
  return out;
}

std::string clip(std::string s, const size_t max) {
  if (s.size() <= max) return s;
  size_t cut = max;
  while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;  // never mid-letter
  s.resize(cut);
  return s;
}

}  // namespace

std::string serialize(const std::vector<Event>& events) {
  std::string out = "V1\n";
  for (const Event& ev : events) {
    char head[64];
    std::snprintf(head, sizeof head, "E\t%u\t", static_cast<unsigned>(ev.id));
    out += head;
    putEscaped(out, ev.title);
    out += '\t' + formatDate(ev.start) + '\t' + formatDate(ev.end) + '\t';
    char mid[64];
    std::snprintf(mid, sizeof mid, "%d\t%d\t%d\t%d\t%u\t", ev.allDay ? 1 : 0, ev.startMin, ev.endMin,
                  static_cast<int>(ev.repeat), static_cast<unsigned>(ev.every));
    out += mid;
    out += ev.hasUntil ? formatDate(ev.until) : std::string("-");
    out += '\t';
    putEscaped(out, ev.location);
    out += '\t';
    putEscaped(out, ev.note);
    out += '\n';
  }
  return out;
}

size_t parse(const std::string& text, std::vector<Event>& out) {
  out.clear();
  if (text.size() > kMaxFileBytes) return 0;
  size_t pos = 0;
  while (pos < text.size() && out.size() < kMaxEvents) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string::npos) eol = text.size();
    const std::string line = text.substr(pos, eol - pos);
    pos = eol + 1;
    if (line.size() < 2 || line[0] != 'E' || line[1] != '\t') continue;
    std::vector<std::string> f;
    size_t a = 2;
    for (;;) {
      const size_t t = line.find('\t', a);
      if (t == std::string::npos) {
        f.push_back(line.substr(a));
        break;
      }
      f.push_back(line.substr(a, t - a));
      a = t + 1;
    }
    if (f.size() < 12) continue;
    Event ev;
    ev.id = static_cast<uint32_t>(std::strtoul(f[0].c_str(), nullptr, 10));
    ev.title = clip(unescape(f[1]), kMaxTitle);
    if (!parseDate(f[2], ev.start) || !parseDate(f[3], ev.end) || ev.end < ev.start) continue;
    ev.allDay = f[4] == "1";
    ev.startMin = static_cast<int16_t>(std::clamp(std::atoi(f[5].c_str()), 0, 1439));
    ev.endMin = static_cast<int16_t>(std::clamp(std::atoi(f[6].c_str()), 0, 1439));
    const int rep = std::atoi(f[7].c_str());
    ev.repeat = rep >= 0 && rep <= 4 ? static_cast<Repeat>(rep) : Repeat::None;
    ev.every = static_cast<uint16_t>(std::clamp(std::atoi(f[8].c_str()), 1, 999));
    ev.hasUntil = f[9] != "-" && parseDate(f[9], ev.until);
    ev.location = clip(unescape(f[10]), kMaxLocation);
    ev.note = clip(unescape(f[11]), kMaxNote);
    out.push_back(ev);
  }
  return out.size();
}

uint32_t nextId(const std::vector<Event>& events) {
  uint32_t top = 0;
  for (const Event& ev : events) top = std::max(top, ev.id);
  return top + 1;
}

// --- To-dos
// --------------------------------------------------------------------
//
// T <id> <list> <title> <created> <due|-> <done|-> <note> [<done minute>] (TAB
// separated) L <id> <name>

namespace {

std::vector<std::string> splitTabs(const std::string& line, const size_t from) {
  std::vector<std::string> f;
  size_t a = from;
  for (;;) {
    const size_t t = line.find('\t', a);
    if (t == std::string::npos) {
      f.push_back(line.substr(a));
      break;
    }
    f.push_back(line.substr(a, t - a));
    a = t + 1;
  }
  return f;
}

}  // namespace

bool isOverdue(const Task& task, const Day today) { return !task.done && task.hasDue && task.due < today; }

std::string serializeTasks(const std::vector<Task>& tasks) {
  std::string out = "V1\n";
  for (const Task& t : tasks) {
    char head[48];
    std::snprintf(head, sizeof head, "T\t%u\t%u\t", static_cast<unsigned>(t.id), static_cast<unsigned>(t.list));
    out += head;
    putEscaped(out, t.title);
    out += '\t' + formatDate(t.created) + '\t';
    out += t.hasDue ? formatDate(t.due) : std::string("-");
    out += '\t';
    out += t.done ? formatDate(t.doneOn) : std::string("-");
    out += '\t';
    putEscaped(out, t.note);
    if (t.done && t.doneMin >= 0 && t.doneMin < 1440) out += '\t' + std::to_string(t.doneMin);
    out += '\n';
  }
  return out;
}

size_t parseTasks(const std::string& text, std::vector<Task>& out) {
  out.clear();
  if (text.size() > kMaxTaskFileBytes) return 0;
  size_t pos = 0;
  while (pos < text.size() && out.size() < kMaxTasks) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string::npos) eol = text.size();
    const std::string line = text.substr(pos, eol - pos);
    pos = eol + 1;
    if (line.size() < 2 || line[0] != 'T' || line[1] != '\t') continue;
    const std::vector<std::string> f = splitTabs(line, 2);
    if (f.size() < 7) continue;
    Task t;
    t.id = static_cast<uint32_t>(std::strtoul(f[0].c_str(), nullptr, 10));
    t.list = static_cast<uint32_t>(std::strtoul(f[1].c_str(), nullptr, 10));
    t.title = clip(unescape(f[2]), kMaxTaskTitle);
    if (t.title.empty() || !parseDate(f[3], t.created)) continue;
    t.hasDue = f[4] != "-" && parseDate(f[4], t.due);
    t.done = f[5] != "-" && parseDate(f[5], t.doneOn);
    t.note = clip(unescape(f[6]), kMaxTaskNote);
    if (t.done && f.size() > 7 && !f[7].empty()) {
      const long m = std::strtol(f[7].c_str(), nullptr, 10);
      if (m >= 0 && m < 1440) t.doneMin = static_cast<int>(m);
    }
    out.push_back(t);
  }
  return out.size();
}

std::string serializeLists(const std::vector<TaskList>& lists) {
  std::string out = "V1\n";
  for (const TaskList& l : lists) {
    char head[32];
    std::snprintf(head, sizeof head, "L\t%u\t", static_cast<unsigned>(l.id));
    out += head;
    putEscaped(out, l.name);
    out += '\n';
  }
  return out;
}

size_t parseLists(const std::string& text, std::vector<TaskList>& out) {
  out.clear();
  if (text.size() > kMaxTaskFileBytes) return 0;
  size_t pos = 0;
  while (pos < text.size() && out.size() < kMaxLists) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string::npos) eol = text.size();
    const std::string line = text.substr(pos, eol - pos);
    pos = eol + 1;
    if (line.size() < 2 || line[0] != 'L' || line[1] != '\t') continue;
    const std::vector<std::string> f = splitTabs(line, 2);
    if (f.size() < 2) continue;
    TaskList l;
    l.id = static_cast<uint32_t>(std::strtoul(f[0].c_str(), nullptr, 10));
    l.name = clip(unescape(f[1]), kMaxListName);
    if (l.id == 0 || l.name.empty()) continue;
    bool dup = false;
    for (const TaskList& o : out) dup = dup || o.id == l.id;
    if (!dup) out.push_back(l);
  }
  return out.size();
}

void repairLists(std::vector<TaskList>& lists, std::vector<Task>& tasks) {
  bool hasMain = false;
  for (const TaskList& l : lists) hasMain = hasMain || l.id == kMainList;
  if (!hasMain) lists.insert(lists.begin(), TaskList{kMainList, kMainListName});
  if (lists.size() > kMaxLists) lists.resize(kMaxLists);
  for (Task& t : tasks) {
    bool known = false;
    for (const TaskList& l : lists) known = known || l.id == t.list;
    if (!known) t.list = kMainList;
  }
}

uint32_t nextTaskId(const std::vector<Task>& tasks) {
  uint32_t top = 0;
  for (const Task& t : tasks) top = std::max(top, t.id);
  return top + 1;
}

uint32_t nextListId(const std::vector<TaskList>& lists) {
  uint32_t top = kMainList;
  for (const TaskList& l : lists) top = std::max(top, l.id);
  return top + 1;
}

size_t listIndex(const std::vector<TaskList>& lists, const uint32_t id) {
  for (size_t i = 0; i < lists.size(); ++i) {
    if (lists[i].id == id) return i;
  }
  return 0;
}

void tasksOfList(const std::vector<Task>& tasks, const uint32_t list, std::vector<size_t>& out) {
  out.clear();
  for (size_t i = 0; i < tasks.size(); ++i) {
    if (tasks[i].list == list) out.push_back(i);
  }
  std::sort(out.begin(), out.end(), [&tasks](const size_t a, const size_t b) {
    const Task& x = tasks[a];
    const Task& y = tasks[b];
    if (x.done != y.done) return !x.done;
    if (!x.done) {
      if (x.hasDue != y.hasDue) return x.hasDue;
      if (x.hasDue && x.due != y.due) return x.due < y.due;
      if (x.created != y.created) return x.created < y.created;
    } else if (x.doneOn != y.doneOn) {
      return x.doneOn > y.doneOn;
    }
    return x.id < y.id;
  });
}

void tasksDueOn(const std::vector<Task>& tasks, const Day day, std::vector<size_t>& out) {
  out.clear();
  for (size_t i = 0; i < tasks.size(); ++i) {
    if (tasks[i].hasDue && tasks[i].due == day) out.push_back(i);
  }
  std::sort(out.begin(), out.end(), [&tasks](const size_t a, const size_t b) {
    if (tasks[a].done != tasks[b].done) return !tasks[a].done;
    return tasks[a].id < tasks[b].id;
  });
}

void overdueTasks(const std::vector<Task>& tasks, const Day today, std::vector<size_t>& out) {
  out.clear();
  for (size_t i = 0; i < tasks.size(); ++i) {
    if (isOverdue(tasks[i], today)) out.push_back(i);
  }
  std::sort(out.begin(), out.end(), [&tasks](const size_t a, const size_t b) {
    if (tasks[a].due != tasks[b].due) return tasks[a].due < tasks[b].due;
    return tasks[a].id < tasks[b].id;
  });
}

void taskBusy(const std::vector<Task>& tasks, const Day first, uint64_t& any, uint64_t& open) {
  any = 0;
  open = 0;
  for (const Task& t : tasks) {
    if (!t.hasDue) continue;
    const Day off = t.due - first;
    if (off < 0 || off >= 42) continue;
    any |= uint64_t{1} << off;
    if (!t.done) open |= uint64_t{1} << off;
  }
}

std::string capitalizeFirst(const std::string& text) {
  std::string out = text;
  if (out.empty()) return out;
  const auto b0 = static_cast<uint8_t>(out[0]);
  if (b0 >= 'a' && b0 <= 'z') {
    out[0] = static_cast<char>(b0 - 32);
  } else if (b0 == 0xC3 && out.size() >= 2) {  // Latin-1 letters: U+00E0..U+00FE, except the
                                               // division sign
    const auto b1 = static_cast<uint8_t>(out[1]);
    if (b1 >= 0xA0 && b1 <= 0xBE && b1 != 0xB7) out[1] = static_cast<char>(b1 - 0x20);
  } else if (b0 == 0xC5 && out.size() >= 2) {  // o and u with double acute
    const auto b1 = static_cast<uint8_t>(out[1]);
    if (b1 == 0x91 || b1 == 0xB1) out[1] = static_cast<char>(b1 - 1);
  }
  return out;
}

// --- Search
// -------------------------------------------------------------------

std::string searchKey(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    const auto b0 = static_cast<uint8_t>(text[i]);
    if (b0 >= 'A' && b0 <= 'Z') {
      out += static_cast<char>(b0 + 32);
    } else if (b0 == 0xC3 && i + 1 < text.size()) {  // Latin-1: U+00C0..U+00FF
      const auto b1 = static_cast<uint8_t>(text[++i]);
      unsigned cp = 0xC0u + (b1 - 0x80u);
      if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) cp += 0x20;  // lower case
      char base = 0;
      if (cp >= 0xE0 && cp <= 0xE5)
        base = 'a';
      else if (cp == 0xE7)
        base = 'c';
      else if (cp >= 0xE8 && cp <= 0xEB)
        base = 'e';
      else if (cp >= 0xEC && cp <= 0xEF)
        base = 'i';
      else if (cp == 0xF1)
        base = 'n';
      else if ((cp >= 0xF2 && cp <= 0xF6) || cp == 0xF8)
        base = 'o';
      else if (cp >= 0xF9 && cp <= 0xFC)
        base = 'u';
      else if (cp == 0xFD || cp == 0xFF)
        base = 'y';
      if (base != 0) {
        out += base;
      } else {
        out += static_cast<char>(0xC3);
        out += static_cast<char>(0x80 + (cp - 0xC0));
      }
    } else if (b0 == 0xC5 && i + 1 < text.size()) {  // o and u with double acute
      const auto b1 = static_cast<uint8_t>(text[++i]);
      if (b1 == 0x90 || b1 == 0x91)
        out += 'o';
      else if (b1 == 0xB0 || b1 == 0xB1)
        out += 'u';
      else {
        out += static_cast<char>(b0);
        out += static_cast<char>(b1);
      }
    } else {
      out += static_cast<char>(b0);
    }
  }
  return out;
}

namespace {
// Every space-separated word of `key` is somewhere in `hay` (both already
// searchKeys).
bool hasAllWords(const std::string& hay, const std::string& key) {
  size_t i = 0;
  bool any = false;
  while (i < key.size()) {
    const size_t e = key.find(' ', i);
    const size_t end = e == std::string::npos ? key.size() : e;
    if (end > i) {
      any = true;
      if (hay.find(key.substr(i, end - i)) == std::string::npos) return false;
    }
    i = end + 1;
  }
  return any;
}
}  // namespace

bool matchesQuery(const Event& event, const std::string& key) {
  return hasAllWords(searchKey(event.title + " " + event.location + " " + event.note), key);
}

bool matchesQuery(const Task& task, const std::string& key) {
  return hasAllWords(searchKey(task.title + " " + task.note), key);
}

// --- Settings
// -------------------------------------------------------------------

namespace {
// The number after "key=" at the start of a line, or `fallback`.
int settingNumber(const std::string& text, const char* key, const int fallback) {
  const std::string tag = std::string(key) + "=";
  size_t at = text.find(tag);
  while (at != std::string::npos && at > 0 && text[at - 1] != '\n') at = text.find(tag, at + 1);
  return at == std::string::npos ? fallback : std::atoi(text.c_str() + at + tag.size());
}
}  // namespace

Settings parseSettings(const std::string& text) {
  Settings s;
  const int first = settingNumber(text, "first", s.firstDay);
  s.firstDay = (first == 0 || first == 6) ? first : 1;
  s.weekNumbers = settingNumber(text, "weeks", s.weekNumbers) != 0;
  s.listHome = settingNumber(text, "list", s.listHome) != 0;
  s.weekHome = settingNumber(text, "grid", s.weekHome) != 0;
  s.agenda = settingNumber(text, "agenda", s.agenda) == 1;
  const int size = settingNumber(text, "size", s.agendaSize);
  s.agendaSize = size == 0 ? 0 : (size == 1 ? 1 : 2);  // Large unless told otherwise
  s.tomorrow = settingNumber(text, "tomorrow", 1) != 0;
  s.agendaBottom = settingNumber(text, "bottom", 0) == 1;
  return s;
}

std::string serializeSettings(const Settings& s) {
  char buf[160];
  std::snprintf(buf, sizeof buf,
                "first=%d\nweeks=%d\nlist=%d\ngrid=%d\nagenda=%d\nsize=%"
                "d\ntomorrow=%d\nbottom=%d\n",
                s.firstDay, s.weekNumbers ? 1 : 0, s.listHome ? 1 : 0, s.weekHome ? 1 : 0, s.agenda ? 1 : 0,
                s.agendaSize, s.tomorrow ? 1 : 0, s.agendaBottom ? 1 : 0);
  return buf;
}

// --- Sleep-screen agenda
// --------------------------------------------------------

bool eventOver(const Event& event, const Occurrence& occurrence, const Day today, const int nowMinutes) {
  if (event.allDay || occurrence.end > today) return false;
  int endMinute = 24 * 60;
  if (occurrence.start < today) {  // the last day of one that began earlier
    if (event.endMin > 0) endMinute = event.endMin;
  } else {
    endMinute = event.endMin > event.startMin ? event.endMin : event.startMin + 60;
  }
  return nowMinutes >= endMinute;
}

namespace {
std::string agendaWhen(const Event& e, const Occurrence& o, const Day day, const bool twelveHour) {
  if (e.allDay) return "";  // no time to give: the title stands alone
  if (o.start < day) {      // a multi-day event already under way
    if (o.end == day && e.endMin > 0) return "until " + formatMinutes(e.endMin, twelveHour);
    return "";
  }
  return formatMinutes(e.startMin, twelveHour);
}

std::string agendaEvent(const Event& e, const Occurrence& o, const Day day, const bool twelveHour) {
  const std::string when = agendaWhen(e, o, day, twelveHour);
  return (when.empty() ? "" : when + " - ") + foldForDisplay(e.title);
}
}  // namespace

Agenda buildAgenda(const std::vector<Event>& events, const std::vector<Task>& tasks, const Day today,
                   const int nowMinutes, const bool twelveHour) {
  Agenda a;
  std::vector<Occurrence> occ;
  occurrencesOn(events, today, occ);
  for (const Occurrence& o : occ) {
    const Event& e = events[o.event];
    if (eventOver(e, o, today, nowMinutes)) {
      a.pastEvents = true;
      continue;
    }
    a.today.push_back({AgendaKind::Event, agendaEvent(e, o, today, twelveHour)});
  }
  std::vector<size_t> ix;
  tasksDueOn(tasks, today, ix);
  for (const size_t i : ix) {
    if (!tasks[i].done) a.today.push_back({AgendaKind::Task, foldForDisplay(tasks[i].title)});
  }
  overdueTasks(tasks, today, ix);
  a.overdue = ix.size();

  occurrencesOn(events, today + 1, occ);
  for (const Occurrence& o : occ) {
    const Event& e = events[o.event];
    a.tomorrow.push_back({AgendaKind::Event, agendaEvent(e, o, today + 1, twelveHour)});
  }
  tasksDueOn(tasks, today + 1, ix);
  for (const size_t i : ix) {
    if (!tasks[i].done) a.tomorrow.push_back({AgendaKind::Task, foldForDisplay(tasks[i].title)});
  }
  return a;
}

namespace {
int lineHeight(const AgendaMetrics& m, const AgendaStyle s) {
  switch (s) {
    case AgendaStyle::Event:
    case AgendaStyle::Task:
      return m.item;
    case AgendaStyle::Strong:
      return m.strong;
    case AgendaStyle::Summary:
      return m.summary;
    case AgendaStyle::Rule:
      return m.rule;
    case AgendaStyle::TomHead:
      return m.tomHead;
    case AgendaStyle::TomEvent:
    case AgendaStyle::TomTask:
      return m.tomItem;
    case AgendaStyle::More:
      return m.more;
  }
  return 0;
}
}  // namespace

std::vector<AgendaLine> layoutAgenda(const Agenda& agenda, const AgendaMetrics& m, const std::string& tomorrowTitle,
                                     const AgendaOptions& opt) {
  if (opt.size == AgendaSize::Small) {
    std::string s = "Due: " + std::to_string(agenda.overdue) + "   Today: " + std::to_string(agenda.today.size());
    if (opt.tomorrow) s += "   Tomorrow: " + std::to_string(agenda.tomorrow.size());
    return {{AgendaStyle::Summary, s}};
  }

  // Tomorrow's block first: its height comes off what today may use.
  std::vector<AgendaLine> tom;
  if (opt.tomorrow && !agenda.tomorrow.empty()) {
    const size_t cap = opt.evening ? 8 : 4;
    size_t n = std::min(agenda.tomorrow.size(), cap);
    const int fixed = m.rule + m.tomHead;
    const int minToday = m.strong + m.item + m.more;  // today keeps room for a row
    const auto height = [&](const size_t rows) {
      return fixed + static_cast<int>(rows) * m.tomItem + (agenda.tomorrow.size() > rows ? m.more : 0);
    };
    while (n > 0 && height(n) > m.avail - minToday) --n;
    if (n > 0) {
      tom.push_back({AgendaStyle::Rule, ""});
      tom.push_back({AgendaStyle::TomHead, tomorrowTitle});
      for (size_t i = 0; i < n; ++i) {
        tom.push_back({agenda.tomorrow[i].kind == AgendaKind::Task ? AgendaStyle::TomTask : AgendaStyle::TomEvent,
                       agenda.tomorrow[i].text});
      }
      if (agenda.tomorrow.size() > n) {
        tom.push_back({AgendaStyle::More, "+" + std::to_string(agenda.tomorrow.size() - n) + " more"});
      }
    }
  }
  int tomH = 0;
  for (const AgendaLine& l : tom) tomH += lineHeight(m, l.style);

  // Today's rows, in order: the overdue count, events, then to-dos.
  std::vector<AgendaLine> rows;
  if (agenda.overdue > 0) rows.push_back({AgendaStyle::Strong, std::to_string(agenda.overdue) + " overdue"});
  for (const AgendaItem& it : agenda.today) {
    rows.push_back({it.kind == AgendaKind::Task ? AgendaStyle::Task : AgendaStyle::Event, it.text});
  }

  const int room = m.avail - tomH;
  const size_t itemCap = opt.evening ? 4 : agenda.today.size() + 1;
  std::vector<AgendaLine> out;
  int used = 0;
  size_t items = 0;
  size_t shown = 0;
  for (; shown < rows.size(); ++shown) {
    const AgendaLine& r = rows[shown];
    const bool counts = r.style != AgendaStyle::Strong && !agenda.today.empty();
    const int need = lineHeight(m, r.style);
    const int reserve = shown + 1 >= rows.size() ? 0 : m.more;  // keep room for "+N more" while rows remain
    if (shown > 0 && used + need + reserve > room) break;       // never show nothing
    if (counts && items >= itemCap) break;
    used += need;
    if (counts) ++items;
    out.push_back(r);
  }
  const size_t hidden = agenda.today.empty() ? 0 : rows.size() - shown;
  if (hidden > 0) out.push_back({AgendaStyle::More, "+" + std::to_string(hidden) + " more"});
  for (const AgendaLine& l : tom) {
    if (out.empty() && l.style == AgendaStyle::Rule) continue;  // nothing above it to divide from
    out.push_back(l);
  }
  return out;
}

}  // namespace daybook
