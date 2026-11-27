#include "DaybookActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#ifndef SIMULATOR
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <variant>

#include "../../CrossPointSettings.h"
#include "../../activities/ActivityResult.h"
#include "../../activities/util/KeyboardEntryActivity.h"
#include "../../components/HeaderBackTapTarget.h"
#include "../../components/UITheme.h"
#include "../ClockFloor.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "DaybookDraw.h"
#include "DaybookPlugin.h"

namespace cal = daybook;

namespace {

constexpr const char* kTag = "CAL";
constexpr const char* kDir = "/.crosspoint/daybook";
constexpr const char* kEventsPath = "/.crosspoint/daybook/events.txt";
constexpr const char* kSettingsPath = "/.crosspoint/daybook/settings.txt";
constexpr const char* kTasksPath = "/.crosspoint/daybook/tasks.txt";
constexpr const char* kListsPath = "/.crosspoint/daybook/lists.txt";
constexpr int kM = 12;  // side margin

constexpr const char* kDow[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
constexpr const char* kDowLong[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
constexpr const char* kMonthName[] = {"January", "February", "March",     "April",   "May",      "June",
                                      "July",    "August",   "September", "October", "November", "December"};
constexpr const char* kMonthShort[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
constexpr const char* kRepeatName[] = {"Never", "Daily", "Weekly", "Monthly", "Yearly"};

constexpr int kBody = toybox::kReadingFontId;
constexpr int kSmall = toybox::kReadingSmallFontId;
constexpr int kBold = toybox::kReadingBoldFontId;
constexpr int kBoldSmall = toybox::kReadingBoldSmallFontId;

// Same two-step as Weather: a write lands beside itself and is renamed, so a
// power cut leaves the old file, never a half one.
bool readText(const char* path, size_t maxBytes, std::string& out) {
  out.clear();
  if (Storage.exists(path)) return Storage.readFileToString(kTag, path, maxBytes, out);
  const std::string tmp = std::string(path) + ".tmp";
  return Storage.exists(tmp.c_str()) && Storage.readFileToString(kTag, tmp, maxBytes, out);
}

bool writeText(const char* path, const std::string& text) {
  Storage.ensureDirectoryExists(kDir);
  const std::string tmp = std::string(path) + ".tmp";
  {
    HalFile file;
    if (!Storage.openFileForWrite(kTag, tmp.c_str(), file)) return false;
    if (file.write(text.data(), text.size()) != text.size()) {
      file.close();
      Storage.remove(tmp.c_str());
      return false;
    }
    file.flush();
  }
  if (!Storage.replaceFile(tmp.c_str(), path)) {
    if (Storage.exists(path)) Storage.remove(tmp.c_str());
    return false;
  }
  return true;
}

using daybook::addMonths;
}  // namespace

std::string DaybookActivity::weekdayName(const cal::Day day) { return kDowLong[cal::weekday(day)]; }

// "Thu 26 Nov 2026", or "Thu 26 Nov" without the year.
namespace {
std::string dayTitle(const cal::Day day, const bool withYear = true) {
  const cal::Date d = cal::civilFromDays(day);
  char buf[40];
  if (withYear) {
    std::snprintf(buf, sizeof buf, "%s %d %s %d", kDow[cal::weekday(day)], d.d, kMonthShort[d.m - 1], d.y);
  } else {
    std::snprintf(buf, sizeof buf, "%s %d %s", kDow[cal::weekday(day)], d.d, kMonthShort[d.m - 1]);
  }
  return buf;
}

// The time column of a row: "All day", "09:00-10:00", or the span of a
// multi-day event.
std::string shortWhen(const cal::Event& ev, const cal::Occurrence& o, const bool twelveHour) {
  if (o.end > o.start) return cal::formatSpan(o.start, o.end);
  if (ev.allDay) return "All day";
  std::string s = cal::formatMinutes(ev.startMin, twelveHour);
  if (ev.endMin > ev.startMin) s += "-" + cal::formatMinutes(ev.endMin, twelveHour);
  return s;
}

const char* unitName(const cal::Repeat r, const bool plural) {
  switch (r) {
    case cal::Repeat::Daily:
      return plural ? "days" : "day";
    case cal::Repeat::Weekly:
      return plural ? "weeks" : "week";
    case cal::Repeat::Monthly:
      return plural ? "months" : "month";
    case cal::Repeat::Yearly:
      return plural ? "years" : "year";
    default:
      return "";
  }
}

}  // namespace

std::unique_ptr<Activity> DaybookActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<DaybookActivity>(renderer, mappedInput);
}

// --- Lifecycle and storage
// ------------------------------------------------------------------

void DaybookActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);  // a wake lands here directly, not through the
                                  // Shelf screen that registers them
  if (started_) {                 // back from the keyboard screen: keep everything as it was
    changed();
    return;
  }
  started_ = true;
  loadAll();
  refreshToday();
  selected_ = today_;
  openMonthOf(today_);
  view_ = home();
  hitsReady_ = false;
  requestUpdate();
}

void DaybookActivity::loadAll() {
  std::string text;
  events_.clear();
  if (readText(kEventsPath, cal::kMaxFileBytes, text)) cal::parse(text, events_);
  if (readText(kSettingsPath, cal::kMaxSettingsBytes, text)) {
    const cal::Settings s = cal::parseSettings(text);
    firstDay_ = s.firstDay;
    weekNums_ = s.weekNumbers;
    listMode_ = s.listHome;
    weekMode_ = s.weekHome;
    agenda_ = s.agenda;
    agendaSize_ = s.agendaSize;
    tomorrow_ = s.tomorrow;
    agendaBottom_ = s.agendaBottom;
  }
  tasks_.clear();
  lists_.clear();
  if (readText(kTasksPath, cal::kMaxTaskFileBytes, text)) cal::parseTasks(text, tasks_);
  if (readText(kListsPath, cal::kMaxTaskFileBytes, text)) cal::parseLists(text, lists_);
  cal::repairLists(lists_, tasks_);  // the default list always exists
  clampList();
}

bool DaybookActivity::saveEvents() { return writeText(kEventsPath, cal::serialize(events_)); }
bool DaybookActivity::saveTasks() { return writeText(kTasksPath, cal::serializeTasks(tasks_)); }
bool DaybookActivity::saveLists() { return writeText(kListsPath, cal::serializeLists(lists_)); }

void DaybookActivity::saveSettings() {
  cal::Settings s;
  s.firstDay = firstDay_;
  s.weekNumbers = weekNums_;
  s.listHome = listMode_;
  s.weekHome = weekMode_;
  s.agenda = agenda_;
  s.agendaSize = agendaSize_;
  s.tomorrow = tomorrow_;
  s.agendaBottom = agendaBottom_;
  writeText(kSettingsPath, cal::serializeSettings(s));
}

int DaybookActivity::nowMinute() const {
  const std::time_t now = std::time(nullptr);
  std::tm t{};
  localtime_r(&now, &t);
  return t.tm_hour * 60 + t.tm_min;
}

void DaybookActivity::refreshToday() {
  const std::time_t now = std::time(nullptr);
  clockKnown_ = clockfloor::trusted(static_cast<int64_t>(now));
  if (clockKnown_) {
    std::tm t{};
    localtime_r(&now, &t);
    today_ = cal::daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  } else {
    today_ = cal::daysFromCivil(2026, 1,
                                1);  // the clock is not set: no "today" is drawn
  }
}

bool DaybookActivity::twelveHour() const { return SETTINGS.clockFormat == 1; }

void DaybookActivity::openMonthOf(const cal::Day day) {
  const cal::Date d = cal::civilFromDays(day);
  year_ = d.y;
  month_ = d.m;
}

void DaybookActivity::changed() {
  hitsReady_ = false;
  requestUpdate();
}

// --- Input
// -------------------------------------------------------------------------------------

void DaybookActivity::back() {
  switch (view_) {
    case View::Month:
    case View::Week:
    case View::List:
      shelf::leave(renderer, mappedInput);
      return;
    case View::Search:
      view_ = home();
      break;
    case View::Pick:
      if (pickYears_) {
        pickYears_ = false;
        page_ = 0;
      } else {
        view_ = home();
      }
      break;
    case View::Day:
      view_ = home();
      break;
    case View::Detail:
      confirmDelete_ = false;
      view_ = detailFrom_;
      break;
    case View::Form:
      view_ = (editIndex_ >= 0 && detailIndex_ >= 0 && detailIndex_ < static_cast<int>(events_.size())) ? View::Detail
                                                                                                        : formFrom_;
      break;
    case View::Settings:
      view_ = home();
      break;
    case View::Todo:
      view_ = home();
      break;
    case View::Lists:
      view_ = View::Todo;
      break;
    case View::TaskForm:
      confirmDelete_ = false;
      view_ = taskFrom_;
      break;
    case View::ListForm:
      confirmDelete_ = false;
      view_ = View::Lists;
      break;
  }
  changed();
}

void DaybookActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    back();
    return;
  }
  if (!hitsReady_.load()) return;

  // Only up/down swipes page. A sideways swipe from the left edge is the Back
  // gesture, so it is not used here: months and lists change with the on-screen
  // buttons and rows.
  const MappedInputManager::SwipeDir swipe = mappedInput.wasSwipe();
  if (view_ == View::Day || view_ == View::List || view_ == View::Todo || view_ == View::Search ||
      view_ == View::Settings || view_ == View::Lists || (view_ == View::Pick && pickYears_)) {
    if (swipe == MappedInputManager::SwipeDir::Up) return route(Act::PageNext, 0);
    if (swipe == MappedInputManager::SwipeDir::Down) return route(Act::PagePrev, 0);
  }

  // Buttons act when the finger goes DOWN, not when it lifts: that is the whole
  // difference between a tap that feels instant and one that feels late. On the
  // screens that swipe (month, list, day, to-do) the rows and day cells still
  // wait for the lift, because a swipe starts on top of them; the buttons there
  // act on the press.
  const bool swipeScreen = view_ == View::Month || view_ == View::Week || view_ == View::List || view_ == View::Day ||
                           view_ == View::Todo || view_ == View::Search || view_ == View::Lists ||
                           view_ == View::Settings || view_ == View::Pick;
  // Where a page swipe can start on a button, every button waits for the lift.
  const bool allOnLift = view_ == View::Lists || view_ == View::Settings || view_ == View::Pick;
  int downX = 0;
  int downY = 0;
  if (mappedInput.wasScreenTouchPressed(downX, downY) && !HeaderBackTapTarget::contains(downX, downY)) {
    const Hit* hit = hitAt(downX, downY);
    if (hit && !(swipeScreen && (allOnLift || isRowAct(hit->act)))) {
      mappedInput.swallowCurrentTouch();  // the lift must not tap what the press
                                          // just did
      route(hit->act, hit->value);
      return;
    }
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;
  if (const Hit* hit = hitAt(tapX, tapY)) route(hit->act, hit->value);
}

const DaybookActivity::Hit* DaybookActivity::hitAt(const int x, const int y) const {
  for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
    if (x >= it->x && x < it->x + it->w && y >= it->y && y < it->y + it->h) return &*it;
  }
  return nullptr;
}

bool DaybookActivity::isRowAct(const Act act) {
  return act == Act::PickDay || act == Act::OpenDay || act == Act::OpenEvent || act == Act::EditTask ||
         act == Act::ToggleTask || act == Act::GoList;
}

void DaybookActivity::startDraft(const int index) {
  draftTodo_ = false;
  draftList_ = cal::kMainList;
  if (index >= 0 && index < static_cast<int>(events_.size())) {
    draft_ = events_[static_cast<size_t>(index)];
    editIndex_ = index;
  } else {
    draft_ = cal::Event{};
    draft_.start = draft_.end = selected_;
    draft_.allDay = true;
    draft_.startMin = 9 * 60;
    draft_.endMin = 10 * 60;
    editIndex_ = -1;
  }
}

void DaybookActivity::route(const Act act, const int32_t value) {
  if (act == Act::Nop) return;
  message_.clear();
  if (act == Act::Save || act == Act::ToggleTask) trash_ = Trash{};  // something new is saved: Undo is gone
  if (routeTodo(act, value)) return;
  switch (act) {
    case Act::PrevMonth:
      if (view_ == View::Week) {  // a week at a time
        selected_ -= 7;
        openMonthOf(selected_);
        page_ = 0;
        break;
      }
      if (--month_ < 1) {
        month_ = 12;
        --year_;
      }
      page_ = 0;
      break;
    case Act::NextMonth:
      if (view_ == View::Week) {
        selected_ += 7;
        openMonthOf(selected_);
        page_ = 0;
        break;
      }
      if (++month_ > 12) {
        month_ = 1;
        ++year_;
      }
      page_ = 0;
      break;
    case Act::Today:
      refreshToday();
      selected_ = today_;
      openMonthOf(today_);
      page_ = 0;
      break;
    case Act::ToggleList:
      listMode_ = !listMode_;
      view_ = home();
      page_ = 0;
      saveSettings();
      break;
    case Act::SetGrid:                          // value 0 = month, 1 = week
      if (value == 1 && view_ != View::Week) {  // the week of the selected day, if
                                                // it is in the month on screen
        const cal::Date d = cal::civilFromDays(selected_);
        if (d.y != year_ || d.m != month_) {
          const cal::Date t = cal::civilFromDays(today_);
          selected_ = (clockKnown_ && t.y == year_ && t.m == month_) ? today_ : cal::daysFromCivil(year_, month_, 1);
        }
      } else if (value != 1 && view_ == View::Week) {
        openMonthOf(selected_);
      }
      listMode_ = false;
      weekMode_ = value == 1;
      view_ = home();
      page_ = 0;
      saveSettings();
      break;
    case Act::OpenSearch:
      search_.clear();
      page_ = 0;
      view_ = View::Search;
      askSearch();
      return;
    case Act::EditSearch:
      askSearch();
      return;
    case Act::Undo:
      restoreTrash();
      break;
    case Act::Settings:
      page_ = 0;
      pluginState_ = daybook::plugin::state();
      view_ = View::Settings;
      break;
    case Act::PickDay: {  // a day in the month grid: the first tap picks it, a
                          // second tap opens it
      const bool again = value == selected_;
      selected_ = value;
      openMonthOf(value);
      page_ = 0;
      if (again) view_ = View::Day;
      break;
    }
    case Act::OpenDay:
      selected_ = value;
      page_ = 0;
      view_ = View::Day;
      break;
    case Act::OpenPicker:  // the title: months of a year, and from there the years
      if (view_ == View::Pick) {
        pickYears_ = true;
        page_ = (std::clamp(pickYear_, 1970, 2200) - 1970) / 20;
      } else {
        pickYear_ = year_;
        pickYears_ = false;
        page_ = 0;
        view_ = View::Pick;
      }
      break;
    case Act::PickYear:
      pickYear_ = std::clamp(static_cast<int>(value), 1970, 2200);
      pickYears_ = false;
      page_ = 0;
      break;
    case Act::PickMonth: {
      year_ = pickYear_;
      month_ = static_cast<int>(value);
      const cal::Date t = cal::civilFromDays(today_);
      selected_ = (clockKnown_ && t.y == year_ && t.m == month_) ? today_ : cal::daysFromCivil(year_, month_, 1);
      page_ = 0;
      view_ = home();
      break;
    }
    case Act::AddEvent:
      formFrom_ = view_;
      startDraft(-1);
      detailIndex_ = -1;
      view_ = View::Form;
      break;
    case Act::OpenEvent:
      detailFrom_ = (view_ == View::List || view_ == View::Week || view_ == View::Search) ? view_ : View::Day;
      detailIndex_ = value;
      confirmDelete_ = false;
      view_ = View::Detail;
      break;
    case Act::Edit:
      startDraft(detailIndex_);
      view_ = View::Form;
      break;
    case Act::Delete:
      confirmDelete_ = true;
      break;
    case Act::CancelDelete:
      confirmDelete_ = false;
      break;
    case Act::ConfirmDelete:
      if (detailIndex_ >= 0 && detailIndex_ < static_cast<int>(events_.size())) {
        discard(Trash::Kind::Event, detailIndex_, events_[static_cast<size_t>(detailIndex_)].title);
        trash_.event = events_[static_cast<size_t>(detailIndex_)];
        events_.erase(events_.begin() + detailIndex_);
        if (!saveEvents()) message_ = "Card write failed";
      }
      detailIndex_ = -1;
      confirmDelete_ = false;
      page_ = 0;
      view_ = detailFrom_;
      break;
    case Act::Save: {
      if (draft_.title.empty()) {
        message_ = "Give it a name first";
        break;
      }
      if (draftTodo_ && editIndex_ < 0) {  // ticked "To-Do": it becomes a task due on that day
        if (!saveDraftAsTask()) break;
        detailIndex_ = -1;
        page_ = 0;
        view_ = View::Day;
        break;
      }
      if (draft_.end < draft_.start) draft_.end = draft_.start;
      if (!draft_.allDay && draft_.end == draft_.start && draft_.endMin < draft_.startMin) {
        draft_.endMin = draft_.startMin;
      }
      if (draft_.repeat == cal::Repeat::None) draft_.hasUntil = false;
      if (editIndex_ >= 0 && editIndex_ < static_cast<int>(events_.size())) {
        events_[static_cast<size_t>(editIndex_)] = draft_;
      } else {
        if (events_.size() >= cal::kMaxEvents) {
          message_ = "Calendar is full";
          break;
        }
        draft_.id = cal::nextId(events_);
        events_.push_back(draft_);
        selected_ = draft_.start;
        openMonthOf(selected_);
      }
      if (!saveEvents()) message_ = "Card write failed";
      detailIndex_ = -1;
      page_ = 0;
      view_ = View::Day;
      break;
    }
    case Act::Cancel:
      back();
      return;
    case Act::Form:
      if ((value >> 3) <= FNote) {
        askText(static_cast<Field>(value >> 3));
        return;
      }
      formTap(value >> 3, value & 7);
      break;
    case Act::PagePrev:
      if (page_ > 0) --page_;
      break;
    case Act::PageNext:
      if (page_ + 1 < pages_) ++page_;
      break;
    case Act::SetFirstDay:
      firstDay_ = firstDay_ == 1 ? 0 : (firstDay_ == 0 ? 6 : 1);
      saveSettings();
      break;
    case Act::ToggleWeek:
      weekNums_ = !weekNums_;
      saveSettings();
      break;
    case Act::ToggleAgenda:
      agenda_ = !agenda_;
      saveSettings();
      break;
    case Act::CycleSize:
      agendaSize_ = (agendaSize_ + 1) % 3;
      saveSettings();
      break;
    case Act::ToggleTomorrow:
      tomorrow_ = !tomorrow_;
      saveSettings();
      break;
    case Act::TogglePosition:
      agendaBottom_ = !agendaBottom_;
      saveSettings();
      break;
    case Act::InstallPlugin:
      message_ = daybook::plugin::install();
      if (message_.empty()) message_ = "Installed. Open File Transfer, then Plugins in the web page.";
      pluginState_ = daybook::plugin::state();
      break;
    case Act::OpenTodo:  // the to-do acts are all handled by routeTodo(), above
    case Act::OpenLists:
    case Act::SelectList:
    case Act::NewList:
    case Act::EditList:
    case Act::ListName:
    case Act::NewTask:
    case Act::NewDayTask:
    case Act::EditTask:
    case Act::ToggleTask:
    case Act::TaskField:
    case Act::GoList:
    case Act::Nop:  // returned above; listed so the switch covers every act
      break;
    case Act::Back:
      back();
      return;
  }
  changed();
}

void DaybookActivity::askString(const char* title, const std::string& current, const size_t limit,
                                std::function<void(const std::string&)> apply) {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, title, current, limit);
  if (!keyboard) {
    message_ = "Not enough memory for the keyboard";
    changed();
    return;
  }
  startActivityForResult(std::move(keyboard), [this, apply](const ActivityResult& result) {
    if (!result.isCancelled) {
      const std::string& text = std::get<KeyboardResult>(result.data).text;
      const size_t a = text.find_first_not_of(' ');
      const size_t b = text.find_last_not_of(' ');
      apply(a == std::string::npos ? std::string() : text.substr(a, b - a + 1));
    }
    changed();
  });
}

void DaybookActivity::askText(const Field field) {
  const char* title = field == FTitle ? "EVENT NAME" : (field == FLocation ? "PLACE" : "NOTE");
  const std::string current = field == FTitle ? draft_.title : (field == FLocation ? draft_.location : draft_.note);
  const size_t limit = field == FTitle ? cal::kMaxTitle : (field == FLocation ? cal::kMaxLocation : cal::kMaxNote);
  askString(title, current, limit, [this, field](const std::string& text) {
    if (field == FTitle)
      draft_.title = cal::capitalizeFirst(text);
    else if (field == FLocation)
      draft_.location = text;
    else
      draft_.note = text;
  });
}

void DaybookActivity::askSearch() {
  askString("FIND", search_, 40, [this](const std::string& text) {
    search_ = text;
    page_ = 0;
  });
}

// Something was just deleted: remember where it was, so the Undo bar can put it
// back.
void DaybookActivity::discard(const Trash::Kind kind, const int at, const std::string& label) {
  trash_ = Trash{};
  trash_.kind = kind;
  trash_.at = at;
  trash_.label = label;
}

void DaybookActivity::restoreTrash() {
  switch (trash_.kind) {
    case Trash::Kind::None:
      return;
    case Trash::Kind::Event:
      if (events_.size() >= cal::kMaxEvents) {
        message_ = "Calendar is full";
        return;
      }
      events_.insert(events_.begin() + std::min<size_t>(static_cast<size_t>(trash_.at), events_.size()), trash_.event);
      if (!saveEvents()) message_ = "Card write failed";
      break;
    case Trash::Kind::Task:
      if (tasks_.size() >= cal::kMaxTasks) {
        message_ = "To-Do list is full";
        return;
      }
      tasks_.insert(tasks_.begin() + std::min<size_t>(static_cast<size_t>(trash_.at), tasks_.size()), trash_.task);
      if (!saveTasks()) message_ = "Card write failed";
      break;
    case Trash::Kind::List:
      if (lists_.size() >= cal::kMaxLists) {
        message_ = "Lists are full";
        return;
      }
      lists_.insert(lists_.begin() + std::min<size_t>(static_cast<size_t>(trash_.at), lists_.size()), trash_.list);
      for (const cal::Task& t : trash_.tasks) {
        if (tasks_.size() < cal::kMaxTasks) tasks_.push_back(t);
      }
      if (!saveLists() || !saveTasks()) message_ = "Card write failed";
      break;
  }
  trash_ = Trash{};
}

cal::Day DaybookActivity::weekStart() const { return selected_ - ((cal::weekday(selected_) - firstDay_ + 7) % 7); }

bool DaybookActivity::saveDraftAsTask() {
  if (tasks_.size() >= cal::kMaxTasks) {
    message_ = "To-Do list is full";
    return false;
  }
  cal::Task t;
  t.id = cal::nextTaskId(tasks_);
  t.list = lists_[cal::listIndex(lists_, draftList_)].id;
  t.title = draft_.title;
  t.note = draft_.note;
  t.created = today_;
  t.hasDue = true;
  t.due = draft_.start;
  tasks_.push_back(t);
  if (!saveTasks()) message_ = "Card write failed";
  selected_ = draft_.start;
  openMonthOf(selected_);
  draftTodo_ = false;
  return true;
}

// zone: 0 = the whole row; 1..4 = "<<", "<", ">", ">>" (month/hour,
// day/quarter-hour)
void DaybookActivity::formTap(const int field, const int zone) {
  const int big = zone == 1 ? -1 : (zone == 4 ? 1 : 0);
  const int small = zone == 2 ? -1 : (zone == 3 ? 1 : 0);
  switch (field) {
    case FAllDay:
      draft_.allDay = !draft_.allDay;
      break;
    case FStartDate: {
      const cal::Day span = draft_.end - draft_.start;
      cal::Day next = draft_.start;
      if (big) next = addMonths(draft_.start, big);
      if (small) next = draft_.start + small;
      draft_.start = next;
      draft_.end = next + span;  // moving the start carries the end with it
      break;
    }
    case FEndDate: {
      cal::Day next = draft_.end;
      if (big) next = addMonths(draft_.end, big);
      if (small) next = draft_.end + small;
      draft_.end = std::max(next, draft_.start);
      break;
    }
    case FStartTime: {
      const int delta = big * 60 + small * 15;
      const int span = draft_.endMin - draft_.startMin;
      const int next = std::clamp(draft_.startMin + delta, 0, 1439);
      draft_.startMin = static_cast<int16_t>(next);
      if (draft_.end == draft_.start)
        draft_.endMin = static_cast<int16_t>(std::clamp(next + std::max(span, 0), 0, 1439));
      break;
    }
    case FEndTime: {
      const int delta = big * 60 + small * 15;
      int next = std::clamp(draft_.endMin + delta, 0, 1439);
      if (draft_.end == draft_.start) next = std::max(next, static_cast<int>(draft_.startMin));
      draft_.endMin = static_cast<int16_t>(next);
      break;
    }
    case FRepeat: {
      const int next = (static_cast<int>(draft_.repeat) + (zone == 2 ? 4 : 1)) % 5;  // "<" steps back
      draft_.repeat = static_cast<cal::Repeat>(next);
      if (draft_.repeat == cal::Repeat::None) draft_.hasUntil = false;
      break;
    }
    case FTodo:
      draftTodo_ = !draftTodo_;
      break;
    case FList: {
      const int n = static_cast<int>(lists_.size());
      const int at = static_cast<int>(cal::listIndex(lists_, draftList_));
      draftList_ = lists_[static_cast<size_t>((at + (zone == 2 ? n - 1 : 1)) % n)].id;
      break;
    }
    case FEvery: {
      const int delta = (big + small) * (big ? 5 : 1);
      draft_.every = static_cast<uint16_t>(std::clamp(static_cast<int>(draft_.every) + delta, 1, 99));
      break;
    }
    case FUntil: {
      if (!draft_.hasUntil) {
        if (big > 0 || small > 0) {
          draft_.hasUntil = true;
          draft_.until = std::max(draft_.end, draft_.start);
          if (big > 0)
            draft_.until = addMonths(draft_.until, 1);
          else
            draft_.until += 1;
        }
        break;
      }
      if (big) draft_.until = addMonths(draft_.until, big);
      if (small) draft_.until += small;
      if (draft_.until < draft_.start) draft_.hasUntil = false;  // stepped back past the start: no end
      break;
    }
    default:
      break;
  }
  // The file holds 1970 to 2200 only; stepping past that would save an event
  // that cannot be read back.
  draft_.start = cal::clampDay(draft_.start);
  draft_.end = std::max(cal::clampDay(draft_.end), draft_.start);
  if (draft_.hasUntil) draft_.until = cal::clampDay(draft_.until);
}

// --- Drawing helpers
// ---------------------------------------------------------------------------

void DaybookActivity::addHit(const int x, const int y, const int w, const int h, const Act act, const int32_t value) {
  hits_.push_back(Hit{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w),
                      static_cast<int16_t>(h), act, value});
}

void DaybookActivity::textBox(const int fontId, const int x, const int y, const int w, const int h,
                              const std::string& text, const bool center, const bool black) const {
  const std::string shown = renderer.truncatedText(fontId, cal::foldForDisplay(text).c_str(), std::max(w - 8, 8));
  const int lh = renderer.getLineHeight(fontId);
  const int tx = center ? x + (w - renderer.getTextWidth(fontId, shown.c_str())) / 2 : x + 4;
  renderer.drawText(fontId, tx, y + (h - lh) / 2, shown.c_str(), black);
}

void DaybookActivity::rbox(const int x, const int y, const int w, const int h, const int thickness,
                           const int radius) const {
  const int r = std::min(radius, std::min(w, h) / 2);
  renderer.drawRoundedRect(x, y, w, h, thickness, r, true);
}

void DaybookActivity::rfill(const int x, const int y, const int w, const int h, const bool black,
                            const int radius) const {
  daybook::fillRoundRect(renderer, x, y, w, h, radius, black);
}

void DaybookActivity::button(const int x, const int y, const int w, const int h, const char* label, const Act act,
                             const int32_t value, const bool filled, const int fontId) {
  if (filled) {
    rfill(x, y, w, h, true, 8);
  } else {
    rbox(x, y, w, h, 2, 8);
  }
  textBox(fontId < 0 ? kBold : fontId, x, y, w, h, label, true, !filled);
  addHit(x, y, w, h, act, value);
}

int DaybookActivity::drawHeader(const std::string& title, const std::string& right, const int fontId) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int top = metrics.topPadding + 6;
  const int hh = renderer.getLineHeight(kBold) + 20;
  button(kM, top, 64, hh, "<", Act::Back);
  const int rightW = right.empty() ? 0 : 60;
  textBox(fontId < 0 ? kBold : fontId, kM + 70, top, sw - 2 * kM - 70 - rightW, hh, title, true);
  if (!right.empty()) textBox(kSmall, sw - kM - rightW, top, rightW, hh, right, true);
  renderer.drawLine(kM, top + hh + 4, sw - kM, top + hh + 4);
  return top + hh + 12;
}

// Prev, the page number and Next in one row of height h whose top edge is y.
void DaybookActivity::drawPager(const int y, const int h) {
  const int sw = renderer.getScreenWidth();
  const int pw = 120;
  button(kM, y, pw, h, "Prev", Act::PagePrev, 0, false, kBoldSmall);
  button(sw - kM - pw, y, pw, h, "Next", Act::PageNext, 0, false, kBoldSmall);
  textBox(kBoldSmall, kM + pw, y, sw - 2 * kM - 2 * pw, h, std::to_string(page_ + 1) + "/" + std::to_string(pages_),
          true);
}

// A thin track down the right edge with a block where this page sits, when
// there is more than one page.
void DaybookActivity::drawScrollbar(const int y0, const int y1) const {
  if (pages_ <= 1 || y1 - y0 < 48) return;
  const int x = renderer.getScreenWidth() - 11;
  const int track = y1 - y0;
  const int thumb = std::max(28, track / pages_);
  const int ty = y0 + (track - thumb) * std::clamp(page_, 0, pages_ - 1) / (pages_ - 1);
  renderer.fillRect(x + 3, y0, 3, track, true);
  renderer.fillRect(x, ty, 9, thumb, true);
}

int DaybookActivity::bottomBar(const int count, const char* const* labels, const Act* acts) {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int bh = renderer.getLineHeight(kBold) + 26;
  const int y = sh - bh - kM;
  const int gap = 8;
  const int bw = (sw - 2 * kM - gap * (count - 1)) / count;
  for (int i = 0; i < count; ++i) button(kM + i * (bw + gap), y, bw, bh, labels[i], acts[i]);
  return y;
}

// Two rows of small buttons: [Today] [M|W] [List view], then [To-Do's] [Find]
// [Settings]. The side buttons are equal; the middle ones are smaller. M, W and
// List view are filled while that view is the one on screen.
int DaybookActivity::homeBar() {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int bh = renderer.getLineHeight(kBoldSmall) + 18;
  const int gap = 8;
  const int midW = 104;
  const int sideW = (sw - 2 * kM - 2 * gap - midW) / 2;
  const int midX = kM + sideW + gap;
  const int rightX = midX + midW + gap;
  const int y2 = sh - bh - kM;
  const int y1 = y2 - bh - gap;
  const int half = (midW - 4) / 2;
  button(kM, y1, sideW, bh, "Today", Act::Today, 0, false, kBoldSmall);
  button(midX, y1, half, bh, "M", Act::SetGrid, 0, view_ == View::Month, kBoldSmall);
  button(midX + midW - half, y1, half, bh, "W", Act::SetGrid, 1, view_ == View::Week, kBoldSmall);
  button(rightX, y1, sideW, bh, "List view", Act::ToggleList, 0, listMode_, kBoldSmall);
  button(kM, y2, sideW, bh, "To-Do's", Act::OpenTodo, 0, false, kBoldSmall);
  button(midX, y2, midW, bh, "Find", Act::OpenSearch, 0, false, kBoldSmall);
  button(rightX, y2, sideW, bh, "Settings", Act::Settings, 0, false, kBoldSmall);
  return y1;
}

std::string DaybookActivity::whenLabel(const cal::Event& ev, const cal::Occurrence& o, const cal::Day day) const {
  const bool multi = o.end > o.start;
  const std::string span = multi ? "  " + cal::formatSpan(o.start, o.end) : std::string();
  if (ev.allDay) return "All day" + span;
  if (!multi) {
    std::string s = cal::formatMinutes(ev.startMin, twelveHour());
    if (ev.endMin > ev.startMin) s += " - " + cal::formatMinutes(ev.endMin, twelveHour());
    return s;
  }
  if (day == o.start) return cal::formatMinutes(ev.startMin, twelveHour()) + " ->" + span;
  if (day == o.end) return "-> " + cal::formatMinutes(ev.endMin, twelveHour()) + span;
  return "All day" + span;
}

std::string DaybookActivity::repeatLabel(const cal::Event& ev) const {
  if (ev.repeat == cal::Repeat::None) return std::string();  // nothing to say for a one-off
  std::string s = ev.every <= 1 ? std::string("Every ") + unitName(ev.repeat, false)
                                : "Every " + std::to_string(ev.every) + " " + unitName(ev.repeat, true);
  if (ev.hasUntil) s += ", until " + cal::formatDate(ev.until);
  return s;
}

// --- Screens
// -----------------------------------------------------------------------------------

int DaybookActivity::drawMonthNav(const std::string& title) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int top = metrics.topPadding + 6;
  const int hh = renderer.getLineHeight(kBold) + 20;
  button(kM, top, 64, hh, "<", Act::PrevMonth);
  button(sw - kM - 64, top, 64, hh, ">", Act::NextMonth);
  // The title is a button too (the month and year picker); a long one gets a
  // smaller face instead of being cut.
  const int tx = kM + 70;
  const int tw = sw - 2 * kM - 140;
  int font = kBold;
  if (renderer.getTextWidth(font, title.c_str()) > tw - 12) font = kBoldSmall;
  if (renderer.getTextWidth(font, title.c_str()) > tw - 12) font = kSmall;
  rbox(tx, top, tw, hh, 1, 8);
  textBox(font, tx, top, tw, hh, title, true);
  addHit(tx, top, tw, hh, Act::OpenPicker, 0);
  return top + hh + 8;
}

// Months of one year, or (after a tap on the year) twenty years a page. A tap
// on a month or a year picks it.
void DaybookActivity::drawPicker() {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  int y = drawHeader(pickYears_ ? "Pick a year" : "Pick a month", "");
  const int gap = 10;
  if (!pickYears_) {
    pages_ = 1;
    const int bh = renderer.getLineHeight(kBold) + 26;
    button(kM, y, 80, bh, "<", Act::PickYear, pickYear_ - 1);
    button(sw - kM - 80, y, 80, bh, ">", Act::PickYear, pickYear_ + 1);
    const int cx = kM + 88;
    const int cwid = sw - 2 * kM - 176;
    rbox(cx, y, cwid, bh, 2, 8);
    textBox(kBold, cx, y, cwid, bh, std::to_string(pickYear_), true);
    addHit(cx, y, cwid, bh, Act::OpenPicker, 0);
    y += bh + 14;
    const int cw = (sw - 2 * kM - 2 * gap) / 3;
    const int ch = std::min(120, (sh - kM - y - 3 * gap) / 4);
    for (int m = 0; m < 12; ++m) {
      const bool current = pickYear_ == year_ && m + 1 == month_;
      button(kM + (m % 3) * (cw + gap), y + (m / 3) * (ch + gap), cw, ch, kMonthName[m], Act::PickMonth, m + 1, current,
             kBoldSmall);
    }
    return;
  }
  constexpr int kPerPage = 20;
  pages_ = (2200 - 1970 + kPerPage) / kPerPage;
  page_ = std::clamp(page_, 0, pages_ - 1);
  const int pagerH = renderer.getLineHeight(kBoldSmall) + 18;
  const int cw = (sw - 2 * kM - 3 * gap) / 4;
  const int ch = std::min(96, (sh - kM - pagerH - 12 - y - 4 * gap) / 5);
  for (int i = 0; i < kPerPage; ++i) {
    const int year = 1970 + page_ * kPerPage + i;
    if (year > 2200) break;
    button(kM + (i % 4) * (cw + gap), y + (i / 4) * (ch + gap), cw, ch, std::to_string(year).c_str(), Act::PickYear,
           year, year == pickYear_, kBoldSmall);
  }
  drawPager(sh - kM - pagerH, pagerH);
  drawScrollbar(y, sh - kM - pagerH - 6);
}

void DaybookActivity::drawMonth() {
  const int sw = renderer.getScreenWidth();
  int y = drawMonthNav(std::string(kMonthName[month_ - 1]) + " " + std::to_string(year_));
  const int barY = homeBar();

  const int wkW = weekNums_ ? 34 : 0;
  const int cw = (sw - 2 * kM - wkW) / 7;
  const int gridX = kM + wkW;
  const int lhS = renderer.getLineHeight(kSmall);
  for (int i = 0; i < 7; ++i) {
    std::string name = kDow[(firstDay_ + i) % 7];
    if (renderer.getTextWidth(kSmall, name.c_str()) > cw - 2) name.resize(2);  // never cut a letter off
    const int tw = renderer.getTextWidth(kSmall, name.c_str());
    renderer.drawText(kSmall, gridX + i * cw + (cw - tw) / 2, y, name.c_str(), true);
  }
  y += lhS + 6;
  // Short rows; what is left under the grid shows the selected day.
  const int stripMin = renderer.getLineHeight(kBoldSmall) + 8 + 2 * (lhS + 8);
  const int ch = std::clamp((barY - 8 - y - stripMin) / 6, 44, 76);

  const cal::Day first = cal::daysFromCivil(year_, month_, 1);
  const int lead = (cal::weekday(first) - firstDay_ + 7) % 7;
  const cal::Day gridStart = first - lead;
  const int weeks = (lead + cal::daysInMonth(year_, month_) + 6) / 7;
  const uint64_t busy = cal::busyDays(events_, gridStart);
  uint64_t taskAny = 0;
  uint64_t taskOpen = 0;
  cal::taskBusy(tasks_, gridStart, taskAny, taskOpen);

  for (int w = 0; w < weeks; ++w) {
    const int ry = y + w * ch;
    if (weekNums_) {
      textBox(kSmall, kM, ry, wkW, ch / 2, std::to_string(cal::isoWeek(gridStart + w * 7 + 3)), true);
    }
    for (int i = 0; i < 7; ++i) {
      const cal::Day day = gridStart + w * 7 + i;
      const int bit = static_cast<int>(day - gridStart);
      const cal::Date d = cal::civilFromDays(day);
      const int cx = gridX + i * cw;
      const bool inMonth = d.m == month_;
      const bool isToday = clockKnown_ && day == today_;
      rbox(cx, ry, cw, ch, 1, 4);
      if (day == selected_) rbox(cx + 1, ry + 1, cw - 2, ch - 2, 3, 5);
      const std::string num = std::to_string(d.d);
      if (isToday) {
        rfill(cx + 4, ry + 4, cw - 8, renderer.getLineHeight(kBoldSmall) + 4, true, 3);
        textBox(kBoldSmall, cx + 4, ry + 4, cw - 8, renderer.getLineHeight(kBoldSmall) + 4, num, true, false);
      } else {
        textBox(inMonth ? kBoldSmall : kSmall, cx + 4, ry + 4, cw - 8, renderer.getLineHeight(kSmall) + 4, num, true);
      }
      if ((busy >> bit) & 1) renderer.fillRect(cx + 8, ry + ch - 10, cw - 16, 5, true);
      if ((taskAny >> bit) & 1) {  // a to-do is due: solid square while one is
                                   // open, hollow when all are done
        const int ms = 10;
        if ((taskOpen >> bit) & 1)
          rfill(cx + cw / 2 - ms / 2, ry + ch - 26, ms, ms, true, 2);
        else
          rbox(cx + cw / 2 - ms / 2, ry + ch - 26, ms, ms, 1, 2);
      }
      addHit(cx, ry, cw, ch, Act::PickDay, day);
    }
  }
  drawDayStrip(y + weeks * ch + 8, barY - 8);
}

// A week as a column of days, each with its events and to-dos in small type. A
// tap on a day's name opens the day; a tap on a row opens that event or to-do.
// When the week is busy every day gets the same number of rows and says "+N
// more" for the rest, so the whole week always fits on one screen.
void DaybookActivity::drawWeek() {
  const int sw = renderer.getScreenWidth();
  const cal::Day start = weekStart();
  const cal::Date a = cal::civilFromDays(start);
  const cal::Date b = cal::civilFromDays(start + 6);
  char span[48];
  if (a.y != b.y) {
    std::snprintf(span, sizeof span, "%d %s %d - %d %s %d", a.d, kMonthShort[a.m - 1], a.y, b.d, kMonthShort[b.m - 1],
                  b.y);
  } else if (a.m != b.m) {
    std::snprintf(span, sizeof span, "%d %s - %d %s %d", a.d, kMonthShort[a.m - 1], b.d, kMonthShort[b.m - 1], b.y);
  } else {
    std::snprintf(span, sizeof span, "%d - %d %s %d", a.d, b.d, kMonthShort[a.m - 1], a.y);
  }
  int y = drawMonthNav((weekNums_ ? "W" + std::to_string(cal::isoWeek(start + 3)) + "  " : std::string()) + span);
  const int barY = homeBar();

  std::vector<cal::Occurrence> occ[7];
  std::vector<size_t> due[7];
  size_t most = 0;
  for (int d = 0; d < 7; ++d) {
    cal::occurrencesOn(events_, start + d, occ[d]);
    cal::tasksDueOn(tasks_, start + d, due[d]);
    most = std::max(most, occ[d].size() + due[d].size());
  }
  const int hdrH = renderer.getLineHeight(kBoldSmall) + 8;
  const int rowH = renderer.getLineHeight(kSmall) + 8;
  const int avail = barY - 8 - y;
  size_t cap = most;
  for (; cap > 0; --cap) {  // the most rows a day may show while the whole week still fits
    int need = 0;
    for (int d = 0; d < 7; ++d) {
      const size_t n = occ[d].size() + due[d].size();
      need += hdrH + static_cast<int>(std::min(n, cap)) * rowH + (n > cap ? rowH : 0);
    }
    if (need <= avail) break;
  }  // 0: no room for rows at all, so a day shows only how many it has
  const int lw = renderer.getTextWidth(kSmall, twelveHour() ? "12:00 PM-12:00 PM" : "00:00-00:00") + 12;
  for (int d = 0; d < 7; ++d) {
    const cal::Day day = start + d;
    const bool isToday = clockKnown_ && day == today_;
    addHit(kM, y, sw - 2 * kM, hdrH, Act::OpenDay, day);
    if (isToday) {
      rfill(kM, y, sw - 2 * kM, hdrH, true, 4);
    } else {
      renderer.drawLine(kM, y + hdrH - 2, sw - kM, y + hdrH - 2);
    }
    textBox(kBoldSmall, kM, y, sw - 2 * kM, hdrH, dayTitle(day, false), false, !isToday);
    const size_t n = occ[d].size() + due[d].size();
    if (cap == 0 && n > 0) {
      const std::string count = std::to_string(n) + (n == 1 ? " item" : " items");
      renderer.drawText(kSmall, sw - kM - 4 - renderer.getTextWidth(kSmall, count.c_str()),
                        y + (hdrH - renderer.getLineHeight(kSmall)) / 2, count.c_str(), !isToday);
    }
    y += hdrH;
    for (size_t i = 0; i < std::min(n, cap); ++i) {
      if (i < occ[d].size()) {
        const cal::Occurrence& o = occ[d][i];
        const cal::Event& ev = events_[o.event];
        textBox(kSmall, kM, y, lw, rowH, shortWhen(ev, o, twelveHour()), false);
        textBox(kBoldSmall, kM + lw, y, sw - 2 * kM - lw, rowH, ev.title, false);
        addHit(kM, y, sw - 2 * kM, rowH, Act::OpenEvent, static_cast<int32_t>(o.event));
      } else {
        const size_t ti = due[d][i - occ[d].size()];
        const cal::Task& t = tasks_[ti];
        drawCheck(kM + 4, y + 4, rowH - 8, t.done, true);
        textBox(kBoldSmall, kM + lw, y, sw - 2 * kM - lw, rowH, t.title, false);
        addHit(kM, y, sw - 2 * kM, rowH, Act::EditTask, static_cast<int32_t>(ti));
        addHit(kM, y, 40, rowH, Act::ToggleTask,
               static_cast<int32_t>(ti));  // the tick box ticks it
      }
      y += rowH;
    }
    if (n > cap) {
      textBox(kSmall, kM, y, sw - 2 * kM, rowH, "+ " + std::to_string(n - cap) + " more", false);
      y += rowH;
    }
  }
}

// The selected day under the month grid: one line per event or task, tap to
// open the day.
void DaybookActivity::drawDayStrip(const int y0, const int y1) {
  const int sw = renderer.getScreenWidth();
  const int hdrH = renderer.getLineHeight(kBoldSmall) + 8;
  const int rowH = renderer.getLineHeight(kSmall) + 8;
  if (y1 - y0 < hdrH) return;
  std::vector<cal::Occurrence> occ;
  cal::occurrencesOn(events_, selected_, occ);
  std::vector<size_t> due;
  cal::tasksDueOn(tasks_, selected_, due);
  renderer.drawLine(kM, y0, sw - kM, y0);
  std::string head = dayTitle(selected_, false);
  if (occ.empty() && due.empty()) head += "  -  nothing planned";
  textBox(kBoldSmall, kM, y0 + 2, sw - 2 * kM, hdrH, head, false);
  addHit(kM, y0, sw - 2 * kM, y1 - y0, Act::OpenDay, selected_);

  int y = y0 + 2 + hdrH;
  const int fit = std::max(0, (y1 - y) / rowH);
  const int total = static_cast<int>(occ.size() + due.size());
  const int shown = total > fit ? std::max(0, fit - 1) : total;  // the last slot may say "+N more"
  const int lw = renderer.getTextWidth(kSmall, twelveHour() ? "12:00 PM-12:00 PM" : "00:00-00:00") + 12;
  for (int i = 0; i < shown; ++i) {
    if (static_cast<size_t>(i) < occ.size()) {
      const cal::Occurrence& o = occ[static_cast<size_t>(i)];
      const cal::Event& ev = events_[o.event];
      textBox(kSmall, kM, y, lw, rowH, shortWhen(ev, o, twelveHour()), false);
      textBox(kBoldSmall, kM + lw, y, sw - 2 * kM - lw, rowH, ev.title, false);
    } else {
      const cal::Task& t = tasks_[due[static_cast<size_t>(i) - occ.size()]];
      drawCheck(kM + 4, y + 5, rowH - 10, t.done, true);
      textBox(kBoldSmall, kM + lw, y, sw - 2 * kM - lw, rowH, t.title, false);
    }
    y += rowH;
  }
  if (fit > 0 && shown < total) {
    textBox(kSmall, kM, y, sw - 2 * kM, rowH, "+ " + std::to_string(total - shown) + " more", false);
  }
}

// Everything from today on: late tasks first (inverted), then events and due
// tasks day by day, a heavy rule and the month's name where a new month starts.
// Paged.
void DaybookActivity::drawList() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int top = metrics.topPadding + 6;
  const int hh = renderer.getLineHeight(kBold) + 20;
  textBox(kBold, kM, top, sw - 2 * kM, hh, "Upcoming", true);
  renderer.drawLine(kM, top + hh + 4, sw - kM, top + hh + 4);
  const int y0 = top + hh + 12;
  const int barY = homeBar();

  // Events, a month at a time so a long list is gathered in date order and cut
  // at a fixed size.
  constexpr size_t kListCap = 400;
  const cal::Day horizon = today_ + 366;
  std::vector<cal::Occurrence> occ;
  for (cal::Day from = today_; from <= horizon && occ.size() < kListCap; from += 31) {
    std::vector<cal::Occurrence> part;
    cal::occurrencesIn(events_, from, std::min<cal::Day>(from + 30, horizon), part);
    std::sort(part.begin(), part.end(),
              [](const cal::Occurrence& a, const cal::Occurrence& b) { return a.start < b.start; });
    for (const cal::Occurrence& o : part) {
      if (from > today_ && o.start < from) continue;  // an earlier window already has it
      if (occ.size() < kListCap) occ.push_back(o);
    }
  }

  struct Entry {
    int group;     // 0 late tasks, 1 everything else
    cal::Day day;  // the day it is listed under
    int kind;      // 0 late task, 1 event, 2 task due
    size_t idx;    // into occ (events) or tasks_ (tasks)
  };
  std::vector<Entry> entries;
  std::vector<size_t> late;
  cal::overdueTasks(tasks_, today_, late);
  for (const size_t t : late) entries.push_back(Entry{0, tasks_[t].due, 0, t});
  for (size_t i = 0; i < occ.size(); ++i) entries.push_back(Entry{1, std::max(occ[i].start, today_), 1, i});
  for (size_t t = 0; t < tasks_.size(); ++t) {
    const cal::Task& k = tasks_[t];
    if (!k.done && k.hasDue && k.due >= today_ && k.due <= horizon) entries.push_back(Entry{1, k.due, 2, t});
  }
  std::sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
    if (a.group != b.group) return a.group < b.group;
    if (a.day != b.day) return a.day < b.day;
    if (a.kind != b.kind) return a.kind < b.kind;  // events before tasks
    if (a.kind == 1) {
      const cal::Event& ea = events_[occ[a.idx].event];
      const cal::Event& eb = events_[occ[b.idx].event];
      if (ea.allDay != eb.allDay) return ea.allDay;
      if (!ea.allDay && ea.startMin != eb.startMin) return ea.startMin < eb.startMin;
      if (ea.title != eb.title) return ea.title < eb.title;
    }
    return a.idx < b.idx;
  });

  enum RowType { kLateHead, kMonthHead, kDayHead, kItem };
  struct Row {
    int type;
    cal::Day day;
    size_t entry;
  };
  std::vector<Row> rows;
  std::vector<size_t> pageStart;
  const int hdrH = renderer.getLineHeight(kBoldSmall) + 12;
  const int monthH = renderer.getLineHeight(kBoldSmall) + 18;
  const int evH = renderer.getLineHeight(kBold) + 16;
  const int pagerH = renderer.getLineHeight(kBoldSmall) + 18;
  const auto monthKey = [](const cal::Day d) {
    const cal::Date c = cal::civilFromDays(d);
    return c.y * 12 + c.m;
  };
  const auto paginate = [&](const int avail) {
    rows.clear();
    pageStart.clear();
    pageStart.push_back(0);
    int used = 0;
    int lastGroup = -1;
    int lastMonth = -1;
    cal::Day lastDay = INT32_MIN;
    for (size_t i = 0; i < entries.size(); ++i) {
      const Entry& e = entries[i];
      bool lateHead = e.group == 0 && lastGroup != 0;
      bool monthHead = e.group == 1 && monthKey(e.day) != lastMonth;
      bool dayHead = e.group == 1 && (monthHead || e.day != lastDay);
      int need = (lateHead ? hdrH : 0) + (monthHead ? monthH : 0) + (dayHead ? hdrH : 0) + evH;
      if (used > 0 && used + need > avail) {  // a new page repeats the heading it continues under
        pageStart.push_back(rows.size());
        used = 0;
        lateHead = e.group == 0;
        dayHead = e.group == 1;
        need = (lateHead ? hdrH : 0) + (monthHead ? monthH : 0) + (dayHead ? hdrH : 0) + evH;
      }
      if (lateHead) rows.push_back(Row{kLateHead, e.day, i});
      if (monthHead) rows.push_back(Row{kMonthHead, e.day, i});
      if (dayHead) rows.push_back(Row{kDayHead, e.day, i});
      rows.push_back(Row{kItem, e.day, i});
      used += need;
      lastGroup = e.group;
      lastDay = e.day;
      if (e.group == 1) lastMonth = monthKey(e.day);
    }
  };
  int avail = barY - 8 - y0;
  paginate(avail);
  if (pageStart.size() > 1) {  // room for the Prev / Next row
    avail -= pagerH + 6;
    paginate(avail);
  }
  pages_ = static_cast<int>(pageStart.size());
  page_ = std::clamp(page_, 0, pages_ - 1);

  if (rows.empty()) textBox(kBody, 0, y0 + 20, sw, evH, "Nothing coming up", true);

  const size_t from = pageStart[static_cast<size_t>(page_)];
  const size_t to = page_ + 1 < pages_ ? pageStart[static_cast<size_t>(page_ + 1)] : rows.size();
  const int lw = renderer.getTextWidth(kSmall, twelveHour() ? "12:00 PM-12:00 PM" : "00:00-00:00") + 12;
  int y = y0;
  for (size_t r = from; r < to; ++r) {
    const Row& row = rows[r];
    if (row.type == kLateHead) {
      textBox(kBoldSmall, kM, y, sw - 2 * kM, hdrH, "Overdue", false);
      renderer.drawLine(kM, y + hdrH - 3, sw - kM, y + hdrH - 3);
      y += hdrH;
    } else if (row.type == kMonthHead) {
      const cal::Date d = cal::civilFromDays(row.day);
      renderer.drawLine(kM, y + 4, sw - kM, y + 4, 3, true);
      textBox(kBoldSmall, kM, y + 6, sw - 2 * kM, monthH - 6,
              std::string(kMonthName[d.m - 1]) + " " + std::to_string(d.y), false);
      y += monthH;
    } else if (row.type == kDayHead) {
      std::string head = dayTitle(row.day, false);
      if (clockKnown_ && row.day == today_) head += "  -  today";
      textBox(kBoldSmall, kM, y, sw - 2 * kM, hdrH, head, false);
      renderer.drawLine(kM, y + hdrH - 3, sw - kM, y + hdrH - 3);
      y += hdrH;
    } else {
      const Entry& e = entries[row.entry];
      if (e.kind == 0) {  // a late task, inverted
        taskRow(y, evH - 4, e.idx, true, false, "",
                "due " + shortDate(tasks_[e.idx].due) + " - " + listName(tasks_[e.idx].list));
      } else if (e.kind == 2) {
        taskRow(y, evH - 4, e.idx, false, false, "", listName(tasks_[e.idx].list));
      } else {
        const cal::Occurrence& o = occ[e.idx];
        const cal::Event& ev = events_[o.event];
        rbox(kM, y, sw - 2 * kM, evH - 4);
        textBox(kSmall, kM + 4, y, lw, evH - 4, shortWhen(ev, o, twelveHour()), false);
        textBox(kBold, kM + 4 + lw, y, sw - 2 * kM - 4 - lw, evH - 4, ev.title, false);
        addHit(kM, y, sw - 2 * kM, evH - 4, Act::OpenEvent, static_cast<int32_t>(o.event));
      }
      y += evH;
    }
  }

  if (pages_ > 1) drawPager(barY - 6 - pagerH, pagerH);
  drawScrollbar(y0, barY - 8 - (pages_ > 1 ? pagerH + 6 : 0));
}

void DaybookActivity::drawDay() {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  int y = drawHeader(dayTitle(selected_), weekNums_ ? "W" + std::to_string(cal::isoWeek(selected_)) : "", kBoldSmall);

  std::vector<cal::Occurrence> occ;
  cal::occurrencesOn(events_, selected_, occ);
  std::vector<size_t> due;
  cal::tasksDueOn(tasks_, selected_, due);
  const size_t total = occ.size() + due.size();

  const int rowH = renderer.getLineHeight(kSmall) + renderer.getLineHeight(kBold) + 22;
  const int bh = renderer.getLineHeight(kBold) + 26;
  const int listBottom = sh - bh - kM - 8;
  const int pagerH = renderer.getLineHeight(kBoldSmall) + 18;
  int perPage = std::max(1, (listBottom - y) / rowH);
  if (static_cast<int>(total) > perPage) perPage = std::max(1, (listBottom - pagerH - 6 - y) / rowH);
  pages_ = std::max(1, (static_cast<int>(total) + perPage - 1) / perPage);
  page_ = std::clamp(page_, 0, pages_ - 1);

  if (total == 0) {
    textBox(kBody, 0, y + 20, sw, rowH, "Nothing planned", true);
  }
  for (int i = 0; i < perPage; ++i) {
    const size_t idx = static_cast<size_t>(page_ * perPage + i);
    if (idx >= total) break;
    const int ry = y + i * rowH;
    if (idx >= occ.size()) {  // a to-do due this day
      const cal::Task& t = tasks_[due[idx - occ.size()]];
      taskRow(ry, rowH - 6, due[idx - occ.size()], false, true, t.done ? doneLabel(t) : "To-do", listName(t.list));
      continue;
    }
    const cal::Event& ev = events_[occ[idx].event];
    rbox(kM, ry, sw - 2 * kM, rowH - 6);
    std::string line1 = whenLabel(ev, occ[idx], selected_);
    if (!ev.location.empty()) line1 += "   @ " + ev.location;
    textBox(kSmall, kM, ry + 4, sw - 2 * kM, renderer.getLineHeight(kSmall) + 4, line1, false);
    textBox(kBold, kM, ry + 6 + renderer.getLineHeight(kSmall), sw - 2 * kM, renderer.getLineHeight(kBold) + 8,
            ev.title, false);
    addHit(kM, ry, sw - 2 * kM, rowH - 6, Act::OpenEvent, static_cast<int32_t>(occ[idx].event));
  }

  const char* labels[2] = {"+ Event", "+ To-Do"};
  const Act acts[2] = {Act::AddEvent, Act::NewDayTask};
  const int barY = bottomBar(2, labels, acts);
  if (pages_ > 1) drawPager(barY - 6 - pagerH, pagerH);
  drawScrollbar(y, barY - 8 - (pages_ > 1 ? pagerH + 6 : 0));
}

void DaybookActivity::drawDetail() {
  const int sw = renderer.getScreenWidth();
  if (detailIndex_ < 0 || detailIndex_ >= static_cast<int>(events_.size())) {
    view_ = View::Day;
    drawDay();
    return;
  }
  const cal::Event& ev = events_[static_cast<size_t>(detailIndex_)];
  int y = drawHeader(dayTitle(ev.start), "",
                     kBoldSmall);  // the date heads the screen
  const int lhB = renderer.getLineHeight(kBold);
  const int lh = renderer.getLineHeight(kBody);
  const std::string title = cal::foldForDisplay(ev.title);
  for (const std::string& line : renderer.wrappedText(kBold, title.c_str(), sw - 2 * kM, 3)) {
    renderer.drawText(kBold, kM, y, line.c_str(), true);
    y += lhB + 2;
  }
  y += 10;
  const auto put = [&](const std::string& s) {
    const std::string shown = cal::foldForDisplay(s);
    for (const std::string& line : renderer.wrappedText(kBody, shown.c_str(), sw - 2 * kM, 6)) {
      renderer.drawText(kBody, kM, y, line.c_str(), true);
      y += lh + 2;
    }
    y += 8;
  };
  if (ev.end > ev.start) put("Until " + dayTitle(ev.end));
  if (ev.allDay) {
    put("All day");
  } else {
    // No end time (0 on a one-day event) says only when it starts.
    const bool hasEnd = ev.endMin > ev.startMin || (ev.end > ev.start && ev.endMin > 0);
    put(cal::formatMinutes(ev.startMin, twelveHour()) +
        (hasEnd ? " - " + cal::formatMinutes(ev.endMin, twelveHour()) : std::string()));
  }
  const std::string rep = repeatLabel(ev);
  if (!rep.empty()) put(rep);
  if (!ev.location.empty()) put("Place: " + ev.location);
  if (!ev.note.empty()) put(ev.note);

  if (confirmDelete_) {
    const char* labels[2] = {"Keep it", "Delete"};
    const Act acts[2] = {Act::CancelDelete, Act::ConfirmDelete};
    const int barY = bottomBar(2, labels, acts);
    const std::string question =
        ev.repeat == cal::Repeat::None ? "Delete this event?" : "Delete event and all repeats?";
    const int lhQ = renderer.getLineHeight(kBoldSmall) + 2;
    const auto lines = renderer.wrappedText(kBoldSmall, question.c_str(), sw - 2 * kM, 2);
    int qy = barY - 12 - static_cast<int>(lines.size()) * lhQ;
    for (const std::string& line : lines) {
      renderer.drawText(kBoldSmall, (sw - renderer.getTextWidth(kBoldSmall, line.c_str())) / 2, qy, line.c_str(), true);
      qy += lhQ;
    }
  } else {
    const char* labels[2] = {"Edit", "Delete"};
    const Act acts[2] = {Act::Edit, Act::Delete};
    bottomBar(2, labels, acts);
  }
}

// --- Form rows. The value column starts at rx; the arrows sit in fixed columns
// so rows line up. -------

void DaybookActivity::rowBox(const int rx, const int ry, const int rw, const int h, const Act act, const int base,
                             const std::string& value) {
  rbox(rx, ry, rw, h, 2, 8);
  textBox(kBody, rx, ry, rw, h, value, false);
  addHit(rx, ry, rw, h, act, base);
}

void DaybookActivity::rowCycler(const int rx, const int ry, const int rw, const int h, const Act act, const int base,
                                const std::string& value) {
  const int bw = 46;
  addHit(rx, ry, rw, h, act,
         base);  // the row itself: next value. The arrows, added after, win.
  button(rx + bw + 4, ry, bw, h, "<", act, base | 2);
  button(rx + rw - 2 * bw - 4, ry, bw, h, ">", act, base | 3);
  textBox(kBody, rx + 2 * bw + 8, ry, rw - 4 * bw - 16, h, value, true);
}

void DaybookActivity::rowStepper(const int rx, const int ry, const int rw, const int h, const Act act, const int base,
                                 const std::string& value, const std::string& sub, const bool centerHit) {
  const int bw = 46;
  if (centerHit) addHit(rx + 2 * bw + 8, ry, rw - 4 * bw - 16, h, act, base);
  button(rx, ry, bw, h, "<<", act, base | 1);
  button(rx + bw + 4, ry, bw, h, "<", act, base | 2);
  button(rx + rw - 2 * bw - 4, ry, bw, h, ">", act, base | 3);
  button(rx + rw - bw, ry, bw, h, ">>", act, base | 4);
  const int vx = rx + 2 * bw + 8;
  const int vw = rw - 4 * bw - 16;
  const int lhS = renderer.getLineHeight(kSmall);
  if (!sub.empty() && h >= renderer.getLineHeight(kBody) + lhS + 4) {  // the weekday, small, under the date
    const int top = (h - renderer.getLineHeight(kBody) - lhS) / 2;
    textBox(kBody, vx, ry + top - 2, vw, renderer.getLineHeight(kBody) + 2, value, true);
    textBox(kSmall, vx, ry + top + renderer.getLineHeight(kBody), vw, lhS + 2, sub, true);
  } else {
    textBox(kBody, vx, ry, vw, h, value, true);
  }
}

void DaybookActivity::drawForm() {
  const int sw = renderer.getScreenWidth();
  const bool asTask = draftTodo_ && editIndex_ < 0;
  int y = drawHeader(asTask ? "New to-do" : (editIndex_ >= 0 ? "Edit event" : "New event"), "");

  std::vector<Field> rows;
  if (asTask) {  // a to-do needs a name, a note, a due day and a list: nothing
                 // else
    rows = {FTitle, FNote, FTodo, FStartDate, FList};
  } else {
    rows = {FTitle, FLocation, FNote};
    if (editIndex_ < 0) rows.push_back(FTodo);
    rows.push_back(FAllDay);
    rows.push_back(FStartDate);
    rows.push_back(FEndDate);
    if (!draft_.allDay) {
      rows.push_back(FStartTime);
      rows.push_back(FEndTime);
    }
    rows.push_back(FRepeat);
    if (draft_.repeat != cal::Repeat::None) {
      rows.push_back(FEvery);
      rows.push_back(FUntil);
    }
  }

  const char* labels[2] = {"Cancel", "Save"};
  const Act acts[2] = {Act::Cancel, Act::Save};
  const int barY = bottomBar(2, labels, acts);
  const int msgH = renderer.getLineHeight(kSmall) + 6;
  const int avail = barY - 6 - y - msgH;
  const int rowH = std::min(64, avail / static_cast<int>(rows.size()));
  static constexpr const char* kLabel[FCount] = {"Name", "Place",  "Note",  "All day", "From",  "To",  "Start",
                                                 "End",  "Repeat", "Every", "Until",   "To-Do", "List"};
  const int labelW = 84;
  const int rx = kM + labelW;
  const int rw = sw - kM - rx;

  for (size_t r = 0; r < rows.size(); ++r) {
    const Field f = rows[r];
    const int ry = y + static_cast<int>(r) * rowH;
    const int h = rowH - 6;
    textBox(kSmall, kM, ry, labelW, h, asTask && f == FStartDate ? "Due" : kLabel[f], false);
    const int base = static_cast<int>(f) << 3;
    std::string value;
    std::string sub;  // the weekday under a date
    switch (f) {
      case FTitle:
        value = draft_.title.empty() ? "tap to type" : draft_.title;
        break;
      case FLocation:
        value = draft_.location.empty() ? "-" : draft_.location;
        break;
      case FNote:
        value = draft_.note.empty() ? "-" : draft_.note;
        break;
      case FAllDay:
        value = draft_.allDay ? "Yes" : "No";
        break;
      case FStartDate:
        value = cal::formatDate(draft_.start);
        sub = kDowLong[cal::weekday(draft_.start)];
        break;
      case FEndDate:
        value = cal::formatDate(draft_.end);
        sub = kDowLong[cal::weekday(draft_.end)];
        break;
      case FStartTime:
        value = cal::formatMinutes(draft_.startMin, twelveHour());
        break;
      case FEndTime:
        value = cal::formatMinutes(draft_.endMin, twelveHour());
        break;
      case FRepeat:
        value = kRepeatName[static_cast<int>(draft_.repeat)];
        break;
      case FEvery:
        value = std::to_string(draft_.every) + " " + unitName(draft_.repeat, draft_.every != 1);
        break;
      case FUntil:
        value = draft_.hasUntil ? cal::formatDate(draft_.until) : "Never";
        if (draft_.hasUntil) sub = kDowLong[cal::weekday(draft_.until)];
        break;
      case FTodo:
        value = draftTodo_ ? "Yes" : "No";
        break;
      case FList:
        value = listName(draftList_);
        break;
      default:
        break;
    }
    switch (f) {
      case FTitle:
      case FLocation:
      case FNote:
        rowBox(rx, ry, rw, h, Act::Form, base, value);
        break;
      case FAllDay:
      case FRepeat:
      case FList:
        rowCycler(rx, ry, rw, h, Act::Form, base, value);
        break;
      case FTodo: {
        rowCycler(rx, ry, rw, h, Act::Form, base, value);
        const int bs = std::min(28, h - 8);  // the tick box beside the word
        const int tw = renderer.getTextWidth(kBody, value.c_str());
        drawCheck(rx + rw / 2 - tw / 2 - bs - 12, ry + (h - bs) / 2, bs, draftTodo_, true);
        break;
      }
      default:  // dates, times, every, until
        rowStepper(rx, ry, rw, h, Act::Form, base, value, sub, false);
        break;
    }
  }
  if (!message_.empty()) {
    textBox(kBoldSmall, 0, barY - msgH - 2, sw, msgH, message_, true);
  } else if (!asTask && draft_.repeat == cal::Repeat::Monthly && cal::civilFromDays(draft_.start).d > 28) {
    textBox(kSmall, 0, barY - msgH - 2, sw, msgH, "Short months: it lands on the last day", true);
  }
}

void DaybookActivity::drawSettings() {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  int y = drawHeader("Settings", "");
  // Two lines a row, the setting's name small over its value, so neither is cut
  // short by the other.
  const int lhS = renderer.getLineHeight(kSmall);
  const int lhB = renderer.getLineHeight(kBold);
  const int rowH = lhS + lhB + 18;
  struct Row {
    const char* label;
    std::string value;
    Act act;
  };
  const Row rows[] = {
      {"First day of week", firstDay_ == 1 ? "Monday" : (firstDay_ == 0 ? "Sunday" : "Saturday"), Act::SetFirstDay},
      {"Week numbers", weekNums_ ? "On" : "Off", Act::ToggleWeek},
      {"Agenda on the sleep screen", agenda_ ? "On" : "Off", Act::ToggleAgenda},
      {"Agenda size", agendaSize_ == 0 ? "Small (counts only)" : (agendaSize_ == 1 ? "Normal" : "Large"),
       Act::CycleSize},
      {"Agenda shows", tomorrow_ ? "Today + tomorrow" : "Today only", Act::ToggleTomorrow},
      {"Agenda position", agendaBottom_ ? "Bottom" : "Top", Act::TogglePosition},
      {"Web page for phone or PC (plugin)",
       pluginState_ == daybook::plugin::State::Missing
           ? "Install"
           : (pluginState_ == daybook::plugin::State::Outdated ? "Update" : "Installed"),
       Act::InstallPlugin},
  };
  const int nRows = static_cast<int>(sizeof rows / sizeof rows[0]);
  // The note under the rows takes two row slots. Whatever does not fit goes on
  // the next page (Prev/Next, or swipe up/down), so the end of the screen is
  // never cut off.
  const int slots = nRows + 2;
  const int pagerH = lhS + 18;
  int perPage = std::max(1, (sh - kM - y) / (rowH + 8));
  if (slots > perPage) perPage = std::max(1, (sh - kM - pagerH - 6 - y) / (rowH + 8));
  pages_ = std::max(1, (slots + perPage - 1) / perPage);
  page_ = std::clamp(page_, 0, pages_ - 1);
  const int first = page_ * perPage;
  for (int i = 0; i < nRows; ++i) {
    if (i < first || i >= first + perPage) continue;
    const Row& r = rows[i];
    rbox(kM, y, sw - 2 * kM, rowH);
    textBox(kSmall, kM + 6, y + 4, sw - 2 * kM - 12, lhS + 2, r.label, false);
    textBox(kBold, kM + 6, y + 6 + lhS, sw - 2 * kM - 12, lhB + 6, r.value, false);
    addHit(kM, y, sw - 2 * kM, rowH, r.act);
    y += rowH + 8;
  }
  if (nRows >= first && nRows < first + perPage) {  // the note starts on this page
    y += 6;
    std::string note =
        "The agenda shows when Sleep screen is set to Custom. "
        "Clock format follows System settings.";
    if (!message_.empty()) {
      note = message_;
    } else if (pluginState_ != daybook::plugin::State::Missing && !daybook::plugin::pluginsEnabled()) {
      note = "The web page needs Settings > Connectivity > Plugins turned on.";
    }
    for (const std::string& line : renderer.wrappedText(kSmall, note.c_str(), sw - 2 * kM, 4)) {
      renderer.drawText(kSmall, kM, y, line.c_str(), true);
      y += renderer.getLineHeight(kSmall) + 2;
    }
    if (!clockKnown_) {
      y += 8;
      textBox(kBoldSmall, 0, y, sw, renderer.getLineHeight(kBoldSmall) + 8, "Clock not set: no 'today' shown", true);
    }
  }
  if (pages_ > 1) drawPager(sh - kM - pagerH, pagerH);
}

// Find: events and to-dos whose name, place or note holds every word typed.
// Soonest first, then what is behind us; to-dos that are done come last. A tap
// opens the event or to-do.
void DaybookActivity::drawSearch() {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  struct Found {
    int group;     // 0 coming up or open, 1 open with no day, 2 over or done
    cal::Day day;  // sort key within the group
    bool isEvent;
    size_t idx;  // into events_ or tasks_
    cal::Day shown;
  };
  std::vector<Found> found;
  const std::string key = cal::searchKey(search_);
  if (!key.empty()) {
    constexpr size_t kCap = 80;
    size_t nEv = 0;
    for (size_t i = 0; i < events_.size() && nEv < kCap; ++i) {
      if (!cal::matchesQuery(events_[i], key)) continue;
      ++nEv;
      std::vector<cal::Event> one{events_[i]};
      std::vector<cal::Occurrence> part;
      cal::occurrencesIn(one, today_, today_ + 400, part);
      if (part.empty()) {
        found.push_back(Found{2, -events_[i].start, true, i, events_[i].start});
      } else {
        cal::Day next = part[0].start;
        for (const cal::Occurrence& o : part) next = std::min(next, std::max(o.start, today_));
        found.push_back(Found{0, next, true, i, next});
      }
    }
    size_t nTask = 0;
    for (size_t i = 0; i < tasks_.size() && nTask < kCap; ++i) {
      const cal::Task& t = tasks_[i];
      if (!cal::matchesQuery(t, key)) continue;
      ++nTask;
      if (t.done)
        found.push_back(Found{2, -t.doneOn, false, i, t.doneOn});
      else if (t.hasDue)
        found.push_back(Found{0, t.due, false, i, t.due});
      else
        found.push_back(Found{1, 0, false, i, 0});
    }
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
      if (a.group != b.group) return a.group < b.group;
      if (a.day != b.day) return a.day < b.day;
      if (a.isEvent != b.isEvent) return a.isEvent;  // events before to-dos
      return a.idx < b.idx;
    });
  }

  int y = drawHeader("Find", key.empty() ? std::string() : std::to_string(found.size()) + " found");
  const int qh = renderer.getLineHeight(kBody) + 22;
  {  // a search box: a magnifier, a rule, then the words (or a hint)
    rbox(kM, y, sw - 2 * kM, qh, 3, 10);
    const int s = qh - 22;
    const int d = s * 2 / 3;
    const int ix = kM + 16;
    const int iy = y + 10;
    renderer.drawRoundedRect(ix, iy, d, d, 2, d / 2, true);
    renderer.drawLine(ix + d - 2, iy + d - 2, ix + s, iy + s, 3, true);
    const int rx = ix + s + 12;
    renderer.drawLine(rx, y + 8, rx, y + qh - 8, true);
    textBox(kBody, rx + 8, y, sw - kM - rx - 16, qh, search_.empty() ? "tap to type" : search_, false,
            !search_.empty());
    addHit(kM, y, sw - 2 * kM, qh, Act::EditSearch, 0);
  }
  y += qh + 10;

  const int evH = renderer.getLineHeight(kBold) + 16;
  const int pagerH = renderer.getLineHeight(kBoldSmall) + 18;
  const int bottom = sh - kM;
  int perPage = std::max(1, (bottom - y) / evH);
  if (static_cast<int>(found.size()) > perPage) perPage = std::max(1, (bottom - y - pagerH - 6) / evH);
  pages_ = std::max(1, (static_cast<int>(found.size()) + perPage - 1) / perPage);
  page_ = std::clamp(page_, 0, pages_ - 1);

  if (key.empty()) {
    textBox(kSmall, 0, y + 20, sw, evH, "Type a word to find events and to-dos", true);
  } else if (found.empty()) {
    textBox(kBody, 0, y + 20, sw, evH, "No matches", true);
  }
  const int lw = renderer.getTextWidth(kSmall, "00 Mmm 0000") + 12;
  for (int i = 0; i < perPage; ++i) {
    const size_t at = static_cast<size_t>(page_ * perPage + i);
    if (at >= found.size()) break;
    const Found& f = found[at];
    const int ry = y + i * evH;
    if (f.isEvent) {
      const cal::Event& ev = events_[f.idx];
      rbox(kM, ry, sw - 2 * kM, evH - 4);
      textBox(kSmall, kM + 4, ry, lw, evH - 4, shortDate(f.shown), false);
      textBox(kBold, kM + 4 + lw, ry, sw - 2 * kM - 4 - lw, evH - 4, ev.title, false);
      addHit(kM, ry, sw - 2 * kM, evH - 4, Act::OpenEvent, static_cast<int32_t>(f.idx));
    } else {
      const cal::Task& t = tasks_[f.idx];
      const bool late = cal::isOverdue(t, today_);
      std::string when;  // "due 24 Nov", "27 Nov" or "done 26 Nov", then the
                         // list, together in the corner
      if (t.done)
        when = doneLabel(t);
      else if (t.hasDue)
        when = (late ? "due " : "") + shortDate(t.due);
      taskRow(ry, evH - 4, f.idx, late, false, "", when.empty() ? listName(t.list) : when + " - " + listName(t.list));
    }
  }
  if (pages_ > 1) drawPager(sh - kM - pagerH, pagerH);
  drawScrollbar(y, sh - kM - (pages_ > 1 ? pagerH + 6 : 0));
}

// "Deleted: Dentist  [Undo]": a bar over the bottom of the screen after a
// delete, until something else is saved.
void DaybookActivity::drawUndo() {
  if (trash_.kind == Trash::Kind::None) return;
  const bool homeView = view_ == View::Month || view_ == View::Week || view_ == View::List;
  if (!homeView && view_ != View::Day && view_ != View::Todo && view_ != View::Lists && view_ != View::Search) return;
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int lhBS = renderer.getLineHeight(kBoldSmall);
  const int barH = lhBS + 18;
  int top;  // the top of whatever sits along the bottom of this screen
  if (homeView) {
    top = sh - 2 * (lhBS + 18) - kM - 8;
  } else if (view_ == View::Search) {
    top = sh - kM;
  } else {
    top = sh - (renderer.getLineHeight(kBold) + 26) - kM;
  }
  int y = top - 6 - barH;
  if ((view_ == View::List || view_ == View::Todo || view_ == View::Search) && pages_ > 1) y -= lhBS + 18 + 6;
  const int bw = 90;
  rfill(kM, y, sw - 2 * kM, barH, false);
  rbox(kM, y, sw - 2 * kM, barH, 2);
  addHit(kM, y, sw - 2 * kM, barH,
         Act::Nop);  // a tap on the bar must not reach what is under it
  textBox(kBoldSmall, kM + 4, y, sw - 2 * kM - bw - 12, barH, "Deleted: " + trash_.label, false);
  button(sw - kM - bw - 4, y + 3, bw, barH - 6, "Undo", Act::Undo, 0, true, kBoldSmall);
}

void DaybookActivity::render(RenderLock&&) {
  // After deep sleep the clock can still be unset when the app reopens; pick it
  // up as soon as it is there, and roll "today" over at midnight.
  const bool wasKnown = clockKnown_;
  refreshToday();
  if (clockKnown_ && !wasKnown) {
    selected_ = today_;
    openMonthOf(today_);
  }
  hits_.clear();
  renderer.clearScreen();
  switch (view_) {
    case View::Month:
      drawMonth();
      break;
    case View::Week:
      drawWeek();
      break;
    case View::Search:
      drawSearch();
      break;
    case View::List:
      drawList();
      break;
    case View::Day:
      drawDay();
      break;
    case View::Detail:
      drawDetail();
      break;
    case View::Form:
      drawForm();
      break;
    case View::Settings:
      drawSettings();
      break;
    case View::Todo:
      drawTodo();
      break;
    case View::Lists:
      drawLists();
      break;
    case View::TaskForm:
      drawTaskForm();
      break;
    case View::ListForm:
      drawListForm();
      break;
    case View::Pick:
      drawPicker();
      break;
  }
  drawUndo();
  hitsReady_ = true;
  renderer.displayBuffer();
}
