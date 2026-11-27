// See DaybookPlugin.h. The script below is the page itself; DaybookActivity's
// Settings writes it to the card.
#include "DaybookPlugin.h"

#include <HalStorage.h>

#include "../../CrossPointSettings.h"
#include "../../util/PluginLocations.h"

namespace daybook::plugin {

namespace {

constexpr const char* kTag = "DBP";
constexpr const char* kName = "daybook";
constexpr const char* kVersion = "1.1.1";  // bump when the script changes: Settings then offers "Update"

const char kReadme[] =
    "Daybook\n"
    "\n"
    "Your calendar and to-dos in the web page. It reads and "
    "writes the same files as the Daybook app, so\n"
    "both always show the same things.\n"
    "\n"
    "To use it: open File Transfer on the device, then the "
    "Plugins page in the web UI on your phone or\n"
    "PC. Leave the Daybook app closed while you edit.\n";

// The script. Raw literal: no escaping, and it never contains the closing
// marker.
const char kScript[] =
    R"JS(// Daybook for the CrossPoint web UI. Reads and writes the same files the Daybook app keeps on the card
// (/.crosspoint/daybook/), so the device and this page always show one set of data.
CrossPoint.registerPlugin(async (container, api) => {
  'use strict';
  const DIR = '/.crosspoint/daybook/';
  const FILES = { e: DIR + 'events.txt', t: DIR + 'tasks.txt', l: DIR + 'lists.txt', s: DIR + 'settings.txt' };
  const LIM = { events: 400, tasks: 500, lists: 14, evFile: 96 * 1024, taskFile: 160 * 1024, title: 60, place: 60, note: 240, list: 30 };
  const MAIN = 1;
  const enc = new TextEncoder();

  // ---- dates: a day is a whole number of days since 1970-01-01, exactly as on the device ----
  const MS = 86400000;
  const pad = (n) => String(n).padStart(2, '0');
  const dim = (y, m) => (m === 2 ? ((y % 4 === 0 && y % 100 !== 0) || y % 400 === 0 ? 29 : 28) : [31, 0, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][m - 1]);
  const dnum = (y, m, d) => Math.round(Date.UTC(y, m - 1, d) / MS);
  const civil = (day) => { const t = new Date(day * MS); return { y: t.getUTCFullYear(), m: t.getUTCMonth() + 1, d: t.getUTCDate() }; };
  const iso = (day) => { const c = civil(day); return String(c.y).padStart(4, '0') + '-' + pad(c.m) + '-' + pad(c.d); };
  const parseDay = (s) => {
    const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(s || '');
    if (!m) return null;
    const y = +m[1], mo = +m[2], d = +m[3];
    return y < 1970 || y > 2200 || mo < 1 || mo > 12 || d < 1 || d > dim(y, mo) ? null : dnum(y, mo, d);
  };
  const wd = (day) => (((day + 4) % 7) + 7) % 7;
  const isoWeek = (day) => { const th = day - ((wd(day) + 6) % 7) + 3; return Math.trunc((th - dnum(civil(th).y, 1, 1)) / 7) + 1; };
  const addMonths = (day, n) => { const c = civil(day), t = c.y * 12 + c.m - 1 + n, y = Math.floor(t / 12), m = (t % 12 + 12) % 12 + 1; return dnum(y, m, Math.min(c.d, dim(y, m))); };
  const now = new Date();
  const today = dnum(now.getFullYear(), now.getMonth() + 1, now.getDate());
  const DOW = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  const DOWL = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
  const MON = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];
  const nowMin = () => { const n = new Date(); return n.getHours() * 60 + n.getMinutes(); };
  const hm = (m) => pad(Math.floor(m / 60)) + ':' + pad(m % 60);
  const parseHm = (s) => { const p = /^(\d{1,2}):(\d{2})/.exec(s || ''); return p ? Math.min(+p[1] * 60 + +p[2], 1439) : 0; };
  const nice = (day) => { const c = civil(day); return DOW[wd(day)] + ' ' + c.d + ' ' + MON[c.m - 1].slice(0, 3); };

  // ---- text helpers ----
  // At most `max` bytes of UTF-8, never cutting a letter in half. Typed text also loses tabs and line breaks (the file's separators).
  const cut = (s, max) => {
    const cs = Array.from(String(s == null ? '' : s));
    while (cs.length && enc.encode(cs.join('')).length > max) cs.pop();
    return cs.join('');
  };
  const clip = (s, max) => cut(String(s == null ? '' : s).replace(/[\t\n\r]/g, ' '), max);
  const cap = (s) => { const cs = Array.from(s); if (cs.length) cs[0] = cs[0].toUpperCase(); return cs.join(''); };
  const esc = (s) => s.replace(/\\/g, '\\\\').replace(/\t/g, '\\t').replace(/\n/g, '\\n').replace(/\r/g, '');
  const unesc = (s) => {
    let o = '';
    for (let i = 0; i < s.length; i++) {
      if (s[i] === '\\' && i + 1 < s.length) { const n = s[++i]; o += n === 't' ? '\t' : n === 'n' ? '\n' : n; } else o += s[i];
    }
    return o;
  };
  const int = (s) => parseInt(s, 10) || 0;
  const clamp = (n, a, b) => Math.min(Math.max(n, a), b);

  // ---- the three files (same text format as DaybookCore.cpp) ----
  const parseEvents = (text) => {
    if (enc.encode(text).length > LIM.evFile) throw new Error('events file is too big');
    const out = [];
    for (const line of text.split('\n')) {
      if (out.length >= LIM.events) break;
      const f = line.split('\t');
      if (f[0] !== 'E' || f.length < 13) continue;
      const e = { id: int(f[1]) >>> 0, title: cut(unesc(f[2]), LIM.title) };
      e.start = parseDay(f[3]); e.end = parseDay(f[4]);
      if (e.start == null || e.end == null || e.end < e.start) continue;
      e.allDay = f[5] === '1';
      e.startMin = clamp(int(f[6]), 0, 1439); e.endMin = clamp(int(f[7]), 0, 1439);
      const r = int(f[8]); e.repeat = r >= 0 && r <= 4 ? r : 0;
      e.every = clamp(int(f[9]), 1, 999);
      e.until = f[10] !== '-' ? parseDay(f[10]) : null;
      e.place = cut(unesc(f[11]), LIM.place); e.note = cut(unesc(f[12]), LIM.note);
      out.push(e);
    }
    return out;
  };
  const putEvents = (v) => 'V1\n' + v.map((e) => ['E', e.id, esc(e.title), iso(e.start), iso(e.end), e.allDay ? 1 : 0, e.startMin, e.endMin, e.repeat, e.every, e.until != null ? iso(e.until) : '-', esc(e.place), esc(e.note)].join('\t') + '\n').join('');
  const parseTasks = (text) => {
    if (enc.encode(text).length > LIM.taskFile) throw new Error('tasks file is too big');
    const out = [];
    for (const line of text.split('\n')) {
      if (out.length >= LIM.tasks) break;
      const f = line.split('\t');
      if (f[0] !== 'T' || f.length < 8) continue;
      const t = { id: int(f[1]) >>> 0, list: int(f[2]) >>> 0, title: cut(unesc(f[3]), LIM.title) };
      t.created = parseDay(f[4]);
      if (!t.title || t.created == null) continue;
      t.due = f[5] !== '-' ? parseDay(f[5]) : null;
      t.doneOn = f[6] !== '-' ? parseDay(f[6]) : null;
      t.done = t.doneOn != null;
      t.note = cut(unesc(f[7]), LIM.note);
      t.doneMin = t.done && /^\d{1,4}$/.test(f[8] || '') && +f[8] < 1440 ? +f[8] : null;
      out.push(t);
    }
    return out;
  };
  const putTasks = (v) => 'V1\n' + v.map((t) => ['T', t.id, t.list, esc(t.title), iso(t.created), t.due != null ? iso(t.due) : '-', t.done ? iso(t.doneOn) : '-', esc(t.note)].concat(t.done && t.doneMin != null ? [t.doneMin] : []).join('\t') + '\n').join('');
  const parseLists = (text) => {
    const out = [];
    for (const line of text.split('\n')) {
      if (out.length >= LIM.lists) break;
      const f = line.split('\t');
      if (f[0] !== 'L' || f.length < 3) continue;
      const l = { id: int(f[1]) >>> 0, name: cut(unesc(f[2]), LIM.list) };
      if (l.id && l.name && !out.some((o) => o.id === l.id)) out.push(l);
    }
    return out;
  };
  const putLists = (v) => 'V1\n' + v.map((l) => 'L\t' + l.id + '\t' + esc(l.name) + '\n').join('');
  const repair = (d) => {
    if (!d.lists.some((l) => l.id === MAIN)) d.lists.unshift({ id: MAIN, name: "Main To-Do's" });
    if (d.lists.length > LIM.lists) d.lists.length = LIM.lists;
    for (const t of d.tasks) if (!d.lists.some((l) => l.id === t.list)) t.list = MAIN;
  };
  const nextId = (v) => v.reduce((m, x) => Math.max(m, x.id), 0) + 1;

  // ---- the card: read, write ----
  // A missing file is empty; any other failure stops the operation, so a bad read can never be written back over good data.
  const rd = async (path) => {
    const r = await fetch('/download?path=' + encodeURIComponent(path), { cache: 'no-store' });
    if (r.status === 404) return '';
    if (!r.ok) throw new Error('cannot read ' + path.split('/').pop() + ' (' + r.status + ')');
    return r.text();
  };
  const wr = async (path, text) => {
    const bytes = enc.encode(text);
    let bin = '';
    for (let i = 0; i < bytes.length; i += 0x8000) bin += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    await api.writeFile(path, btoa(bin));
  };
  const load = async () => {
    const d = { events: parseEvents(await rd(FILES.e)), tasks: parseTasks(await rd(FILES.t)), lists: parseLists(await rd(FILES.l)), first: 1, weeks: true };
    repair(d);
    const s = await rd(FILES.s).catch(() => '');
    const first = /^first=(\d)/m.exec(s), weeks = /^weeks=(\d)/m.exec(s);
    if (first && [0, 1, 6].includes(+first[1])) d.first = +first[1];
    if (weeks) d.weeks = weeks[1] === '1';
    return d;
  };

  // ---- repeats: the same rules as occurrencesIn() on the device ----
  const occIn = (events, from, to) => {
    const out = [];
    events.forEach((ev, i) => {
      const dur = ev.end > ev.start ? ev.end - ev.start : 0;
      const add = (s) => { if (s + dur >= from && s <= to) out.push({ ev, i, start: s, end: s + dur }); };
      if (!ev.repeat) return add(ev.start);
      const every = Math.max(ev.every, 1), limit = ev.until != null ? Math.min(to, ev.until) : to;
      if (ev.repeat === 1 || ev.repeat === 2) {
        const per = every * (ev.repeat === 2 ? 7 : 1);
        let k = ev.start < from - dur ? Math.trunc((from - dur - ev.start) / per) : 0;
        for (let g = 0; g < 4000; g++, k++) { const s = ev.start + k * per; if (s > limit) break; add(s); }
      } else {
        const p = civil(ev.start), step = ev.repeat === 4 ? 12 * every : every, f = civil(from);
        let k = Math.max(Math.trunc((f.y * 12 + f.m - (p.y * 12 + p.m)) / step) - 1, 0);
        for (let g = 0; g < 4000; g++, k++) {
          const t = p.y * 12 + p.m - 1 + k * step, y = Math.floor(t / 12), m = t % 12 + 1;
          const s = dnum(y, m, Math.min(p.d, dim(y, m)));
          if (s > limit) break;
          add(s);
        }
      }
    });
    return out;
  };
  const sortOn = (list, day) => list.sort((a, b) => {
    const ka = a.ev.allDay || day !== a.start ? -1 : a.ev.startMin, kb = b.ev.allDay || day !== b.start ? -1 : b.ev.startMin;
    return ka - kb || (a.ev.title < b.ev.title ? -1 : a.ev.title > b.ev.title ? 1 : 0);
  });

  // ---- operations: each re-reads the card first, changes one thing, writes it back, one at a time ----
  let chain = Promise.resolve();
  const op = (fn) => { const p = chain.then(fn); chain = p.catch(() => {}); return p; };
  const ops = {
    eventSave: (v) => op(async () => {
      const d = { events: parseEvents(await rd(FILES.e)) };
      const e = { title: cap(clip(v.title, LIM.title).trim()), place: clip(v.place, LIM.place), note: clip(v.note, LIM.note), allDay: !!v.allDay };
      if (!e.title) throw new Error('Give it a title');
      if (v.start == null) throw new Error('Pick a start date');
      e.start = v.start; e.end = v.end != null && v.end >= v.start ? v.end : v.start;
      e.startMin = e.allDay ? 0 : clamp(v.startMin | 0, 0, 1439); e.endMin = e.allDay ? 0 : clamp(v.endMin | 0, 0, 1439);
      e.repeat = clamp(v.repeat | 0, 0, 4); e.every = clamp(v.every | 0 || 1, 1, 999);
      e.until = e.repeat && v.until != null && v.until >= e.start ? v.until : null;
      const at = v.id ? d.events.findIndex((x) => x.id === v.id) : -1;
      if (at >= 0) { e.id = v.id; d.events[at] = e; } else {
        if (d.events.length >= LIM.events) throw new Error('The event list is full');
        e.id = nextId(d.events); d.events.push(e);
      }
      await wr(FILES.e, putEvents(d.events));
    }),
    eventDelete: (id) => op(async () => {
      const ev = parseEvents(await rd(FILES.e)), at = ev.findIndex((x) => x.id === id);
      if (at < 0) throw new Error('That event is already gone');
      ev.splice(at, 1);
      await wr(FILES.e, putEvents(ev));
    }),
    taskSave: (v) => op(async () => {
      const d = { tasks: parseTasks(await rd(FILES.t)), lists: parseLists(await rd(FILES.l)) };
      repair(d);
      const t = { title: cap(clip(v.title, LIM.title).trim()), note: clip(v.note, LIM.note), list: d.lists.some((l) => l.id === v.list) ? v.list : MAIN, due: v.due != null ? v.due : null };
      if (!t.title) throw new Error('Give it a title');
      const at = v.id ? d.tasks.findIndex((x) => x.id === v.id) : -1;
      if (at >= 0) { const o = d.tasks[at]; Object.assign(t, { id: o.id, created: o.created, done: o.done, doneOn: o.doneOn, doneMin: o.doneMin }); d.tasks[at] = t; } else {
        if (d.tasks.length >= LIM.tasks) throw new Error('The to-do list is full');
        Object.assign(t, { id: nextId(d.tasks), created: today, done: false, doneOn: null, doneMin: null }); d.tasks.push(t);
      }
      await wr(FILES.t, putTasks(d.tasks));
    }),
    taskToggle: (id) => op(async () => {
      const d = { tasks: parseTasks(await rd(FILES.t)) }, t = d.tasks.find((x) => x.id === id);
      if (!t) throw new Error('That task is already gone');
      t.done = !t.done; t.doneOn = t.done ? today : null; t.doneMin = t.done ? nowMin() : null;
      await wr(FILES.t, putTasks(d.tasks));
    }),
    taskDelete: (id) => op(async () => {
      const ts = parseTasks(await rd(FILES.t)), at = ts.findIndex((x) => x.id === id);
      if (at < 0) throw new Error('That task is already gone');
      ts.splice(at, 1);
      await wr(FILES.t, putTasks(ts));
    }),
    listAdd: (name) => op(async () => {
      const d = { tasks: [], lists: parseLists(await rd(FILES.l)) };
      repair(d);
      name = cap(clip(name, LIM.list).trim());
      if (!name) throw new Error('Give the list a name');
      if (d.lists.length >= LIM.lists) throw new Error('That is the most lists there can be');
      const l = { id: nextId(d.lists), name };
      d.lists.push(l);
      await wr(FILES.l, putLists(d.lists));
      return l.id;
    }),
    listRename: (id, name) => op(async () => {
      const d = { tasks: [], lists: parseLists(await rd(FILES.l)) };
      repair(d);
      name = cap(clip(name, LIM.list).trim());
      const l = d.lists.find((x) => x.id === id);
      if (!name || !l) throw new Error('Cannot rename that list');
      l.name = name;
      await wr(FILES.l, putLists(d.lists));
    }),
    listDelete: (id) => op(async () => {
      const d = { tasks: parseTasks(await rd(FILES.t)), lists: parseLists(await rd(FILES.l)) };
      repair(d);
      if (id === MAIN || !d.lists.some((x) => x.id === id)) throw new Error('That list cannot be deleted');
      d.tasks = d.tasks.filter((t) => t.list !== id);
      d.lists = d.lists.filter((l) => l.id !== id);
      await wr(FILES.t, putTasks(d.tasks));
      await wr(FILES.l, putLists(d.lists));
    }),
  };
  if (window.__dbkTest) window.__dbkTest = { parseEvents, putEvents, parseTasks, putTasks, parseLists, putLists, occIn, ops, load, iso, parseDay, dnum, clip, cap };

  // ---- the page ----
  const h = (tag, props, ...kids) => {
    const n = document.createElement(tag);
    for (const [k, v] of Object.entries(props || {})) {
      if (k === 'class') n.className = v; else if (k.startsWith('on')) n[k] = v; else if (k in n) n[k] = v; else n.setAttribute(k, v);
    }
    for (const c of kids.flat()) if (c != null && c !== false) n.append(c);
    return n;
  };
  const css = `
.dbk{--ac:#4338ca;--bd:rgba(128,128,128,.35);--soft:rgba(128,128,128,.12);--bad:#c62828;font:15px/1.4 system-ui,-apple-system,sans-serif;position:relative}
.dbk *{box-sizing:border-box}
.dbk button,.dbk input,.dbk select{font:inherit;color:inherit;background:Canvas;border:1px solid var(--bd);border-radius:8px;padding:6px 11px}
.dbk button{cursor:pointer;transition:background .12s,transform .06s}.dbk button:hover{background:var(--soft)}.dbk button:active{transform:scale(.97)}
.dbk button.pri{background:var(--ac);border-color:var(--ac);color:#fff}.dbk button.pri:hover{filter:brightness(1.12)}
.dbk button.bad{color:var(--bad)}.dbk .grow{flex:1;min-width:0}
.dbk .top{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:10px}.dbk .top h2{margin:0;font-size:22px;letter-spacing:-.01em}
.dbk .seg{display:inline-flex;border:1px solid var(--bd);border-radius:10px;overflow:hidden}.dbk .seg button{border:0;border-radius:0;background:none;padding:6px 14px}
.dbk .seg button.on{background:var(--ac);color:#fff}
.dbk .bar{display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin:8px 0}
.dbk .mt{font-size:19px;font-weight:700;text-align:center}
.dbk .grid{display:grid;grid-template-columns:repeat(7,minmax(0,1fr));gap:3px}.dbk .grid.wk{grid-template-columns:26px repeat(7,minmax(0,1fr))}
.dbk .dow{text-align:center;font-size:12px;color:GrayText;padding:3px 0}.dbk .wn{font-size:11px;color:GrayText;display:flex;align-items:center;justify-content:center}
.dbk .cell{min-height:84px;border:1px solid var(--bd);border-radius:8px;padding:3px 4px;cursor:pointer;overflow:hidden;background:Canvas;transition:background .12s}
.dbk .cell:hover{background:var(--soft)}.dbk .cell.out{opacity:.45}.dbk .cell.sel{outline:2px solid var(--ac);outline-offset:-1px}
.dbk .n{display:inline-block;min-width:22px;text-align:center;font-weight:700;font-size:13px;border-radius:11px;padding:0 4px}.dbk .cell.today .n{background:var(--ac);color:#fff}
.dbk .chip{font-size:11.5px;line-height:1.35;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;border-radius:4px;margin-top:2px;padding:0 4px;background:rgba(67,56,202,.16)}
.dbk .chip.t{background:none;border:1px solid var(--bd)}.dbk .chip.o{background:var(--bad);color:#fff;border-color:var(--bad)}.dbk .chip.m{color:GrayText;background:none}
.dbk .dot{display:none}
.dbk .card{border:1px solid var(--bd);border-radius:12px;padding:12px;margin:10px 0;animation:dbkin .18s ease-out}
@keyframes dbkin{from{opacity:0;transform:translateY(4px)}to{opacity:1;transform:none}}
.dbk .day-h{font-weight:700;font-size:16px}.dbk .sub{color:GrayText;font-size:13px}
.dbk .row{display:flex;align-items:center;gap:10px;padding:8px 6px;border-bottom:1px solid var(--soft);cursor:pointer;border-radius:6px}.dbk .row:hover{background:var(--soft)}.dbk .row:last-child{border:0}
.dbk .row .tt{font-weight:600}.dbk .row.done .tt{text-decoration:line-through;color:GrayText;font-weight:400}.dbk .row.over .tt,.dbk .row.over .sub{color:var(--bad)}
.dbk .row input[type=checkbox]{width:20px;height:20px;accent-color:var(--ac);flex:none}
.dbk .tag{font-size:12px;font-weight:700;color:var(--ac);flex:none;min-width:46px}
.dbk .gh{font-weight:700;margin:14px 0 2px;padding-bottom:3px;border-bottom:2px solid var(--ac)}.dbk .gh.ov{color:var(--bad);border-color:var(--bad)}.dbk .gm{font-size:19px;font-weight:700;margin:18px 0 0}
.dbk .tabs{display:flex;gap:6px;flex-wrap:wrap}.dbk .tabs button.on{background:var(--ac);border-color:var(--ac);color:#fff}
.dbk .prog{height:8px;border-radius:4px;background:var(--soft);overflow:hidden;margin:8px 0}.dbk .prog i{display:block;height:100%;background:var(--ac);transition:width .25s}
.dbk .quick{display:flex;gap:6px;margin:8px 0}.dbk .quick input{flex:1;min-width:0}
.dbk dialog{border:1px solid var(--bd);border-radius:14px;padding:16px;width:min(440px,94vw);background:Canvas;color:CanvasText}.dbk dialog::backdrop{background:rgba(0,0,0,.4)}
.dbk dialog h3{margin:0 0 6px}.dbk dialog label{display:block;margin:9px 0 2px;font-size:13px;color:GrayText}.dbk dialog input[type=text],.dbk dialog input[type=date],.dbk dialog input[type=time],.dbk dialog input[type=number],.dbk dialog select{width:100%}
.dbk dialog .two{display:flex;gap:10px}.dbk dialog .two>div{flex:1;min-width:0}.dbk dialog .chk{display:flex;align-items:center;gap:8px;color:inherit;font-size:15px}.dbk .wk-l{font-size:12px;color:var(--ac);min-height:1.2em}
.dbk .toast{position:fixed;left:50%;bottom:22px;transform:translateX(-50%);background:#222;color:#fff;border-radius:10px;padding:8px 16px;z-index:20;box-shadow:0 4px 18px rgba(0,0,0,.3);max-width:90vw}.dbk .toast.err{background:var(--bad)}
.dbk.busy .top h2::after{content:' \u2026';color:GrayText}
@media(max-width:600px){.dbk .cell{min-height:56px}.dbk .chip{display:none}.dbk .dot{display:inline-block;width:6px;height:6px;border-radius:3px;background:var(--ac);margin:1px}.dbk .wn{font-size:9px}.dbk .grid.wk{grid-template-columns:18px repeat(7,minmax(0,1fr))}}`;

  const root = h('div', { class: 'dbk' });
  container.replaceChildren(root);
  root.append(h('style', {}, css));
  const S = { view: 'm', sel: today, cur: MAIN, y: civil(today).y, m: civil(today).m, d: null };
  const app = h('div');
  const toast = (msg, bad) => {
    const t = h('div', { class: 'toast' + (bad ? ' err' : '') }, msg);
    root.append(t);
    setTimeout(() => t.remove(), bad ? 4500 : 1600);
  };
  const reload = async () => { S.d = await load(); draw(); };
  const run = async (p, ok) => {
    root.classList.add('busy');
    try { const r = await p; await reload(); if (ok) toast(ok); return r; } catch (e) { toast(String(e.message || e), true); try { await reload(); } catch (x) {} } finally { root.classList.remove('busy'); }
  };

  const evText = (o, day) => (o.ev.allDay || day !== o.start ? '' : hm(o.ev.startMin) + ' ') + o.ev.title;
  const evSub = (o) => nice(o.start) + (o.ev.allDay ? '' : ' \u00b7 ' + hm(o.ev.startMin) + (o.ev.endMin ? '\u2013' + hm(o.ev.endMin) : '')) + (o.ev.end > o.ev.start ? ' \u2192 ' + nice(o.end) : '') + (o.ev.repeat ? ' \u00b7 repeats' : '') + (o.ev.place ? ' \u00b7 ' + o.ev.place : '');
  const listName = (id) => (S.d.lists.find((l) => l.id === id) || { name: '' }).name;
  const taskSub = (t, withList) => [t.due != null ? 'due ' + nice(t.due) : '', t.done && t.doneOn != null ? 'done ' + nice(t.doneOn) + (t.doneMin != null ? ' ' + hm(t.doneMin) : '') : '', withList ? listName(t.list) : '', t.note].filter(Boolean).join(' \u00b7 ');
  const isOver = (t) => !t.done && t.due != null && t.due < today;

  const taskRow = (t, withList) => h('div', { class: 'row' + (t.done ? ' done' : isOver(t) ? ' over' : ''), onclick: () => taskDialog(t) },
    h('input', { type: 'checkbox', checked: t.done, onclick: (e) => e.stopPropagation(), onchange: () => { run(ops.taskToggle(t.id)); } }),
    h('div', { class: 'grow' }, h('div', { class: 'tt' }, t.title), h('div', { class: 'sub' }, taskSub(t, withList))));
  const evRow = (o) => h('div', { class: 'row', onclick: () => eventDialog(o.ev) },
    h('div', { class: 'tag' }, o.ev.allDay ? 'all day' : hm(o.ev.startMin)),
    h('div', { class: 'grow' }, h('div', { class: 'tt' }, o.ev.title), h('div', { class: 'sub' }, evSub(o))));

  const drawMonth = () => {
    const d = S.d, first = dnum(S.y, S.m, 1), lead = (wd(first) - d.first + 7) % 7, start = first - lead;
    const occ = occIn(d.events, start, start + 41);
    const byDay = (day) => sortOn(occ.filter((o) => o.start <= day && day <= o.end).map((o) => ({ ...o })), day);
    const nav = h('div', { class: 'bar' },
      h('button', { onclick: () => { S.m--; if (S.m < 1) { S.m = 12; S.y--; } draw(); } }, '\u2039'),
      h('div', { class: 'grow mt' }, MON[S.m - 1] + ' ' + S.y),
      h('button', { onclick: () => { S.y = civil(today).y; S.m = civil(today).m; S.sel = today; draw(); } }, 'Today'),
      h('button', { onclick: () => { S.m++; if (S.m > 12) { S.m = 1; S.y++; } draw(); } }, '\u203a'));
    const grid = h('div', { class: 'grid' + (d.weeks ? ' wk' : '') });
    if (d.weeks) grid.append(h('div'));
    for (let i = 0; i < 7; i++) grid.append(h('div', { class: 'dow' }, DOW[(d.first + i) % 7]));
    for (let w = 0; w < 6; w++) {
      if (d.weeks) grid.append(h('div', { class: 'wn' }, isoWeek(start + w * 7 + 3)));
      for (let k = 0; k < 7; k++) {
        const day = start + w * 7 + k, evs = byDay(day), ts = d.tasks.filter((t) => t.due === day);
        const chips = evs.map((o) => h('div', { class: 'chip' }, evText(o, day))).concat(ts.map((t) => h('div', { class: 'chip ' + (isOver(t) ? 'o' : 't') }, (t.done ? '\u2611 ' : '\u2610 ') + t.title)));
        const extra = chips.length - 3;
        grid.append(h('div', { class: 'cell' + (civil(day).m !== S.m ? ' out' : '') + (day === today ? ' today' : '') + (day === S.sel ? ' sel' : ''), onclick: () => { S.sel = day; draw(); }, ondblclick: () => eventDialog(null, day) },
          h('span', { class: 'n' }, civil(day).d), chips.slice(0, 3), extra > 0 && h('div', { class: 'chip m' }, '+' + extra + ' more'), chips.map(() => h('span', { class: 'dot' }))));
      }
    }
    const evs = byDay(S.sel), ts = d.tasks.filter((t) => t.due === S.sel);
    const panel = h('div', { class: 'card' },
      h('div', { class: 'bar' }, h('div', { class: 'grow' }, h('div', { class: 'day-h' }, DOWL[wd(S.sel)] + ', ' + MON[civil(S.sel).m - 1] + ' ' + civil(S.sel).d), h('div', { class: 'sub' }, iso(S.sel))),
        h('button', { class: 'pri', onclick: () => eventDialog(null, S.sel) }, '+ Event'), h('button', { onclick: () => taskDialog(null, S.sel) }, '+ Task')),
      evs.map((o) => evRow(o)), ts.map((t) => taskRow(t, true)), !evs.length && !ts.length && h('div', { class: 'sub' }, 'Nothing planned.'));
    app.append(nav, grid, panel);
  };

  const drawList = () => {
    const d = S.d, horizon = today + 365, items = [];
    occIn(d.events, today, horizon).forEach((o) => items.push({ day: Math.max(o.start, today), o, k: 1 }));
    d.tasks.forEach((t) => { if (!t.done && t.due != null && t.due >= today && t.due <= horizon) items.push({ day: t.due, t, k: 2 }); });
    items.sort((a, b) => a.day - b.day || a.k - b.k || (a.o && b.o ? (a.o.ev.allDay === b.o.ev.allDay ? a.o.ev.startMin - b.o.ev.startMin : a.o.ev.allDay ? -1 : 1) : 0));
    const late = d.tasks.filter(isOver).sort((a, b) => a.due - b.due);
    app.append(h('div', { class: 'bar' }, h('div', { class: 'grow mt', style: 'text-align:left' }, 'Upcoming'), h('button', { class: 'pri', onclick: () => eventDialog(null, today) }, '+ Event'), h('button', { onclick: () => taskDialog(null, today) }, '+ Task')));
    const card = h('div', { class: 'card' });
    if (late.length) card.append(h('div', { class: 'gh ov' }, 'Overdue'), late.map((t) => taskRow(t, true)));
    let lm = -1, ld = -1;
    for (const it of items) {
      const c = civil(it.day), mk = c.y * 12 + c.m;
      if (mk !== lm) { lm = mk; ld = -1; card.append(h('div', { class: 'gm' }, MON[c.m - 1] + ' ' + c.y)); }
      if (it.day !== ld) { ld = it.day; card.append(h('div', { class: 'gh' }, DOWL[wd(it.day)] + ' ' + c.d + (it.day === today ? ' \u00b7 today' : ''))); }
      card.append(it.o ? evRow({ ...it.o, start: it.day }) : taskRow(it.t, true));
    }
    if (!late.length && !items.length) card.append(h('div', { class: 'sub' }, 'Nothing coming up.'));
    app.append(card);
  };

  const drawTodo = () => {
    const d = S.d;
    if (!d.lists.some((l) => l.id === S.cur)) S.cur = MAIN;
    const open = (id) => d.tasks.filter((t) => t.list === id && !t.done).length;
    const tasks = d.tasks.filter((t) => t.list === S.cur);
    const done = tasks.filter((t) => t.done).length;
    tasks.sort((a, b) => (a.done - b.done) || (a.done ? (b.doneOn || 0) - (a.doneOn || 0) : (a.due == null) - (b.due == null) || (a.due || 0) - (b.due || 0)));
    const quick = h('input', { type: 'text', maxlength: 60, placeholder: 'Add a to-do and press Enter', onkeydown: (e) => { if (e.key === 'Enter' && quick.value.trim()) { const v = quick.value; quick.value = ''; run(ops.taskSave({ title: v, list: S.cur, note: '', due: null })); } } });
    const more = () => { const v = quick.value; quick.value = ''; taskDialog(null, null, v); };
    app.append(
      h('div', { class: 'tabs' }, d.lists.map((l) => h('button', { class: l.id === S.cur ? 'on' : '', onclick: () => { S.cur = l.id; draw(); } }, l.name + ' (' + open(l.id) + ')')),
        h('button', { onclick: () => { const n = prompt('New list name'); if (n) run(ops.listAdd(n).then((id) => { S.cur = id; })); } }, '+')),
      h('div', { class: 'card' },
        h('div', { class: 'bar' }, h('div', { class: 'grow day-h' }, listName(S.cur)),
          h('button', { onclick: () => { const n = prompt('Rename list', listName(S.cur)); if (n) run(ops.listRename(S.cur, n)); } }, 'Rename'),
          S.cur !== MAIN && h('button', { class: 'bad', onclick: () => { if (confirm('Delete this list and its to-dos?')) { const id = S.cur; S.cur = MAIN; run(ops.listDelete(id)); } } }, 'Delete')),
        tasks.length > 0 && h('div', { class: 'sub' }, done + ' of ' + tasks.length + ' done'),
        tasks.length > 0 && h('div', { class: 'prog' }, h('i', { style: 'width:' + Math.round((100 * done) / tasks.length) + '%' })),
        h('div', { class: 'quick' }, quick, h('button', { class: 'pri', onclick: more }, 'More\u2026')),
        tasks.map((t) => taskRow(t, false)), !tasks.length && h('div', { class: 'sub' }, 'No to-dos yet.')));
  };

  const draw = () => {
    if (!S.d) return;
    app.replaceChildren();
    ({ m: drawMonth, l: drawList, t: drawTodo })[S.view]();
    seg.querySelectorAll('button').forEach((b) => b.classList.toggle('on', b.dataset.v === S.view));
  };
  const seg = h('div', { class: 'seg' }, [['m', 'Month'], ['l', 'List'], ['t', 'To-do']].map(([v, n]) => h('button', { 'data-v': v, onclick: () => { S.view = v; draw(); } }, n)));
  root.append(h('div', { class: 'top' }, h('h2', {}, 'Daybook'), h('div', { class: 'grow' }), seg, h('button', { title: 'Reload from the card', onclick: () => run(Promise.resolve()) }, '\u27f3')), app);

  // ---- editors ----
  const field = (label, input, extra) => h('div', {}, h('label', {}, label), input, extra);
  const dateField = (label, init) => {
    const inp = h('input', { type: 'date', value: init != null ? iso(init) : '' }), wl = h('div', { class: 'wk-l' });
    const upd = () => { const v = parseDay(inp.value); wl.textContent = v == null ? '' : DOWL[wd(v)]; };
    inp.oninput = upd; upd();
    return { inp, node: field(label, inp, wl) };
  };
  const dlg = (title, body, buttons) => {
    const d = h('dialog', {}, h('h3', {}, title), body, h('div', { class: 'bar' }, buttons));
    d.onclose = () => d.remove();
    root.append(d); d.showModal();
    return d;
  };
  const eventDialog = (ev, day) => {
    const title = h('input', { type: 'text', maxlength: 60, value: ev ? ev.title : '' });
    const s = dateField('Start', ev ? ev.start : day), e = dateField('End', ev ? ev.end : null);
    const all = h('input', { type: 'checkbox', checked: ev ? ev.allDay : true });
    const from = h('input', { type: 'time', value: ev && !ev.allDay ? hm(ev.startMin) : '09:00' }), to = h('input', { type: 'time', value: ev && ev.endMin ? hm(ev.endMin) : '' });
    const times = h('div', { class: 'two' }, field('From', from), field('To', to));
    const rep = h('select', {}, ['Never', 'Daily', 'Weekly', 'Monthly', 'Yearly'].map((n, i) => h('option', { value: i, selected: ev ? ev.repeat === i : i === 0 }, n)));
    const every = h('input', { type: 'number', min: 1, max: 999, value: ev ? ev.every : 1 });
    const until = dateField('Repeat until', ev ? ev.until : null);
    const place = h('input', { type: 'text', maxlength: 60, value: ev ? ev.place : '' }), note = h('input', { type: 'text', maxlength: 240, value: ev ? ev.note : '' });
    const sync = () => { times.style.display = all.checked ? 'none' : ''; };
    all.onchange = sync; sync();
    const body = h('div', {}, field('Title', title), h('div', { class: 'two' }, s.node, e.node), h('label', { class: 'chk' }, all, 'All day'), times,
      h('div', { class: 'two' }, field('Repeat', rep), field('Every', every)), until.node, field('Place', place), field('Note', note));
    const save = h('button', { class: 'pri', onclick: () => {
      const v = { id: ev ? ev.id : 0, title: title.value, place: place.value, note: note.value, start: parseDay(s.inp.value), end: parseDay(e.inp.value), allDay: all.checked, startMin: parseHm(from.value), endMin: to.value ? parseHm(to.value) : 0, repeat: +rep.value, every: +every.value, until: parseDay(until.inp.value) };
      if (!v.title.trim() || v.start == null) return toast(!v.title.trim() ? 'Give it a title' : 'Pick a start date', true);
      d.close(); run(ops.eventSave(v), 'Saved');
    } }, 'Save');
    const d = dlg(ev ? 'Edit event' : 'New event', body, [save, h('button', { onclick: () => d.close() }, 'Cancel'), h('span', { class: 'grow' }),
      ev && h('button', { class: 'bad', onclick: () => { if (confirm('Delete this event?')) { d.close(); run(ops.eventDelete(ev.id), 'Deleted'); } } }, 'Delete')]);
    title.focus();
  };
  const taskDialog = (t, day, pre) => {
    const title = h('input', { type: 'text', maxlength: 60, value: t ? t.title : pre || '' });
    const list = h('select', {}, S.d.lists.map((l) => h('option', { value: l.id, selected: t ? t.list === l.id : l.id === S.cur }, l.name)));
    const due = dateField('Due', t ? t.due : day);
    const note = h('input', { type: 'text', maxlength: 240, value: t ? t.note : '' });
    const body = h('div', {}, field('To-do', title), field('List', list), due.node, field('Note', note));
    const save = h('button', { class: 'pri', onclick: () => {
      if (!title.value.trim()) return toast('Give it a title', true);
      d.close(); run(ops.taskSave({ id: t ? t.id : 0, title: title.value, list: +list.value, note: note.value, due: parseDay(due.inp.value) }), 'Saved');
    } }, 'Save');
    const d = dlg(t ? 'Edit to-do' : 'New to-do', body, [save, h('button', { onclick: () => d.close() }, 'Cancel'), h('span', { class: 'grow' }),
      t && h('button', { class: 'bad', onclick: () => { if (confirm('Delete this to-do?')) { d.close(); run(ops.taskDelete(t.id), 'Deleted'); } } }, 'Delete')]);
    title.focus();
  };

  try { S.d = await load(); draw(); } catch (e) { app.append(h('div', { class: 'card' }, 'Could not read the Daybook files from the card: ' + (e.message || e))); }
});
)JS";

std::string manifestText() {
  return std::string("{\n  \"name\": \"") + kName +
         "\",\n  \"title\": \"Daybook\",\n  \"description\": \"Calendar and "
         "to-dos, the same ones as on the "
         "device.\",\n  \"version\": \"" +
         kVersion + "\",\n  \"mount\": \"settings\"\n}\n";
}

std::string dirOf(const char* root) { return std::string(root) + "/" + kName; }

// An existing copy keeps its place, in whichever root it lives; a new one goes
// to the first root.
std::string pluginDir() {
  for (size_t i = 0; i < PluginLocations::ROOT_COUNT; i++) {
    const std::string manifest = dirOf(PluginLocations::ROOTS[i]) + "/manifest.json";
    if (Storage.exists(manifest.c_str())) return dirOf(PluginLocations::ROOTS[i]);
  }
  return dirOf(PluginLocations::ROOTS[0]);
}

bool writeFile(const std::string& path, const char* text, const size_t size) {
  const std::string tmp = path + ".tmp";
  {
    HalFile file;
    if (!Storage.openFileForWrite(kTag, tmp.c_str(), file)) return false;
    if (file.write(text, size) != size) {
      file.close();
      Storage.remove(tmp.c_str());
      return false;
    }
    file.flush();
  }
  if (!Storage.replaceFile(tmp.c_str(), path.c_str())) {
    if (Storage.exists(path.c_str())) Storage.remove(tmp.c_str());
    return false;
  }
  return true;
}

}  // namespace

State state() {
  const std::string dir = pluginDir();
  std::string manifest;
  if (!Storage.exists((dir + "/manifest.json").c_str())) return State::Missing;
  if (!Storage.readFileToString(kTag, dir + "/manifest.json", 1024, manifest)) return State::Outdated;
  const std::string want = std::string("\"version\": \"") + kVersion + "\"";
  if (manifest.find(want) == std::string::npos) return State::Outdated;
  return Storage.exists((dir + "/plugin.js").c_str()) ? State::Current : State::Outdated;
}

std::string install() {
  const std::string dir = pluginDir();
  if (!Storage.ensureDirectoryExists(dir.c_str())) return "Could not make the plugin folder.";
  const std::string manifest = manifestText();
  // Script last: a plugin only shows up as a page once its script is there.
  if (!writeFile(dir + "/README.md", kReadme, sizeof kReadme - 1) ||
      !writeFile(dir + "/manifest.json", manifest.c_str(), manifest.size()) ||
      !writeFile(dir + "/plugin.js", kScript, sizeof kScript - 1)) {
    return "Could not write to the card.";
  }
  return {};
}

bool pluginsEnabled() { return SETTINGS.pluginsEnabled != 0; }

}  // namespace daybook::plugin
