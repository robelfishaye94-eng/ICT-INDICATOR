//---------------------------------------------------------------------------
// Sessions indicator for Forex Tester 6
// Draws the Asia, London and New York sessions as boxes (session high/low)
// on the chart window.
//---------------------------------------------------------------------------

#include <windows.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "IndicatorInterfaceUnit.h"

#define NSESS 3

struct TSession
{
  bool    Show;
  int     StartHour;   // GMT, inclusive
  int     EndHour;     // GMT, exclusive (0..24)
  TColor  Color;
  TIndexBuffer Hi;     // session high  -> buffer 2*k
  TIndexBuffer Lo;     // session low   -> buffer 2*k+1
};

// External variables
int      GmtOffset;    // chart time = GMT + GmtOffset (hours)
bool     FillBoxes;
TSession S[NSESS];

static const char* SessionNames[NSESS] = { "Asia", "London", "New York" };

//---------------------------------------------------------------------------
// Helpers
//---------------------------------------------------------------------------

// minutes since midnight in GMT for a chart time stamp
static int GmtMinutes(TDateTime t)
{
  double frac = t - floor(t);
  int m = (int)floor(frac * 1440.0 + 0.5);
  m = (m - GmtOffset * 60) % 1440;
  if (m < 0) m += 1440;
  return m;
}

// is the GMT minute inside the session? (supports sessions crossing midnight)
static bool InSession(const TSession& s, int m)
{
  int a = s.StartHour * 60;
  int b = s.EndHour * 60;
  if (a == b) return false;
  if (a < b)  return (m >= a && m < b);
  return (m >= a || m < b);
}

// apply colors / styles (called from Init and after every parameter change)
static void ApplyStyles()
{
  for (int k = 0; k < NSESS; k++)
  {
    char label[64];

    SetIndexStyle(2*k, ds_Line, psSolid, 1, S[k].Color);
    sprintf_s(label, sizeof(label), "%s High", SessionNames[k]);
    SetIndexLabel(2*k, label);

    SetIndexStyle(2*k + 1, FillBoxes ? ds_Fill : ds_Line, psSolid, 1, S[k].Color);
    sprintf_s(label, sizeof(label), "%s Low", SessionNames[k]);
    SetIndexLabel(2*k + 1, label);
  }
}

//---------------------------------------------------------------------------
// Initialize indicator
//---------------------------------------------------------------------------
EXPORT void __stdcall Init()
{
  IndicatorShortName("Sessions");
  SetOutputWindow(ow_ChartWindow);
  SetEmptyValue(0.0);

  // ---- options ----
  AddSeparator("Common");

  RegOption("Chart time offset from GMT (hours)", ot_Integer, &GmtOffset);
  SetOptionRange("Chart time offset from GMT (hours)", -12, 14);
  GmtOffset = 0;

  RegOption("Fill session boxes", ot_Boolean, &FillBoxes);
  FillBoxes = true;

  AddSeparator("Asia (hours in GMT)");
  RegOption("Show Asia", ot_Boolean, &S[0].Show);
  RegOption("Asia start hour", ot_Integer, &S[0].StartHour);
  SetOptionRange("Asia start hour", 0, 23);
  RegOption("Asia end hour", ot_Integer, &S[0].EndHour);
  SetOptionRange("Asia end hour", 0, 24);
  RegOption("Asia color", ot_Color, &S[0].Color);
  S[0].Show = true;  S[0].StartHour = 0;  S[0].EndHour = 9;  S[0].Color = clLightSkyBlue;

  AddSeparator("London (hours in GMT)");
  RegOption("Show London", ot_Boolean, &S[1].Show);
  RegOption("London start hour", ot_Integer, &S[1].StartHour);
  SetOptionRange("London start hour", 0, 23);
  RegOption("London end hour", ot_Integer, &S[1].EndHour);
  SetOptionRange("London end hour", 0, 24);
  RegOption("London color", ot_Color, &S[1].Color);
  S[1].Show = true;  S[1].StartHour = 7;  S[1].EndHour = 16; S[1].Color = clLightGreen;

  AddSeparator("New York (hours in GMT)");
  RegOption("Show New York", ot_Boolean, &S[2].Show);
  RegOption("New York start hour", ot_Integer, &S[2].StartHour);
  SetOptionRange("New York start hour", 0, 23);
  RegOption("New York end hour", ot_Integer, &S[2].EndHour);
  SetOptionRange("New York end hour", 0, 24);
  RegOption("New York color", ot_Color, &S[2].Color);
  S[2].Show = true;  S[2].StartHour = 12; S[2].EndHour = 21; S[2].Color = clLightSalmon;

  // ---- buffers: for every session a High line and a Low line (+ fill) ----
  for (int k = 0; k < NSESS; k++)
  {
    S[k].Hi = CreateIndexBuffer();
    S[k].Lo = CreateIndexBuffer();
  }

  IndicatorBuffers(2 * NSESS);
  for (int k = 0; k < NSESS; k++)
  {
    SetIndexBuffer(2*k,     S[k].Hi);
    SetIndexBuffer(2*k + 1, S[k].Lo);
  }

  ApplyStyles();
}

EXPORT void __stdcall OnParamsChange()
{
  ApplyStyles();
}

//---------------------------------------------------------------------------
// Calculate requested bar (index 0 = newest bar)
//---------------------------------------------------------------------------
EXPORT void __stdcall Calculate(int index)
{
  int n = Bars();
  if (index < 0 || index >= n) return;

  bool tfTooHigh = (Timeframe() >= PERIOD_D1);   // sessions make no sense on D1+

  TDateTime t = Time(index);
  int m = GmtMinutes(t);

  for (int k = 0; k < NSESS; k++)
  {
    double hi = 0.0, lo = 0.0;

    if (S[k].Show && !tfTooHigh && InSession(S[k], m))
    {
      double h = High(index);
      double l = Low(index);
      double pHi = 0.0, pLo = 0.0;
      bool   cont = false;

      // is the previous (older) bar part of the same session?
      if (index + 1 < n)
      {
        TDateTime tp = Time(index + 1);
        if ((t - tp) < 0.5 && InSession(S[k], GmtMinutes(tp)))
        {
          pHi = S[k].Hi.GetValue(index + 1);
          pLo = S[k].Lo.GetValue(index + 1);
          cont = (pHi != 0.0 && pLo != 0.0);
        }
      }

      if (!cont)
      {
        hi = h;
        lo = l;
      }
      else
      {
        hi = (h > pHi) ? h : pHi;
        lo = (l < pLo) ? l : pLo;

        // new extreme: stretch the box over all earlier bars of this session
        if (hi != pHi || lo != pLo)
        {
          int j = index + 1;
          TDateTime tj = Time(j);
          for (;;)
          {
            S[k].Hi.SetValue(j, hi);
            S[k].Lo.SetValue(j, lo);

            if (j + 1 >= n) break;
            TDateTime tn = Time(j + 1);
            if ((tj - tn) >= 0.5 || !InSession(S[k], GmtMinutes(tn))) break;
            j++;
            tj = tn;
          }
        }
      }
    }

    S[k].Hi.SetValue(index, hi);
    S[k].Lo.SetValue(index, lo);
  }
}
