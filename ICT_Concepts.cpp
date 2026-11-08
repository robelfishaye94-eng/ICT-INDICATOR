//---------------------------------------------------------------------------
// ICT Concepts - port for Forex Tester (C++ indicator API v1.13/1.14)
//
// Based on "ICT Concepts [LuxAlgo]" for TradingView
// Original (c) LuxAlgo, licensed CC BY-NC-SA 4.0
// https://creativecommons.org/licenses/by-nc-sa/4.0/
// This port keeps the same license: credit LuxAlgo, share alike, no commercial use.
//
// Includes : Market Structure (MSS / BOS), Order Blocks, Liquidity,
//            Fair Value Gaps, Volume Imbalance, NWOG / NDOG, Displacement,
//            swing points.
// Not ported: Killzones, Fibonacci tool, BPR / IFVG (see notes in README).
//
// Design notes
//  - Forex Tester has no box / label objects, so zones are drawn as pairs of
//    trend lines (top + bottom) with a text object.
//  - All drawing is rebuilt from scratch once per new bar, from COMPLETED bars
//    only (bar 0 is the forming bar and is ignored), so nothing repaints.
//  - Bar index convention of the FT API: 0 = newest bar, larger = older.
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
int   gMaxBars;          // how many bars back to analyse / draw
int   gSwingLen;         // swing length for market structure

bool  gShowMS, gShowMSS, gShowBOS;
int   gMaxStruct;
TColor gColMSSBull, gColMSSBear, gColBOSBull, gColBOSBear;

bool  gShowOB;
int   gOBLookback, gShowBullOB, gShowBearOB;
bool  gUseBody;
TColor gColOBBull, gColOBBear, gColOBBrk;

bool  gShowLiq;
double gLiqMargin;
int   gVisLiq;
TColor gColLiqB, gColLiqS;

bool  gShowFVG, gFVGNeedDisp;
int   gVisFVG;
TColor gColFVGBull, gColFVGBear, gColFVGMit;

bool  gShowVI;
int   gVisVI;
TColor gColVI;

bool  gShowNWOG, gShowNDOG;
int   gMaxNWOG, gMaxNDOG;
TColor gColNWOG, gColNDOG;

bool  gShowDisp;

//---------------------------------------------------------------------------
// Buffers (symbols on the chart)
//---------------------------------------------------------------------------
TIndexBuffer bufSwingHigh, bufSwingLow, bufDispUp, bufDispDn;

//---------------------------------------------------------------------------
// Internal state
//---------------------------------------------------------------------------
static TDateTime gLastTime  = 0;
static int       gLastBars  = 0;
static bool      gForce     = true;
static int       gObjCount  = 0;

struct LXPiv  { int bar; double price; };
struct LXBrk  { int k; bool bull; bool mss; int pbar; double lvl; };
struct LXOb   { int loc; double top; double btm; bool bull; int brk; bool removed; };
struct LXZone { int left; double mid; double m; int detect; };

//---------------------------------------------------------------------------
// Small helpers (named to avoid the min/max macros of windows.h)
//---------------------------------------------------------------------------
static int    LXMinI(int a, int b)       { return a < b ? a : b; }
static double LXMaxD(double a, double b) { return a > b ? a : b; }
static double LXMinD(double a, double b) { return a < b ? a : b; }

static double BodyHi(int i) { return LXMaxD(Open(i), Close(i)); }
static double BodyLo(int i) { return LXMinD(Open(i), Close(i)); }

static double LXAtr(int i, int per)
{
  int N = Bars();
  double s = 0;
  int cnt = 0;
  for (int j = i; j < i + per && j + 1 < N; j++)
  {
    double h = High(j), l = Low(j), pc = Close(j + 1);
    double tr = h - l;
    if (fabs(h - pc) > tr) tr = fabs(h - pc);
    if (fabs(l - pc) > tr) tr = fabs(l - pc);
    s += tr;
    cnt++;
  }
  return cnt ? s / cnt : 0;
}

// pivot needs 'len' completed bars on each side (bar 0 = forming bar, ignored)
static bool IsPivotHigh(int c, int len)
{
  if (c - len < 1 || c + len >= Bars()) return false;
  double h = High(c);
  for (int k = 1; k <= len; k++)
    if (High(c - k) >= h || High(c + k) > h) return false;
  return true;
}

static bool IsPivotLow(int c, int len)
{
  if (c - len < 1 || c + len >= Bars()) return false;
  double l = Low(c);
  for (int k = 1; k <= len; k++)
    if (Low(c - k) <= l || Low(c + k) < l) return false;
  return true;
}

// displacement candle: big body, tiny wicks (LuxAlgo: wick < 36% of body)
static bool DispCandle(int i, bool up)
{
  int N = Bars();
  if (i < 0 || i + gSwingLen >= N) return false;
  double o = Open(i), c = Close(i), h = High(i), l = Low(i);
  double mx = LXMaxD(o, c), mn = LXMinD(o, c);
  double body = fabs(c - o);
  if (body <= 0) return false;
  if (!((h - mx) < body * 0.36 && (mn - l) < body * 0.36)) return false;
  double s = 0;
  for (int j = i; j < i + gSwingLen; j++) s += fabs(Close(j) - Open(j));
  double mean = s / gSwingLen;
  if (body <= mean) return false;
  return up ? (c > o) : (c < o);
}

//---------------------------------------------------------------------------
// Object helpers
//---------------------------------------------------------------------------
static void LXClear()
{
  for (int i = ObjectsTotal() - 1; i >= 0; i--)
  {
    PChar nm = ObjectName(i);
    if (nm != NULL && strncmp(nm, "LX_", 3) == 0) ObjectDelete(nm);
  }
  gObjCount = 0;
}

static void LXLine(const char* tag, TDateTime t1, double p1, TDateTime t2, double p2,
                   TColor col, int width, TPenStyle st)
{
  char nm[64];
  sprintf(nm, "LX_%s_%d", tag, gObjCount++);
  if (ObjectCreate(nm, obj_TrendLine, 0, t1, p1, t2, p2))
  {
    ObjectSet(nm, OBJPROP_COLOR, (double)col);
    ObjectSet(nm, OBJPROP_WIDTH, width);
    ObjectSet(nm, OBJPROP_STYLE, st);
  }
}

static void LXText(const char* tag, TDateTime t, double p, const char* text, TColor col, int size)
{
  char nm[64];
  sprintf(nm, "LX_%s_%d", tag, gObjCount++);
  if (ObjectCreate(nm, obj_Text, 0, t, p))
    ObjectSetText(nm, (PChar)text, size, (PChar)"Arial", col);
}

// a "box" = top line + bottom line + caption
static void LXBox(const char* tag, TDateTime t1, TDateTime t2, double top, double btm,
                  TColor col, TPenStyle st, const char* caption)
{
  LXLine(tag, t1, top, t2, top, col, 1, st);
  LXLine(tag, t1, btm, t2, btm, col, 1, st);
  if (caption != NULL && caption[0] != 0) LXText(tag, t1, top, caption, col, 8);
}

//---------------------------------------------------------------------------
// Pivots
//---------------------------------------------------------------------------
static void BuildPivots(int len, int maxBar, std::vector<LXPiv>& hi, std::vector<LXPiv>& lo)
{
  int N = Bars();
  int top = LXMinI(maxBar, N - 1 - len);
  for (int p = len + 1; p <= top; p++)
  {
    if (IsPivotHigh(p, len)) { LXPiv v; v.bar = p; v.price = High(p); hi.push_back(v); }
    if (IsPivotLow (p, len)) { LXPiv v; v.bar = p; v.price = Low(p);  lo.push_back(v); }
  }
}

// Scan completed bars oldest -> newest, return first close beyond the nearest
// confirmed swing (one event per swing, like LuxAlgo's "crossed" flag).
// Output list is chronological (oldest first).
static void FindBreaks(int len, int startK, const std::vector<LXPiv>& ph,
                       const std::vector<LXPiv>& pl, std::vector<LXBrk>& out)
{
  int lastH = -1, lastL = -1, dir = 0;
  for (int k = startK; k >= 1; k--)
  {
    const LXPiv* sh = NULL;
    const LXPiv* sl = NULL;
    for (size_t i = 0; i < ph.size(); i++) if (ph[i].bar >= k + len) { sh = &ph[i]; break; }
    for (size_t i = 0; i < pl.size(); i++) if (pl[i].bar >= k + len) { sl = &pl[i]; break; }

    double c = Close(k);
    if (sh != NULL && c > sh->price && sh->bar != lastH)
    {
      lastH = sh->bar;
      LXBrk b; b.k = k; b.bull = true; b.mss = (dir != 1); b.pbar = sh->bar; b.lvl = sh->price;
      out.push_back(b);
      dir = 1;
    }
    else if (sl != NULL && c < sl->price && sl->bar != lastL)
    {
      lastL = sl->bar;
      LXBrk b; b.k = k; b.bull = false; b.mss = (dir != -1); b.pbar = sl->bar; b.lvl = sl->price;
      out.push_back(b);
      dir = -1;
    }
  }
}

//---------------------------------------------------------------------------
// Redraw everything
//---------------------------------------------------------------------------
static void DrawStructure(int N, int lastK, TDateTime tEnd, double atrNow)
{
  int len = gSwingLen;
  int startK = LXMinI(lastK + 150, N - 2 * len - 5);
  if (startK < 5) return;
  std::vector<LXPiv> ph, pl;
  BuildPivots(len, LXMinI(startK + len + 200, N - 1 - len), ph, pl);

  std::vector<LXBrk> ev;
  FindBreaks(len, startK, ph, pl, ev);

  // market structure lines
  if (gShowMS)
  {
    int shown = 0;
    for (int e = (int)ev.size() - 1; e >= 0 && shown < gMaxStruct; e--)
    {
      const LXBrk& b = ev[e];
      if (b.k > lastK) break;
      if (b.mss && !gShowMSS) continue;
      if (!b.mss && !gShowBOS) continue;
      TColor col = b.bull ? (b.mss ? gColMSSBull : gColBOSBull) : (b.mss ? gColMSSBear : gColBOSBear);
      LXLine("MS", Time(b.pbar), b.lvl, Time(b.k), b.lvl, col, 1, b.mss ? psSolid : psDot);
      double off = atrNow * 0.15;
      LXText("MSt", Time((b.pbar + b.k) / 2), b.bull ? b.lvl + off : b.lvl - off,
             b.mss ? "MSS" : "BOS", col, 8);
      shown++;
    }
  }

  // liquidity (clusters of equal highs / lows)
  if (gShowLiq)
  {
    for (int side = 0; side < 2; side++)
    {
      const std::vector<LXPiv>& pv = side == 0 ? ph : pl;
      std::vector<LXZone> zones;
      for (int ii = (int)pv.size() - 1; ii >= 0; ii--)
      {
        if (pv[ii].bar > startK) continue;
        double y = pv[ii].price;
        double m = LXAtr(pv[ii].bar, 10) * gLiqMargin / 10.0;
        if (m <= 0) continue;
        int count = 0, st = 0;
        double mxv = 0, mnv = 1e12;
        for (int jj = ii; jj < (int)pv.size() && jj < ii + 25; jj++)
        {
          double yj = pv[jj].price;
          if (side == 0 ? (yj > y + m) : (yj < y - m)) break;
          if (yj > y - m && yj < y + m)
          {
            count++;
            st = pv[jj].bar;
            if (yj > mxv) mxv = yj;
            if (yj < mnv) mnv = yj;
          }
        }
        if (count > 2)
        {
          LXZone z; z.left = st; z.mid = (mxv + mnv) / 2; z.m = m; z.detect = pv[ii].bar;
          if (!zones.empty() && zones.back().left == st) zones.back() = z;
          else zones.push_back(z);
        }
      }
      int shown = 0;
      for (int zi = (int)zones.size() - 1; zi >= 0 && shown < gVisLiq; zi--, shown++)
      {
        const LXZone& z = zones[zi];
        double top = z.mid + z.m, btm = z.mid - z.m;
        int swept = -1;
        for (int j = z.detect - 1; j >= 1; j--)
        {
          if (side == 0 ? (Close(j) > top) : (Close(j) < btm)) { swept = j; break; }
        }
        TDateTime t2 = swept > 0 ? Time(swept) : tEnd;
        LXBox(side == 0 ? "LQB" : "LQS", Time(z.left), t2, top, btm,
              side == 0 ? gColLiqB : gColLiqS, swept > 0 ? psDot : psSolid,
              side == 0 ? "Buyside liquidity" : "Sellside liquidity");
      }
    }
  }
}

static void DrawOrderBlocks(int N, int lastK, TDateTime tEnd)
{
  int olen = gOBLookback;
  int startK = LXMinI(lastK + 150, N - 2 * olen - 5);
  if (startK < 5) return;
  std::vector<LXPiv> ph, pl;
  BuildPivots(olen, LXMinI(startK + olen + 200, N - 1 - olen), ph, pl);
  std::vector<LXBrk> ev;
  FindBreaks(olen, startK, ph, pl, ev);

  std::vector<LXOb> obs;
  for (size_t e = 0; e < ev.size(); e++)
  {
    const LXBrk& b = ev[e];
    int from = b.k + 1, to = b.pbar - 1;
    if (to < from) continue;
    LXOb o;
    o.bull = b.bull; o.brk = -1; o.removed = false;
    double ext = b.bull ? 1e12 : -1e12;
    o.loc = from; o.top = 0; o.btm = 0;
    for (int j = from; j <= to; j++)
    {
      double hiV = gUseBody ? BodyHi(j) : High(j);
      double loV = gUseBody ? BodyLo(j) : Low(j);
      if (b.bull) { if (loV <= ext) { ext = loV; o.loc = j; o.btm = loV; o.top = hiV; } }
      else        { if (hiV >= ext) { ext = hiV; o.loc = j; o.top = hiV; o.btm = loV; } }
    }
    // breaker / removal (same rules as the original)
    for (int j = b.k; j >= 1; j--)
    {
      if (o.brk < 0)
      {
        if (b.bull ? (BodyLo(j) < o.btm) : (BodyHi(j) > o.top)) o.brk = j;
      }
      else
      {
        if (b.bull ? (Close(j) > o.top) : (Close(j) < o.btm)) { o.removed = true; break; }
      }
    }
    obs.push_back(o);
  }

  for (int side = 0; side < 2; side++)
  {
    bool bull = (side == 0);
    int want = bull ? gShowBullOB : gShowBearOB;
    int shown = 0;
    for (int i = (int)obs.size() - 1; i >= 0 && shown < want; i--)
    {
      const LXOb& o = obs[i];
      if (o.bull != bull || o.removed) continue;
      TColor col = o.brk > 0 ? gColOBBrk : (bull ? gColOBBull : gColOBBear);
      TDateTime t2 = tEnd;
      LXBox(bull ? "OBB" : "OBS", Time(o.loc), t2, o.top, o.btm, col,
            o.brk > 0 ? psDot : psSolid,
            o.brk > 0 ? (bull ? "BB" : "BB") : (bull ? "+OB" : "-OB"));
      shown++;
    }
  }
}

static void DrawFVG(int N, int lastK, TDateTime tEnd)
{
  for (int dir = 0; dir < 2; dir++)
  {
    bool bull = (dir == 0);
    int shown = 0;
    for (int k = 1; k <= lastK && k + 2 < N && shown < gVisFVG; k++)
    {
      double top, btm;
      if (bull)
      {
        if (!(Low(k) > High(k + 2))) continue;
        if (gFVGNeedDisp && !DispCandle(k + 1, true)) continue;
        btm = High(k + 2); top = Low(k);
      }
      else
      {
        if (!(High(k) < Low(k + 2))) continue;
        if (gFVGNeedDisp && !DispCandle(k + 1, false)) continue;
        top = Low(k + 2); btm = High(k);
      }
      int mit = -1;
      for (int j = k - 1; j >= 1; j--)
      {
        if (bull ? (Low(j) < btm) : (High(j) > top)) { mit = j; break; }
      }
      TColor col = mit > 0 ? gColFVGMit : (bull ? gColFVGBull : gColFVGBear);
      LXBox(bull ? "FVGB" : "FVGS", Time(k + 2), mit > 0 ? Time(mit) : tEnd, top, btm,
            col, mit > 0 ? psDot : psSolid, "FVG");
      shown++;
    }
  }
}

static void DrawVI(int N, int lastK, TDateTime tEnd, double bd)
{
  int shown = 0;
  for (int k = 1; k <= lastK && k + 1 < N && shown < gVisVI; k++)
  {
    double mx = BodyHi(k), mn = BodyLo(k);
    bool bl = Open(k) > Close(k + 1) && High(k + 1) > Low(k) && Close(k) > Close(k + 1) &&
              Open(k) > Open(k + 1) && High(k + 1) < mn;
    bool br = Open(k) < Close(k + 1) && Low(k + 1) < High(k) && Close(k) < Close(k + 1) &&
              Open(k) < Open(k + 1) && Low(k + 1) > mx;
    if (!bl && !br) continue;
    TDateTime t1 = Time(k + 1), t2 = Time(k) + 3 * bd;
    double a = bl ? BodyHi(k + 1) : BodyLo(k + 1);
    double b = bl ? mn : mx;
    LXLine("VI", t1, a, t2, a, gColVI, 1, psSolid);
    LXLine("VI", Time(k), b, t2, b, gColVI, 1, psSolid);
    LXText("VIt", t2, (a + b) / 2, "VI", gColVI, 8);
    shown++;
  }
}

static void DrawGaps(int N, int lastK, TDateTime tEnd)
{
  if (Timeframe() >= PERIOD_D1) return;
  int nw = 0, nd = 0;
  for (int k = 1; k <= lastK && k + 1 < N; k++)
  {
    double c = Close(k + 1), o = Open(k);
    if (fabs(o - c) <= 0) continue;
    double gap = Time(k) - Time(k + 1);
    bool newWeek = gap > 1.5;
    bool newDay  = floor(Time(k)) != floor(Time(k + 1));
    double top = LXMaxD(o, c), btm = LXMinD(o, c), mid = (o + c) / 2;
    if (newWeek && gShowNWOG && nw < gMaxNWOG)
    {
      LXLine("NWOG", Time(k + 1), top, tEnd, top, gColNWOG, 1, psSolid);
      LXLine("NWOG", Time(k + 1), btm, tEnd, btm, gColNWOG, 1, psSolid);
      LXLine("NWOG", Time(k),     mid, tEnd, mid, gColNWOG, 1, psDot);
      LXText("NWOGt", Time(k + 1), top, "NWOG", gColNWOG, 8);
      nw++;
    }
    if (newDay && gShowNDOG && nd < gMaxNDOG)
    {
      LXLine("NDOG", Time(k + 1), top, tEnd, top, gColNDOG, 1, psSolid);
      LXLine("NDOG", Time(k + 1), btm, tEnd, btm, gColNDOG, 1, psSolid);
      LXLine("NDOG", Time(k),     mid, tEnd, mid, gColNDOG, 1, psDot);
      LXText("NDOGt", Time(k + 1), top, "NDOG", gColNDOG, 8);
      nd++;
    }
  }
}

static void LXRedraw()
{
  LXClear();
  int N = Bars();
  if (N < 60) return;
  int widest = gSwingLen > gOBLookback ? gSwingLen : gOBLookback;
  int lastK = LXMinI(gMaxBars, N - 2 * widest - 6);
  if (lastK < 5) return;

  double bd = Timeframe() / 1440.0;
  TDateTime tEnd = Time(0) + 8 * bd;
  double atrNow = LXAtr(1, 14);

  DrawStructure(N, lastK, tEnd, atrNow);
  if (gShowOB)  DrawOrderBlocks(N, lastK, tEnd);
  if (gShowFVG) DrawFVG(N, lastK, tEnd);
  if (gShowVI)  DrawVI(N, lastK, tEnd, bd);
  if (gShowNWOG || gShowNDOG) DrawGaps(N, lastK, tEnd);
}

//---------------------------------------------------------------------------
// Option registration helpers
//---------------------------------------------------------------------------
#define OPT_INT(var, name, def, lo, hi) { RegOption((PChar)name, ot_Integer, &var); SetOptionRange((PChar)name, lo, hi); var = def; }
#define OPT_BOOL(var, name, def)        { RegOption((PChar)name, ot_Boolean, &var); var = def; }
#define OPT_COL(var, name, def)         { RegOption((PChar)name, ot_Color, &var); var = def; }

//---------------------------------------------------------------------------
// Initialize indicator
//---------------------------------------------------------------------------
EXPORT void __stdcall Init()
{
  IndicatorShortName((PChar)"ICT Concepts");
  SetOutputWindow(ow_ChartWindow);
  SetEmptyValue(0.0);

  AddSeparator((PChar)"Common");
  OPT_INT (gMaxBars,   "Bars to analyse",     400, 50, 3000);
  OPT_INT (gSwingLen,  "Swing length",        5,   3,  10);

  AddSeparator((PChar)"Market Structures");
  OPT_BOOL(gShowMS,    "Show market structure", true);
  OPT_BOOL(gShowMSS,   "Show MSS",              true);
  OPT_BOOL(gShowBOS,   "Show BOS",              true);
  OPT_INT (gMaxStruct, "Max structure lines",   8, 1, 100);
  OPT_COL (gColMSSBull,"MSS bullish color",     RGB(0x00, 0xE6, 0xA1));
  OPT_COL (gColMSSBear,"MSS bearish color",     RGB(0xE6, 0x04, 0x00));
  OPT_COL (gColBOSBull,"BOS bullish color",     RGB(0x00, 0xE6, 0xA1));
  OPT_COL (gColBOSBear,"BOS bearish color",     RGB(0xE6, 0x04, 0x00));

  AddSeparator((PChar)"Order Blocks");
  OPT_BOOL(gShowOB,     "Show order blocks",    true);
  OPT_INT (gOBLookback, "OB swing lookback",    10, 3, 50);
  OPT_INT (gShowBullOB, "Show last bullish OB", 1, 0, 20);
  OPT_INT (gShowBearOB, "Show last bearish OB", 1, 0, 20);
  OPT_BOOL(gUseBody,    "Use candle body",      true);
  OPT_COL (gColOBBull,  "Bullish OB color",     RGB(0x3E, 0x89, 0xFA));
  OPT_COL (gColOBBear,  "Bearish OB color",     RGB(0xFF, 0x31, 0x31));
  OPT_COL (gColOBBrk,   "Broken OB color",      RGB(0x80, 0x80, 0x80));

  AddSeparator((PChar)"Liquidity");
  OPT_BOOL(gShowLiq,   "Show liquidity",        true);
  RegOption((PChar)"Liquidity margin", ot_Double, &gLiqMargin);
  SetOptionRange((PChar)"Liquidity margin", 2, 7);
  SetOptionDigits((PChar)"Liquidity margin", 1);
  gLiqMargin = 4.0;
  OPT_INT (gVisLiq,    "Visible liquidity zones", 2, 1, 50);
  OPT_COL (gColLiqB,   "Buyside liquidity color",  RGB(0xFA, 0x45, 0x1C));
  OPT_COL (gColLiqS,   "Sellside liquidity color", RGB(0x1C, 0xE4, 0xFA));

  AddSeparator((PChar)"Fair Value Gaps");
  OPT_BOOL(gShowFVG,     "Show FVGs",                   true);
  OPT_BOOL(gFVGNeedDisp, "FVG needs displacement candle", true);
  OPT_INT (gVisFVG,      "Visible FVGs per side",       3, 1, 20);
  OPT_COL (gColFVGBull,  "Bullish FVG color",           RGB(0x00, 0xE6, 0x76));
  OPT_COL (gColFVGBear,  "Bearish FVG color",           RGB(0xFF, 0x52, 0x52));
  OPT_COL (gColFVGMit,   "Mitigated FVG color",         RGB(0x80, 0x80, 0x00));

  AddSeparator((PChar)"Volume Imbalance");
  OPT_BOOL(gShowVI,    "Show volume imbalance", true);
  OPT_INT (gVisVI,     "Visible VIs",           2, 1, 100);
  OPT_COL (gColVI,     "VI color",              RGB(0x06, 0xB2, 0xD0));

  AddSeparator((PChar)"NWOG / NDOG");
  OPT_BOOL(gShowNWOG,  "Show NWOG",   true);
  OPT_INT (gMaxNWOG,   "Max NWOG",    3, 0, 50);
  OPT_COL (gColNWOG,   "NWOG color",  RGB(0xFF, 0x52, 0x52));
  OPT_BOOL(gShowNDOG,  "Show NDOG",   false);
  OPT_INT (gMaxNDOG,   "Max NDOG",    1, 0, 50);
  OPT_COL (gColNDOG,   "NDOG color",  RGB(0xFF, 0x98, 0x00));

  AddSeparator((PChar)"Displacement");
  OPT_BOOL(gShowDisp,  "Show displacement", false);

  // buffers: swing points + displacement arrows
  bufSwingHigh = CreateIndexBuffer();
  bufSwingLow  = CreateIndexBuffer();
  bufDispUp    = CreateIndexBuffer();
  bufDispDn    = CreateIndexBuffer();

  IndicatorBuffers(4);
  SetIndexBuffer(0, bufSwingHigh);
  SetIndexBuffer(1, bufSwingLow);
  SetIndexBuffer(2, bufDispUp);
  SetIndexBuffer(3, bufDispDn);

  SetIndexStyle(0, ds_Symbol, psSolid, 1, RGB(0xFF, 0xD7, 0x00));
  SetIndexSymbol(0, 159, 0, -8);            // Wingdings bullet above swing high
  SetIndexLabel(0, (PChar)"Swing High");

  SetIndexStyle(1, ds_Symbol, psSolid, 1, RGB(0xFF, 0xD7, 0x00));
  SetIndexSymbol(1, 159, 0, 8);             // bullet below swing low
  SetIndexLabel(1, (PChar)"Swing Low");

  SetIndexStyle(2, ds_Symbol, psSolid, 1, clLime);
  SetIndexSymbol(2, 233, 0, 10);            // up arrow under displacement candle
  SetIndexLabel(2, (PChar)"Displacement Up");

  SetIndexStyle(3, ds_Symbol, psSolid, 1, clRed);
  SetIndexSymbol(3, 234, 0, -10);           // down arrow over displacement candle
  SetIndexLabel(3, (PChar)"Displacement Down");

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
  int N = Bars();
  int len = gSwingLen;

  // swing points: confirmed by 'len' completed bars after the pivot
  if (index <= len)
  {
    bufSwingHigh[index] = 0;
    bufSwingLow[index]  = 0;
  }
  int c = index + len + 1;
  if (c < N)
  {
    bufSwingHigh[c] = IsPivotHigh(c, len) ? High(c) : 0;
    bufSwingLow[c]  = IsPivotLow (c, len) ? Low(c)  : 0;
  }

  // displacement arrows
  bufDispUp[index] = (gShowDisp && DispCandle(index, true))  ? Low(index)  : 0;
  bufDispDn[index] = (gShowDisp && DispCandle(index, false)) ? High(index) : 0;

  // zones / lines: rebuilt once per new bar
  if (index == 0)
  {
    TDateTime t0 = Time(0);
    if (gForce || t0 != gLastTime || N != gLastBars)
    {
      gForce = false;
      gLastTime = t0;
      gLastBars = N;
      LXRedraw();
    }
  }
}
