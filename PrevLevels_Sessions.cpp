//---------------------------------------------------------------------------
// Previous Levels + Sessions - indicator for Forex Tester (C++ API)
//
//  * Previous Month  High / Low   (PMH / PML)
//  * Previous Week   High / Low   (PWH / PWL)
//  * Previous Day    High / Low   (PDH / PDL)
//  * Weekly Open                  (W Open)
//  * High / Low of every session  (Asia, London, New York)
//
// Everything is recalculated automatically on every new bar, so when a new
// day / week / month starts, the levels move by themselves. Works in replay.
//
// Times: the chart (server) time of your data. Session hours are entered in
// GMT and shifted with "Chart time offset from GMT".
// Sunday bars are counted as part of Monday (normal for forex data).
//---------------------------------------------------------------------------

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "IndicatorInterfaceUnit.h"
#include "TechnicalFunctions.h"

//---------------------------------------------------------------------------
// Options
//---------------------------------------------------------------------------
bool   gShowPM, gShowPW, gShowPD, gShowWO;
TColor gColPM, gColPW, gColPD, gColWO;
int    gLineWidth;
int    gFontSize;
bool   gShowPrice;
int    gGmtOffset;      // chart time = GMT + offset (hours)
int    gSessCount;      // how many sessions per type to show

struct SessCfg
{
  bool   on;
  int    startH;
  int    endH;
  TColor col;
};
SessCfg gSess[3];
static const char* kSessName[3] = { "Asia", "London", "NY" };

TIndexBuffer bufDummy;

//---------------------------------------------------------------------------
// State
//---------------------------------------------------------------------------
struct LvlCache
{
  int        curKey;
  bool       valid;
  double     hi, lo;
  TDateTime  tHi, tLo;
};
static LvlCache  gC[3];            // 0 = day, 1 = week, 2 = month
static bool      gHaveKey[3];
static double    gWkOpen = 0;
static TDateTime gTWkOpen = 0;
static bool      gWkOk = false;

static TDateTime gLastTime = 0;
static int       gLastBars = 0;
static bool      gForce = true;
static int       gObjCount = 0;

//---------------------------------------------------------------------------
// Date helpers (TDateTime = days since 1899-12-30, which was a Saturday)
//---------------------------------------------------------------------------
static int DayNumMerged(TDateTime t)
{
  int d = (int)floor(t);
  if (d % 7 == 1) d++;               // Sunday -> Monday
  return d;
}

static int WeekKey(TDateTime t)
{
  return (DayNumMerged(t) - 2) / 7;  // weeks start on Monday
}

static int MonthKey(TDateTime t)
{
  long z = (long)floor(t) - 25569 + 719468;   // days since 1970 -> civil algorithm
  long era = (z >= 0 ? z : z - 146096) / 146097;
  long doe = z - era * 146097;
  long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long y = yoe + era * 400;
  long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  long mp = (5 * doy + 2) / 153;
  long m = mp < 10 ? mp + 3 : mp - 9;
  if (m <= 2) y++;
  return (int)(y * 12 + m);
}

static int KeyOf(int kind, TDateTime t)
{
  if (kind == 0) return DayNumMerged(t);
  if (kind == 1) return WeekKey(t);
  return MonthKey(t);
}

//---------------------------------------------------------------------------
// Object helpers
//---------------------------------------------------------------------------
static void LXClear()
{
  for (int i = ObjectsTotal() - 1; i >= 0; i--)
  {
    PChar nm = ObjectName(i);
    if (nm != NULL && strncmp(nm, "PL_", 3) == 0) ObjectDelete(nm);
  }
  gObjCount = 0;
}

static void PLLine(const char* tag, TDateTime t1, double p1, TDateTime t2, double p2,
                   TColor col, int width, TPenStyle st)
{
  char nm[64];
  sprintf(nm, "PL_%s_%d", tag, gObjCount++);
  if (ObjectCreate(nm, obj_TrendLine, 0, t1, p1, t2, p2))
  {
    ObjectSet(nm, OBJPROP_COLOR, (double)col);
    ObjectSet(nm, OBJPROP_WIDTH, width);
    ObjectSet(nm, OBJPROP_STYLE, st);
  }
}

static void PLText(const char* tag, TDateTime t, double p, const char* label, TColor col)
{
  char nm[64];
  char txt[96];
  if (gShowPrice) sprintf(txt, "%s  %.*f", label, Digits(), p);
  else            sprintf(txt, "%s", label);
  sprintf(nm, "PL_%s_%d", tag, gObjCount++);
  if (ObjectCreate(nm, obj_Text, 0, t, p))
    ObjectSetText(nm, (PChar)txt, gFontSize, (PChar)"Arial", col);
}

static void DrawLevel(const char* label, TDateTime t1, double price, TDateTime tEnd,
                      TColor col, TPenStyle st)
{
  PLLine("LV", t1, price, tEnd, price, col, gLineWidth, st);
  PLText("LT", tEnd, price, label, col);
}

//---------------------------------------------------------------------------
// Previous day / week / month levels (cached; recomputed only when the
// current period changes)
//---------------------------------------------------------------------------
static void ComputeLevels(int kind)
{
  int N = Bars();
  LvlCache& c = gC[kind];
  c.valid = false;
  if (N < 2) return;

  int cur = KeyOf(kind, Time(0));
  c.curKey = cur;
  gHaveKey[kind] = true;

  int k = 0;
  while (k < N && KeyOf(kind, Time(k)) == cur) k++;

  if (kind == 1 && k > 0)
  {
    gWkOpen = Open(k - 1);
    gTWkOpen = Time(k - 1);
    gWkOk = true;
  }
  if (k >= N) return;

  int pk = KeyOf(kind, Time(k));
  double hi = -1e18, lo = 1e18;
  TDateTime tHi = 0, tLo = 0;
  while (k < N && KeyOf(kind, Time(k)) == pk)
  {
    if (High(k) > hi) { hi = High(k); tHi = Time(k); }
    if (Low(k)  < lo) { lo = Low(k);  tLo = Time(k); }
    k++;
  }
  c.hi = hi; c.lo = lo; c.tHi = tHi; c.tLo = tLo;
  c.valid = true;
}

static void RefreshCaches()
{
  if (Bars() < 2) return;
  TDateTime t0 = Time(0);
  for (int kind = 0; kind < 3; kind++)
  {
    bool need = gForce || !gHaveKey[kind] || gC[kind].curKey != KeyOf(kind, t0);
    if (need) ComputeLevels(kind);
  }
}

//---------------------------------------------------------------------------
// Sessions
//---------------------------------------------------------------------------
struct SessInst
{
  long      key;
  double    hi, lo;
  TDateTime tHi, tLo;
};

static void DrawSessions(int N, TDateTime tEnd)
{
  std::vector<SessInst> inst[3];
  double off = gGmtOffset / 24.0;
  TDateTime tNow = Time(0);
  double limitT = tNow - (gSessCount + 2);

  for (int k = 0; k < N; k++)
  {
    TDateTime t = Time(k);
    if (t < limitT) break;
    double gmt = t - off;
    long gd = (long)floor(gmt);
    int gm = (int)floor((gmt - gd) * 1440.0 + 0.5);
    if (gm >= 1440) { gm -= 1440; gd++; }

    for (int s = 0; s < 3; s++)
    {
      if (!gSess[s].on) continue;
      int sm = gSess[s].startH * 60, em = gSess[s].endH * 60;
      bool cross = sm >= em;
      bool in = cross ? (gm >= sm || gm < em) : (gm >= sm && gm < em);
      if (!in) continue;
      long key = (cross && gm < em) ? gd - 1 : gd;
      std::vector<SessInst>& v = inst[s];
      if (v.empty() || v.back().key != key)
      {
        SessInst si;
        si.key = key; si.hi = High(k); si.lo = Low(k); si.tHi = t; si.tLo = t;
        v.push_back(si);
      }
      else
      {
        SessInst& si = v.back();
        if (High(k) > si.hi) { si.hi = High(k); si.tHi = t; }
        if (Low(k)  < si.lo) { si.lo = Low(k);  si.tLo = t; }
      }
    }
  }

  for (int s = 0; s < 3; s++)
  {
    if (!gSess[s].on) continue;
    int shown = 0;
    for (size_t i = 0; i < inst[s].size() && shown < gSessCount; i++, shown++)
    {
      const SessInst& si = inst[s][i];
      TDateTime tStart = si.key + gSess[s].startH / 24.0 + off;
      TDateTime t2 = (tStart + 1.0 < tEnd) ? tStart + 1.0 : tEnd;
      char lab[32];
      sprintf(lab, "%s H", kSessName[s]);
      PLLine("SS", tStart, si.hi, t2, si.hi, gSess[s].col, gLineWidth, psSolid);
      PLText("ST", tStart, si.hi, lab, gSess[s].col);
      sprintf(lab, "%s L", kSessName[s]);
      PLLine("SS", tStart, si.lo, t2, si.lo, gSess[s].col, gLineWidth, psSolid);
      PLText("ST", tStart, si.lo, lab, gSess[s].col);
    }
  }
}

//---------------------------------------------------------------------------
// Redraw
//---------------------------------------------------------------------------
static void LXRedraw()
{
  LXClear();
  int N = Bars();
  if (N < 5) return;

  double bd = Timeframe() / 1440.0;
  TDateTime tEnd = Time(0) + 6 * bd;

  if (gShowPM && gC[2].valid)
  {
    DrawLevel("PMH", gC[2].tHi, gC[2].hi, tEnd, gColPM, psSolid);
    DrawLevel("PML", gC[2].tLo, gC[2].lo, tEnd, gColPM, psSolid);
  }
  if (gShowPW && gC[1].valid)
  {
    DrawLevel("PWH", gC[1].tHi, gC[1].hi, tEnd, gColPW, psSolid);
    DrawLevel("PWL", gC[1].tLo, gC[1].lo, tEnd, gColPW, psSolid);
  }
  if (gShowPD && gC[0].valid)
  {
    DrawLevel("PDH", gC[0].tHi, gC[0].hi, tEnd, gColPD, psSolid);
    DrawLevel("PDL", gC[0].tLo, gC[0].lo, tEnd, gColPD, psSolid);
  }
  if (gShowWO && gWkOk)
    DrawLevel("W Open", gTWkOpen, gWkOpen, tEnd, gColWO, psDot);

  DrawSessions(N, tEnd);
}

//---------------------------------------------------------------------------
// Options
//---------------------------------------------------------------------------
#define OPT_INT(var, name, def, lo, hi) { RegOption((PChar)name, ot_Integer, &var); SetOptionRange((PChar)name, lo, hi); var = def; }
#define OPT_BOOL(var, name, def)        { RegOption((PChar)name, ot_Boolean, &var); var = def; }
#define OPT_COL(var, name, def)         { RegOption((PChar)name, ot_Color, &var); var = def; }

EXPORT void __stdcall Init()
{
  IndicatorShortName((PChar)"Previous Levels + Sessions");
  SetOutputWindow(ow_ChartWindow);

  AddSeparator((PChar)"Previous levels");
  OPT_BOOL(gShowPM, "Show previous month H/L", true);
  OPT_COL (gColPM,  "Month color",             RGB(0xFF, 0xD7, 0x00));
  OPT_BOOL(gShowPW, "Show previous week H/L",  true);
  OPT_COL (gColPW,  "Week color",              RGB(0xFF, 0x98, 0x00));
  OPT_BOOL(gShowPD, "Show previous day H/L",   true);
  OPT_COL (gColPD,  "Day color",               RGB(0x29, 0xB6, 0xF6));
  OPT_BOOL(gShowWO, "Show weekly open",        true);
  OPT_COL (gColWO,  "Weekly open color",       RGB(0xFF, 0xEB, 0x3B));

  AddSeparator((PChar)"Time");
  OPT_INT (gGmtOffset, "Chart time offset from GMT (hours)", 0, -12, 14);

  AddSeparator((PChar)"Sessions (hours in GMT)");
  OPT_INT (gSessCount, "Sessions shown per type", 3, 1, 10);

  OPT_BOOL(gSess[0].on,     "Show Asia",          true);
  OPT_INT (gSess[0].startH, "Asia start hour",    0,  0, 23);
  OPT_INT (gSess[0].endH,   "Asia end hour",      8,  0, 24);
  OPT_COL (gSess[0].col,    "Asia color",         RGB(0x9C, 0x27, 0xB0));

  OPT_BOOL(gSess[1].on,     "Show London",        true);
  OPT_INT (gSess[1].startH, "London start hour",  8,  0, 23);
  OPT_INT (gSess[1].endH,   "London end hour",    16, 0, 24);
  OPT_COL (gSess[1].col,    "London color",       RGB(0x4C, 0xAF, 0x50));

  OPT_BOOL(gSess[2].on,     "Show New York",      true);
  OPT_INT (gSess[2].startH, "New York start hour",13, 0, 23);
  OPT_INT (gSess[2].endH,   "New York end hour",  21, 0, 24);
  OPT_COL (gSess[2].col,    "New York color",     RGB(0xF4, 0x43, 0x36));

  AddSeparator((PChar)"Style");
  OPT_INT (gLineWidth, "Line width",         1, 1, 5);
  OPT_INT (gFontSize,  "Label font size",    8, 6, 20);
  OPT_BOOL(gShowPrice, "Show price in label", true);

  bufDummy = CreateIndexBuffer();
  IndicatorBuffers(1);
  SetIndexBuffer(0, bufDummy);
  SetIndexStyle(0, ds_None, psSolid, 1, clWhite);
  SetIndexLabel(0, (PChar)"Levels");

  gHaveKey[0] = gHaveKey[1] = gHaveKey[2] = false;
  gWkOk = false;
  gForce = true;
  gLastBars = 0;
  gLastTime = 0;
}

EXPORT void __stdcall OnParamsChange()
{
  gForce = true;
}

EXPORT void __stdcall Done()
{
  LXClear();
}

//---------------------------------------------------------------------------
// Calculate requested bar
//---------------------------------------------------------------------------
EXPORT void __stdcall Calculate(int index)
{
  if (index != 0) return;

  int N = Bars();
  TDateTime t0 = Time(0);
  if (gForce || t0 != gLastTime || N != gLastBars)
  {
    RefreshCaches();
    gForce = false;
    gLastTime = t0;
    gLastBars = N;
    LXRedraw();
  }
}
