#pragma once

// Daybook: the calendar and the to-dos in one app. A month grid, an "upcoming"
// list, a list for the day, a form to add and edit events, and to-do lists
// whose due dates also show on the calendar. Every rule (dates, repeats, tasks,
// the file formats) lives in DaybookCore, which has a host suite; these files
// are the screens and the card.
//
// DaybookActivity.cpp   the lifecycle, input, storage, the calendar screens
// DaybookTodo.cpp       the to-do screens and their input
//
// Text goes through the firmware's own keyboard screen (KeyboardEntryActivity),
// so it follows the keyboard layouts chosen in Settings.
//
// Touch only. Hit rectangles are collected while drawing and read by loop();
// `hitsReady_` is the hand-off between the render task and the loop task, the
// same job Weather's interactionsReady_ does.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "DaybookCore.h"
#include "DaybookPlugin.h"

class DaybookActivity final : public Activity {
 public:
  DaybookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Daybook", renderer, mappedInput) {}

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t {
    Month,
    Week,
    List,
    Day,
    Detail,
    Form,
    Settings,
    Todo,
    Lists,
    TaskForm,
    ListForm,
    Search,
    Pick,  // month and year picker
  };
  enum class Act : uint8_t {
    PrevMonth,
    NextMonth,
    Today,
    ToggleList,
    OpenTodo,
    Settings,
    PickDay,
    AddEvent,
    OpenEvent,
    Edit,
    Delete,
    ConfirmDelete,
    CancelDelete,
    Save,
    Cancel,
    Form,
    PagePrev,
    PageNext,
    SetFirstDay,
    ToggleWeek,
    ToggleAgenda,
    CycleSize,
    ToggleTomorrow,
    TogglePosition,
    InstallPlugin,
    SetGrid,
    NewDayTask,
    Undo,
    OpenSearch,
    EditSearch,
    Nop,
    Back,
    OpenLists,
    SelectList,
    NewList,
    EditList,
    ListName,
    NewTask,
    EditTask,
    ToggleTask,
    TaskField,
    OpenDay,
    OpenPicker,
    PickMonth,
    PickYear,
    GoList,
  };
  enum Field : uint8_t {
    FTitle,
    FLocation,
    FNote,
    FAllDay,
    FStartDate,
    FEndDate,
    FStartTime,
    FEndTime,
    FRepeat,
    FEvery,
    FUntil,
    FTodo,
    FList,
    FCount,
  };
  enum TaskFieldId : uint8_t { TTitle, TNote, TList, TDue, TDone, TCount };
  struct Hit {
    int16_t x, y, w, h;
    Act act;
    int32_t value;
  };

  // --- state ---
  bool started_ = false;
  View view_ = View::Month;
  std::vector<daybook::Event> events_;
  std::vector<daybook::Task> tasks_;
  std::vector<daybook::TaskList> lists_;
  daybook::Day today_ = 0;
  bool clockKnown_ = false;
  int year_ = 2026;
  int month_ = 1;
  daybook::Day selected_ = 0;
  int firstDay_ = 1;  // 0 Sunday, 1 Monday, 6 Saturday
  bool weekNums_ = true;
  bool agenda_ = false;        // draw today's agenda on the sleep screen
  int agendaSize_ = 2;         // ...how much it says: 0 Small (counts), 1 Normal, 2 Large
  bool tomorrow_ = true;       // ...with tomorrow under it
  bool agendaBottom_ = false;  // ...at the bottom of the screen
  bool listMode_ = false;      // the home screen: the upcoming list...
  bool weekMode_ = false;      // ...or else a week (true) or a month (false) grid
  std::string search_;         // the Find screen's words
  daybook::plugin::State pluginState_ = daybook::plugin::State::Missing;  // read when Settings opens

  // The last thing deleted, kept until something else is saved so the Undo bar
  // can bring it back.
  struct Trash {
    enum class Kind : uint8_t { None, Event, Task, List } kind = Kind::None;
    int at = 0;  // where it sat in its list
    daybook::Event event;
    daybook::Task task;
    daybook::TaskList list;
    std::vector<daybook::Task> tasks;  // a deleted list's tasks
    std::string label;
  } trash_;
  View detailFrom_ = View::Day;  // where Back from an event goes
  View formFrom_ = View::Day;    // where Cancel from a new event goes
  View taskFrom_ = View::Todo;   // where Back from a task goes
  int page_ = 0;
  int pages_ = 1;
  int pickYear_ = 2026;     // the year the picker shows
  bool pickYears_ = false;  // the picker is on its list of years
  int detailIndex_ = -1;
  bool confirmDelete_ = false;
  daybook::Event draft_;
  int editIndex_ = -1;                       // -1 = a new event
  bool draftTodo_ = false;                   // the event form's "To-Do" tick: save it as a task
  uint32_t draftList_ = daybook::kMainList;  // the list that task goes to
  daybook::Task taskDraft_;
  int taskEdit_ = -1;  // -1 = a new task
  daybook::TaskList listDraft_;
  int listEdit_ = -1;   // -1 = a new list
  size_t curList_ = 0;  // index into lists_: the list the To-Do screen shows
  std::string message_;

  // Hand-off to the loop task
  std::vector<Hit> hits_;
  std::atomic<bool> hitsReady_{false};

  // --- logic (DaybookActivity.cpp) ---
  void loadAll();
  bool saveEvents();
  bool saveTasks();
  bool saveLists();
  void saveSettings();
  void restoreTrash();
  void askSearch();
  void discard(Trash::Kind kind, int at,
               const std::string& label);  // starts a fresh Trash
  daybook::Day weekStart() const;
  void refreshToday();
  void back();
  const Hit* hitAt(int x,
                   int y) const;                     // the top-most hit under a point, or null
  static std::string weekdayName(daybook::Day day);  // "Monday"
  static bool isRowAct(Act act);                     // a row or day cell: acts on the lift, not the press
  void route(Act act, int32_t value);
  void startDraft(int index);
  void formTap(int field, int zone);
  void askString(const char* title, const std::string& current, size_t limit,
                 std::function<void(const std::string&)> apply);
  void askText(Field field);  // the stock keyboard screen, then back here
  void changed();             // state moved: stop trusting the hit table, redraw
  void openMonthOf(daybook::Day day);
  bool saveDraftAsTask();

  // --- logic (DaybookTodo.cpp) ---
  bool routeTodo(Act act, int32_t value);  // true when it handled `act`
  void taskTap(int field, int zone);
  void deleteList(int index);
  size_t openCount(uint32_t list) const;
  void clampList();

  // --- drawing (DaybookActivity.cpp) ---
  void addHit(int x, int y, int w, int h, Act act, int32_t value = 0);
  // fontId < 0 means the default bold face.
  void button(int x, int y, int w, int h, const char* label, Act act, int32_t value = 0, bool filled = false,
              int fontId = -1);
  void textBox(int fontId, int x, int y, int w, int h, const std::string& text, bool center, bool black = true) const;
  int drawHeader(const std::string& title, const std::string& right, int fontId = -1);
  void drawPager(int y, int h);
  void drawScrollbar(int y0, int y1) const;  // a thin track on the right edge when there is more than one page
  void drawPicker();
  int bottomBar(int count, const char* const* labels, const Act* acts);
  int homeBar();  // the two small button rows of the month and list screens;
                  // returns their top
  View home() const { return listMode_ ? View::List : (weekMode_ ? View::Week : View::Month); }
  int drawMonthNav(const std::string& title);  // "< title >" row; returns the y just below it
  void drawMonth();
  void drawWeek();
  void drawSearch();
  void drawUndo();
  void drawList();
  void drawDayStrip(int y0, int y1);
  void drawDay();
  void drawDetail();
  void drawForm();
  // Boxes all have small rounded corners; "filled" ones keep them too.
  static constexpr int kRound = 6;
  void rbox(int x, int y, int w, int h, int thickness = 1, int radius = kRound) const;
  void rfill(int x, int y, int w, int h, bool black, int radius = kRound) const;
  void drawSettings();
  // Form rows, drawn in the value column that starts at rx. `base` is (field <<
  // 3); zones 1..4 are the arrows.
  void rowBox(int rx, int ry, int rw, int h, Act act, int base, const std::string& value);
  void rowCycler(int rx, int ry, int rw, int h, Act act, int base,
                 const std::string& value);  // < value >
  void rowStepper(int rx, int ry, int rw, int h, Act act, int base, const std::string& value, const std::string& sub,
                  bool centerHit);  // << < value > >>
  std::string whenLabel(const daybook::Event& ev, const daybook::Occurrence& o, daybook::Day day) const;
  std::string repeatLabel(const daybook::Event& ev) const;
  bool twelveHour() const;

  // --- drawing (DaybookTodo.cpp) ---
  void drawCheck(int x, int y, int size, bool checked, bool black) const;
  // One task: a box to tick, the title, and small text (a due or done date) on
  // the right, or above the title when `twoLine`. Tapping the box ticks it;
  // tapping the rest opens the task.
  void taskRow(int y, int h, size_t index, bool inverted, bool twoLine, const std::string& small,
               const std::string& corner = "");
  std::string shortDate(daybook::Day day) const;
  std::string doneLabel(const daybook::Task& task) const;  // "done 27 Nov 14:05"
  int nowMinute() const;
  std::string listName(uint32_t id) const;
  void drawTodo();
  void drawLists();
  void drawTaskForm();
  void drawListForm();
};
