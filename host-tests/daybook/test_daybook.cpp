#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/apps_local/daybook/DaybookCore.h"

using namespace daybook;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                       \
  do {                                                 \
    ++g_checks;                                        \
    if (!(c)) {                                        \
      ++g_fail;                                        \
      std::printf("FAIL line %d: %s\n", __LINE__, #c); \
    }                                                  \
  } while (0)

static Day D(int y, int m, int d) { return daysFromCivil(y, m, d); }

static Event ev(const char* title, Day s, Day e, Repeat r = Repeat::None, int every = 1) {
  Event x;
  x.id = 1;
  x.title = title;
  x.start = s;
  x.end = e;
  x.repeat = r;
  x.every = static_cast<uint16_t>(every);
  return x;
}

static size_t countIn(const std::vector<Event>& v, Day from, Day to) {
  std::vector<Occurrence> o;
  occurrencesIn(v, from, to, o);
  return o.size();
}

int main() {
  // --- dates ---
  CHECK(D(1970, 1, 1) == 0);
  CHECK(weekday(D(1970, 1, 1)) == 4);    // Thursday
  CHECK(weekday(D(2000, 1, 1)) == 6);    // Saturday
  CHECK(weekday(D(2026, 11, 26)) == 4);  // Thursday
  CHECK(weekday(D(1969, 12, 31)) == 3);  // Wednesday, before the epoch
  for (Day d = -800; d < 30000; d += 7) {
    const Date c = civilFromDays(d);
    CHECK(daysFromCivil(c.y, c.m, c.d) == d);
  }
  CHECK(daysInMonth(2024, 2) == 29);
  CHECK(daysInMonth(2026, 2) == 28);
  CHECK(daysInMonth(1900, 2) == 28);
  CHECK(daysInMonth(2000, 2) == 29);
  CHECK(isoWeek(D(2026, 11, 25)) == 48);
  CHECK(isoWeek(D(2020, 12, 31)) == 53);
  CHECK(isoWeek(D(2021, 1, 3)) == 53);
  CHECK(isoWeek(D(2021, 1, 4)) == 1);
  CHECK(isoWeek(D(2024, 12, 30)) == 1);
  CHECK(isoWeek(D(2026, 1, 1)) == 1);

  Day p = 0;
  CHECK(parseDate("2026-11-24", p) && p == D(2026, 11, 24));
  CHECK(!parseDate("2026-02-30", p));
  CHECK(!parseDate("2026-13-01", p));
  CHECK(!parseDate("garbage", p));
  CHECK(!parseDate("2026-11-24x", p));
  CHECK(formatDate(D(2026, 11, 24)) == "2026-11-24");
  CHECK(formatSpan(D(2026, 11, 20), D(2026, 11, 24)) == "11.20-11.24");
  CHECK(formatMinutes(0, false) == "00:00");
  CHECK(formatMinutes(9 * 60 + 5, false) == "09:05");
  CHECK(formatMinutes(0, true) == "12:00 AM");
  CHECK(formatMinutes(13 * 60 + 30, true) == "1:30 PM");
  CHECK(formatMinutes(12 * 60, true) == "12:00 PM");

  // --- single and multi-day ---
  {
    std::vector<Event> v{ev("a", D(2026, 11, 20), D(2026, 11, 24))};
    CHECK(countIn(v, D(2026, 11, 19), D(2026, 11, 19)) == 0);
    CHECK(countIn(v, D(2026, 11, 20), D(2026, 11, 20)) == 1);
    CHECK(countIn(v, D(2026, 11, 22), D(2026, 11, 22)) == 1);  // middle of the span
    CHECK(countIn(v, D(2026, 11, 24), D(2026, 11, 24)) == 1);
    CHECK(countIn(v, D(2026, 11, 25), D(2026, 11, 25)) == 0);
    CHECK(countIn(v, D(2026, 11, 1), D(2026, 11, 30)) == 1);
  }

  // --- daily / weekly ---
  {
    std::vector<Event> daily{ev("d", D(2026, 1, 1), D(2026, 1, 1), Repeat::Daily, 3)};
    CHECK(countIn(daily, D(2026, 1, 1), D(2026, 1, 10)) == 4);  // 1,4,7,10
    CHECK(countIn(daily, D(2026, 1, 2), D(2026, 1, 3)) == 0);
    CHECK(countIn(daily, D(2030, 6, 1), D(2030, 6, 1)) == (((D(2030, 6, 1) - D(2026, 1, 1)) % 3 == 0) ? 1u : 0u));
    CHECK(countIn(daily, D(2025, 1, 1), D(2025, 12, 31)) == 0);  // before it starts

    std::vector<Event> weekly{ev("w", D(2026, 11, 2), D(2026, 11, 2), Repeat::Weekly, 2)};
    CHECK(countIn(weekly, D(2026, 11, 2), D(2026, 11, 30)) == 3);  // 2, 16, 30
    CHECK(countIn(weekly, D(2026, 11, 9), D(2026, 11, 9)) == 0);
    CHECK(countIn(weekly, D(2026, 11, 16), D(2026, 11, 16)) == 1);

    // A repeating multi-day event shows on the tail of an earlier occurrence.
    std::vector<Event> span{ev("s", D(2026, 11, 1), D(2026, 11, 3), Repeat::Weekly)};
    CHECK(countIn(span, D(2026, 11, 9), D(2026, 11, 9)) == 1);  // day 2 of the 8-10 run
    CHECK(countIn(span, D(2026, 11, 11), D(2026, 11, 14)) == 0);
  }

  // --- until ---
  {
    Event e = ev("u", D(2026, 1, 1), D(2026, 1, 1), Repeat::Daily);
    e.hasUntil = true;
    e.until = D(2026, 1, 5);
    std::vector<Event> v{e};
    CHECK(countIn(v, D(2026, 1, 1), D(2026, 12, 31)) == 5);
  }

  // --- monthly / yearly clamping ---
  {
    std::vector<Event> m{ev("m", D(2026, 1, 31), D(2026, 1, 31), Repeat::Monthly)};
    std::vector<Occurrence> o;
    occurrencesIn(m, D(2026, 2, 1), D(2026, 2, 28), o);
    CHECK(o.size() == 1 && o[0].start == D(2026, 2, 28));  // 31st clamps to Feb 28
    o.clear();
    occurrencesIn(m, D(2026, 4, 1), D(2026, 4, 30), o);
    CHECK(o.size() == 1 && o[0].start == D(2026, 4, 30));
    o.clear();
    occurrencesIn(m, D(2026, 3, 1), D(2026, 3, 31), o);
    CHECK(o.size() == 1 && o[0].start == D(2026, 3, 31));
    CHECK(countIn(m, D(2026, 1, 1), D(2026, 12, 31)) == 12);

    std::vector<Event> m2{ev("m2", D(2026, 1, 15), D(2026, 1, 15), Repeat::Monthly, 2)};
    CHECK(countIn(m2, D(2026, 1, 1), D(2026, 12, 31)) == 6);
    CHECK(countIn(m2, D(2026, 2, 1), D(2026, 2, 28)) == 0);

    std::vector<Event> y{ev("y", D(2024, 2, 29), D(2024, 2, 29), Repeat::Yearly)};
    o.clear();
    occurrencesIn(y, D(2026, 2, 1), D(2026, 3, 1), o);
    CHECK(o.size() == 1 && o[0].start == D(2026, 2, 28));  // leap day clamps
    o.clear();
    occurrencesIn(y, D(2028, 2, 1), D(2028, 3, 1), o);
    CHECK(o.size() == 1 && o[0].start == D(2028, 2, 29));
    CHECK(countIn(y, D(2024, 1, 1), D(2033, 12, 31)) == 10);

    std::vector<Event> far{ev("far", D(2026, 5, 5), D(2026, 5, 5), Repeat::Monthly)};
    CHECK(countIn(far, D(2090, 5, 1), D(2090, 5, 31)) == 1);  // far future, no timeout
  }

  // --- ordering on a day ---
  {
    Event allday = ev("zzz all day", D(2026, 11, 26), D(2026, 11, 26));
    Event late = ev("late", D(2026, 11, 26), D(2026, 11, 26));
    late.allDay = false;
    late.startMin = 18 * 60;
    Event early = ev("early", D(2026, 11, 26), D(2026, 11, 26));
    early.allDay = false;
    early.startMin = 7 * 60;
    std::vector<Event> v{late, allday, early};
    std::vector<Occurrence> o;
    occurrencesOn(v, D(2026, 11, 26), o);
    CHECK(o.size() == 3);
    CHECK(v[o[0].event].title == "zzz all day");
    CHECK(v[o[1].event].title == "early");
    CHECK(v[o[2].event].title == "late");
  }

  // --- busy bits ---
  {
    const Day first = D(2026, 11, 1) - weekday(D(2026, 11, 1));  // grid starts on a Sunday
    std::vector<Event> v{ev("a", D(2026, 11, 20), D(2026, 11, 22)), ev("b", D(2026, 11, 1), D(2026, 11, 1))};
    const uint64_t bits = busyDays(v, first);
    CHECK((bits >> (D(2026, 11, 1) - first)) & 1);
    CHECK((bits >> (D(2026, 11, 21) - first)) & 1);
    CHECK(!((bits >> (D(2026, 11, 10) - first)) & 1));
    CHECK(busyDays({}, first) == 0);
  }

  // --- display fold ---
  CHECK(foldForDisplay("Fodrász") == "Fodr\xC3\xA1sz");  // a with acute stays
  CHECK(foldForDisplay("\xC5\x91") == "\xC3\xB6");       // o double acute -> o umlaut
  CHECK(foldForDisplay("\xC5\xB1") == "\xC3\xBC");       // u double acute -> u umlaut
  CHECK(foldForDisplay("\xC5\x90\xC5\xB0") == "\xC3\x96\xC3\x9C");
  CHECK(foldForDisplay("x\xE2\x82\xAC") == "x?");  // euro sign has no glyph
  CHECK(foldForDisplay("plain") == "plain");
  CHECK(foldForDisplay("trunc\xC3") == "trunc?");  // torn UTF-8 does not read past the end

  // --- storage round trip ---
  {
    Event a =
        ev("T\xC5\x91"
           "k\xC3\xA9s\tmeet",
           D(2026, 11, 20), D(2026, 11, 24), Repeat::Monthly, 2);
    a.id = 7;
    a.allDay = false;
    a.startMin = 9 * 60 + 30;
    a.endMin = 11 * 60;
    a.location = "Room 5";
    a.note = "line one\nline two\\ end";
    a.hasUntil = true;
    a.until = D(2027, 1, 1);
    Event b = ev("simple", D(2026, 12, 24), D(2026, 12, 24));
    b.id = 8;
    std::vector<Event> in{a, b}, out;
    const std::string text = serialize(in);
    CHECK(parse(text, out) == 2);
    CHECK(out.size() == 2);
    CHECK(out[0].id == 7 && out[0].title == a.title && out[0].location == "Room 5");
    CHECK(out[0].note == a.note);
    CHECK(out[0].start == a.start && out[0].end == a.end && !out[0].allDay);
    CHECK(out[0].startMin == 570 && out[0].endMin == 660);
    CHECK(out[0].repeat == Repeat::Monthly && out[0].every == 2);
    CHECK(out[0].hasUntil && out[0].until == D(2027, 1, 1));
    CHECK(out[1].allDay && !out[1].hasUntil && out[1].repeat == Repeat::None);
    CHECK(nextId(out) == 9);
    CHECK(nextId({}) == 1);
  }

  // --- hostile / damaged files ---
  {
    std::vector<Event> out;
    CHECK(parse("", out) == 0);
    CHECK(parse("garbage\nE\nE\tx\n\n\t\t\n", out) == 0);
    CHECK(parse("V1\nE\t1\tok\t2026-11-20\t2026-11-19\t1\t0\t0\t0\t1\t-\t\t\n",
                out) == 0);  // end before start
    CHECK(parse("V1\nE\t1\tok\t2026-02-30\t2026-02-30\t1\t0\t0\t0\t1\t-\t\t\n",
                out) == 0);  // not a date
    CHECK(parse("V1\nE\t1\tgood\t2026-11-20\t2026-11-20\t1\t0\t0\t9\t0\t-\t\t\n",
                out) == 1);  // bad repeat/every healed
    CHECK(out[0].repeat == Repeat::None && out[0].every == 1);
    std::string huge = "V1\n";
    for (int i = 0; i < 1000; ++i) huge += "E\t1\tx\t2026-01-01\t2026-01-01\t1\t0\t0\t0\t1\t-\t\t\n";
    out.clear();
    CHECK(parse(huge, out) == kMaxEvents);
    std::string big(kMaxFileBytes + 1, 'x');
    CHECK(parse(big, out) == 0);
    Event longTitle = ev(std::string(500, 'a').c_str(), D(2026, 1, 1), D(2026, 1, 1));
    std::vector<Event> lt{longTitle}, back;
    parse(serialize(lt), back);
    CHECK(back.size() == 1 && back[0].title.size() == kMaxTitle);
  }

  // --- addMonths ---
  CHECK(addMonths(D(2026, 1, 31), 1) == D(2026, 2, 28));
  CHECK(addMonths(D(2024, 1, 31), 1) == D(2024, 2, 29));
  CHECK(addMonths(D(2026, 11, 26), 1) == D(2026, 12, 26));
  CHECK(addMonths(D(2026, 12, 26), 1) == D(2027, 1, 26));
  CHECK(addMonths(D(2026, 1, 15), -1) == D(2025, 12, 15));
  CHECK(addMonths(D(2026, 3, 31), -1) == D(2026, 2, 28));
  CHECK(addMonths(D(2026, 5, 5), 0) == D(2026, 5, 5));

  // --- to-dos ---
  {
    std::vector<Task> ts;
    auto mk = [](uint32_t id, uint32_t list, const char* title, bool due, Day d, bool done = false, Day doneOn = 0) {
      Task t;
      t.id = id;
      t.list = list;
      t.title = title;
      t.created = D(2026, 11, 1);
      t.hasDue = due;
      t.due = d;
      t.done = done;
      t.doneOn = doneOn;
      return t;
    };
    ts.push_back(mk(1, 1, "late", true, D(2026, 11, 20)));
    ts.push_back(mk(2, 1, "undated", false, 0));
    ts.push_back(mk(3, 1, "today", true, D(2026, 11, 26)));
    ts.push_back(mk(4, 1, "done old", true, D(2026, 11, 10), true, D(2026, 11, 11)));
    ts.push_back(mk(5, 1, "done new", false, 0, true, D(2026, 11, 25)));
    ts.push_back(mk(6, 2, "other list", true, D(2026, 11, 26)));
    ts.push_back(mk(7, 1, "soon", true, D(2026, 11, 27)));

    CHECK(isOverdue(ts[0], D(2026, 11, 26)));
    CHECK(!isOverdue(ts[2], D(2026, 11, 26)));  // due today is not overdue
    CHECK(!isOverdue(ts[3], D(2026, 11, 26)));  // done is never overdue
    CHECK(!isOverdue(ts[1], D(2026, 11, 26)));  // undated is never overdue

    std::vector<size_t> ix;
    tasksOfList(ts, 1, ix);
    CHECK(ix.size() == 6);
    CHECK(ix[0] == 0 && ix[1] == 2 && ix[2] == 6 && ix[3] == 1);  // late, today, soon, then undated
    CHECK(ix[4] == 4 && ix[5] == 3);                              // done: latest first
    tasksOfList(ts, 2, ix);
    CHECK(ix.size() == 1 && ix[0] == 5);
    tasksOfList(ts, 9, ix);
    CHECK(ix.empty());

    overdueTasks(ts, D(2026, 11, 26), ix);
    CHECK(ix.size() == 1 && ix[0] == 0);
    overdueTasks(ts, D(2026, 11, 28), ix);
    CHECK(ix.size() == 4 && ix[0] == 0 && ix[1] == 2 && ix[2] == 5 && ix[3] == 6);

    ts[3].done = false;  // a task due on a day where one is done and one open
    ts.push_back(mk(8, 1, "done same day", true, D(2026, 11, 26), true, D(2026, 11, 26)));
    tasksDueOn(ts, D(2026, 11, 26), ix);
    CHECK(ix.size() == 3 && ts[ix[2]].done);  // open ones first
    ts[3].done = true;

    uint64_t any = 0, open = 0;
    taskBusy(ts, D(2026, 11, 23), any, open);
    CHECK((any >> 3) & 1 && (open >> 3) & 1);  // 26th
    CHECK((any >> 4) & 1 && (open >> 4) & 1);  // 27th
    CHECK(!((any >> 0) & 1));                  // 23rd: nothing
    taskBusy(ts, D(2026, 11, 1), any, open);
    CHECK((any >> 9) & 1 && !((open >> 9) & 1));  // 10th: only a done task
    CHECK(!((any >> 41) & 1));

    // round trip, including hostile text
    ts[1].title = "tab\there \\ and\nnewline";
    ts[1].note = "n\to\\te";
    std::string blob = serializeTasks(ts);
    std::vector<Task> back;
    CHECK(parseTasks(blob, back) == ts.size());
    CHECK(back[1].title == ts[1].title && back[1].note == ts[1].note);
    CHECK(back[3].done && back[3].doneOn == D(2026, 11, 11));
    CHECK(back[0].hasDue && back[0].due == D(2026, 11, 20) && !back[1].hasDue);
    CHECK(back[5].list == 2);
    CHECK(nextTaskId(back) == 9 && nextTaskId({}) == 1);

    // damaged task files never crash and never keep junk
    CHECK(parseTasks("", back) == 0);
    CHECK(parseTasks("V1\nT\n T\t1\n\nT\t1\t1\t\t2026-11-01\t-\t-\t\n", back) == 0);  // empty title
    CHECK(parseTasks("V1\nT\t1\t1\tx\t2026-02-30\t-\t-\t\n", back) == 0);             // bad created
    CHECK(parseTasks("V1\nT\t1\t1\tx\t2026-11-01\t2026-13-01\t-\t\n", back) == 1 && !back[0].hasDue);
    std::string many = "V1\n";
    for (int i = 0; i < 1200; ++i) many += "T\t1\t1\tx\t2026-01-01\t-\t-\t\n";
    CHECK(parseTasks(many, back) == kMaxTasks);
    CHECK(parseTasks(std::string(kMaxTaskFileBytes + 1, 'x'), back) == 0);
    Task lt = mk(1, 1, std::string(300, 'a').c_str(), false, 0);
    lt.note = std::string(900, 'b');
    std::vector<Task> lv{lt};
    parseTasks(serializeTasks(lv), back);
    CHECK(back.size() == 1 && back[0].title.size() == kMaxTaskTitle && back[0].note.size() == kMaxTaskNote);

    // lists
    std::vector<TaskList> ls;
    CHECK(parseLists("", ls) == 0);
    std::vector<Task> orphans{mk(1, 5, "lost", false, 0)};
    repairLists(ls, orphans);
    CHECK(ls.size() == 1 && ls[0].id == kMainList && ls[0].name == "Main To-Do's");
    CHECK(orphans[0].list == kMainList);  // moved to the default list
    ls[0].name = "Home";                  // the default list can be renamed...
    ls.push_back(TaskList{nextListId(ls), "Work"});
    CHECK(ls[1].id == 2 && nextListId(ls) == 3);
    std::vector<TaskList> lb;
    CHECK(parseLists(serializeLists(ls), lb) == 2 && lb[0].name == "Home" && lb[1].name == "Work");
    std::vector<Task> none;
    repairLists(lb, none);
    CHECK(lb[0].id == kMainList && lb[0].name == "Home");  // ...and a repair keeps its name
    CHECK(listIndex(lb, 2) == 1 && listIndex(lb, 99) == 0);
    CHECK(parseLists("V1\nL\t0\tzero\nL\t3\t\nL\t4\tok\nL\t4\tdup\nL\n", lb) == 1 && lb[0].name == "ok");
    std::vector<TaskList> noMain{TaskList{4, "Only"}};
    std::vector<Task> t4;
    repairLists(noMain, t4);
    CHECK(noMain.size() == 2 && noMain[0].id == kMainList && noMain[1].id == 4);
    std::string lmany = "V1\n";
    for (int i = 1; i < 40; ++i) lmany += "L\t" + std::to_string(i) + "\tl\n";
    CHECK(parseLists(lmany, lb) == kMaxLists);
  }

  {  // capitalizeFirst: titles are fixed up on save, not while typing
    CHECK(capitalizeFirst("buy milk") == "Buy milk");
    CHECK(capitalizeFirst("Already") == "Already" && capitalizeFirst("").empty() && capitalizeFirst("1st") == "1st");
    CHECK(capitalizeFirst("\xC3\xA9let") == "\xC3\x89let");  // é -> É
    CHECK(capitalizeFirst("\xC5\x91ri") == "\xC5\x90ri");    // ő -> Ő
    CHECK(capitalizeFirst("\xC5\xB1r") == "\xC5\xB0r");      // ű -> Ű
    CHECK(capitalizeFirst("\xC3\xB7") == "\xC3\xB7");        // ÷ is not a letter
  }

  {  // search: case and accents ignored, every word must appear
    CHECK(searchKey("Dentist") == "dentist");
    CHECK(searchKey("\xC3\x81rv\xC3\xADzt\xC5\xB1r\xC5\x91") == "arvizturo");  // Arvizturo with accents
    CHECK(searchKey("\xC3\xA1rv\xC3\xADzt\xC5\xB1r\xC5\x91") == "arvizturo");
    CHECK(searchKey("\xC3\x9F") == "\xC3\x9F");                           // a letter with no plain form stays
    CHECK(searchKey("\xC3\x97") == "\xC3\x97");                           // the multiplication sign is untouched
    CHECK(searchKey("\xC3\x96") == "o" && searchKey("\xC3\x9C") == "u");  // O and U with umlaut
    Event e = ev("Call the Dentist", D(2026, 11, 27), D(2026, 11, 27));
    e.location = "Main street";
    e.note = "bring \xC3\x89va's card";
    CHECK(matchesQuery(e, "dentist") && matchesQuery(e, "call dent") && matchesQuery(e, "dentist call"));
    CHECK(matchesQuery(e, "main") && matchesQuery(e, "eva"));
    CHECK(!matchesQuery(e, "dentists") && !matchesQuery(e, "") && !matchesQuery(e, "   "));
    CHECK(!matchesQuery(e, "dentist zebra"));
    Task k;
    k.title = "Buy MILK";
    k.note = "2 litres";
    CHECK(matchesQuery(k, "milk") && matchesQuery(k, "buy litres") && !matchesQuery(k, "bread"));
  }

  {  // sleep agenda: what is shown, and what is over
    std::vector<Event> evs;
    const Day today = D(2026, 11, 27);
    Event a = ev("Standup", today, today);
    a.allDay = false;
    a.startMin = 9 * 60;
    a.endMin = 9 * 60 + 30;
    Event b = ev("Dentist", today, today);
    b.allDay = false;
    b.startMin = 14 * 60 + 30;
    b.endMin = 0;                            // no end time
    Event c = ev("Birthday", today, today);  // all day
    Event d = ev("Conference", today - 1, today + 1);
    Event f = ev("Trip home", today - 2, today);
    f.allDay = false;
    f.startMin = 20 * 60;
    f.endMin = 11 * 60;
    Event g = ev("Gym", today + 1, today + 1);
    g.allDay = false;
    g.startMin = 7 * 60;
    g.endMin = 8 * 60;
    Event h = ev("Day off", today + 1, today + 1);
    evs = {a, b, c, d, f, g, h};
    std::vector<Occurrence> occ;
    occurrencesOn(evs, today, occ);
    CHECK(occ.size() == 5);
    CHECK(!eventOver(a, Occurrence{0, today, today}, today, 9 * 60 + 29));
    CHECK(eventOver(a, Occurrence{0, today, today}, today,
                    9 * 60 + 30));  // over at its end minute
    CHECK(!eventOver(b, Occurrence{0, today, today}, today,
                     15 * 60 + 29));  // no end: an hour
    CHECK(eventOver(b, Occurrence{0, today, today}, today, 15 * 60 + 30));
    CHECK(!eventOver(c, Occurrence{0, today, today}, today,
                     23 * 60 + 59));  // all day never over
    CHECK(!eventOver(d, Occurrence{0, today - 1, today + 1}, today,
                     23 * 60));  // still running tomorrow
    CHECK(!eventOver(f, Occurrence{0, today - 2, today}, today,
                     10 * 60 + 59));  // last day: until 11:00
    CHECK(eventOver(f, Occurrence{0, today - 2, today}, today, 11 * 60));

    std::vector<Task> ts;
    auto task = [&](const char* title, Day due, bool done) {
      Task t;
      t.id = static_cast<uint32_t>(ts.size() + 1);
      t.title = title;
      t.hasDue = true;
      t.due = due;
      t.done = done;
      ts.push_back(t);
    };
    task("Pay rent", today, false);
    task("Done thing", today, true);
    task("Late A", today - 3, false);
    task("Late B", today - 1, false);
    task("Late but done", today - 2, true);
    task("Call mum", today + 1, false);
    task("Tomorrow done", today + 1, true);

    Agenda morning = buildAgenda(evs, ts, today, 8 * 60, false);
    CHECK(!morning.pastEvents);
    CHECK(morning.today.size() == 6);  // Standup, Dentist, Birthday, Conference, Trip home, then Pay rent
    // events first (all-day ones first), tasks last
    size_t firstTask = 0;
    while (firstTask < morning.today.size() && morning.today[firstTask].kind == AgendaKind::Event) ++firstTask;
    CHECK(firstTask == 5);
    CHECK(morning.overdue == 2);
    CHECK(morning.today.back().kind == AgendaKind::Task && morning.today.back().text == "Pay rent");
    CHECK(morning.tomorrow.size() == 4);  // Conference (still running) and Day
                                          // off (all day), Gym 07:00, Call mum
    CHECK(morning.tomorrow[0].text == "Conference" && morning.tomorrow[1].text == "Day off");
    CHECK(morning.tomorrow[2].text == "07:00 - Gym");
    CHECK(morning.tomorrow[3].kind == AgendaKind::Task && morning.tomorrow[3].text == "Call mum");

    Agenda afternoon = buildAgenda(evs, ts, today, 16 * 60, true);
    CHECK(afternoon.pastEvents);  // Standup, Dentist and Trip home are over
    bool sawStandup = false, sawDentist = false, sawTrip = false, sawBirthday = false, sawConf = false;
    for (const auto& it : afternoon.today) {
      sawStandup |= it.text.find("Standup") != std::string::npos;
      sawDentist |= it.text.find("Dentist") != std::string::npos;
      sawTrip |= it.text.find("Trip home") != std::string::npos;
      sawBirthday |= it.text == "Birthday";
      sawConf |= it.text == "Conference";
    }
    CHECK(!sawStandup && !sawDentist && !sawTrip && sawBirthday && sawConf);
    CHECK(afternoon.tomorrow[2].text == "7:00 AM - Gym");  // the clock format is honoured

    Agenda empty = buildAgenda({}, {}, today, 12 * 60, false);
    CHECK(empty.today.empty() && empty.tomorrow.empty() && empty.overdue == 0 && !empty.pastEvents);
  }

  {  // sleep agenda layout: always inside the height it is given
    AgendaMetrics m;
    m.item = 26;
    m.strong = 28;
    m.summary = 18;
    m.rule = 10;
    m.tomHead = 22;
    m.tomItem = 20;
    m.more = 20;
    const auto total = [&](const std::vector<AgendaLine>& v) {
      int h = 0;
      for (const auto& l : v) {
        switch (l.style) {
          case AgendaStyle::Event:
          case AgendaStyle::Task:
            h += m.item;
            break;
          case AgendaStyle::Strong:
            h += m.strong;
            break;
          case AgendaStyle::Summary:
            h += m.summary;
            break;
          case AgendaStyle::Rule:
            h += m.rule;
            break;
          case AgendaStyle::TomHead:
            h += m.tomHead;
            break;
          case AgendaStyle::TomEvent:
          case AgendaStyle::TomTask:
            h += m.tomItem;
            break;
          case AgendaStyle::More:
            h += m.more;
            break;
        }
      }
      return h;
    };
    const auto count = [&](const std::vector<AgendaLine>& v, AgendaStyle s) {
      size_t n = 0;
      for (const auto& l : v) n += l.style == s ? 1 : 0;
      return n;
    };
    const auto opts = [](bool evening, bool tomorrow = true, AgendaSize size = AgendaSize::Large) {
      AgendaOptions o;
      o.evening = evening;
      o.tomorrow = tomorrow;
      o.size = size;
      return o;
    };
    Agenda a;
    m.avail = 500;
    // nothing at all
    auto l0 = layoutAgenda(a, m, "Tomorrow", opts(false));
    CHECK(l0.empty());  // an empty day says nothing
    a.pastEvents = true;
    CHECK(layoutAgenda(a, m, "Tomorrow", opts(false)).empty());
    a.pastEvents = false;
    // no headings; events then to-dos; the overdue count first; tomorrow empty:
    // no block, no rule
    a.today = {{AgendaKind::Event, "09:00 - A"}, {AgendaKind::Task, "T1"}};
    a.overdue = 3;
    auto l1 = layoutAgenda(a, m, "Tomorrow", opts(false));
    CHECK(count(l1, AgendaStyle::Rule) == 0 && count(l1, AgendaStyle::TomHead) == 0);
    CHECK(l1.size() == 3);
    CHECK(l1[0].style == AgendaStyle::Strong && l1[0].text == "3 overdue");
    CHECK(l1[1].style == AgendaStyle::Event && l1[1].text == "09:00 - A");
    CHECK(l1[2].style == AgendaStyle::Task && l1[2].text == "T1");
    // tomorrow has 6 rows: 4 shown by day, then "+2 more"
    for (int i = 0; i < 6; ++i)
      a.tomorrow.push_back({i == 5 ? AgendaKind::Task : AgendaKind::Event, "t" + std::to_string(i)});
    auto l2 = layoutAgenda(a, m, "Tomorrow, Sat 28 Nov", opts(false));
    CHECK(count(l2, AgendaStyle::TomEvent) == 4 && count(l2, AgendaStyle::Rule) == 1);
    CHECK(l2.back().style == AgendaStyle::More && l2.back().text == "+2 more");
    CHECK(count(l2, AgendaStyle::TomHead) == 1);
    // "today only": no tomorrow even when there is some
    auto l2b = layoutAgenda(a, m, "Tomorrow", opts(false, false));
    CHECK(count(l2b, AgendaStyle::Rule) == 0 && count(l2b, AgendaStyle::TomEvent) == 0 && l2b.size() == 3);
    // in the evening: up to 8 rows for tomorrow, to-dos keep their kind
    for (int i = 6; i < 12; ++i) a.tomorrow.push_back({AgendaKind::Event, "t" + std::to_string(i)});
    auto l3 = layoutAgenda(a, m, "Tomorrow", opts(true));
    CHECK(count(l3, AgendaStyle::TomEvent) + count(l3, AgendaStyle::TomTask) == 8 && l3.back().text == "+4 more");
    a.tomorrow[5].kind = AgendaKind::Task;
    CHECK(count(layoutAgenda(a, m, "Tomorrow", opts(true)), AgendaStyle::TomTask) == 1);
    // evening: today shows at most 4 rows, the rest "+N more", before
    // tomorrow's block
    Agenda busy;
    for (int i = 0; i < 7; ++i) busy.today.push_back({AgendaKind::Event, "e" + std::to_string(i)});
    busy.today.push_back({AgendaKind::Task, "task"});
    busy.tomorrow.push_back({AgendaKind::Event, "x"});
    m.avail = 700;
    auto l4 = layoutAgenda(busy, m, "Tomorrow", opts(true));
    CHECK(count(l4, AgendaStyle::Event) + count(l4, AgendaStyle::Task) == 4);
    size_t moreAt = 0, ruleAt = 0;
    for (size_t i = 0; i < l4.size(); ++i) {
      if (l4[i].style == AgendaStyle::More) moreAt = i;
      if (l4[i].style == AgendaStyle::Rule) ruleAt = i;
    }
    CHECK(l4[moreAt].text == "+4 more" && moreAt < ruleAt);
    // daytime: today is cut by height only
    auto l5 = layoutAgenda(busy, m, "Tomorrow", opts(false));
    CHECK(count(l5, AgendaStyle::Event) + count(l5, AgendaStyle::Task) == 8 && count(l5, AgendaStyle::More) == 0);
    // Small: one line of counts, whatever is there
    auto s1 = layoutAgenda(busy, m, "Tomorrow", opts(false, true, AgendaSize::Small));
    CHECK(s1.size() == 1 && s1[0].style == AgendaStyle::Summary && s1[0].text == "Due: 0   Today: 8   Tomorrow: 1");
    busy.overdue = 2;
    CHECK(layoutAgenda(busy, m, "Tomorrow", opts(false, false, AgendaSize::Small))[0].text == "Due: 2   Today: 8");
    CHECK(layoutAgenda(Agenda{}, m, "Tomorrow", opts(false, true, AgendaSize::Small))[0].text ==
          "Due: 0   Today: 0   Tomorrow: 0");
    // Normal lays out like Large (the fonts differ, the lines do not)
    CHECK(layoutAgenda(busy, m, "Tomorrow", opts(false, true, AgendaSize::Normal)).size() ==
          layoutAgenda(busy, m, "Tomorrow", opts(false)).size());
    // every height, every mix: inside avail
    for (int avail = 80; avail <= 760; avail += 7) {
      for (int nt = 0; nt <= 12; nt += 3) {
        for (int nd = 0; nd <= 14; nd += 2) {
          for (int ev = 0; ev < 2; ++ev) {
            for (size_t od = 0; od < 2; ++od) {
              Agenda x;
              for (int i = 0; i < nd; ++i) x.today.push_back({i % 3 ? AgendaKind::Event : AgendaKind::Task, "d"});
              std::stable_sort(x.today.begin(), x.today.end(),
                               [](const AgendaItem& p, const AgendaItem& q) { return p.kind < q.kind; });
              for (int i = 0; i < nt; ++i) x.tomorrow.push_back({AgendaKind::Event, "t"});
              x.overdue = od * 2;
              m.avail = avail;
              auto lines = layoutAgenda(x, m, "Tomorrow", opts(ev == 1));
              CHECK(total(lines) <= avail);
              CHECK(!lines.empty() || (nd == 0 && od == 0));  // an empty day says nothing
            }
          }
        }
      }
    }
  }

  // --- the minute a to-do was ticked ---
  {
    Task t;
    t.id = 9;
    t.title = "timed";
    t.created = daysFromCivil(2026, 1, 1);
    t.done = true;
    t.doneOn = daysFromCivil(2026, 1, 2);
    t.doneMin = 14 * 60 + 5;
    std::vector<Task> back;
    CHECK(parseTasks(serializeTasks({t}), back) == 1 && back[0].doneMin == 845 && back[0].doneOn == t.doneOn);
    CHECK(parseTasks("V1\nT\t5\t1\told\t2026-01-01\t-\t2026-01-02\t\n", back) == 1 && back[0].done &&
          back[0].doneMin == -1);  // a file from before the column
    CHECK(parseTasks("V1\nT\t5\t1\tx\t2026-01-01\t-\t2026-01-02\t\t99999\n", back) == 1 && back[0].doneMin == -1);
    t.done = false;
    CHECK(serializeTasks({t}).find("\t845") == std::string::npos);  // open: nothing written
    CHECK(kMaxLists == 14);
  }

  // --- settings file ---
  {
    Settings d = parseSettings("");  // no file: the defaults
    CHECK(d.firstDay == 1 && d.weekNumbers && !d.listHome && !d.weekHome && !d.agenda && d.agendaSize == 2 &&
          d.tomorrow && !d.agendaBottom);
    Settings s;
    s.firstDay = 0;
    s.weekNumbers = false;
    s.listHome = true;
    s.weekHome = true;
    s.agenda = true;
    s.agendaSize = 1;
    s.tomorrow = false;
    s.agendaBottom = true;
    const Settings r = parseSettings(serializeSettings(s));  // what is written is what is read
    CHECK(r.firstDay == 0 && !r.weekNumbers && r.listHome && r.weekHome && r.agenda && r.agendaSize == 1 &&
          !r.tomorrow && r.agendaBottom);
    CHECK(serializeSettings(r) == serializeSettings(s));
    CHECK(serializeSettings(s).size() < kMaxSettingsBytes);
    CHECK(parseSettings("first=6\n").firstDay == 6);
    CHECK(parseSettings("first=3\n").firstDay == 1);   // not a day the screen offers: Monday
    CHECK(parseSettings("size=7\n").agendaSize == 2);  // junk reads as Large
    CHECK(parseSettings("size=0\n").agendaSize == 0);
    CHECK(parseSettings("agenda=1\n").agenda && !parseSettings("agenda=0\n").agenda &&
          !parseSettings("agenda=2\n").agenda);
    CHECK(parseSettings("xtomorrow=0\n").tomorrow);  // a key must start its line
    CHECK(!parseSettings("agenda=1\ntomorrow=0\n").tomorrow && parseSettings("agenda=1\ntomorrow=0\n").agenda);
    CHECK(parseSettings("bottom=1\nfuture=whatever\n").agendaBottom);  // unknown keys are ignored
    CHECK(parseSettings("agenda=1\r\nsize=1\r\n").agendaSize == 1);    // CRLF from a PC editor
  }
  // --- the file holds 1970 .. 2200 ---
  {
    CHECK(clampDay(-5) == 0 && clampDay(0) == 0 && clampDay(100) == 100);
    CHECK(clampDay(D(2200, 12, 31)) == D(2200, 12, 31) && clampDay(D(2200, 12, 31) + 1) == D(2200, 12, 31));
    CHECK(clampDay(2000000000) == D(2200, 12, 31));
    Event e;
    e.id = 1;
    e.title = "Edge";
    e.start = e.end = clampDay(D(2300, 1, 1));
    std::vector<Event> back;
    parse(serialize({e}), back);
    CHECK(back.size() == 1);  // an event at the clamp is read back, not dropped
  }

  std::printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
