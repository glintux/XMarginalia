// Daybook, the to-do half: the task list screen, the list manager, the task and
// list forms, and the rows that show a task on the calendar screens. Rules and
// file formats are in DaybookCore.

#include <algorithm>
#include <cstdio>

#include "../../components/UITheme.h"
#include "../ui/ToyboxFonts.h"
#include "DaybookActivity.h"

namespace cal = daybook;

namespace {

constexpr int kM = 12;  // side margin
constexpr int kBody = toybox::kReadingFontId;
constexpr int kSmall = toybox::kReadingSmallFontId;
constexpr int kBold = toybox::kReadingBoldFontId;
constexpr int kBoldSmall = toybox::kReadingBoldSmallFontId;

constexpr const char* kMonthShort[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

using daybook::addMonths;

}  // namespace

// --- Small helpers
// --------------------------------------------------------------------------------

void DaybookActivity::clampList() {
  if (lists_.empty()) {
    curList_ = 0;
  } else if (curList_ >= lists_.size()) {
    curList_ = lists_.size() - 1;
  }
}

size_t DaybookActivity::openCount(const uint32_t list) const {
  size_t n = 0;
  for (const cal::Task& t : tasks_) {
    if (t.list == list && !t.done) ++n;
  }
  return n;
}

std::string DaybookActivity::listName(const uint32_t id) const {
  if (lists_.empty()) return std::string();
  return lists_[cal::listIndex(lists_, id)].name;
}

// "26 Nov", with the year added when it is not this year.
std::string DaybookActivity::doneLabel(const cal::Task& task) const {
  std::string s = "done " + shortDate(task.doneOn);
  if (task.doneMin >= 0) s += " " + cal::formatMinutes(task.doneMin, twelveHour());
  return s;
}

std::string DaybookActivity::shortDate(const cal::Day day) const {
  const cal::Date d = cal::civilFromDays(day);
  std::string s = std::to_string(d.d) + " " + kMonthShort[d.m - 1];
  if (clockKnown_ && d.y != cal::civilFromDays(today_).y) s += " " + std::to_string(d.y);
  return s;
}

void DaybookActivity::deleteList(const int index) {
  if (index < 0 || index >= static_cast<int>(lists_.size())) return;
  const uint32_t id = lists_[static_cast<size_t>(index)].id;
  if (id == cal::kMainList) return;  // the default list stays
  discard(Trash::Kind::List, index, lists_[static_cast<size_t>(index)].name);
  trash_.list = lists_[static_cast<size_t>(index)];
  for (const cal::Task& t : tasks_) {
    if (t.list == id) trash_.tasks.push_back(t);
  }
  tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(), [id](const cal::Task& t) { return t.list == id; }),
               tasks_.end());
  lists_.erase(lists_.begin() + index);
  clampList();
  const bool a = saveTasks();
  const bool b = saveLists();
  if (!a || !b) message_ = "Card write failed";
}

// zone: 0 = the row; 1..4 = "<<", "<", ">", ">>"
void DaybookActivity::taskTap(const int field, const int zone) {
  const int big = zone == 1 ? -1 : (zone == 4 ? 1 : 0);
  const int small = zone == 2 ? -1 : (zone == 3 ? 1 : 0);
  switch (field) {
    case TList: {
      const int n = static_cast<int>(lists_.size());
      const int at = static_cast<int>(cal::listIndex(lists_, taskDraft_.list));
      taskDraft_.list = lists_[static_cast<size_t>((at + (zone == 2 ? n - 1 : 1)) % n)].id;
      break;
    }
    case TDue:
      if (zone == 0) {  // the middle: clear the date, or start one today
        taskDraft_.hasDue = !taskDraft_.hasDue;
        if (taskDraft_.hasDue) taskDraft_.due = today_;
      } else if (!taskDraft_.hasDue) {  // an arrow on "None" starts from today
        taskDraft_.hasDue = true;
        taskDraft_.due = today_;
      } else if (big) {
        taskDraft_.due = cal::clampDay(addMonths(taskDraft_.due, big));
      } else {
        taskDraft_.due = cal::clampDay(taskDraft_.due + small);
      }
      break;
    case TDone:
      taskDraft_.done = !taskDraft_.done;
      taskDraft_.doneOn = taskDraft_.done ? today_ : 0;
      taskDraft_.doneMin = taskDraft_.done ? nowMinute() : -1;
      break;
    default:
      break;
  }
}

// --- Input
// ----------------------------------------------------------------------------------------

bool DaybookActivity::routeTodo(const Act act, const int32_t value) {
  const bool taskView = view_ == View::TaskForm;
  const bool listView = view_ == View::ListForm;
  const bool taskOk = value >= 0 && static_cast<size_t>(value) < tasks_.size();
  const bool listOk = value >= 0 && static_cast<size_t>(value) < lists_.size();
  switch (act) {
    case Act::OpenTodo:
      clampList();
      page_ = 0;
      view_ = View::Todo;
      break;
    case Act::OpenLists:
      page_ = 0;
      view_ = View::Lists;
      break;
    case Act::GoList:
    case Act::SelectList:
      if (listOk) curList_ = static_cast<size_t>(value);
      page_ = 0;
      view_ = View::Todo;
      break;
    case Act::NewList:
      if (lists_.size() >= cal::kMaxLists) {
        message_ = "Lists are full";
        break;
      }
      listDraft_ = cal::TaskList{};
      listEdit_ = -1;
      confirmDelete_ = false;
      view_ = View::ListForm;
      break;
    case Act::EditList:
      if (!listOk) break;
      listDraft_ = lists_[static_cast<size_t>(value)];
      listEdit_ = value;
      confirmDelete_ = false;
      view_ = View::ListForm;
      break;
    case Act::ListName:
      askString("LIST NAME", listDraft_.name, cal::kMaxListName,
                [this](const std::string& text) { listDraft_.name = cal::capitalizeFirst(text); });
      return true;
    case Act::NewTask:
      clampList();
      taskFrom_ = view_;
      taskDraft_ = cal::Task{};
      taskDraft_.list = lists_[curList_].id;
      taskDraft_.created = today_;
      taskEdit_ = -1;
      confirmDelete_ = false;
      view_ = View::TaskForm;
      break;
    case Act::NewDayTask:  // "+ To-Do" on a day: due that day
      clampList();
      taskFrom_ = view_;
      taskDraft_ = cal::Task{};
      taskDraft_.list = lists_[curList_].id;
      taskDraft_.created = today_;
      taskDraft_.hasDue = true;
      taskDraft_.due = selected_;
      taskEdit_ = -1;
      confirmDelete_ = false;
      view_ = View::TaskForm;
      break;
    case Act::EditTask:
      if (!taskOk) break;
      taskFrom_ = view_;
      taskDraft_ = tasks_[static_cast<size_t>(value)];
      taskEdit_ = value;
      confirmDelete_ = false;
      view_ = View::TaskForm;
      break;
    case Act::ToggleTask:
      if (taskOk) {
        cal::Task& t = tasks_[static_cast<size_t>(value)];
        t.done = !t.done;
        t.doneOn = t.done ? today_ : 0;
        t.doneMin = t.done ? nowMinute() : -1;
        if (!saveTasks()) message_ = "Card write failed";
      }
      break;
    case Act::TaskField:
      if ((value >> 3) == TTitle) {
        askString("TASK", taskDraft_.title, cal::kMaxTaskTitle,
                  [this](const std::string& text) { taskDraft_.title = cal::capitalizeFirst(text); });
        return true;
      }
      if ((value >> 3) == TNote) {
        askString("NOTE", taskDraft_.note, cal::kMaxTaskNote,
                  [this](const std::string& text) { taskDraft_.note = text; });
        return true;
      }
      taskTap(value >> 3, value & 7);
      break;
    case Act::Save:
      if (taskView) {
        if (taskDraft_.title.empty()) {
          message_ = "Give it a name first";
          break;
        }
        taskDraft_.doneOn = taskDraft_.done ? (taskDraft_.doneOn != 0 ? taskDraft_.doneOn : today_) : 0;
        if (taskEdit_ >= 0 && taskEdit_ < static_cast<int>(tasks_.size())) {
          tasks_[static_cast<size_t>(taskEdit_)] = taskDraft_;
        } else {
          if (tasks_.size() >= cal::kMaxTasks) {
            message_ = "To-Do list is full";
            break;
          }
          taskDraft_.id = cal::nextTaskId(tasks_);
          tasks_.push_back(taskDraft_);
        }
        if (!saveTasks()) message_ = "Card write failed";
        if (taskFrom_ == View::Todo) curList_ = cal::listIndex(lists_, taskDraft_.list);  // show where it went
        page_ = 0;
        view_ = taskFrom_;
      } else if (listView) {
        if (listDraft_.name.empty()) {
          message_ = "Give it a name first";
          break;
        }
        if (listEdit_ >= 0 && listEdit_ < static_cast<int>(lists_.size())) {
          lists_[static_cast<size_t>(listEdit_)].name = listDraft_.name;
        } else {
          if (lists_.size() >= cal::kMaxLists) {
            message_ = "Lists are full";
            break;
          }
          listDraft_.id = cal::nextListId(lists_);
          lists_.push_back(listDraft_);
        }
        if (!saveLists()) message_ = "Card write failed";
        view_ = View::Lists;
      } else {
        return false;  // the event form's Save
      }
      break;
    case Act::Delete:
    case Act::CancelDelete:
    case Act::ConfirmDelete:
      if (!taskView && !listView) return false;  // an event's own delete
      if (act == Act::Delete) {
        confirmDelete_ = taskView ? taskEdit_ >= 0
                                  : (listEdit_ >= 0 && listEdit_ < static_cast<int>(lists_.size()) &&
                                     lists_[static_cast<size_t>(listEdit_)].id != cal::kMainList);
      } else if (act == Act::CancelDelete) {
        confirmDelete_ = false;
      } else {
        if (taskView) {
          if (taskEdit_ >= 0 && taskEdit_ < static_cast<int>(tasks_.size())) {
            discard(Trash::Kind::Task, taskEdit_, tasks_[static_cast<size_t>(taskEdit_)].title);
            trash_.task = tasks_[static_cast<size_t>(taskEdit_)];
            tasks_.erase(tasks_.begin() + taskEdit_);
            if (!saveTasks()) message_ = "Card write failed";
          }
          view_ = taskFrom_;
        } else {
          deleteList(listEdit_);
          view_ = View::Lists;
        }
        confirmDelete_ = false;
        page_ = 0;
      }
      break;
    default:
      return false;
  }
  changed();
  return true;
}

// --- Drawing
// --------------------------------------------------------------------------------------

// A box with a tick in it when `checked`.
void DaybookActivity::drawCheck(const int x, const int y, const int size, const bool checked, const bool black) const {
  renderer.drawRoundedRect(x, y, size, size, 2, std::min(4, size / 4), black);
  if (!checked) return;
  const int x1 = x + size * 22 / 100;
  const int y1 = y + size * 52 / 100;
  const int x2 = x + size * 42 / 100;
  const int y2 = y + size * 74 / 100;
  const int x3 = x + size * 80 / 100;
  const int y3 = y + size * 26 / 100;
  renderer.drawLine(x1, y1, x2, y2, 3, black);
  renderer.drawLine(x2, y2, x3, y3, 3, black);
}

void DaybookActivity::taskRow(const int y, const int h, const size_t index, const bool inverted, const bool twoLine,
                              const std::string& small, const std::string& corner) {
  const cal::Task& t = tasks_[index];
  const int sw = renderer.getScreenWidth();
  const int w = sw - 2 * kM;
  const bool ink = !inverted;  // the colour of text and lines on this row
  if (inverted) {
    rfill(kM, y, w, h, true);
  } else {
    rbox(kM, y, w, h);
  }
  const int bs = std::min(32, h - 10);
  drawCheck(kM + 10, y + (h - bs) / 2, bs, t.done, ink);
  const int tx = kM + 10 + bs + 12;
  const int right = kM + w - 8;
  const std::string title = cal::foldForDisplay(t.title);
  const int lhB = renderer.getLineHeight(kBold);
  const int lhS = renderer.getLineHeight(kSmall);
  // The list this to-do belongs to: small, in a little box in the bottom right corner, and the title yields to it.
  const std::string cornerText = corner.empty() ? corner : renderer.truncatedText(kSmall, corner.c_str(), 200);
  const int cw = cornerText.empty() ? 0 : renderer.getTextWidth(kSmall, cornerText.c_str()) + 8;
  if (twoLine) {
    textBox(kSmall, tx - 4, y + 4, right - tx, lhS + 4, small, false, ink);
    const std::string shown = renderer.truncatedText(kBold, title.c_str(), right - tx - cw);
    const int ty = y + 6 + lhS;
    renderer.drawText(kBold, tx, ty, shown.c_str(), ink);
    if (t.done) {
      const int sy = ty + lhB / 2;
      renderer.drawLine(tx, sy, tx + renderer.getTextWidth(kBold, shown.c_str()), sy, 2, ink);
    }
  } else {
    const int rw = small.empty() ? 0 : renderer.getTextWidth(kSmall, small.c_str()) + 12;
    const std::string shown = renderer.truncatedText(kBold, title.c_str(), std::max(right - tx - std::max(rw, cw), 16));
    const int ty = y + (h - lhB) / 2;
    renderer.drawText(kBold, tx, ty, shown.c_str(), ink);
    if (t.done) {
      const int sy = ty + lhB / 2;
      renderer.drawLine(tx, sy, tx + renderer.getTextWidth(kBold, shown.c_str()), sy, 2, ink);
    }
    if (rw > 0) renderer.drawText(kSmall, right - rw + 6, y + (h - lhS) / 2, small.c_str(), ink);
  }
  if (cw > 0) {  // a box with small round corners says the list's name can be touched
    renderer.drawRoundedRect(right - cw, y + h - lhS - 7, cw, lhS + 4, 1, 4, ink);
    renderer.drawText(kSmall, right - cw + 4, y + h - lhS - 5, cornerText.c_str(), ink);
  }
  addHit(kM, y, w, h, Act::EditTask, static_cast<int32_t>(index));
  if (!corner.empty()) {  // a tap on the list's name goes to that list
    const int cwHit =
        std::min(renderer.getTextWidth(kSmall, renderer.truncatedText(kSmall, corner.c_str(), 200).c_str()) + 8, 208);
    const int cx = right - cwHit - 4;
    addHit(cx, y + h / 2, kM + w - cx, h - h / 2, Act::GoList, static_cast<int32_t>(cal::listIndex(lists_, t.list)));
  }
  addHit(kM, y, 56, h, Act::ToggleTask,
         static_cast<int32_t>(index));  // added last, so it wins its corner
}

void DaybookActivity::drawTodo() {
  clampList();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const cal::TaskList& list = lists_[curList_];
  const bool several = lists_.size() > 1;
  const int titleW = sw - 2 * kM - 70 - (several ? 60 : 0) - 8;
  const int titleFont =
      renderer.getTextWidth(kBold, cal::foldForDisplay(list.name).c_str()) <= titleW ? kBold : kBoldSmall;
  int y = drawHeader(list.name,
                     several ? std::to_string(curList_ + 1) + "/" + std::to_string(lists_.size()) : std::string(),
                     titleFont);
  addHit(kM + 70, metrics.topPadding + 6, sw - 2 * kM - 70, renderer.getLineHeight(kBold) + 20, Act::OpenLists);

  std::vector<size_t> ix;
  cal::tasksOfList(tasks_, list.id, ix);
  const char* labels[2] = {"Lists", "+ Task"};
  const Act acts[2] = {Act::OpenLists, Act::NewTask};
  const int barY = bottomBar(2, labels, acts);

  const int rowH = renderer.getLineHeight(kBold) + 20;
  const int pagerH = renderer.getLineHeight(kBoldSmall) + 18;
  const int avail = barY - 8 - y;
  int perPage = std::max(1, avail / rowH);
  if (static_cast<int>(ix.size()) > perPage) perPage = std::max(1, (avail - pagerH - 6) / rowH);
  pages_ = std::max(1, (static_cast<int>(ix.size()) + perPage - 1) / perPage);
  page_ = std::clamp(page_, 0, pages_ - 1);

  if (ix.empty()) textBox(kBody, 0, y + 20, sw, rowH, "No tasks yet", true);
  for (int i = 0; i < perPage; ++i) {
    const size_t idx = static_cast<size_t>(page_ * perPage + i);
    if (idx >= ix.size()) break;
    const cal::Task& t = tasks_[ix[idx]];
    const bool late = cal::isOverdue(t, today_);
    std::string small;
    if (t.done)
      small = doneLabel(t);
    else if (t.hasDue)
      small = (late ? "due " : "") + shortDate(t.due);
    taskRow(y + i * rowH, rowH - 4, ix[idx], late, false, small);
  }
  if (pages_ > 1) drawPager(barY - 6 - pagerH, pagerH);
  drawScrollbar(y, barY - 8 - (pages_ > 1 ? pagerH + 6 : 0));
  if (!message_.empty()) textBox(kBoldSmall, 0, barY - pagerH - 12, sw, pagerH, message_, true);
}

void DaybookActivity::drawLists() {
  const int sw = renderer.getScreenWidth();
  int y = drawHeader("To-Do lists", "");
  const char* labels[1] = {"+ List"};
  const Act acts[1] = {Act::NewList};
  const int barY = bottomBar(1, labels, acts);

  const int rowH = renderer.getLineHeight(kBold) + 26;
  const int editW = 92;
  const int pagerH = renderer.getLineHeight(kBoldSmall) + 18;
  int perPage = std::max(1, (barY - 8 - y) / (rowH + 8));
  if (static_cast<int>(lists_.size()) > perPage) perPage = std::max(1, (barY - 8 - pagerH - 6 - y) / (rowH + 8));
  pages_ = std::max(1, (static_cast<int>(lists_.size()) + perPage - 1) / perPage);
  page_ = std::clamp(page_, 0, pages_ - 1);
  for (int k = 0; k < perPage; ++k) {
    const size_t i = static_cast<size_t>(page_ * perPage + k);
    if (i >= lists_.size()) break;
    const int ry = y + k * (rowH + 8);
    rbox(kM, ry, sw - 2 * kM, rowH, 2, 8);
    if (i == curList_) rbox(kM + 3, ry + 3, sw - 2 * kM - 6, rowH - 6, 1, 6);
    addHit(kM, ry, sw - 2 * kM, rowH, Act::SelectList, static_cast<int32_t>(i));
    const std::string count = std::to_string(openCount(lists_[i].id)) + " open";
    const int cw = renderer.getTextWidth(kSmall, count.c_str()) + 16;
    textBox(kBold, kM + 6, ry, sw - 2 * kM - editW - cw - 12, rowH, lists_[i].name, false);
    textBox(kSmall, sw - kM - editW - cw - 4, ry, cw, rowH, count, true);
    button(sw - kM - editW - 4, ry + 4, editW, rowH - 8, "Edit", Act::EditList, static_cast<int32_t>(i), false,
           kBoldSmall);
  }
  if (pages_ > 1) drawPager(barY - 6 - pagerH, pagerH);
  drawScrollbar(y, barY - 8 - (pages_ > 1 ? pagerH + 6 : 0));
  if (!message_.empty()) textBox(kBoldSmall, 0, barY - 40 - (pages_ > 1 ? pagerH + 6 : 0), sw, 32, message_, true);
}

void DaybookActivity::drawTaskForm() {
  const int sw = renderer.getScreenWidth();
  int y = drawHeader(taskEdit_ >= 0 ? "Edit task" : "New task", "");

  if (confirmDelete_) {
    const int lhB = renderer.getLineHeight(kBold);
    const std::string title = cal::foldForDisplay(taskDraft_.title);
    for (const std::string& line : renderer.wrappedText(kBold, title.c_str(), sw - 2 * kM, 3)) {
      renderer.drawText(kBold, kM, y, line.c_str(), true);
      y += lhB + 2;
    }
    const char* labels[2] = {"Keep it", "Delete"};
    const Act acts[2] = {Act::CancelDelete, Act::ConfirmDelete};
    const int barY = bottomBar(2, labels, acts);
    const int lhQ = renderer.getLineHeight(kBoldSmall) + 2;
    const std::string question = "Delete this task?";
    renderer.drawText(kBoldSmall, (sw - renderer.getTextWidth(kBoldSmall, question.c_str())) / 2, barY - 12 - lhQ,
                      question.c_str(), true);
    return;
  }

  std::vector<int> rows{TTitle, TNote, TList, TDue};
  if (taskEdit_ >= 0) rows.push_back(TDone);

  int barY = 0;
  if (taskEdit_ >= 0) {
    const char* labels[3] = {"Cancel", "Delete", "Save"};
    const Act acts[3] = {Act::Cancel, Act::Delete, Act::Save};
    barY = bottomBar(3, labels, acts);
  } else {
    const char* labels[2] = {"Cancel", "Save"};
    const Act acts[2] = {Act::Cancel, Act::Save};
    barY = bottomBar(2, labels, acts);
  }
  const int infoH = renderer.getLineHeight(kSmall) + 6;
  const int msgH = renderer.getLineHeight(kSmall) + 6;
  const int avail = barY - 6 - y - msgH - infoH;
  const int rowH = std::min(64, avail / static_cast<int>(rows.size()));
  static constexpr const char* kLabel[TCount] = {"Task", "Note", "List", "Due", "Done"};
  const int labelW = 84;
  const int rx = kM + labelW;
  const int rw = sw - kM - rx;

  for (size_t r = 0; r < rows.size(); ++r) {
    const int f = rows[r];
    const int ry = y + static_cast<int>(r) * rowH;
    const int h = rowH - 6;
    const int base = f << 3;
    textBox(kSmall, kM, ry, labelW, h, kLabel[f], false);
    switch (f) {
      case TTitle:
        rowBox(rx, ry, rw, h, Act::TaskField, base, taskDraft_.title.empty() ? "tap to type" : taskDraft_.title);
        break;
      case TNote:
        rowBox(rx, ry, rw, h, Act::TaskField, base, taskDraft_.note.empty() ? "-" : taskDraft_.note);
        break;
      case TList:
        rowCycler(rx, ry, rw, h, Act::TaskField, base, listName(taskDraft_.list));
        break;
      case TDue:  // tap the date itself to clear it, or to start one
        rowStepper(rx, ry, rw, h, Act::TaskField, base, taskDraft_.hasDue ? cal::formatDate(taskDraft_.due) : "None",
                   taskDraft_.hasDue ? weekdayName(taskDraft_.due) : "", true);
        break;
      case TDone:
        rowCycler(rx, ry, rw, h, Act::TaskField, base,
                  taskDraft_.done ? "Yes  " + shortDate(taskDraft_.doneOn != 0 ? taskDraft_.doneOn : today_) : "No");
        break;
      default:
        break;
    }
  }
  if (taskEdit_ >= 0) {
    std::string info = "Created " + cal::formatDate(taskDraft_.created);
    textBox(kSmall, kM, y + static_cast<int>(rows.size()) * rowH, sw - 2 * kM, infoH, info, false);
  }
  if (!message_.empty()) textBox(kBoldSmall, 0, barY - msgH - 2, sw, msgH, message_, true);
}

void DaybookActivity::drawListForm() {
  const int sw = renderer.getScreenWidth();
  int y = drawHeader(listEdit_ >= 0 ? "Rename list" : "New list", "");
  const bool editing = listEdit_ >= 0 && listEdit_ < static_cast<int>(lists_.size());
  const bool isMain = editing && lists_[static_cast<size_t>(listEdit_)].id == cal::kMainList;

  if (confirmDelete_) {
    const int lhB = renderer.getLineHeight(kBold);
    const std::string name = cal::foldForDisplay(listDraft_.name);
    for (const std::string& line : renderer.wrappedText(kBold, name.c_str(), sw - 2 * kM, 2)) {
      renderer.drawText(kBold, kM, y, line.c_str(), true);
      y += lhB + 2;
    }
    size_t n = 0;
    if (editing) {
      for (const cal::Task& t : tasks_) n += t.list == lists_[static_cast<size_t>(listEdit_)].id ? 1 : 0;
    }
    const char* labels[2] = {"Keep it", "Delete"};
    const Act acts[2] = {Act::CancelDelete, Act::ConfirmDelete};
    const int barY = bottomBar(2, labels, acts);
    const int lhQ = renderer.getLineHeight(kBoldSmall) + 2;
    const std::string question =
        n == 0 ? "Delete this list?" : "Delete this list and its " + std::to_string(n) + " tasks?";
    const auto lines = renderer.wrappedText(kBoldSmall, question.c_str(), sw - 2 * kM, 2);
    int qy = barY - 12 - static_cast<int>(lines.size()) * lhQ;
    for (const std::string& line : lines) {
      renderer.drawText(kBoldSmall, (sw - renderer.getTextWidth(kBoldSmall, line.c_str())) / 2, qy, line.c_str(), true);
      qy += lhQ;
    }
    return;
  }

  const int labelW = 84;
  const int rx = kM + labelW;
  const int rw = sw - kM - rx;
  const int h = 56;
  textBox(kSmall, kM, y + 10, labelW, h, "Name", false);
  rowBox(rx, y + 10, rw, h, Act::ListName, 0, listDraft_.name.empty() ? "tap to type" : listDraft_.name);
  y += h + 24;
  if (isMain) {
    const std::string note = "The default list can be renamed, but not removed.";
    for (const std::string& line : renderer.wrappedText(kSmall, note.c_str(), sw - 2 * kM, 3)) {
      renderer.drawText(kSmall, kM, y, line.c_str(), true);
      y += renderer.getLineHeight(kSmall) + 2;
    }
  }

  int barY = 0;
  if (editing && !isMain) {
    const char* labels[3] = {"Cancel", "Delete", "Save"};
    const Act acts[3] = {Act::Cancel, Act::Delete, Act::Save};
    barY = bottomBar(3, labels, acts);
  } else {
    const char* labels[2] = {"Cancel", "Save"};
    const Act acts[2] = {Act::Cancel, Act::Save};
    barY = bottomBar(2, labels, acts);
  }
  if (!message_.empty()) textBox(kBoldSmall, 0, barY - 40, sw, 32, message_, true);
}
