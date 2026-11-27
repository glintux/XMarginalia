#pragma once

// Calendar rules and storage format. Freestanding C++17: no Arduino, no
// renderer, no SD card, so host-tests/calendar drives all of it on a PC.
//
// A day is a number: days since 1970-01-01 in the device's LOCAL calendar.
// The activity turns the clock into one (via localtime_r); nothing in here
// knows about timezones, so DST can never move an event.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace daybook {

using Day = int32_t;

struct Date {
  int y = 1970;
  int m = 1;  // 1..12
  int d = 1;  // 1..31
};

Day daysFromCivil(int y, int m, int d);
Date civilFromDays(Day day);
int weekday(Day day);  // 0 = Sunday
int daysInMonth(int y, int m);
int isoWeek(Day day);  // 1..53, ISO 8601
// The same day number `months` later (or earlier); the day clamps to the month
// end (31st -> 30th/28th).
Day addMonths(Day day, int months);
// The days the file format can hold: 1970-01-01 to 2200-12-31. A date outside
// them would not be read back.
Day clampDay(Day day);

enum class Repeat : uint8_t { None, Daily, Weekly, Monthly, Yearly };

constexpr size_t kMaxEvents = 400;
constexpr size_t kMaxTitle = 60;
constexpr size_t kMaxLocation = 60;
constexpr size_t kMaxNote = 240;
constexpr size_t kMaxFileBytes = 96 * 1024;

struct Event {
  uint32_t id = 0;
  std::string title;
  std::string location;
  std::string note;
  Day start = 0;  // first day
  Day end = 0;    // last day, inclusive; never before start
  bool allDay = true;
  int16_t startMin = 0;  // minutes after midnight, used when !allDay
  int16_t endMin = 0;    // 0 with !allDay means "no end time"
  Repeat repeat = Repeat::None;
  uint16_t every = 1;  // every N days / weeks / months / years
  bool hasUntil = false;
  Day until = 0;  // last day an occurrence may START on
};

// One showing of an event: [start, end] in days. Repeats shift both.
struct Occurrence {
  size_t event = 0;  // index into the event list
  Day start = 0;
  Day end = 0;
};

// Every occurrence overlapping [from, to], unsorted.
void occurrencesIn(const std::vector<Event>& events, Day from, Day to, std::vector<Occurrence>& out);

// The occurrences touching `day`: all-day first, then by time, then title.
void occurrencesOn(const std::vector<Event>& events, Day day, std::vector<Occurrence>& out);

// A fixed-size answer for a month grid: bit i of the result is set when
// day (first + i) has at least one occurrence. Covers 42 days.
uint64_t busyDays(const std::vector<Event>& events, Day first);

// "09:30" or "9:30 AM".
std::string formatMinutes(int minutes, bool twelveHour);
// "2026-11-25".
std::string formatDate(Day day);
// "11.20-11.24" style short span for a multi-day event.
std::string formatSpan(Day start, Day end);
// Parses "YYYY-MM-DD"; false when it is not a real date.
bool parseDate(const std::string& text, Day& out);

// What the title fonts can draw: Latin-1 is kept; o and u with double acute
// (Hungarian) become o and u with umlaut; anything else past Latin-1 becomes
// '?'. Display only: what is stored keeps the real letters.
std::string foldForDisplay(const std::string& utf8);

// Text-file form, one event per line, tab separated, text escaped.
std::string serialize(const std::vector<Event>& events);
// Reads that form. Bad lines are skipped, never fatal; returns the count kept.
size_t parse(const std::string& text, std::vector<Event>& out);

// The text with its first letter in capitals: a-z, Latin-1 lowercase, and the
// Hungarian double-acute o and u. Anything else is left alone. Applied when a
// title is saved, so a title always starts with a capital.
std::string capitalizeFirst(const std::string& text);

// The next unused id.
uint32_t nextId(const std::vector<Event>& events);

// --- To-dos
// -------------------------------------------------------------------
//
// A task belongs to one list. It has a created day, an optional due day, and
// a done day once it is ticked. A task with a due day also shows on the
// calendar; an open one whose due day has passed is "overdue".

constexpr size_t kMaxTasks = 500;
constexpr size_t kMaxLists = 14;
constexpr size_t kMaxTaskTitle = 60;
constexpr size_t kMaxTaskNote = 240;
constexpr size_t kMaxListName = 30;
constexpr size_t kMaxTaskFileBytes = 160 * 1024;
constexpr uint32_t kMainList = 1;  // the default list: can be renamed, never removed
constexpr const char* kMainListName = "Main To-Do's";

struct Task {
  uint32_t id = 0;
  uint32_t list = kMainList;
  std::string title;
  std::string note;
  Day created = 0;
  bool hasDue = false;
  Day due = 0;
  bool done = false;
  Day doneOn = 0;
  int doneMin = -1;  // minute of the day it was ticked, -1 when not known
};

struct TaskList {
  uint32_t id = 0;
  std::string name;
};

bool isOverdue(const Task& task,
               Day today);  // open, has a due day, due day before today

std::string serializeTasks(const std::vector<Task>& tasks);
size_t parseTasks(const std::string& text, std::vector<Task>& out);
std::string serializeLists(const std::vector<TaskList>& lists);
size_t parseLists(const std::string& text, std::vector<TaskList>& out);

// Makes the pair safe to use: the default list exists (first, named
// kMainListName if the file had none), names are not empty, and a task whose
// list is gone moves to the default list.
void repairLists(std::vector<TaskList>& lists, std::vector<Task>& tasks);

uint32_t nextTaskId(const std::vector<Task>& tasks);
uint32_t nextListId(const std::vector<TaskList>& lists);
// Index of the list with this id, or 0 when there is none.
size_t listIndex(const std::vector<TaskList>& lists, uint32_t id);

// Indexes into `tasks`. One list in screen order: open ones (due soonest first,
// undated last), then done ones (latest first).
void tasksOfList(const std::vector<Task>& tasks, uint32_t list, std::vector<size_t>& out);
// Every task due on `day`, open ones first.
void tasksDueOn(const std::vector<Task>& tasks, Day day, std::vector<size_t>& out);
// Open tasks past their due day, oldest due first.
void overdueTasks(const std::vector<Task>& tasks, Day today, std::vector<size_t>& out);
// Like busyDays for tasks: bit i of `any` is set when a task is due on
// (first + i); bit i of `open` when one of them is not done. 42 days.
void taskBusy(const std::vector<Task>& tasks, Day first, uint64_t& any, uint64_t& open);

// --- Search
// -------------------------------------------------------------------
//
// Matching ignores case and accents: "arvizturo" finds "Arvizturo", "ARVIZTURO"
// and the Hungarian spelling with accents. Several words must all appear (in
// any order).

// Lower case, accents dropped (Latin-1 letters and the Hungarian double acute o
// and u), for matching only.
std::string searchKey(const std::string& text);
// True when every word of `key` (already a searchKey) is somewhere in the
// event's title, place or note.
bool matchesQuery(const Event& event, const std::string& key);
// The same for a task's title and note.
bool matchesQuery(const Task& task, const std::string& key);

// --- Sleep-screen agenda
// --------------------------------------------------------
//
// What the sleep screen shows: today's events that are not over yet, today's
// open to-dos, a count of overdue ones, and (optionally) tomorrow's events and
// to-dos. The planner below fits it in a given height, so the layout is tested
// on the host; the drawing is the only part left on the device.

enum class AgendaKind : uint8_t { Event, Task };
struct AgendaItem {
  AgendaKind kind = AgendaKind::Event;
  std::string text;  // "09:30  Dentist" or the task's title; already folded for display
};
struct Agenda {
  std::vector<AgendaItem> today;     // events not over yet, then open tasks due today
  std::vector<AgendaItem> tomorrow;  // events, then open tasks due tomorrow
  size_t overdue = 0;                // open tasks past their due day
  bool pastEvents = false;           // some of today's events are already over (and hidden)
};

// A timed event is over at its end time; with no end time, an hour after it
// starts. A multi-day one that began earlier runs to its end time on the last
// day. All-day events are never over before midnight.
bool eventOver(const Event& event, const Occurrence& occurrence, Day today, int nowMinutes);
Agenda buildAgenda(const std::vector<Event>& events, const std::vector<Task>& tasks, Day today, int nowMinutes,
                   bool twelveHour);

enum class AgendaStyle : uint8_t {
  Event,     // an event of today (nothing in front of it)
  Task,      // an open to-do of today (drawn with an empty box in front of it)
  Strong,    // "2 overdue"
  Summary,   // Small size: "Due: 2   Today: 3   Tomorrow: 4"
  Rule,      // a thin line between today and tomorrow
  TomHead,   // "Tomorrow, Sat 28 Nov"
  TomEvent,  // tomorrow's rows
  TomTask,
  More,  // "+3 more"
};
struct AgendaLine {
  AgendaStyle style = AgendaStyle::Event;
  std::string text;
};
struct AgendaMetrics {  // pixel heights of one line of each style, and the
                        // height there is to fill
  int item = 0;         // Event and Task
  int strong = 0, summary = 0, rule = 0, tomHead = 0;
  int tomItem = 0;  // TomEvent and TomTask
  int more = 0;
  int avail = 0;
};
// How much the sleep screen says. Large and Normal list the day; Small only
// counts.
enum class AgendaSize : uint8_t { Small, Normal, Large };
struct AgendaOptions {
  AgendaSize size = AgendaSize::Large;
  bool evening = false;  // late enough that tomorrow is what matters
  bool tomorrow = true;  // show tomorrow at all
};
// The lines to draw, top to bottom, whose heights add up to at most `avail`. In
// the evening tomorrow gets up to 8 rows and today at most 4; otherwise
// tomorrow gets up to 4 and today the rest of the room. Small gives one Summary
// line.
std::vector<AgendaLine> layoutAgenda(const Agenda& agenda, const AgendaMetrics& metrics,
                                     const std::string& tomorrowTitle, const AgendaOptions& options);
constexpr int kEveningMinute = 21 * 60;

// --- Settings
// -----------------------------------------------------------------
//
// settings.txt: "key=value" lines. The app writes it; the sleep screen reads
// it. Unknown keys are ignored and a missing one keeps its default, so an older
// file still works.

constexpr size_t kMaxSettingsBytes = 512;

struct Settings {
  int firstDay = 1;  // 0 Sunday, 1 Monday, 6 Saturday
  bool weekNumbers = true;
  bool listHome = false;  // the home screen is the upcoming list...
  bool weekHome = false;  // ...or else a week (true) or a month (false)
  bool agenda = false;    // draw the agenda on the sleep screen
  int agendaSize = 2;     // 0 Small, 1 Normal, 2 Large
  bool tomorrow = true;   // with tomorrow under today
  bool agendaBottom = false;
};
Settings parseSettings(const std::string& text);
std::string serializeSettings(const Settings& settings);

}  // namespace daybook
