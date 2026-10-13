/****************************************************************************
 *  Genesis Plus -- Win32 GUI frontend
 *
 *  dialogs.c -- the modal dialogs reachable from the menu.
 ****************************************************************************/

#include <windows.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <commctrl.h>
#include <shellapi.h>
#include <commdlg.h>
#define COBJMACROS
#include <shobjidl.h>

#include <uxtheme.h>
#include <zlib.h>

#include "shared.h"
#include "gui.h"
#include "resource.h"
#include "theme.h"
#include "netplay.h"
#include "toolbar.h"

/****************************************************************************
 * Tooltips for the dialogs: one tooltip window per dialog, hiding itself
 * after 30 seconds. A tip goes on a control by id, and/or on the plain text
 * label (a static with that text) next to it.
 ****************************************************************************/

#define TipDef GuiTip
#define dlg_add_tips gui_dialog_tips

static void dlg_add_tip(HWND tt, HWND dlg, HWND ctl, const char *text)
{
  TOOLINFOA ti;

  if (!ctl || !text) return;
  memset(&ti, 0, sizeof(ti));
  ti.cbSize = sizeof(ti);
  ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  ti.hwnd = dlg;
  ti.uId = (UINT_PTR)ctl;
  ti.lpszText = (LPSTR)text;
  SendMessageA(tt, TTM_ADDTOOLA, 0, (LPARAM)&ti);
}

void gui_dialog_tips(HWND dlg, const GuiTip *defs, int n)
{
  HWND tt = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                            0, 0, 0, 0, dlg, NULL, GetModuleHandle(NULL), NULL);
  int i;

  if (!tt) return;
  SendMessage(tt, TTM_SETMAXTIPWIDTH, 0, TBS(320));
  SendMessage(tt, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);
  SendMessage(tt, TTM_SETDELAYTIME, TTDT_INITIAL, 400);
  SendMessage(tt, WM_SETFONT, SendMessage(dlg, WM_GETFONT, 0, 0), FALSE);

  for (i = 0; i < n; i++)
  {
    if (defs[i].id) dlg_add_tip(tt, dlg, GetDlgItem(dlg, defs[i].id), defs[i].tip);
    if (defs[i].label)
    {
      HWND c;
      for (c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
      {
        char cls[16], txt[96];
        GetClassNameA(c, cls, sizeof(cls));
        if (lstrcmpiA(cls, "Static") != 0) continue;
        GetWindowTextA(c, txt, sizeof(txt));
        if (lstrcmpA(txt, defs[i].label) != 0) continue;
        SetWindowLongPtr(c, GWL_STYLE, GetWindowLongPtr(c, GWL_STYLE) | SS_NOTIFY);   /* so it gets the mouse */
        dlg_add_tip(tt, dlg, c, defs[i].tip);
      }
    }
  }
}

/****************************************************************************
 * Input configuration
 *
 * Assigning works by listening rather than by asking the user to pick from a
 * list: choose a button, press what you want it to be. The same listening
 * pass covers the keyboard and the gamepad, so there is no separate mode to
 * switch into for controllers.
 ****************************************************************************/

/* Listening state machine */
enum { LISTEN_OFF = 0, LISTEN_WAIT_RELEASE, LISTEN_ACTIVE };

static struct
{
  int       player;     /* the tab showing */
  int       sel;        /* the mapping being assigned */
  int       test;       /* Test Keys is ticked: the keyboard only lights spots, it does not operate the window */
  unsigned  live;       /* bit per pad button: its key or gamepad button is held right now */
  HBITMAP   picture;    /* the controller, scaled to pic */
  HFONT     font;       /* the dialog font, 21% larger */
  RECT      pic;        /* where it is drawn, in dialog client coordinates */
  struct { RECT rc; UINT fmt; char text[32]; } label[16];   /* the names beside the buttons: painted, not controls, so they never cover the picture */
  int       labels;
  struct { t_pad_map map; uint8 padtype; int deadzone; } other[2];   /* each player's working copy while its tab is not showing */
  t_pad_map map;        /* working copy of the showing player, committed on Save */
  uint8     padtype;
  int       deadzone;   /* working copy, 0-100% */
  int       listen;
  int       listen_target;  /* which row listening is for, fixed at start */
  DWORD     listen_deadline;  /* GetTickCount() value; listening cancels itself past this */
  char      notice[300];        /* what the last assignment displaced or collides with, shown as the hint */
  int       listen_shown_secs;  /* countdown value the hint currently shows, -1 forces a redraw */
  int       auto_advance;   /* set by Auto Assign only; Assign/double-click leave it off */
  HWND      dlg;
} inp;

/* Shared by every dialog with a slider-plus-percentage-readout control
   (Audio Levels and Latency, and this one) -- sets the slider's range and
   starting position and writes the matching text into its paired readout,
   all in one call instead of five repeated at every call site. */
static void audio_set_slider(HWND dlg, int id, int text_id, int lo, int hi,
                             int value, const char *suffix)
{
  HWND bar = GetDlgItem(dlg, id);
  char buf[32];

  SendMessage(bar, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
  SendMessage(bar, TBM_SETTICFREQ, (WPARAM)((hi - lo) / 8 ? (hi - lo) / 8 : 1), 0);
  SendMessage(bar, TBM_SETPOS, TRUE, (LPARAM)value);

  wsprintfA(buf, "%d%s", value, suffix);
  SetDlgItemTextA(dlg, text_id, buf);
}

static int input_list_count(void)
{
  return PAD_KEYS;
}

static const char *input_list_entry_name(int sel)
{
  return input_button_label(sel);
}

/* The window is drawn 21% larger than the rest of the dialogs (10% twice). */
#define IS(v) (TBS(v) * 121 / 100)

#define INPUT_HINT_BASE "Click a button (or its spot on the pad), then press the key or gamepad button you want. Right-click a button to clear it (A, B, C, X, Y and Z also offer Turbo, which fires the button repeatedly while it is held). Pressing a mapped key or button lights its spot."
#define INPUT_HINT_TEST "Test mode: press any key or pad button and its spot lights on the pad. Keys no longer move around or press buttons in this window (Enter, Tab, Esc ...). Click Test Keys again to leave."
#define INPUT_HINT_IDLE (inp.test ? INPUT_HINT_TEST : INPUT_HINT_BASE)

/* Each mapping is a button on the controller picture's sides, with its name
   written beside it; the button shows what it is set to. */
static void input_fill_list(HWND dlg)
{
  int i, count = input_list_count();

  for (i = 0; i < count; i++)
  {
    char line[160];
    int key, button, dev;

    key = inp.map.key[i]; button = inp.map.button[i]; dev = inp.map.device;

    if (inp.listen != LISTEN_OFF && i == inp.listen_target)
      lstrcpyA(line, "Press a key or button...");
    else
      wsprintfA(line, "%s  /  %s", input_key_name(key), input_pad_button_name(dev, button));
    if (i < PAD_KEYS && ((0x3F0u >> i) & 1u) && (inp.map.turbo & (1u << i)) && !(inp.listen != LISTEN_OFF && i == inp.listen_target))
      lstrcatA(line, "  (Turbo)");
    SetDlgItemTextA(dlg, IDC_INPUT_LABEL_BASE + i, line);
  }
}

/* Where each button sits on the picture, in thousandths of its width/height. */
static const short input_hotspot[PAD_KEYS][2] =
{
  { 249, 323 }, { 249, 550 }, { 195, 433 }, { 303, 433 },   /* Up Down Left Right */
  { 669, 569 }, { 761, 497 }, { 855, 433 },                 /* A B C */
  { 636, 385 }, { 710, 309 }, { 796, 269 },                 /* X Y Z */
  { 485, 385 }, { 796, 142 }                                /* Start, Mode (above Z) */
};

static RECT input_pic_rc(void)
{
  return inp.pic;
}

static void input_redraw_picture(HWND dlg)
{
  RECT rc = input_pic_rc();
  InflateRect(&rc, IS(14), IS(14));
  InvalidateRect(dlg, &rc, TRUE);
}

/* The controller, scaled once to the size it is shown at. It is stored
   compressed and premultiplied (pad.bin), so it blends onto either theme. */
static HBITMAP input_make_picture(int dw, int dh)
{
  HRSRC res = FindResourceA(NULL, MAKEINTRESOURCEA(IDR_PADIMG), (LPCSTR)RT_RCDATA);
  const unsigned char *src;
  unsigned char *raw, *dst;
  BITMAPINFO bi;
  HBITMAP bmp = NULL;
  int sw, sh, x, y;
  uLongf rawlen;

  if (!res) return NULL;
  src = (const unsigned char *)LockResource(LoadResource(NULL, res));
  if (!src) return NULL;
  sw = ((const int *)src)[0];
  sh = ((const int *)src)[1];
  rawlen = (uLongf)sw * sh * 4;
  raw = (unsigned char *)malloc(rawlen);
  if (!raw) return NULL;
  if (uncompress(raw, &rawlen, src + 8, SizeofResource(NULL, res) - 8) != Z_OK) { free(raw); return NULL; }

  ZeroMemory(&bi, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = dw;
  bi.bmiHeader.biHeight = -dh;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&dst, NULL, 0);
  if (bmp)
  {
    for (y = 0; y < dh; y++)
    {
      int y0 = y * sh / dh, y1 = ((y + 1) * sh + dh - 1) / dh;
      if (y1 <= y0) y1 = y0 + 1;
      if (y1 > sh) y1 = sh;
      for (x = 0; x < dw; x++)
      {
        int x0 = x * sw / dw, x1 = ((x + 1) * sw + dw - 1) / dw;
        int sx, sy, n = 0, c[4] = { 0, 0, 0, 0 };
        if (x1 <= x0) x1 = x0 + 1;
        if (x1 > sw) x1 = sw;
        for (sy = y0; sy < y1; sy++)
          for (sx = x0; sx < x1; sx++)
          {
            const unsigned char *p = raw + ((size_t)sy * sw + sx) * 4;
            c[0] += p[0]; c[1] += p[1]; c[2] += p[2]; c[3] += p[3];
            n++;
          }
        dst[((size_t)y * dw + x) * 4 + 0] = (unsigned char)(c[0] / n);
        dst[((size_t)y * dw + x) * 4 + 1] = (unsigned char)(c[1] / n);
        dst[((size_t)y * dw + x) * 4 + 2] = (unsigned char)(c[2] / n);
        dst[((size_t)y * dw + x) * 4 + 3] = (unsigned char)(c[3] / n);
      }
    }
  }
  free(raw);
  return bmp;
}

static void input_paint(HWND dlg)
{
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(dlg, &ps);
  RECT rc = input_pic_rc();

  if (inp.picture)
  {
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, inp.picture);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
               mem, 0, 0, rc.right - rc.left, rc.bottom - rc.top, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
  }

  /* A ring marks the button being assigned, and one lights for every mapped
     button or key that is held down right now. */
  {
    int i;
    HPEN pen = CreatePen(PS_SOLID, IS(3), RGB(255, 140, 0));
    HBRUSH fill = CreateSolidBrush(RGB(255, 140, 0));
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));

    for (i = 0; i < PAD_KEYS; i++)
    {
      int cx = rc.left + (rc.right - rc.left) * input_hotspot[i][0] / 1000;
      int cy = rc.top  + (rc.bottom - rc.top) * input_hotspot[i][1] / 1000;
      int r = IS(11), d = IS(4);

      if (inp.listen != LISTEN_OFF && i == inp.listen_target)
        Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
      if (inp.live & (1u << i))
      {
        Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
        SelectObject(dc, fill);
        Ellipse(dc, cx - d, cy - d, cx + d, cy + d);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
      }
    }
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
    DeleteObject(fill);
  }

  /* The names beside the buttons, drawn straight on the window over the picture's edge. */
  {
    int i;
    HGDIOBJ of = SelectObject(dc, (HFONT)SendMessage(dlg, WM_GETFONT, 0, 0));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, theme_is_dark() ? RGB(240, 240, 240) : GetSysColor(COLOR_WINDOWTEXT));
    for (i = 0; i < inp.labels; i++)
      DrawTextA(dc, inp.label[i].text, -1, &inp.label[i].rc, inp.label[i].fmt | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, of);
  }

  EndPaint(dlg, &ps);
}

/* Rebuilds the "Press the key or gamepad button..." hint with however many
   seconds are left before listening times out -- called every tick rather
   than once, but only actually touches the control (SetDlgItemTextA) when
   the displayed number changes, so this isn't repainting 60+ times a
   second for no visible difference. */
static void input_update_listen_hint(HWND dlg)
{
  char hint[160];
  int remaining_ms = (int)(inp.listen_deadline - GetTickCount());
  int secs = (remaining_ms > 0) ? (remaining_ms + 999) / 1000 : 0;

  if (secs == inp.listen_shown_secs) return;
  inp.listen_shown_secs = secs;

  wsprintfA(hint, "Press the key or gamepad button for \"%s\".%s Esc cancels (times out in %ds).",
            input_list_entry_name(inp.listen_target),
            inp.auto_advance ? " Then moves to the next button." : "",
            secs);
  SetDlgItemTextA(dlg, IDC_INPUT_HINT, hint);
}

static void input_set_listening(HWND dlg, int on)
{
  static const int controls[] =
  {
    IDC_INPUT_AUTOASSIGN, IDC_INPUT_DEFAULTS, IDC_INPUT_PADHK, IDC_INPUT_DEVICE, IDC_INPUT_PADTYPE, IDOK, IDCANCEL
  };
  int i;

  /* Focus moves onto the hint text before anything is disabled: disabling the
     focused control lets Windows pass focus (and a key press still in
     flight) on to whatever is next, which started a second assignment. */
  if (on) SetFocus(GetDlgItem(dlg, IDC_INPUT_HINT));

  for (i = 0; i < (int)(sizeof(controls) / sizeof(controls[0])); i++)
  {
    HWND ctl = GetDlgItem(dlg, controls[i]);
    EnableWindow(ctl, on ? FALSE : TRUE);
    RedrawWindow(ctl, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
  }
  for (i = 0; i < input_list_count(); i++)
  {
    HWND ctl = GetDlgItem(dlg, IDC_INPUT_LABEL_BASE + i);
    EnableWindow(ctl, on ? FALSE : TRUE);
    RedrawWindow(ctl, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
  }

  /* The blue default-button border tracks focus history, not enabled state,
     so it is handed back to the dialog's real default. */
  SendMessage(dlg, DM_SETDEFID, (WPARAM)IDOK, 0);

  if (on)
  {
    inp.listen = LISTEN_WAIT_RELEASE;
    inp.listen_target = inp.sel;
    inp.listen_deadline = GetTickCount() + 6000;
    inp.listen_shown_secs = -1;   /* forces input_update_listen_hint() to draw immediately below */

    input_update_listen_hint(dlg);
    SetTimer(dlg, 1, 15, NULL);
  }
  else
  {
    inp.listen = LISTEN_OFF;
    KillTimer(dlg, 1);
    SetDlgItemTextA(dlg, IDC_INPUT_HINT, INPUT_HINT_IDLE);
  }
  input_fill_list(dlg);
  input_redraw_picture(dlg);
}

/* After a successful assignment, either moves on to the next button and
   re-arms listening automatically (Auto Assign), or just stops there
   (a click on one button) -- inp.auto_advance, set by whichever one
   actually started this listen, decides which. Reuses
   input_set_listening(dlg, 1) as-is when advancing, including its
   "wait for the current key/button to be released first" safety --
   without that, whatever's still physically held from the assignment
   just made could get immediately captured again as the next one. */
/* A key or pad button was just given to pad button sel. Takes it off any other button of this player (and of the
   other player, for keys, or a pad on the same device), and writes down what happened. A key that is also a shortcut,
   or a pad button that is also a gamepad hotkey, is only mentioned. */
/* Appends to the notice without ever running past its end (a key can sit on several buttons). */
static void notice_add(char *n, const char *fmt, ...)
{
  char tmp[200];
  va_list ap;

  va_start(ap, fmt);
  wvsprintfA(tmp, fmt, ap);
  va_end(ap);
  lstrcpynA(n + lstrlenA(n), tmp, (int)sizeof(inp.notice) - lstrlenA(n));
}

static void input_resolve_conflicts(int sel, int is_key, int value)
{
  char *n = inp.notice;
  int j, o = 1 - inp.player;
  const char *other;

  wsprintfA(n, "%s is now on %s.", is_key ? input_key_name(value) : input_pad_button_name(inp.map.device, value), input_list_entry_name(sel));

  for (j = 0; j < PAD_KEYS; j++)
  {
    if (j == sel) continue;
    if (is_key ? inp.map.key[j] == value : inp.map.button[j] == value)
    {
      if (is_key) inp.map.key[j] = 0; else inp.map.button[j] = 0;
      notice_add(n, " It was on %s: removed there.", input_list_entry_name(j));
    }
  }
  for (j = 0; j < PAD_KEYS && (is_key || inp.other[o].map.device == inp.map.device); j++)
  {
    if (is_key ? inp.other[o].map.key[j] == value : inp.other[o].map.button[j] == value)
    {
      if (is_key) inp.other[o].map.key[j] = 0; else inp.other[o].map.button[j] = 0;
      notice_add(n, " It was on Player %d %s: removed there.", o + 1, input_list_entry_name(j));
    }
  }
  if (is_key && (other = hotkey_owner_of_key(value)) != NULL)
    notice_add(n, " Note: it is also the shortcut \"%s\" (Input > Hotkeys).", other);
  if (!is_key && inp.map.device >= 0 && inp.map.device < GUI_INPUT_XPAD_COUNT && (other = padhk_owner_of_button(value)) != NULL)
    notice_add(n, " Note: it is also the gamepad hotkey \"%s\" (Controls > Gamepad Hotkeys).", other);
}

static void input_advance_or_stop(HWND dlg, int sel)
{
  if (inp.auto_advance && sel + 1 < PAD_KEYS)   /* the pad buttons only, not the Fast Forward / Rewind hotkeys */
  {
    inp.sel = sel + 1;
    input_set_listening(dlg, 0);
    input_set_listening(dlg, 1);
  }
  else
  {
    input_set_listening(dlg, 0);
    if (inp.notice[0]) SetDlgItemTextA(dlg, IDC_INPUT_HINT, inp.notice);
  }
}

static void input_clear(HWND dlg, int sel)
{
  if (sel < 0 || sel >= input_list_count()) return;
  inp.map.key[sel] = 0; inp.map.button[sel] = 0;
  input_fill_list(dlg);
}

static void input_capture_tick(HWND dlg)
{
  int sel = inp.listen_target;
  int vk, button;

  if (sel < 0 || sel >= input_list_count())
  {
    input_set_listening(dlg, 0);
    return;
  }

  /* GetAsyncKeyState below (both directly for Escape, and inside
     input_capture_key()/input_capture_gamepad()) queries key/button
     state system-wide, with no idea which window is focused -- without
     this check, switching away to a different application entirely
     while a button was mid-assignment would still have whatever's
     typed there captured and assigned here, in the background. */
  if (GetForegroundWindow() != dlg) return;

  input_update_listen_hint(dlg);

  /* No key or gamepad button in 6 seconds -- most likely nothing was ever
     going to arrive (wrong device selected, a controller that isn't
     actually connected), so this backs out to the same ungreyed state Esc
     gives rather than leaving the dialog stuck waiting indefinitely. */
  if ((int)(GetTickCount() - inp.listen_deadline) >= 0)
  {
    input_set_listening(dlg, 0);
    return;
  }

  if (inp.listen == LISTEN_WAIT_RELEASE)
  {
    /* Do not capture the very keypress that opened the prompt. */
    if (!input_any_input_down(inp.map.device)) inp.listen = LISTEN_ACTIVE;
    return;
  }

  if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
  {
    input_set_listening(dlg, 0);
    return;
  }

  vk = input_capture_key();
  if (vk)
  {
    inp.map.key[sel] = vk;
    input_resolve_conflicts(sel, 1, vk);
    input_fill_list(dlg);
    input_advance_or_stop(dlg, sel);
    return;
  }

  button = input_capture_gamepad(inp.map.device);
  if (button)
  {
    inp.map.button[sel] = button;
    input_resolve_conflicts(sel, 0, button);
    input_fill_list(dlg);
    input_advance_or_stop(dlg, sel);
  }
}

/* Runs continuously while the dialog is open and idle (a separate, slower
   timer than input_capture_tick()'s -- that one only exists to service an
   active "listening" capture). Lets someone tell two same-named XInput pads
   apart -- "Gamepad 1"/"Gamepad 2" carry no identifying information of their
   own, since which physical pad Windows hands which XInput slot depends on
   connection order, not anything this app controls -- without going through
   a full Assign. Whichever pad is currently selected in the device combo
   just needs a button pressed on it; nothing gets recorded anywhere. */
static void input_update_identify_hint(HWND dlg)
{
  /* The resting hint is all the text needs now: whichever pad is selected
     shows its presses live on the picture (input_update_live). */
  if (inp.listen != LISTEN_OFF) return;
  SetDlgItemTextA(dlg, IDC_INPUT_HINT, INPUT_HINT_IDLE);
}

/* Lights the spot of every mapped button whose key or gamepad button is held
   down: polled on a short timer, and only repainted when something changed. */
static void input_update_live(HWND dlg)
{
  unsigned now = 0;
  int i, dev = (int)SendMessage(GetDlgItem(dlg, IDC_INPUT_DEVICE), CB_GETCURSEL, 0, 0) - 1;

  if (inp.listen == LISTEN_OFF && GetForegroundWindow() == dlg)
  {
    for (i = 0; i < PAD_KEYS; i++)
    {
      if (inp.map.key[i] && (GetAsyncKeyState(inp.map.key[i]) & 0x8000)) now |= 1u << i;
      else if (dev >= 0 && inp.map.button[i] && input_pad_button_down(dev, inp.map.button[i])) now |= 1u << i;
    }
  }

  if (now != inp.live)
  {
    inp.live = now;
    input_redraw_picture(dlg);
  }
}

static void input_load_controls(HWND dlg)
{
  HWND dev = GetDlgItem(dlg, IDC_INPUT_DEVICE);
  HWND type = GetDlgItem(dlg, IDC_INPUT_PADTYPE);
  int i, joy_count;

  /* "Keyboard Only", then one entry per XInput slot, then one per attached
     DirectInput joystick -- contiguous with t_pad_map.device's own numbering
     (-1, 0.., GUI_INPUT_XPAD_COUNT..), so the combo's selected index and the
     device value it maps to are always exactly one apart, list position and
     device range alike. */
  SendMessage(dev, CB_RESETCONTENT, 0, 0);
  SendMessageA(dev, CB_ADDSTRING, 0, (LPARAM)"Keyboard Only");
  for (i = 0; i < GUI_INPUT_XPAD_COUNT; i++)
  {
    char buf[32];
    wsprintfA(buf, "Gamepad %d", i + 1);
    SendMessageA(dev, CB_ADDSTRING, 0, (LPARAM)buf);
  }
  joy_count = gui_input_joystick_count();
  for (i = 0; i < joy_count; i++)
  {
    SendMessageA(dev, CB_ADDSTRING, 0, (LPARAM)gui_input_joystick_name(i));
  }

  /* A joystick's product name easily runs longer than "Gamepad 4" ever did
     -- widens the dropdown (not the closed combo box itself, which stays
     whatever width the .rc template gives it) so a longer one isn't
     truncated the moment the list opens. */
  if (joy_count) SendMessage(dev, CB_SETDROPPEDWIDTH, 220, 0);
  SendMessage(dev, CB_SETCURSEL, (WPARAM)(inp.map.device + 1), 0);

  SendMessage(type, CB_RESETCONTENT, 0, 0);
  SendMessageA(type, CB_ADDSTRING, 0, (LPARAM)"Match the Game");
  SendMessageA(type, CB_ADDSTRING, 0, (LPARAM)"3-Button Pad");
  SendMessageA(type, CB_ADDSTRING, 0, (LPARAM)"6-Button Pad");
  SendMessageA(type, CB_ADDSTRING, 0, (LPARAM)"2-Button Pad");

  switch (inp.padtype)
  {
    case DEVICE_PAD3B: SendMessage(type, CB_SETCURSEL, 1, 0); break;
    case DEVICE_PAD6B: SendMessage(type, CB_SETCURSEL, 2, 0); break;
    case DEVICE_PAD2B: SendMessage(type, CB_SETCURSEL, 3, 0); break;
    default:           SendMessage(type, CB_SETCURSEL, 0, 0); break;
  }

  audio_set_slider(dlg, IDC_INPUT_DEADZONE, IDC_INPUT_DEADZONE_TEXT, 0, 100, inp.deadzone, "%");
}

static void input_read_controls(HWND dlg)
{
  int dev = (int)SendMessage(GetDlgItem(dlg, IDC_INPUT_DEVICE), CB_GETCURSEL, 0, 0);
  int type = (int)SendMessage(GetDlgItem(dlg, IDC_INPUT_PADTYPE), CB_GETCURSEL, 0, 0);

  /* CB_ERR here doesn't mean the user picked nothing -- the combo always has
     at least "Keyboard Only", so a real choice never comes back this way.
     It means input_load_controls()'s own CB_SETCURSEL(device+1) failed
     because inp.map.device pointed past the currently-populated list (a
     saved DirectInput mapping whose joystick isn't attached right now, so
     it couldn't be resolved to a valid combo item -- see
     gui_input_resolve_device()). Leaving inp.map.device/joy_name untouched
     in that case preserves the saved assignment; overwriting it with -1
     would silently downgrade the player to keyboard-only just because the
     dialog happened to be opened (for an unrelated key rebind, say) while
     their joystick was unplugged. */
  if (dev != CB_ERR)
  {
    inp.map.device = dev - 1;

    /* Remembered so a saved mapping can find the same physical joystick
       again even if it re-enumerates at a different index next time (see
       gui_input_resolve_device()). Meaningless for keyboard/XInput, so
       cleared there instead of left stale from whatever DirectInput device
       used to be picked. */
    if (inp.map.device >= GUI_INPUT_XPAD_COUNT)
      lstrcpynA(inp.map.joy_name, gui_input_joystick_name(inp.map.device - GUI_INPUT_XPAD_COUNT), sizeof(inp.map.joy_name));
    else
      inp.map.joy_name[0] = '\0';
  }

  switch (type)
  {
    case 1:  inp.padtype = DEVICE_PAD3B; break;
    case 2:  inp.padtype = DEVICE_PAD6B; break;
    case 3:  inp.padtype = DEVICE_PAD2B; break;
    default: inp.padtype = DEVICE_PAD2B | DEVICE_PAD3B | DEVICE_PAD6B; break;
  }

  inp.deadzone = (int)SendMessage(GetDlgItem(dlg, IDC_INPUT_DEADZONE), TBM_GETPOS, 0, 0);
}

/* The text of a name beside a button: "Up:", "A:" ... */
static const char *input_label_text(int i)
{
  static char buf[PAD_KEYS][32];
  wsprintfA(buf[i], "%s:", input_list_entry_name(i));
  return buf[i];
}

static void input_add_label(const char *text, int x, int y, int w, int h, UINT fmt)
{
  if (inp.labels >= 16) return;
  SetRect(&inp.label[inp.labels].rc, x, y, x + w, y + h);
  inp.label[inp.labels].fmt = fmt;
  lstrcpynA(inp.label[inp.labels].text, text, sizeof(inp.label[inp.labels].text));
  inp.labels++;
}

static HWND input_make(HWND dlg, const char *cls, const char *text, DWORD style,
                       int x, int y, int w, int h, int id)
{
  HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, dlg,
                           (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
  SendMessage(c, WM_SETFONT, (WPARAM)(inp.font ? inp.font : (HFONT)SendMessage(dlg, WM_GETFONT, 0, 0)), TRUE);
  return c;
}


/* The whole window is built here, around the controller picture: the d-pad,
   Start and Mode on its left, the face buttons on its right, the hotkeys under
   it. */
static void input_build(HWND dlg)
{
  static const char *const player_tabs[] = { "Player 1", "Player 2" };
  static const int left_col[]  = { 0, 1, 2, 3, 10, 11 };     /* Up Down Left Right Start Mode */
  static const int right_col[] = { 7, 8, 9, 4, 5, 6 };       /* X Y Z A B C */
  const DWORD bs = WS_TABSTOP | BS_PUSHBUTTON | BS_MULTILINE;
  int M = IS(14), th = settings_tab_height() * 121 / 100;
  int cw = IS(720), colw = IS(170), bh = IS(36), pitch = IS(42);
  int nudge = IS(21);                      /* the right column sits a little in from the edge */
  int y0 = th + M, ym = y0 + IS(40), i, y, picw, pich, picx, picy;
  RECT rc, wr;
  HWND tab;
  DWORD style = (DWORD)GetWindowLongPtr(dlg, GWL_STYLE), ex = (DWORD)GetWindowLongPtr(dlg, GWL_EXSTYLE);

  if (!inp.font)
  {
    LOGFONTA lf;
    if (GetObjectA((HFONT)SendMessage(dlg, WM_GETFONT, 0, 0), sizeof(lf), &lf))
    {
      /* Larger UI already makes the dialog font bigger; scaling it again looked heavy, so it is used as is. */
      if (!gui.large_ui) lf.lfHeight = lf.lfHeight * 121 / 100;
      lf.lfWeight = FW_NORMAL;
      inp.font = CreateFontIndirectA(&lf);
      if (inp.font) SendMessage(dlg, WM_SETFONT, (WPARAM)inp.font, FALSE);
    }
  }
  inp.labels = 0;
  settings_register_classes();
  tab = input_make(dlg, "GPSettingsTabs", "", WS_TABSTOP, 0, 0, cw, th, IDC_INPUT_TAB);
  settings_set_tabs(tab, player_tabs, 2);
  SetWindowLongPtr(tab, GWLP_USERDATA, inp.player);

  input_make(dlg, "STATIC", "Input Devices:", SS_LEFT, M, y0 + IS(4), IS(88), IS(16), IDC_STATIC);
  input_make(dlg, "COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
             M + IS(92), y0, IS(230), IS(220), IDC_INPUT_DEVICE);
  input_make(dlg, "STATIC", "Pad Type:", SS_RIGHT, cw - M - IS(170) - IS(70), y0 + IS(4), IS(64), IS(16), IDC_STATIC);
  input_make(dlg, "COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
             cw - M - IS(170), y0, IS(170), IS(160), IDC_INPUT_PADTYPE);

  {
    int lw = IS(46);                       /* the name beside each button */
    for (i = 0; i < 6; i++)
    {
      int id = left_col[i], id2 = right_col[i];
      y = ym + i * pitch;
      input_add_label(input_label_text(id), M, y + (bh - IS(16)) / 2, lw - IS(6), IS(16), DT_RIGHT);
      input_make(dlg, "BUTTON", "", bs, M + lw, y, colw - lw, bh, IDC_INPUT_LABEL_BASE + id);
      /* a narrow box, right next to its button, so it never covers the picture's edge */
      input_add_label(input_label_text(id2), cw - M - colw + lw - nudge - IS(8) - IS(26), y + (bh - IS(16)) / 2, IS(26), IS(16), DT_RIGHT);
      input_make(dlg, "BUTTON", "", bs, cw - M - colw + lw - nudge, y, colw - lw, bh, IDC_INPUT_LABEL_BASE + id2);
    }
  }

  /* The picture, at its full size, between the two columns: a little in from the left
     one and a little above their middle. The two hotkey buttons sit under where the
     picture's middle would be, shifted a little to the right. */
  picw = cw - 2 * (M + colw + M);
  pich = picw * 206 / 400;
  picx = M + colw + IS(20);
  {
    int mid_y = ym + (5 * pitch + bh - pich) / 2;     /* the same middle as the two columns of buttons */
    picy = mid_y - IS(18);
    SetRect(&inp.pic, picx, picy, picx + picw, picy + pich);
    if (inp.picture) DeleteObject(inp.picture);
    inp.picture = input_make_picture(picw, pich);
  }

  y = ym + 5 * pitch + bh;                       /* below the columns ... */
  y += IS(14);
  input_make(dlg, "STATIC", "Deadzone:", SS_LEFT, M, y + IS(4), IS(64), IS(16), IDC_INPUT_DEADZONE_LABEL);
  input_make(dlg, "msctls_trackbar32", "", TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP,
             M + IS(70), y, IS(140), IS(26), IDC_INPUT_DEADZONE);
  input_make(dlg, "STATIC", "", SS_LEFT, M + IS(218), y + IS(4), IS(44), IS(16), IDC_INPUT_DEADZONE_TEXT);

  y += IS(36);
  input_make(dlg, "STATIC", INPUT_HINT_IDLE, SS_LEFT, M, y, cw - 2 * M, IS(32), IDC_INPUT_HINT);

  y += IS(40);
  input_make(dlg, "BUTTON", "Restore Defaults", WS_TABSTOP | BS_PUSHBUTTON, M, y, IS(130), IS(24), IDC_INPUT_DEFAULTS);
  input_make(dlg, "BUTTON", "Auto Assign...", WS_TABSTOP | BS_PUSHBUTTON, M + IS(138), y, IS(120), IS(24), IDC_INPUT_AUTOASSIGN);
  input_make(dlg, "BUTTON", "Test Keys", WS_TABSTOP | BS_AUTOCHECKBOX, M + IS(436), y + IS(2), IS(100), IS(20), IDC_INPUT_TEST);
  input_make(dlg, "BUTTON", "Gamepad Hotkeys...", WS_TABSTOP | BS_PUSHBUTTON, M + IS(266), y, IS(160), IS(24), IDC_INPUT_PADHK);
  input_make(dlg, "BUTTON", "Save", WS_TABSTOP | BS_DEFPUSHBUTTON, cw - M - IS(168), y, IS(80), IS(24), IDOK);
  input_make(dlg, "BUTTON", "Cancel", WS_TABSTOP | BS_PUSHBUTTON, cw - M - IS(80), y, IS(80), IS(24), IDCANCEL);

  {
    static const char *const how = " Click it, then press the key or gamepad button you want. Right-click to clear it (A B C X Y Z: or to set Turbo).";
    static char map_tip[PAD_KEYS][200];
    static const char *const what[PAD_KEYS] =
    {
      "D-Pad Up.", "D-Pad Down.", "D-Pad Left.", "D-Pad Right.", "Button A.", "Button B.", "Button C.",
      "Button X (6-button pads).", "Button Y (6-button pads).", "Button Z (6-button pads).",
      "The Start button.", "The Mode button (6-button pads)."
    };
    static const TipDef tips[] =
    {
      { IDC_INPUT_TAB,      NULL, "Switch between Player 1 and Player 2." },
      { IDC_INPUT_DEVICE,   "Input Devices:", "Which device controls this player: the keyboard only, a gamepad, or a joystick." },
      { IDC_INPUT_PADTYPE,  "Pad Type:", "Which Genesis pad this player has: match the game, or force a 2, 3 or 6 button pad." },
      { IDC_INPUT_DEADZONE, "Deadzone:", "How far a thumbstick has to move off-center before it registers at all." },
      { IDC_INPUT_AUTOASSIGN, NULL, "Go through the pad buttons one after another, pressing a key or button for each." },
      { IDC_INPUT_TEST, NULL, "Try your keys: while ticked, the keyboard no longer moves around or presses buttons in this window, so you can press Enter, Tab, Esc and the arrows and see the pad spots light." },
      { IDC_INPUT_DEFAULTS, NULL, "Put this player's keys and buttons back to their defaults." },
      { IDC_INPUT_PADHK, NULL, "Choose which gamepad button runs each shortcut: fast forward, rewind, slow motion, save and load state, previous and next slot." },
      { IDOK,               NULL, "Save the controls of both players." },
      { IDCANCEL,           NULL, "Close without saving." },
    };
    TipDef all[10 + PAD_KEYS];
    int k;

    memcpy(all, tips, sizeof(tips));   /* (10 entries) */
    for (k = 0; k < PAD_KEYS; k++)
    {
      wsprintfA(map_tip[k], "%s%s", what[k], how);
      all[10 + k].id = IDC_INPUT_LABEL_BASE + k;
      all[10 + k].label = input_label_text(k);
      all[10 + k].tip = map_tip[k];
    }
    dlg_add_tips(dlg, all, 10 + PAD_KEYS);
  }

  /* Size the window to that client area and center it on its owner. */
  SetRect(&rc, 0, 0, cw, y + IS(24) + M);
  AdjustWindowRectEx(&rc, style, FALSE, ex);
  {
    HWND owner = GetWindow(dlg, GW_OWNER);
    int w = rc.right - rc.left, h = rc.bottom - rc.top, x, yy;
    if (!owner || !IsWindowVisible(owner)) owner = GetDesktopWindow();
    GetWindowRect(owner, &wr);
    x = wr.left + ((wr.right - wr.left) - w) / 2;
    yy = wr.top + ((wr.bottom - wr.top) - h) / 2;
    if (x < 0) x = 0;
    if (yy < 0) yy = 0;
    SetWindowPos(dlg, NULL, x, yy, w, h, SWP_NOZORDER);
  }
}

/* In test mode every key press is taken before the dialog sees it, so Enter does not Save, Esc
   does not Cancel and Tab does not move the focus. The pad spots light from the keys' real state
   (GetAsyncKeyState), which this does not touch. */
static HHOOK input_hook;

static LRESULT CALLBACK input_key_hook(int code, WPARAM wp, LPARAM lp)
{
  if (code >= 0 && inp.test && inp.dlg && GetActiveWindow() == inp.dlg) return 1;
  return CallNextHookEx(input_hook, code, wp, lp);
}

static INT_PTR CALLBACK input_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
  {
    case WM_INITDIALOG:
    {
      inp.dlg = dlg;
      input_hook = SetWindowsHookExA(WH_KEYBOARD, input_key_hook, NULL, GetCurrentThreadId());
      SetWindowTextA(dlg, "Controls");
      input_build(dlg);
      input_load_controls(dlg);
      input_fill_list(dlg);
      input_update_identify_hint(dlg);
      SetTimer(dlg, 2, 30, NULL);   /* keeps the live presses on the picture current while the dialog is open */

      /* Explicit, not left to default tab-order focus: a stray Return still
         on its way from the menu would otherwise press a mapping button and
         start listening. The tab strip takes focus instead. */
      SetFocus(GetDlgItem(dlg, IDC_INPUT_TAB));
      SendMessage(dlg, DM_SETDEFID, (WPARAM)IDOK, 0);

      /* Applied last, after focus is already settled -- SWP_FRAMECHANGED
         inside this can trigger synchronous frame recalculation, which
         risks pumping other pending messages before focus was parked. */
      theme_apply_to_window(dlg);
      return FALSE;
    }

    case WM_PAINT:
      input_paint(dlg);
      return TRUE;

    case WM_DESTROY:
      if (input_hook) { UnhookWindowsHookEx(input_hook); input_hook = NULL; }
      inp.test = 0;
      if (inp.picture) { DeleteObject(inp.picture); inp.picture = NULL; }
      if (inp.font) { DeleteObject(inp.font); inp.font = NULL; }
      return FALSE;

    case WM_TIMER:
      if (wp == 1)      { if (inp.listen != LISTEN_OFF) input_capture_tick(dlg); }
      else if (wp == 2) { input_update_live(dlg); }
      return TRUE;

    case WM_HSCROLL:
    {
      HWND bar = (HWND)lp;

      if (GetDlgCtrlID(bar) == IDC_INPUT_DEADZONE)
      {
        char buf[16];
        inp.deadzone = (int)SendMessage(bar, TBM_GETPOS, 0, 0);
        wsprintfA(buf, "%d%%", inp.deadzone);
        SetDlgItemTextA(dlg, IDC_INPUT_DEADZONE_TEXT, buf);
      }
      return TRUE;
    }

    case WM_LBUTTONDOWN:
    {
      /* A click on a button's spot on the picture starts assigning it. */
      RECT rc = input_pic_rc();
      int mx = (short)LOWORD(lp), my = (short)HIWORD(lp), i, best = -1, bestd = IS(16) * IS(16);
      POINT pt;
      pt.x = mx; pt.y = my;
      if (inp.listen != LISTEN_OFF || !PtInRect(&rc, pt)) return FALSE;
      for (i = 0; i < PAD_KEYS; i++)
      {
        int dx = mx - (rc.left + (rc.right - rc.left) * input_hotspot[i][0] / 1000);
        int dy = my - (rc.top  + (rc.bottom - rc.top) * input_hotspot[i][1] / 1000);
        if (dx * dx + dy * dy < bestd) { bestd = dx * dx + dy * dy; best = i; }
      }
      if (best >= 0)
      {
        input_read_controls(dlg);
        inp.sel = best;
        inp.auto_advance = 0;
        input_set_listening(dlg, 1);
      }
      return TRUE;
    }

    case WM_CONTEXTMENU:
    {
      int id = GetDlgCtrlID((HWND)wp);
      if (id >= IDC_INPUT_LABEL_BASE && id < IDC_INPUT_LABEL_BASE + input_list_count() && inp.listen == LISTEN_OFF)
      {
        int b = id - IDC_INPUT_LABEL_BASE;
        if (b < PAD_KEYS && ((0x3F0u >> b) & 1u))
        {
          HMENU m = CreatePopupMenu();
          POINT pt;
          int cmd;
          GetCursorPos(&pt);
          if (lp != (LPARAM)-1) { pt.x = (short)LOWORD(lp); pt.y = (short)HIWORD(lp); }
          AppendMenuA(m, MF_STRING | (((inp.map.turbo >> b) & 1u) ? MF_CHECKED : 0), 1, "Turbo");
          AppendMenuA(m, MF_STRING, 2, "Clear");
          cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, dlg, NULL);
          DestroyMenu(m);
          if (cmd == 1) { inp.map.turbo ^= (1u << b); input_fill_list(dlg); }
          else if (cmd == 2) input_clear(dlg, b);
        }
        else input_clear(dlg, b);
      }
      return TRUE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
      HBRUSH br = theme_ctlcolor((HDC)wp);
      if (br) return (LRESULT)br;
      break;
    }

    case WM_COMMAND:
    {
      int id = LOWORD(wp);

      if (id >= IDC_INPUT_LABEL_BASE && id < IDC_INPUT_LABEL_BASE + input_list_count())
      {
        if (HIWORD(wp) == BN_CLICKED && inp.listen == LISTEN_OFF)
        {
          input_read_controls(dlg);
          inp.sel = id - IDC_INPUT_LABEL_BASE;
          inp.auto_advance = 0;
          input_set_listening(dlg, 1);
        }
        return TRUE;
      }

      switch (id)
      {
        case IDC_INPUT_TAB:
          if (HIWORD(wp) == GP_TABS_CHANGED)
          {
            int to = (int)GetWindowLongPtr((HWND)lp, GWLP_USERDATA);
            if (to != inp.player && (to == 0 || to == 1))
            {
              if (inp.listen != LISTEN_OFF) input_set_listening(dlg, 0);
              input_read_controls(dlg);
              inp.other[inp.player].map = inp.map;
              inp.other[inp.player].padtype = inp.padtype;
              inp.other[inp.player].deadzone = inp.deadzone;
              inp.player = to;
              inp.map = inp.other[to].map;
              inp.padtype = inp.other[to].padtype;
              inp.deadzone = inp.other[to].deadzone;
              input_load_controls(dlg);
              input_fill_list(dlg);
              input_update_identify_hint(dlg);
            }
          }
          return TRUE;

        case IDC_INPUT_DEVICE:
          /* Otherwise the identify hint only catches up on its next 100ms
             tick -- rare to notice, but free to fix. */
          if (HIWORD(wp) == CBN_SELCHANGE) input_update_identify_hint(dlg);
          return TRUE;

        case IDC_INPUT_TEST:
          if (HIWORD(wp) == BN_CLICKED)
          {
            inp.test = (SendDlgItemMessage(dlg, IDC_INPUT_TEST, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (inp.listen == LISTEN_OFF) SetDlgItemTextA(dlg, IDC_INPUT_HINT, INPUT_HINT_IDLE);
          }
          return TRUE;

        case IDC_INPUT_PADHK:
          if (HIWORD(wp) == BN_CLICKED && inp.listen == LISTEN_OFF) dlg_pad_hotkeys(dlg);
          return TRUE;

        case IDC_INPUT_AUTOASSIGN:
          input_read_controls(dlg);
          inp.sel = 0;
          inp.auto_advance = 1;
          input_set_listening(dlg, 1);
          return TRUE;

        case IDC_INPUT_DEFAULTS:
        {
          /* set_config_defaults() resets everything: the frontend settings, the
             emulation settings (config) and the port devices (input.system).
             Only the controls are wanted here, so put the rest back. */
          t_gui_config saved_gui = gui;
          t_config     saved_cfg = config;
          uint8        saved_sys[2];

          saved_sys[0] = input.system[0];
          saved_sys[1] = input.system[1];

          set_config_defaults();
          inp.map = gui.pad[inp.player];
          inp.padtype = config.input[inp.player].padtype;
          inp.deadzone = gui.deadzone[inp.player];

          gui = saved_gui;
          config = saved_cfg;
          input.system[0] = saved_sys[0];
          input.system[1] = saved_sys[1];
          input_load_controls(dlg);
          input_fill_list(dlg);
          return TRUE;
        }

        case IDOK:
        {
          int p;
          input_read_controls(dlg);
          inp.other[inp.player].map = inp.map;
          inp.other[inp.player].padtype = inp.padtype;
          inp.other[inp.player].deadzone = inp.deadzone;
          for (p = 0; p < 2; p++)
          {
            gui.pad[p] = inp.other[p].map;
            config.input[p].padtype = inp.other[p].padtype;
            gui.deadzone[p] = inp.other[p].deadzone;
          }
          EndDialog(dlg, IDOK);
          return TRUE;
        }

        case IDCANCEL:
          if (inp.listen != LISTEN_OFF) { input_set_listening(dlg, 0); return TRUE; }
          EndDialog(dlg, IDCANCEL);
          return TRUE;
      }
      return FALSE;
    }

    case WM_CLOSE:
      EndDialog(dlg, IDCANCEL);
      return TRUE;
  }

  return FALSE;
}

void dlg_input(HWND parent, int player)
{
  if (player < 0 || player > 1) return;

  /* Picks up any DirectInput joystick plugged in since the app started (or
     since this dialog was last opened) before the device list below is
     built from it -- and, as part of that same refresh, re-resolves both
     players' device mappings by name against the new list (see
     gui_input_refresh_joysticks()), so inp.map below already has the right
     one without this needing to redo that itself. */
  gui_input_refresh_joysticks();

  ZeroMemory(&inp, sizeof(inp));
  inp.player  = player;
  {
    int p;
    for (p = 0; p < 2; p++)
    {
      inp.other[p].map = gui.pad[p];
      inp.other[p].padtype = config.input[p].padtype;
      inp.other[p].deadzone = gui.deadzone[p];
    }
  }
  inp.map     = gui.pad[player];
  inp.padtype = config.input[player].padtype;
  inp.deadzone = gui.deadzone[player];

  if (gui_dialog_box(gui.large_ui ? IDD_INPUT_LARGE : IDD_INPUT, parent, input_proc) == IDOK)
  {
    config_save();
    gui_status("Controls saved");
  }
}

/****************************************************************************
 * Audio levels and latency
 ****************************************************************************/

static struct
{
  int volume;
  int fm;
  int psg;
  int cdda;
  int latency;
} aud;

static void audio_refresh_labels(HWND dlg)
{
  char buf[32];

  wsprintfA(buf, "%d%%", aud.volume);
  SetDlgItemTextA(dlg, IDC_AUDIO_VOLUME_TEXT, buf);
  wsprintfA(buf, "%d%%", aud.fm);
  SetDlgItemTextA(dlg, IDC_AUDIO_FM_TEXT, buf);
  wsprintfA(buf, "%d%%", aud.psg);
  SetDlgItemTextA(dlg, IDC_AUDIO_PSG_TEXT, buf);
  wsprintfA(buf, "%d%%", aud.cdda);
  SetDlgItemTextA(dlg, IDC_AUDIO_CDDA_TEXT, buf);
  wsprintfA(buf, "%d", aud.latency);
  SetDlgItemTextA(dlg, IDC_AUDIO_LATENCY_TEXT, buf);
}

static void audio_load_controls(HWND dlg)
{
  audio_set_slider(dlg, IDC_AUDIO_VOLUME,  IDC_AUDIO_VOLUME_TEXT,  0, 100, aud.volume,  "%");
  audio_set_slider(dlg, IDC_AUDIO_FM,      IDC_AUDIO_FM_TEXT,      0, 200, aud.fm,      "%");
  audio_set_slider(dlg, IDC_AUDIO_PSG,     IDC_AUDIO_PSG_TEXT,     0, 200, aud.psg,     "%");
  audio_set_slider(dlg, IDC_AUDIO_CDDA,    IDC_AUDIO_CDDA_TEXT,    0, 200, aud.cdda,    "%");
  audio_set_slider(dlg, IDC_AUDIO_LATENCY, IDC_AUDIO_LATENCY_TEXT, 2, 8,   aud.latency, "");
}

static INT_PTR CALLBACK audio_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
  {
    case WM_INITDIALOG:
    {
      static const TipDef tips[] =
      {
        { IDC_AUDIO_VOLUME,  "Master Volume",   "Overall loudness of the sound." },
        { IDC_AUDIO_FM,      "FM Preamp",       "Level of the FM sound channels (the Genesis's main music chip)." },
        { IDC_AUDIO_PSG,     "PSG Preamp",      "Level of the PSG sound channels (the square-wave chip)." },
        { IDC_AUDIO_CDDA,    "CD Audio",        "Volume of the Sega CD music tracks." },
        { IDC_AUDIO_LATENCY, "Buffered Frames", "How many frames of sound are buffered. Fewer means less delay, but more chance of crackle on a slow PC." },
        { IDC_AUDIO_DEFAULTS, NULL,             "Put these levels back to their defaults." },
        { IDOK,              NULL,              "Save these settings." },
        { IDCANCEL,          NULL,              "Close without saving." },
      };
      theme_apply_to_window(dlg);
      audio_load_controls(dlg);
      dlg_add_tips(dlg, tips, (int)(sizeof(tips) / sizeof(tips[0])));
      return TRUE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
      HBRUSH br = theme_ctlcolor((HDC)wp);
      if (br) return (LRESULT)br;
      break;
    }

    case WM_HSCROLL:
    {
      HWND bar = (HWND)lp;
      int pos = (int)SendMessage(bar, TBM_GETPOS, 0, 0);
      int id = GetDlgCtrlID(bar);

      switch (id)
      {
        case IDC_AUDIO_VOLUME:  aud.volume  = pos; break;
        case IDC_AUDIO_FM:      aud.fm      = pos; break;
        case IDC_AUDIO_PSG:     aud.psg     = pos; break;
        case IDC_AUDIO_CDDA:    aud.cdda    = pos; break;
        case IDC_AUDIO_LATENCY: aud.latency = pos; break;
        default: break;
      }

      audio_refresh_labels(dlg);

      /* Master volume is audible straight away, the rest applies on Save. */
      if (id == IDC_AUDIO_VOLUME) waveout_set_volume(aud.volume);
      return TRUE;
    }

    case WM_COMMAND:
      switch (LOWORD(wp))
      {
        case IDC_AUDIO_DEFAULTS:
          aud.volume  = 100;
          aud.fm      = 100;
          aud.psg     = 150;
          aud.cdda    = 100;
          aud.latency = 4;
          audio_load_controls(dlg);
          waveout_set_volume(aud.volume);
          return TRUE;

        case IDOK:
          EndDialog(dlg, IDOK);
          return TRUE;

        case IDCANCEL:
          waveout_set_volume(gui.volume);   /* undo the live preview */
          EndDialog(dlg, IDCANCEL);
          return TRUE;
      }
      return FALSE;

    case WM_CLOSE:
      waveout_set_volume(gui.volume);
      EndDialog(dlg, IDCANCEL);
      return TRUE;
  }

  return FALSE;
}

void dlg_audio(HWND parent)
{
  aud.volume  = gui.volume;
  aud.fm      = config.fm_preamp;
  aud.psg     = config.psg_preamp;
  aud.cdda    = config.cdda_volume;
  aud.latency = gui.latency;

  if (gui_dialog_box(gui.large_ui ? IDD_AUDIO_LARGE : IDD_AUDIO, parent, audio_proc) == IDOK)
  {
    gui.volume         = aud.volume;
    config.fm_preamp   = (int16)aud.fm;
    config.psg_preamp  = (int16)aud.psg;
    config.cdda_volume = (int16)aud.cdda;
    gui.latency        = aud.latency;

    waveout_set_volume(gui.volume);
    emu_apply_audio_settings();
    config_save();
    gui_status("Audio settings saved");
  }
}

/****************************************************************************
 * Advanced audio: filter, equalizer, high-quality FM, Master System FM,
 * Sega CD PCM volume
 ****************************************************************************/

static struct
{
  int filter;                 /* 0 off, 1 low-pass, 2 3-band EQ */
  int lp;                     /* low-pass strength, % */
  int lg, mg, hg;             /* EQ gains, % */
  int lowf, highf;            /* EQ band edges, Hz */
  int hqfm;
  int fmunit;                 /* 0 auto, 1 off, 2 on */
  int opll;                   /* 0 MAME, 1 Nuked */
  int pcm;
} adv;

static void adv_enable_by_filter(HWND dlg)
{
  int lp = (adv.filter == 1), eq = (adv.filter == 2);

  EnableWindow(GetDlgItem(dlg, IDC_ADV_LP), lp);
  EnableWindow(GetDlgItem(dlg, IDC_ADV_LP_TEXT), lp);
  EnableWindow(GetDlgItem(dlg, IDC_ADV_LABEL_LP), lp);
  EnableWindow(GetDlgItem(dlg, IDC_ADV_LG), eq);
  EnableWindow(GetDlgItem(dlg, IDC_ADV_MG), eq);
  EnableWindow(GetDlgItem(dlg, IDC_ADV_HG), eq);
  EnableWindow(GetDlgItem(dlg, IDC_ADV_LG_TEXT), eq);
  EnableWindow(GetDlgItem(dlg, IDC_ADV_MG_TEXT), eq);
  EnableWindow(GetDlgItem(dlg, IDC_ADV_HG_TEXT), eq);
  EnableWindow(GetDlgItem(dlg, IDC_ADV_LOWF), eq);
  EnableWindow(GetDlgItem(dlg, IDC_ADV_HIGHF), eq);
  EnableWindow(GetDlgItem(dlg, IDC_ADV_LABEL_LG), eq);
  EnableWindow(GetDlgItem(dlg, IDC_ADV_LABEL_MG), eq);
  EnableWindow(GetDlgItem(dlg, IDC_ADV_LABEL_HG), eq);
  EnableWindow(GetDlgItem(dlg, IDC_ADV_LABEL_LOWF), eq);
  EnableWindow(GetDlgItem(dlg, IDC_ADV_LABEL_HIGHF), eq);
}

static void adv_load_controls(HWND dlg)
{
  HWND c;

  c = GetDlgItem(dlg, IDC_ADV_FILTER);
  SendMessage(c, CB_RESETCONTENT, 0, 0);
  SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)"Off");
  SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)"Low-Pass");
  SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)"3-Band EQ");
  SendMessage(c, CB_SETCURSEL, (WPARAM)adv.filter, 0);

  c = GetDlgItem(dlg, IDC_ADV_FMUNIT);
  SendMessage(c, CB_RESETCONTENT, 0, 0);
  SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)"Auto (When the Game Uses It)");
  SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)"Off");
  SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)"On");
  SendMessage(c, CB_SETCURSEL, (WPARAM)adv.fmunit, 0);

  c = GetDlgItem(dlg, IDC_ADV_OPLL);
  SendMessage(c, CB_RESETCONTENT, 0, 0);
  SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)"MAME (YM2413)");
  SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)"Nuked (YM2413)");
  SendMessage(c, CB_SETCURSEL, (WPARAM)adv.opll, 0);

  audio_set_slider(dlg, IDC_ADV_LP,  IDC_ADV_LP_TEXT,  0, 99,  adv.lp, "%");
  audio_set_slider(dlg, IDC_ADV_LG,  IDC_ADV_LG_TEXT,  0, 200, adv.lg, "%");
  audio_set_slider(dlg, IDC_ADV_MG,  IDC_ADV_MG_TEXT,  0, 200, adv.mg, "%");
  audio_set_slider(dlg, IDC_ADV_HG,  IDC_ADV_HG_TEXT,  0, 200, adv.hg, "%");
  audio_set_slider(dlg, IDC_ADV_PCM, IDC_ADV_PCM_TEXT, 0, 200, adv.pcm, "%");

  SetDlgItemInt(dlg, IDC_ADV_LOWF, (UINT)adv.lowf, FALSE);
  SetDlgItemInt(dlg, IDC_ADV_HIGHF, (UINT)adv.highf, FALSE);
  CheckDlgButton(dlg, IDC_ADV_HQFM, adv.hqfm ? BST_CHECKED : BST_UNCHECKED);

  adv_enable_by_filter(dlg);
}

static INT_PTR CALLBACK audio_adv_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
  {
    case WM_INITDIALOG:
    {
      static const TipDef tips[] =
      {
        { IDC_ADV_FILTER, "Audio Filter:",       "Off, a Low-Pass filter, or a 3-Band equalizer." },
        { IDC_ADV_LP,     "Low-Pass Strength",   "How strongly the high frequencies are rolled off (Low-Pass filter)." },
        { IDC_ADV_LG,     "EQ Low Gain",         "Level of the low band (3-Band equalizer)." },
        { IDC_ADV_MG,     "EQ Mid Gain",         "Level of the middle band (3-Band equalizer)." },
        { IDC_ADV_HG,     "EQ High Gain",        "Level of the high band (3-Band equalizer)." },
        { IDC_ADV_LOWF,   "EQ Low Frequency (Hz):",  "Where the low band ends and the middle band begins, in Hz." },
        { IDC_ADV_HIGHF,  "EQ High Frequency (Hz):", "Where the middle band ends and the high band begins, in Hz." },
        { IDC_ADV_HQFM,   NULL,                  "Band-limited FM synthesis: cleaner high notes, for a little more CPU." },
        { IDC_ADV_FMUNIT, "Master System FM Unit:", "Whether the Master System's FM sound unit is emulated, for games that use it." },
        { IDC_ADV_OPLL,   "Master System FM Chip:", "Which chip emulates the Master System's FM sound unit." },
        { IDC_ADV_PCM,    "Sega CD PCM Volume",  "Volume of the Sega CD's PCM sound chip." },
        { IDC_ADV_DEFAULTS, NULL,                "Put these settings back to their defaults." },
        { IDOK,           NULL,                  "Save these settings." },
        { IDCANCEL,       NULL,                  "Close without saving." },
      };
      theme_apply_to_window(dlg);
      adv_load_controls(dlg);
      dlg_add_tips(dlg, tips, (int)(sizeof(tips) / sizeof(tips[0])));
      return TRUE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
      HBRUSH br = theme_ctlcolor((HDC)wp);
      if (br) return (LRESULT)br;
      break;
    }

    case WM_HSCROLL:
    {
      HWND bar = (HWND)lp;
      int pos = (int)SendMessage(bar, TBM_GETPOS, 0, 0);
      int id = GetDlgCtrlID(bar), text = 0;
      char buf[16];

      switch (id)
      {
        case IDC_ADV_LP:  adv.lp  = pos; text = IDC_ADV_LP_TEXT;  break;
        case IDC_ADV_LG:  adv.lg  = pos; text = IDC_ADV_LG_TEXT;  break;
        case IDC_ADV_MG:  adv.mg  = pos; text = IDC_ADV_MG_TEXT;  break;
        case IDC_ADV_HG:  adv.hg  = pos; text = IDC_ADV_HG_TEXT;  break;
        case IDC_ADV_PCM: adv.pcm = pos; text = IDC_ADV_PCM_TEXT; break;
        default: break;
      }
      if (text)
      {
        wsprintfA(buf, "%d%%", pos);
        SetDlgItemTextA(dlg, text, buf);
      }
      return TRUE;
    }

    case WM_COMMAND:
      switch (LOWORD(wp))
      {
        case IDC_ADV_FILTER:
          if (HIWORD(wp) == CBN_SELCHANGE)
          {
            adv.filter = (int)SendDlgItemMessage(dlg, IDC_ADV_FILTER, CB_GETCURSEL, 0, 0);
            if (adv.filter < 0) adv.filter = 0;
            adv_enable_by_filter(dlg);
          }
          return TRUE;

        case IDC_ADV_DEFAULTS:
          adv.filter = 1; adv.lp = 60;
          adv.lg = adv.mg = adv.hg = 100;
          adv.lowf = 200; adv.highf = 8000;
          adv.hqfm = 1; adv.fmunit = 0; adv.opll = 0; adv.pcm = 100;
          adv_load_controls(dlg);
          return TRUE;

        case IDOK:
        {
          int lowf = (int)GetDlgItemInt(dlg, IDC_ADV_LOWF, NULL, FALSE);
          int highf = (int)GetDlgItemInt(dlg, IDC_ADV_HIGHF, NULL, FALSE);

          if (lowf < 20 || lowf > 2000 || highf < 1000 || highf > 20000 || lowf >= highf)
          {
            MessageBoxA(dlg, "The EQ bands need a low frequency of 20-2000 Hz, a high frequency of\n"
                             "1000-20000 Hz, and the low one below the high one.",
                        APP_NAME, MB_OK | MB_ICONINFORMATION);
            return TRUE;
          }
          adv.lowf = lowf;
          adv.highf = highf;
          adv.hqfm = (IsDlgButtonChecked(dlg, IDC_ADV_HQFM) == BST_CHECKED);
          adv.fmunit = (int)SendDlgItemMessage(dlg, IDC_ADV_FMUNIT, CB_GETCURSEL, 0, 0);
          adv.opll = (int)SendDlgItemMessage(dlg, IDC_ADV_OPLL, CB_GETCURSEL, 0, 0);
          if (adv.fmunit < 0) adv.fmunit = 0;
          if (adv.opll < 0) adv.opll = 0;
          EndDialog(dlg, IDOK);
          return TRUE;
        }

        case IDCANCEL:
          EndDialog(dlg, IDCANCEL);
          return TRUE;
      }
      return FALSE;

    case WM_CLOSE:
      EndDialog(dlg, IDCANCEL);
      return TRUE;
  }

  return FALSE;
}

void dlg_audio_advanced(HWND parent)
{
  int fm_mode_now = (config.ym2413 & 2) ? 0 : ((config.ym2413 & 1) ? 2 : 1);

  adv.filter = config.filter > 2 ? 0 : config.filter;
  adv.lp     = (int)((config.lp_range * 100u + 32768u) / 65536u);
  adv.lg     = config.lg;
  adv.mg     = config.mg;
  adv.hg     = config.hg;
  adv.lowf   = config.low_freq;
  adv.highf  = config.high_freq;
  adv.hqfm   = config.hq_fm ? 1 : 0;
  adv.fmunit = fm_mode_now;
  adv.opll   = config.opll ? 1 : 0;
  adv.pcm    = config.pcm_volume;

  if (gui_dialog_box(gui.large_ui ? IDD_AUDIO_ADV_LARGE : IDD_AUDIO_ADV, parent, audio_adv_proc) == IDOK)
  {
    int old_unit = fm_mode_now, old_opll = config.opll ? 1 : 0;

    config.filter    = (uint8)adv.filter;
    config.lp_range  = (uint32)((adv.lp * 65536) / 100);
    config.lg        = (int16)adv.lg;
    config.mg        = (int16)adv.mg;
    config.hg        = (int16)adv.hg;
    config.low_freq  = (int16)adv.lowf;
    config.high_freq = (int16)adv.highf;
    config.hq_fm     = (uint8)adv.hqfm;
    config.pcm_volume = (int16)adv.pcm;
    config.ym2413    = (uint8)(adv.fmunit == 0 ? 2 : (adv.fmunit == 2 ? 1 : 0));
    config.opll      = (uint8)adv.opll;

    /* Filter, strength and EQ take effect straight away. */
    if (emu_running) audio_set_equalizer();

    /* A different Master System FM unit or chip means creating the chip anew. */
    if (adv.fmunit != old_unit || adv.opll != old_opll) emu_apply_fm_settings();

    gui_update_menu();
    config_save();
    gui_status("Advanced audio settings saved");
  }
}

/****************************************************************************
 * Keyboard shortcuts
 ****************************************************************************/

static INT_PTR CALLBACK shortcuts_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
  (void)lp;

  switch (msg)
  {
    case WM_INITDIALOG:
    {
      HWND list = GetDlgItem(dlg, IDC_SHORTCUTS_TEXT);
      LVCOLUMNA col;
      LVITEMA it;
      RECT rc;
      char keys[4096], *line;
      int row = 0;

      SendMessage(list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
      GetClientRect(list, &rc);
      memset(&col, 0, sizeof(col));
      col.mask = LVCF_TEXT | LVCF_WIDTH;
      col.pszText = "Action";
      col.cx = (rc.right - rc.left) * 42 / 100;
      SendMessageA(list, LVM_INSERTCOLUMNA, 0, (LPARAM)&col);
      col.pszText = "Shortcut";
      col.cx = (rc.right - rc.left) * 38 / 100;
      SendMessageA(list, LVM_INSERTCOLUMNA, 1, (LPARAM)&col);

      /* hotkeys_describe gives "Action<TAB>Shortcut" lines. */
      hotkeys_describe(keys, sizeof(keys));
      for (line = strtok(keys, "\r\n"); line; line = strtok(NULL, "\r\n"))
      {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT;
        it.iItem = row;
        it.pszText = line;
        SendMessageA(list, LVM_INSERTITEMA, 0, (LPARAM)&it);
        it.iSubItem = 1;
        it.pszText = tab + 1;
        SendMessageA(list, LVM_SETITEMTEXTA, (WPARAM)row, (LPARAM)&it);
        row++;
      }

      theme_apply_to_window(dlg);
      {
        HWND header = ListView_GetHeader(list);
        if (header && theme_is_dark()) { SetWindowTheme(header, L"ItemsView", NULL); InvalidateRect(header, NULL, TRUE); }
      }
      SetDlgItemTextA(dlg, IDC_SHORTCUTS_HINT,
        "Change any of these under Input > Hotkeys. Player 1 keyboard defaults: arrows, Z X C = A B C, A S D = X Y Z, Enter = Start, Right Shift = Mode (Input > Controls). A connected gamepad works without setup.");
      return TRUE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
      HBRUSH br = theme_ctlcolor((HDC)wp);
      if (br) return (LRESULT)br;
      break;
    }

    case WM_COMMAND:
      if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)
      {
        EndDialog(dlg, LOWORD(wp));
        return TRUE;
      }
      return FALSE;

    case WM_CLOSE:
      EndDialog(dlg, IDCANCEL);
      return TRUE;
  }

  return FALSE;
}

void dlg_shortcuts(HWND parent)
{
  gui_dialog_box(gui.large_ui ? IDD_SHORTCUTS_LARGE : IDD_SHORTCUTS, parent, shortcuts_proc);
}

static const char menu_guide_text[] =
  "Every menu in one page. Keyboard shortcuts are in brackets (the original keys; Input > Hotkeys changes them).\r\n"
  "\r\n"
  "FILE\r\n"
  "  Open ROM... (Ctrl+O):\r\n"
  "   Pick a game file to play: Mega Drive / Genesis, Master System, Game Gear, SG-1000, Mega CD, or a .zip / .7z / .gz that holds one.\r\n"
  "\r\n"
  "  ROMs Directory... (Ctrl+R):\r\n"
  "   Shows the game browser for a folder of games. Double-click a game to play it, right-click for more choices (play with a saved state, cheats, cover image, clear the recently played list: it is greyed when there is nothing to clear). Folders inside it are searched too (Settings > General > Scan ROMs Subfolders, on by default).\r\n"
  "\r\n"
  "  Recent Files:\r\n"
  "   Your last games, each shown with its console.\r\n"
  "\r\n"
  "  Close ROM (Ctrl+W):\r\n"
  "   Stops the game.\r\n"
  "\r\n"
  "  ROM Information...:\r\n"
  "   The game's name, console, genre, favorite state, file size and location, the CRC32, the cartridge header (console name, type, titles, product code, region, copyright, supported controllers, save RAM), and the ROM size and checksum.\r\n"
  "\r\n"
  "  Open Genesis Plus Folder:\r\n"
  "   Opens the program folder: settings, saves, states, screenshots, recordings, cheats, covers and BIOS files.\r\n"
  "\r\n"
  "  Exit:\r\n"
  "   Quits.\r\n"
  "\r\n"
  "EMULATION\r\n"
  "  Pause (F2 / Pause), Stop (F3):\r\n"
  "   Freeze the game, or end it.\r\n"
  "\r\n"
  "  Reset (F4), Hard Reset (Ctrl+F4):\r\n"
  "   Reset is the console's reset button. Hard Reset power-cycles the console (saved game data is kept).\r\n"
  "\r\n"
  "  Load State:\r\n"
  "   Load State from Selected Slot (F8) restores the exact moment saved in the selected slot. Load State from Slot (keys 0-9) loads any of the slots 0-9 directly, whichever is selected (empty slots are greyed). Undo Load State (Ctrl+F8) puts the game back as it was just before the last load; one level, cleared when the game is closed.\r\n"
  "\r\n"
  "  Save State:\r\n"
  "   Save State to Selected Slot (F5) saves the exact moment into the selected slot. Save State to Slot (Shift+0 - Shift+9) saves into any of the slots 0-9 directly. Undo Save State puts the slot back as it was before the last save (or empties it if it was empty); one level, cleared when the game is closed.\r\n"
  "\r\n"
  "  Select State Slot:\r\n"
  "   Choose slot 0-9 for F5 / F8 (F6 / F7 = previous / next). Each entry shows when the slot was last saved.\r\n"
  "\r\n"
  "  Manage States...:\r\n"
  "   Thumbnails of all ten slots: left-click loads one, right-click saves or deletes.\r\n"
  "\r\n"
  "  Enable Rewind (Backspace, Hold):\r\n"
  "   Keeps a history of recent play so holding Backspace runs it backwards. Uses about 200 MB and some CPU while on; switch it off if you never rewind. Not available during netplay.\r\n"
  "\r\n"
  "  Pause When Inactive:\r\n"
  "   Pauses the game while another window has focus.\r\n"
  "\r\n"
  "  Auto Resume:\r\n"
  "   Saves the game when you close it (or quit) and, the next time you open that game, asks whether to carry on from there. The question has a \"Do not ask again\" box that remembers your answer; the Restore Dialog button next to Auto Resume in Settings brings it back. It uses a state of its own, so your ten save slots are untouched.\r\n"
  "\r\n"
  "  Slow Motion (Ctrl+Tab):\r\n"
  "   Runs the game at half speed (the sound slows down with it). To step one frame at a time, pause and press the Advance One Frame key, \\ by default.\r\n"
  "\r\n"
  "  Fast Forward (Tab), Rewind (Backspace):\r\n"
  "   Ticked items that keep the game running fast, or backwards, until you untick them (Rewind needs Enable Rewind). The keys themselves work while held; they can be changed under Input > Hotkeys. Only one of Fast Forward, Slow Motion and Rewind is on at a time; Slow Motion is greyed out when no game is running.\r\n"
  "\r\n"
  "VIEW\r\n"
  "  View Mode:\r\n"
  "   List View or Grid View: how the game browser shows your games.\r\n"
  "\r\n"
  "  List Columns:\r\n"
  "   Choose which columns the list view shows: Favorite (a star: click it to mark a game as a favorite), and Console (Name is always shown), and Genre (shown after Console, hidden at first). Click a column's title to sort by it (View > Show Recently Played shows what you played last). Right-clicking the column header does the same. The choices are greyed out while the grid view is shown.\r\n"
  "\r\n"
  "  Theme:\r\n"
  "   System (follow Windows), Light or Dark.\r\n"
  "\r\n"
  "  Larger UI:\r\n"
  "   Bigger menus and dialogs.\r\n"
  "\r\n"
  "  Show Toolbar, Show Search Bar (Ctrl+F), Show Filter Bar:\r\n"
  "   Switch the rows above the game list on or off. The toolbar has buttons for open ROM, refresh, favorites only, play, pause, stop, reset, switch fullscreen, screenshot, settings, the Video and Audio settings tabs and the player controls; a star button next to Refresh shows only your favorites. The toolbar stays above the game while it runs in a window; in fullscreen it slides in at the top edge of the screen together with the menu bar (the picture makes room for it) (View > Toolbar in Fullscreen switches that on; it is off to start with). The search bar narrows the list as you type (Ctrl+F jumps to it; Close or Show Search Bar hides it, and Show Search Bar brings it back). The filter bar narrows it by console, genre (looked up from the game's title in a table built into the program; a game that is not in it is listed as Unknown) and region (read from tags such as (USA) in the file name).\r\n"
  "\r\n"
  "  Toolbar in Fullscreen:\r\n"
  "   In fullscreen, shows the toolbar together with the menu bar when the mouse touches the top edge of the screen.\r\n"
  "\r\n"
  "  Show Favorites Only:\r\n"
  "   Mark a game as a favorite by clicking its star in the Favorite column (the right-click menu has Add To Favorites too). Show Favorites Only, or the star on the toolbar, hides every other game. Sorting by the Favorite column puts your favorites first.\r\n"
  "\r\n"
  "  Show Recently Played:\r\n"
  "   Shows the last few games you opened as a group at the top of the list, above all the others. Choose it again to switch it off.\r\n"
  "\r\n"
  "  Clean Game Names:\r\n"
  "   The game list shows \"Sonic The Hedgehog\" instead of \"Sonic The Hedgehog (USA, Europe) [!].md\". Only the list changes, not the files.\r\n"
  "\r\n"
  "  Show Grid Lines:\r\n"
  "   Thin horizontal lines between the rows of the lists; the lines between columns are always shown.\r\n"
  "\r\n"
  "  Reset Window Size:\r\n"
  "   Puts the window back at the size chosen under Video > Window Size.\r\n"
  "\r\n"
  "VIDEO\r\n"
  "  Window Size:\r\n"
  "   1x to 6x (3x to start with).\r\n"
  "\r\n"
  "  Fullscreen (Alt+Enter), Start ROM in Fullscreen:\r\n"
  "   Esc also toggles fullscreen. In fullscreen, move the mouse to the top edge of the screen to show the menu bar; it hides again when the mouse moves away. The mouse cursor stays visible in fullscreen and hides itself after a few idle seconds. Closing the ROM leaves fullscreen and returns to the selected window size.\r\n"
  "\r\n"
  "  Always on Top:\r\n"
  "   Keeps the window above every other window.\r\n"
  "\r\n"
  "  Render Filter:\r\n"
  "   Pixel-art scalers and CRT looks (Scale2x, Smooth, xBRZ, Scanlines, CRT...) applied before the picture is shown.\r\n"
  "\r\n"
  "  Shaders:\r\n"
  "   Runs a librashader (.slangp) GPU shader chain on top of the picture, Direct3D 11 only, and 64-bit exe only (the 32-bit exe has no Shaders menu); librashader.dll ships with the exe (see the README).\r\n"
  "\r\n"
  "  Visual Effects:\r\n"
  "   Smooth Scaling (soft edges when enlarged), Integer Scaling (enlarges the picture by whole multiples only, 1x, 2x, 3x ..., so every pixel is the same size; it is centered, with borders, when the window is between two sizes, and follows the Aspect Ratio setting for the shape of the pixels), Brighten (a gentle brightness lift), LCD Ghosting (bright pixels fade slowly, like a handheld screen).\r\n"
  "\r\n"
  "  Scanlines:\r\n"
  "   Dark lines between rows: Off, 25, 50, 75 or 100 %.\r\n"
  "\r\n"
  "  NTSC Filter:\r\n"
  "   Composite, S-Video or RGB television look.\r\n"
  "\r\n"
  "  Aspect Ratio:\r\n"
  "   Square Pixels, 4:3 (as on a CRT) or Fill the Window.\r\n"
  "\r\n"
  "  Borders:\r\n"
  "   Hide or show the overscan borders (top/bottom, left/right).\r\n"
  "\r\n"
  "  Interlaced Mode:\r\n"
  "   Single Field (the default) or Double Field, for games that use the 448-line interlaced mode.\r\n"
  "\r\n"
  "  Frameskip:\r\n"
  "   Skips drawing some frames on a slow PC (Off, Auto, or Manual by audio-buffer level). Not used during netplay or recording.\r\n"
  "\r\n"
  "  VSync:\r\n"
  "   Wait for the screen's refresh after each frame so motion is even. Works with any renderer; it is skipped for a frame whenever the sound buffer runs low, so it never causes crackle. Off by default.\r\n"
  "\r\n"
  "  Renderer:\r\n"
  "   GDI draws with the CPU; Direct3D 9 and Direct3D 11 use the graphics card instead (each falls back to GDI automatically if it isn't available). Direct3D 11 is what a GPU shader tool such as librashader needs.\r\n"
  "\r\n"
  "AUDIO\r\n"
  "  Mute (Ctrl+M):\r\n"
  "   Silences the sound; untick to hear it again.\r\n"
  "\r\n"
  "  Mute FM Sound (Ctrl+N), Mute PSG Sound (Ctrl+B):\r\n"
  "   Silence the FM chip (the main music) or the PSG chip (the square-wave sound) on its own, without touching the saved levels.\r\n"
  "\r\n"
  "  Levels and Latency...:\r\n"
  "   Master volume, FM and PSG levels, CD audio, and how many frames of sound are buffered.\r\n"
  "\r\n"
  "  Advanced Audio...:\r\n"
  "   Audio filter (Off, Low-Pass or a 3-Band EQ) and its settings, High-Quality FM, Master System FM unit and chip, and Sega CD PCM volume.\r\n"
  "\r\n"
  "  FM Chip:\r\n"
  "   Which Genesis FM sound chip is emulated: YM2612 or YM3438, the exact Nuked versions (default is YM2612 Nuked) or MAME.\r\n"
  "\r\n"
  "  Sample Rate:\r\n"
  "   44100 or 48000 Hz.\r\n"
  "\r\n"
  "  High-Quality PSG Resampling, Low-Pass Filter, Mono Output:\r\n"
  "   Cleaner PSG sound, a soft high-frequency roll-off, and mono instead of stereo.\r\n"
  "\r\n"
  "INPUT\r\n"
  "  Controls...:\r\n"
  "   One window with a tab for Player 1 and one for Player 2, laid out around a picture of the pad. Click a button, then press the key or gamepad button you want (right-click a button to clear it; clicking its spot on the picture works too). Right-click A, B, C, X, Y or Z to turn Turbo on for it: held down, it then fires repeatedly. A key or button can only be on one pad button: assigning it takes it off the one that had it (also on the other player, for keys), and a line under the pad says what was removed, and when the key is also a shortcut or the pad button also a gamepad hotkey. Auto Assign goes through them all in turn. Pressing a mapped key or gamepad button lights its spot on the picture, so you can check a pad; tick Test Keys to try keys such as Enter, Tab and Esc without them operating the window. Set keyboard keys and gamepad buttons, and set the gamepad thumbstick's deadzone (how far it has to move off-center before it registers at all). The right stick counts as four more buttons (Right Stick Up, Down, Left, Right) that can be assigned like any other; this works for XInput pads and, where the pad reports the stick on its Rx / Ry or Z / Rz axes, for DirectInput ones. Gamepad Hotkeys... opens a window to choose which pad button runs each shortcut, pressed on its own (no Back needed): Fast Forward and Rewind (held), Slow Motion, Save State and Load State (current slot), and Previous and Next Slot. By default the right stick does the last four (up = save, down = load, left = previous slot, right = next slot) and LB (Left Bumper) / LT (Left Trigger) are Fast Forward / Rewind. A button used there also reaches the game, so the right stick is a good choice. Xbox-style (XInput) pad of Player 1 only. Player 1 defaults: arrows, Z X C = A B C, A S D = X Y Z, Enter = Start, Right Shift = Mode.\r\n"
  "\r\n"
  "  Hotkeys...:\r\n"
  "   Every keyboard shortcut in one list: select an action, click Assign (or double-click it) and press the key you want, alone or with Ctrl, Shift or Alt. A key can only be on one action: the old one loses it. Clear removes a shortcut and Restore Defaults brings the original keys back. The menu labels and the Keyboard Shortcuts window follow your choices. Fast Forward and Rewind (the keys you hold, Tab and Backspace by default) are in the list too; their gamepad buttons are under Controls > Gamepad Hotkeys.\r\n"
  "\r\n"
  "  Port A / Port B Device:\r\n"
  "   What is plugged in: Control Pad, Mouse, a light gun (Light Phaser, Menacer or Justifier, depending on the port), XE-1AP analog joystick, Activator, Paddle Control, Sports Pad, Graphic Board (Pico's drawing tablet), or a 4-player Team Player adapter -- not every device is on both ports.\r\n"
  "\r\n"
  "  Enable Background Input:\r\n"
  "   Keep reading the controller while another window has focus.\r\n"
  "\r\n"
  "TOOLS\r\n"
  "  Cheats... (Ctrl+C):\r\n"
  "   Add Game Genie, Action Replay or raw codes, or load a .cht file.\r\n"
  "\r\n"
  "  Netplay... / Stop Netplay:\r\n"
  "   Two-player game over a local network: one PC hosts, the other joins. Both need the same ROM, the same program file and the same settings.\r\n"
  "\r\n"
  "  Record:\r\n"
  "   Video + Audio (Shift+F11) makes an .mp4, Audio Only (Ctrl+F11) a .wav. Files go in the recordings folder; Stop Recording (Alt+F11) ends it.\r\n"
  "\r\n"
  "  Save Screenshot (F12):\r\n"
  "   Saves a PNG named after the game and the time.\r\n"
  "\r\n"
  "  Screenshot Output:\r\n"
  "   Final: the render filter is applied, then the picture is stretched to 4:3. Corrected: the plain picture stretched to 4:3 at 640x480. Raw: exactly what the emulator drew, at its own size.\r\n"
  "\r\n"
  "OPTIONS\r\n"
  "  Settings...:\r\n"
  "   Every option of the menus on tabs (General, Video, Audio, Input and Options). Hover over an option for a short description (it hides after 30 seconds). Restore Defaults (bottom left) puts every setting on all tabs back to its default, keeping your controls, ROM folder and cheats. A change takes effect at once, exactly as if you had used the menu; the windows that have their own settings open from a button.\r\n"
  "\r\n"
  "  Region:\r\n"
  "   Which region the console reports to the game: detect from the ROM, USA, Europe or Japan.\r\n"
  "\r\n"
  "  Force VDP Mode:\r\n"
  "   Run the video timing at 60 Hz (NTSC) or 50 Hz (PAL) whatever the region says.\r\n"
  "\r\n"
  "  Console:\r\n"
  "   Detect the console from the game, or force a specific model.\r\n"
  "\r\n"
  "  Lock-On Cartridge:\r\n"
  "   Attach a Game Genie, Action Replay or Sonic & Knuckles to the game.\r\n"
  "\r\n"
  "  Boot from BIOS When Available:\r\n"
  "   Run the console's start-up ROM when a BIOS file is present (see below).\r\n"
  "\r\n"
  "  Emulate Address Error Exceptions:\r\n"
  "   Hardware-accurate crashes on bad memory access; turn off only for a few hacked games.\r\n"
  "\r\n"
  "  Show Extended Game Gear Screen:\r\n"
  "   Show the whole Game Gear picture instead of only the part its screen displayed.\r\n"
  "\r\n"
  "  Show Master System Side Borders:\r\n"
  "   Off by default, which crops the 8-pixel columns at each side; tick to show them.\r\n"
  "\r\n"
  "  Show Frame Rate:\r\n"
  "   Frames per second on screen.\r\n"
  "\r\n"
  "  Show On-Screen Display:\r\n"
  "   Short messages over the picture, such as \"Saved to slot 3\" or the name of a screenshot. Untick to hide them (the status bar still shows them).\r\n"
  "\r\n"
  "HELP\r\n"
  "  Keyboard Shortcuts (F1):\r\n"
  "   The list of hotkeys.\r\n"
  "\r\n"
  "  Menu Guide:\r\n"
  "   This page. Pick a section on the left to show just that part.\r\n"
  "\r\n"
  "  About Genesis Plus:\r\n"
  "   Version and credits.\r\n"
  "\r\n"
  "BIOS AND FIRMWARE FILES\r\n"
  "Required or optional firmware files go in the program's \"bios\" folder (Open Genesis Plus Folder, then bios).\r\n"
  "\r\n"
  "Filename:\tDescription:\r\n"
  "\r\n"
  "- bios_MD.bin\tMega Drive TMSS start-up ROM - optional\r\n"
  "- bios_CD_E.bin\tMega CD Europe BIOS - required for Mega CD Europe games\r\n"
  "- bios_CD_U.bin\tSega CD USA BIOS - required for Sega CD USA games\r\n"
  "- bios_CD_J.bin\tMega CD Japan BIOS - required for Mega CD Japan games\r\n"
  "- bios_E.sms\tMaster System Europe BIOS - optional\r\n"
  "- bios_U.sms\tMaster System USA BIOS - optional\r\n"
  "- bios_J.sms\tMaster System Japan BIOS - optional\r\n"
  "- bios.gg\tGame Gear BIOS - optional\r\n"
  "- sk.bin\tSonic & Knuckles ROM (lock-on) - optional\r\n"
  "- sk2chip.bin\tSonic & Knuckles UPMEM ROM (lock-on) - optional\r\n"
  "- areplay.bin\tAction Replay ROM (lock-on) - optional\r\n"
  "- ggenie.bin\tGame Genie ROM (lock-on) - optional";

/* One page of text, split at run time into its sections (a header is a line
   with no indent, in capitals; entries are the indented lines under it) so the
   guide can be browsed by section and filtered by a search word. */
#define GUIDE_MAX_SECTIONS 16

static struct
{
  char *text;                          /* working copy, lines end in \n */
  int   count;                         /* sections, [0] = introduction */
  const char *name[GUIDE_MAX_SECTIONS];
  char *first[GUIDE_MAX_SECTIONS];     /* first line (the header) of each */
  char *end[GUIDE_MAX_SECTIONS];
} guide;

static int guide_is_header(const char *line)
{
  const char *p = line;
  int letters = 0;

  if (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\0') return 0;
  for (; *p && *p != '\r' && *p != '\n'; p++)
  {
    if (*p >= 'a' && *p <= 'z') return 0;
    if (*p >= 'A' && *p <= 'Z') letters++;
  }
  return letters >= 3;
}

static void guide_parse(void)
{
  char *p;
  int s = 0;

  guide.text = (char *)malloc(sizeof(menu_guide_text) + 1);
  if (!guide.text) return;
  lstrcpyA(guide.text, menu_guide_text);
  lstrcatA(guide.text, "\r\n");

  guide.name[0] = "Introduction";
  guide.first[0] = guide.text;

  for (p = guide.text; *p; )
  {
    char *eol = strchr(p, '\n');
    if (!eol) eol = p + lstrlenA(p) - 1;

    if (guide_is_header(p) && s + 1 < GUIDE_MAX_SECTIONS)
    {
      guide.end[s] = p;
      s++;
      guide.first[s] = p;
      guide.name[s] = p;               /* trimmed to the line when listed */
    }
    p = eol + 1;
  }
  guide.end[s] = p;
  guide.count = s + 1;
}

/* Shows the chosen section (or all of them) in the text pane. */
static void guide_show(HWND dlg)
{
  int sel = (int)SendDlgItemMessage(dlg, IDC_MENUGUIDE_LIST, LB_GETCURSEL, 0, 0);
  int s;
  size_t cap = sizeof(menu_guide_text) + 16;
  char *out = (char *)malloc(cap);
  char *o = out;

  if (!out || !guide.text) { free(out); return; }

  for (s = 0; s < guide.count; s++)
  {
    size_t n;

    if (sel > 0 && sel != s) continue;   /* row 0 is "All", row n is section n */
    n = (size_t)(guide.end[s] - guide.first[s]);
    memcpy(o, guide.first[s], n);
    o += n;
  }
  *o = '\0';

  SetDlgItemTextA(dlg, IDC_MENUGUIDE_TEXT, out);
  free(out);
}

static INT_PTR CALLBACK menu_guide_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
  {
    case WM_INITDIALOG:
      theme_apply_to_window(dlg);
      {
        int tabs[1] = { 130 };
        int s;
        HWND list = GetDlgItem(dlg, IDC_MENUGUIDE_LIST);

        SendDlgItemMessage(dlg, IDC_MENUGUIDE_TEXT, EM_SETTABSTOPS, 1, (LPARAM)tabs);

        guide_parse();
        SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)"All");
        for (s = 0; s < guide.count; s++)
        {
          char title[64];
          int n = 0;
          const char *src = guide.name[s];

          for (; src[n] && src[n] != '\r' && src[n] != '\n' && n < 60; n++) title[n] = src[n];
          title[n] = '\0';
          if (s == 0) continue;          /* the introduction is only in "All" */
          /* "BIOS AND FIRMWARE FILES" -> "Bios And Firmware Files" */
          {
            int k, up = 1;
            for (k = 0; title[k]; k++)
            {
              if (title[k] == ' ') up = 1;
              else if (up) up = 0;
              else if (title[k] >= 'A' && title[k] <= 'Z') title[k] = (char)(title[k] + 32);
            }
          }
          SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)title);
        }
        SendMessage(list, LB_SETCURSEL, 0, 0);
        guide_show(dlg);
      }
      return TRUE;

    case WM_DESTROY:
      free(guide.text);
      guide.text = NULL;
      guide.count = 0;
      return FALSE;

    case WM_CTLCOLORDLG:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
    {
      /* The clickable list of sections is #2D2D2D in dark mode. */
      HBRUSH br = (GetDlgCtrlID((HWND)lp) == IDC_MENUGUIDE_LIST)
                ? theme_ctlcolor_custom((HDC)wp, RGB(0x2D, 0x2D, 0x2D), 2)
                : theme_ctlcolor((HDC)wp);
      if (br) return (LRESULT)br;
      break;
    }

    case WM_COMMAND:
      if (LOWORD(wp) == IDC_MENUGUIDE_LIST && HIWORD(wp) == LBN_SELCHANGE)
      {
        guide_show(dlg);
        return TRUE;
      }
      if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)
      {
        EndDialog(dlg, LOWORD(wp));
        return TRUE;
      }
      return FALSE;

    case WM_CLOSE:
      EndDialog(dlg, IDCANCEL);
      return TRUE;
  }

  (void)lp;
  return FALSE;
}

void dlg_menu_guide(HWND parent)
{
  gui_dialog_box(gui.large_ui ? IDD_MENUGUIDE_LARGE : IDD_MENUGUIDE, parent, menu_guide_proc);
}

static const char *system_name(void)
{
  if (system_hw == SYSTEM_MCD) return "Sega CD / Mega CD";
  if ((system_hw & SYSTEM_PBC) == SYSTEM_MD) return "Genesis / Mega Drive";
  if (system_hw == SYSTEM_GG || system_hw == SYSTEM_GGMS) return "Game Gear";
  if (system_hw == SYSTEM_SMS || system_hw == SYSTEM_SMS2) return "Master System";
  if (system_hw == SYSTEM_SG || system_hw == SYSTEM_SGII || system_hw == SYSTEM_SGII_RAM_EXT) return "SG-1000";
  return "Unknown";
}

static void copy_text_to_clipboard(HWND owner, const char *text)
{
  size_t len = strlen(text) + 1;
  HGLOBAL h;

  if (!OpenClipboard(owner)) return;
  EmptyClipboard();
  h = GlobalAlloc(GMEM_MOVEABLE, len);
  if (h)
  {
    char *p = (char *)GlobalLock(h);
    if (p) { memcpy(p, text, len); GlobalUnlock(h); SetClipboardData(CF_TEXT, h); }
    else GlobalFree(h);
  }
  CloseClipboard();
}

/* One row of the ROM Information list as "Field<TAB>Value" (just the field for a section title). */
static void rominfo_row_text(HWND list, int row, char *out, int len)
{
  char a[256] = "", b[512] = "";

  ListView_GetItemText(list, row, 0, a, sizeof(a));
  ListView_GetItemText(list, row, 1, b, sizeof(b));
  if (b[0]) gui_snprintf(out, len, "%s\t%s", a, b);
  else lstrcpynA(out, a, len);
}

static INT_PTR CALLBACK rominfo_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
  {
    case WM_CONTEXTMENU:
    {
      HWND list = GetDlgItem(dlg, IDC_ROMINFO_TEXT);
      int sel = ListView_GetNextItem(list, -1, LVNI_SELECTED);

      if ((HWND)wp == list)
      {
        POINT pt;
        HMENU menu;
        int cmd;

        pt.x = (short)LOWORD(lp);
        pt.y = (short)HIWORD(lp);
        if (pt.x == -1 && pt.y == -1) { RECT r; GetWindowRect(list, &r); pt.x = r.left + 20; pt.y = r.top + 20; }
        else
        {
          LVHITTESTINFO hit;
          POINT cp = pt;
          ScreenToClient(list, &cp);
          memset(&hit, 0, sizeof(hit));
          hit.pt = cp;
          if (ListView_HitTest(list, &hit) >= 0) { sel = hit.iItem; ListView_SetItemState(list, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); }
        }
        menu = CreatePopupMenu();
        AppendMenuA(menu, MF_STRING | (sel >= 0 ? 0 : (UINT)MF_GRAYED), 1, "Copy");
        cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, dlg, NULL);
        DestroyMenu(menu);
        if (cmd == 1 && sel >= 0)
        {
          char row_text[800];
          rominfo_row_text(list, sel, row_text, sizeof(row_text));
          copy_text_to_clipboard(dlg, row_text);
        }
        return TRUE;
      }
      return FALSE;
    }

    case WM_TIMER:
      if (wp == 1)
      {
        KillTimer(dlg, 1);
        SetDlgItemTextA(dlg, IDC_ROMINFO_COPY, "Copy");
      }
      return TRUE;

    case WM_INITDIALOG:
      theme_apply_to_window(dlg);
      {
        char text[4096];
        char domestic[50], international[50], product[14], romtype_str[4],
             country[18], copyright[18];
        uint32 rom_size;

        /* Copied out and trimmed -- the core's own fields are fixed-size
           and space-padded, not necessarily null-terminated at the point
           the real text ends. */
        lstrcpynA(domestic, rominfo.domestic, sizeof(domestic));
        lstrcpynA(international, rominfo.international, sizeof(international));
        lstrcpynA(product, rominfo.product, sizeof(product));
        lstrcpynA(romtype_str, rominfo.ROMType, sizeof(romtype_str));
        lstrcpynA(country, rominfo.country, sizeof(country));
        lstrcpynA(copyright, rominfo.copyright, sizeof(copyright));

        rom_size = rominfo.romend - rominfo.romstart + 1;

        {
          char genre[40] = "", sizebuf[40] = "", console_hdr[20], devs[160] = "", ramtxt[40], crctxt[16], typetxt[32];
          static const char *const dev_names[15] = { "3-Button Pad", "6-Button Pad", "Keyboard", "Printer", "Control Ball",
            "Floppy Drive", "Activator", "Team Player", "MS Joypad", "RS232C Serial", "Tablet", "Paddle", "CD-ROM", "Mega Mouse", "Menacer" };
          int d;
          WIN32_FILE_ATTRIBUTE_DATA fa;
          const char *path = emu_rom_path();

          if (path[0])
          {
            browser_genre_for(path, genre, sizeof(genre));
            if (GetFileAttributesExA(path, GetFileExInfoStandard, &fa))
              wsprintfA(sizebuf, "%lu KB", (unsigned long)((fa.nFileSizeLow + 1023) / 1024));
          }

          lstrcpynA(console_hdr, rominfo.consoletype, sizeof(console_hdr));
          for (d = lstrlenA(console_hdr); d > 0 && console_hdr[d - 1] == ' '; d--) console_hdr[d - 1] = '\0';
          for (d = 0; d < 15; d++)
            if (rominfo.peripherals & (1u << d))
            {
              if (devs[0]) lstrcatA(devs, ", ");
              lstrcatA(devs, dev_names[d]);
            }
          if (!strncmp(rominfo.ROMType, "GM", 2))      lstrcpyA(typetxt, "Game");
          else if (!strncmp(rominfo.ROMType, "AL", 2)) lstrcpyA(typetxt, "Educational program");
          else if (!strncmp(rominfo.ROMType, "BR", 2)) lstrcpyA(typetxt, "Boot ROM");
          else typetxt[0] = '\0';
          if (sram.detected && sram.end >= sram.start)
            wsprintfA(ramtxt, "Yes (%lu KB)", (unsigned long)((sram.end - sram.start + 1 + 1023) / 1024));
          else lstrcpyA(ramtxt, "No");
          wsprintfA(crctxt, "%08lX", (unsigned long)crc32(0L, (const Bytef *)cart.rom, (uInt)cart.romsize));

          snprintf(text, sizeof(text),      /* not wsprintf: that stops at 1024 bytes, and a long path would cut the last rows */
            "GAME\r\n"
            "Name:\t%s\r\n"
            "Console:\t%s\r\n"
            "Genre:\t%s\r\n"
            "Favorite:\t%s\r\n"
            "File size:\t%s\r\n"
            "CRC32:\t%s\r\n"
            "Location:\t%s\r\n"
            "\r\n"
            "CARTRIDGE HEADER\r\n"
            "Console name:\t%s\r\n"
            "Type:\t%s\r\n"
            "Domestic title:\t%s\r\n"
            "International title:\t%s\r\n"
            "Product code:\t%s\r\n"
            "Region:\t%s\r\n"
            "Copyright:\t%s\r\n"
            "Controllers:\t%s\r\n"
            "Save RAM:\t%s\r\n"
            "\r\n"
            "ROM\r\n"
            "ROM size:\t%lu KB\r\n"
            "Header checksum:\t%04X\r\n"
            "Calculated checksum:\t%04X %s\r\n",
            emu_rom_filename()[0] ? emu_rom_filename() : "(unknown)",
            system_name(),
            genre[0] ? genre : "(unknown)",
            path[0] && config_fav_has(path) ? "Yes" : "No",
            sizebuf[0] ? sizebuf : "(unknown)",
            crctxt,
            path[0] ? path : "(unknown)",
            console_hdr[0] ? console_hdr : "(none)",
            typetxt[0] ? typetxt : "(none)",
            domestic[0] ? domestic : "(none)",
            international[0] ? international : "(none)",
            product[0] ? product : "(none)",
            country[0] ? country : "(none)",
            copyright[0] ? copyright : "(none)",
            devs[0] ? devs : "(none)",
            ramtxt,
            (unsigned long)(rom_size / 1024),
            rominfo.checksum,
            rominfo.realchecksum,
            (rominfo.checksum == rominfo.realchecksum) ? "(match)" : "(mismatch)");
        }

        {
          /* "Field<TAB>Value" lines into a two-column list, as in the Keyboard Shortcuts window;
             a line with no tab is a section title. */
          HWND list = GetDlgItem(dlg, IDC_ROMINFO_TEXT);
          LVCOLUMNA col;
          LVITEMA it;
          RECT rc;
          char *line;
          int row = 0;

          SendMessage(list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
          GetClientRect(list, &rc);
          memset(&col, 0, sizeof(col));
          col.mask = LVCF_TEXT | LVCF_WIDTH;
          col.pszText = "Field";
          col.cx = (rc.right - rc.left) * 32 / 100;
          SendMessageA(list, LVM_INSERTCOLUMNA, 0, (LPARAM)&col);
          col.pszText = "Value";
          col.cx = (rc.right - rc.left) * 64 / 100;
          SendMessageA(list, LVM_INSERTCOLUMNA, 1, (LPARAM)&col);

          for (line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n"))
          {
            char *tab = strchr(line, '\t');
            const char *name = line, *value = "";
            char title[64];

            if (tab) { *tab = '\0'; value = tab + 1; }
            else
            {
              /* A section title: capitals become "Game", "Cartridge Header", "Rom" ... */
              int k, up = 1;
              lstrcpynA(title, line, sizeof(title));
              for (k = 0; title[k]; k++)
              {
                if (title[k] == ' ') up = 1;
                else if (up) up = 0;
                else if (title[k] >= 'A' && title[k] <= 'Z') title[k] = (char)(title[k] + 32);
              }
              if (!lstrcmpA(title, "Rom")) lstrcpyA(title, "ROM");
              name = title;
            }
            memset(&it, 0, sizeof(it));
            it.mask = LVIF_TEXT;
            it.iItem = row;
            it.pszText = (char *)name;
            SendMessageA(list, LVM_INSERTITEMA, 0, (LPARAM)&it);
            it.iSubItem = 1;
            it.pszText = (char *)value;
            SendMessageA(list, LVM_SETITEMTEXTA, (WPARAM)row, (LPARAM)&it);
            row++;
          }

          theme_apply_to_window(dlg);
          {
            HWND header = ListView_GetHeader(list);
            if (header && theme_is_dark()) { SetWindowTheme(header, L"ItemsView", NULL); InvalidateRect(header, NULL, TRUE); }
          }
        }
      }
      return TRUE;

    case WM_CTLCOLORDLG:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
      HBRUSH br = theme_ctlcolor((HDC)wp);
      if (br) return (LRESULT)br;
      break;
    }

    case WM_COMMAND:
      if (LOWORD(wp) == IDC_ROMINFO_COPY)
      {
        HWND list = GetDlgItem(dlg, IDC_ROMINFO_TEXT);
        int i, cnt = ListView_GetItemCount(list);
        char *all = (char *)malloc((size_t)cnt * 800 + 16);

        if (all)
        {
          char row_text[800];
          all[0] = '\0';
          for (i = 0; i < cnt; i++)
          {
            rominfo_row_text(list, i, row_text, sizeof(row_text));
            strcat(all, row_text);
            strcat(all, "\r\n");
          }
          copy_text_to_clipboard(dlg, all);
          free(all);
          SetDlgItemTextA(dlg, IDC_ROMINFO_COPY, "Copied");
          SetTimer(dlg, 1, 1500, NULL);
        }
        return TRUE;
      }
      if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)
      {
        KillTimer(dlg, 1);
        EndDialog(dlg, LOWORD(wp));
        return TRUE;
      }
      return FALSE;

    case WM_CLOSE:
      KillTimer(dlg, 1);
      EndDialog(dlg, IDCANCEL);
      return TRUE;
  }

  return FALSE;
}

void dlg_rom_info(HWND parent)
{
  if (!emu_running && !emu_rom_filename()[0]) return;
  gui_dialog_box(gui.large_ui ? IDD_ROMINFO_LARGE : IDD_ROMINFO, parent, rominfo_proc);
}

/****************************************************************************
 * About
 ****************************************************************************/

/* STM_SETICON hands the control a handle without taking ownership of it
   (it just returns whatever icon was there before) -- kept here so
   WM_DESTROY below can DestroyIcon() it, since this LoadImageA() call
   (explicit size, no LR_SHARED) returns a private handle this dialog is
   responsible for freeing itself. */
static HICON about_big_icon;

static INT_PTR CALLBACK about_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
  (void)lp;

  switch (msg)
  {
    case WM_INITDIALOG:
      theme_apply_to_window(dlg);
      if (gui.large_ui)
      {
        about_big_icon = (HICON)LoadImageA(g_inst, MAKEINTRESOURCEA(IDI_APPICON),
                                            IMAGE_ICON, 64, 64, 0);
        if (about_big_icon) SendDlgItemMessage(dlg, IDC_ABOUT_ICON, STM_SETICON, (WPARAM)about_big_icon, 0);
      }
      SetDlgItemTextA(dlg, IDC_ABOUT_TEXT,
        "Genesis Plus v" APP_VERSION " (" APP_BITS ")\r\n"
        "\r\n"
        "Genesis Plus, a Windows emulator based on Genesis Plus GX\r\n"
        "Sega Mega Drive / Genesis, Master System, Game Gear, SG-1000, Mega CD and Pico emulator.\r\n"
        "\r\n"
        "Core by Charles MacDonald (1998-2003) and Eke-Eke (2007-2026).\r\n"
        "Windows GUI by Hazem Abdelghani (2026).\r\n"
        "\r\n"
        "Distributed under the Genesis Plus GX license: source must accompany modified redistributions, and it may not be sold or used commercially.");

      /* The Genesis Plus link first, the original Genesis Plus GX second. */
      SendDlgItemMessage(dlg, IDC_ABOUT_LINK1, WM_SETTEXT, 0,
        (LPARAM)"<a href=\"https://github.com/hazem-abdelghani/Genesis-Plus\">Genesis Plus</a>");
      SendDlgItemMessage(dlg, IDC_ABOUT_LINK2, WM_SETTEXT, 0,
        (LPARAM)"<a href=\"https://github.com/ekeeke/Genesis-Plus-GX\">Genesis Plus GX</a>");
      return TRUE;

    case WM_NOTIFY:
    {
      NMHDR *hdr = (NMHDR *)lp;
      if ((hdr->code == NM_CLICK || hdr->code == NM_RETURN) &&
          (hdr->idFrom == IDC_ABOUT_LINK1 || hdr->idFrom == IDC_ABOUT_LINK2))
      {
        NMLINK *link = (NMLINK *)lp;
        char url[256];

        WideCharToMultiByte(CP_ACP, 0, link->item.szUrl, -1, url, sizeof(url), NULL, NULL);
        ShellExecuteA(dlg, "open", url, NULL, NULL, SW_SHOWNORMAL);
        return TRUE;
      }
      break;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
      HBRUSH br = theme_ctlcolor((HDC)wp);
      if (br) return (LRESULT)br;
      break;
    }

    case WM_COMMAND:
      if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)
      {
        EndDialog(dlg, LOWORD(wp));
        return TRUE;
      }
      return FALSE;

    case WM_CLOSE:
      EndDialog(dlg, IDCANCEL);
      return TRUE;

    case WM_DESTROY:
      if (about_big_icon) { DestroyIcon(about_big_icon); about_big_icon = NULL; }
      return TRUE;
  }

  return FALSE;
}

void dlg_about(HWND parent)
{
  gui_dialog_box(gui.large_ui ? IDD_ABOUT_LARGE : IDD_ABOUT, parent, about_proc);
}

/****************************************************************************
 * Mouse cursor while a dialog is open
 ****************************************************************************/

/* Makes sure the cursor is visible. Returns how many ShowCursor(TRUE) calls
   it took (always at least one), so gui_cursor_restore() can undo exactly
   that and leave the counter where fullscreen had put it. */
int gui_cursor_show(void)
{
  int calls = 1;

  while (ShowCursor(TRUE) < 0 && calls < 64) calls++;
  return calls;
}

void gui_cursor_restore(int hidden_count)
{
  while (hidden_count-- > 0) ShowCursor(FALSE);
}

INT_PTR gui_dialog_box(int template_id, HWND parent, DLGPROC proc)
{
  INT_PTR r;
  int c = gui_cursor_show();

  r = DialogBoxParamA(g_inst, MAKEINTRESOURCEA(template_id), parent, proc, 0);
  gui_cursor_restore(c);
  return r;
}

/****************************************************************************
 * Open-file picker
 *
 * Uses the Vista+ "common item" dialog instead of GetOpenFileName. The old
 * API lets Windows restore its own remembered state -- last folder, window
 * size / maximised, even a previous search -- and only treats the start
 * folder as a hint. Here the start folder is forced (SetFolder) and the
 * remembered state is cleared every time, per picker (each has its own
 * GUID so they can't affect one another).
 *
 * `filter` uses the same "Name\0pattern\0Name\0pattern\0\0" layout as
 * OPENFILENAME. Returns 1 and fills `out` with the chosen path, else 0.
 ****************************************************************************/

static const GUID pick_guids[GUI_PICK_COUNT] =
{
  { 0x6f1a5c10, 0x3b7e, 0x4c1a, { 0x9a, 0x21, 0x5d, 0x0e, 0x7b, 0x11, 0xa0, 0x01 } },  /* Open ROM     */
  { 0x6f1a5c10, 0x3b7e, 0x4c1a, { 0x9a, 0x21, 0x5d, 0x0e, 0x7b, 0x11, 0xa0, 0x02 } },  /* Cheat file   */
  { 0x6f1a5c10, 0x3b7e, 0x4c1a, { 0x9a, 0x21, 0x5d, 0x0e, 0x7b, 0x11, 0xa0, 0x03 } },  /* Cover        */
  { 0x6f1a5c10, 0x3b7e, 0x4c1a, { 0x9a, 0x21, 0x5d, 0x0e, 0x7b, 0x11, 0xa0, 0x04 } }   /* Shader preset */
};

static int pick_file_legacy(HWND owner, const char *title, const char *filter,
                            const char *start_dir, char *out, int out_len)
{
  OPENFILENAMEA ofn;

  out[0] = '\0';
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize     = sizeof(ofn);
  ofn.hwndOwner       = owner;
  ofn.lpstrFile       = out;
  ofn.nMaxFile        = (DWORD)out_len;
  ofn.lpstrTitle      = title;
  ofn.lpstrFilter     = filter;
  ofn.nFilterIndex    = 1;
  ofn.lpstrInitialDir = (start_dir && start_dir[0]) ? start_dir : NULL;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
              OFN_EXPLORER | OFN_NOCHANGEDIR;

  return GetOpenFileNameA(&ofn) ? 1 : 0;
}

static int pick_file_impl(HWND owner, int which, const char *title, const char *filter,
                          const char *start_dir, char *out, int out_len)
{
  static wchar_t names[8][128], pats[8][256];
  COMDLG_FILTERSPEC specs[8];
  wchar_t wtitle[128], wpath[GUI_PATH_LEN];
  IFileOpenDialog *dlg = NULL;
  HRESULT hr, hr_init;
  const char *p;
  FILEOPENDIALOGOPTIONS opts = 0;
  int n = 0, ok = 0;

  if (!out || out_len <= 0) return 0;
  out[0] = '\0';
  if (which < 0 || which >= GUI_PICK_COUNT) which = 0;

  hr_init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                        &IID_IFileOpenDialog, (void **)&dlg);
  if (FAILED(hr) || !dlg)
  {
    if (SUCCEEDED(hr_init)) CoUninitialize();
    return pick_file_legacy(owner, title, filter, start_dir, out, out_len);
  }

  IFileOpenDialog_SetClientGuid(dlg, &pick_guids[which]);
  IFileOpenDialog_ClearClientData(dlg);

  MultiByteToWideChar(CP_ACP, 0, title, -1, wtitle, 128);
  IFileOpenDialog_SetTitle(dlg, wtitle);

  for (p = filter; p && *p && n < 8; n++)
  {
    MultiByteToWideChar(CP_ACP, 0, p, -1, names[n], 128);
    p += lstrlenA(p) + 1;
    MultiByteToWideChar(CP_ACP, 0, p, -1, pats[n], 256);
    p += lstrlenA(p) + 1;
    specs[n].pszName = names[n];
    specs[n].pszSpec = pats[n];
  }
  if (n > 0)
  {
    IFileOpenDialog_SetFileTypes(dlg, (UINT)n, specs);
    IFileOpenDialog_SetFileTypeIndex(dlg, 1);
  }

  IFileOpenDialog_GetOptions(dlg, &opts);
  IFileOpenDialog_SetOptions(dlg, opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST |
                                  FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);

  if (start_dir && start_dir[0])
  {
    IShellItem *folder = NULL;

    MultiByteToWideChar(CP_ACP, 0, start_dir, -1, wpath, GUI_PATH_LEN);
    if (SUCCEEDED(SHCreateItemFromParsingName(wpath, NULL, &IID_IShellItem, (void **)&folder)) && folder)
    {
      IFileOpenDialog_SetFolder(dlg, folder);
      IShellItem_Release(folder);
    }
  }

  hr = IFileOpenDialog_Show(dlg, owner);
  if (SUCCEEDED(hr))
  {
    IShellItem *item = NULL;

    if (SUCCEEDED(IFileOpenDialog_GetResult(dlg, &item)) && item)
    {
      PWSTR w = NULL;

      if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &w)) && w)
      {
        ok = WideCharToMultiByte(CP_ACP, 0, w, -1, out, out_len, NULL, NULL) > 0;
        CoTaskMemFree(w);
      }
      IShellItem_Release(item);
    }
  }

  IFileOpenDialog_Release(dlg);
  if (SUCCEEDED(hr_init)) CoUninitialize();

  if (!ok) out[0] = '\0';
  return ok;
}

int gui_pick_file(HWND owner, int which, const char *title, const char *filter,
                  const char *start_dir, char *out, int out_len)
{
  int c = gui_cursor_show();
  int r = pick_file_impl(owner, which, title, filter, start_dir, out, out_len);

  gui_cursor_restore(c);
  return r;
}

/****************************************************************************
 * Netplay (LAN)
 ****************************************************************************/

static np_found np_hosts[8];
static int      np_host_count;

static void np_dlg_status(HWND dlg, const char *text)
{
  SetDlgItemTextA(dlg, IDC_NP_STATUS, text);
}

/* In dark mode the system draws a group box's title in black whatever colour
   the dialog asks for, which vanishes against the dark background. So the
   group boxes of this dialog paint themselves: a thin grey frame and a light
   title. (Only the frame and title are drawn; everything inside is left to the
   controls on top.) */
static LRESULT CALLBACK np_groupbox_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                         UINT_PTR id, DWORD_PTR ref)
{
  (void)ref;

  if (msg == WM_ERASEBKGND) return 1;   /* the dialog has already painted the background */

  if (msg == WM_PAINT)
  {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    char text[128];
    HFONT font = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
    HGDIOBJ old_font = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    SIZE sz;
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    HBRUSH bg = CreateSolidBrush(RGB(32, 32, 32));
    RECT patch;

    GetClientRect(hwnd, &rc);
    GetWindowTextA(hwnd, text, sizeof(text));
    GetTextExtentPoint32A(dc, text, lstrlenA(text), &sz);

    Rectangle(dc, rc.left, rc.top + sz.cy / 2, rc.right, rc.bottom);

    /* Clear the stretch of frame behind the title, then write the title. */
    patch.left = rc.left + 7; patch.right = rc.left + 13 + sz.cx;
    patch.top = rc.top;       patch.bottom = rc.top + sz.cy;
    FillRect(dc, &patch, bg);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(240, 240, 240));
    TextOutA(dc, rc.left + 10, rc.top, text, lstrlenA(text));

    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_font);
    DeleteObject(pen);
    DeleteObject(bg);
    EndPaint(hwnd, &ps);
    return 0;
  }

  if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, np_groupbox_proc, id);

  return DefSubclassProc(hwnd, msg, wp, lp);
}

static BOOL CALLBACK np_untheme_groupbox(HWND child, LPARAM lp)
{
  char cls[16];

  (void)lp;
  if (GetClassNameA(child, cls, sizeof(cls)) && !lstrcmpiA(cls, "Button") &&
      (GetWindowLongPtr(child, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX)
  {
    SetWindowSubclass(child, np_groupbox_proc, 1, 0);
    InvalidateRect(child, NULL, TRUE);
  }
  return TRUE;
}

static INT_PTR CALLBACK netplay_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
  {
    case WM_INITDIALOG:
    {
      char buf[160];
      HWND combo = GetDlgItem(dlg, IDC_NP_DELAY);
      int i;

      theme_apply_to_window(dlg);
      if (theme_is_dark()) EnumChildWindows(dlg, np_untheme_groupbox, 0);

      wsprintfA(buf, "%d", gui.np_port);
      SetDlgItemTextA(dlg, IDC_NP_HOST_PORT, buf);
      SetDlgItemTextA(dlg, IDC_NP_PORT, buf);
      SetDlgItemTextA(dlg, IDC_NP_ADDR, gui.np_addr);

      for (i = 1; i <= 4; i++)
      {
        wsprintfA(buf, "%d frame%s", i, i == 1 ? "" : "s");
        SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)buf);
      }
      SendMessage(combo, CB_SETCURSEL, (WPARAM)(gui.np_delay - 1), 0);

      wsprintfA(buf, "Your address on this network: %s", netplay_local_addresses()[0] ? netplay_local_addresses() : "(unknown)");
      SetDlgItemTextA(dlg, IDC_NP_MYADDR, buf);
      np_dlg_status(dlg, "Both players must load the same ROM and use the same Genesis Plus.exe.");
      {
        static const GuiTip tips[] =
        {
          { IDC_NP_HOST_PORT, NULL, "The port this game listens on. The other player joins this port." },
          { IDC_NP_DELAY, "Input Delay:", "Frames of input delay: more is smoother on a slow network, fewer feels more immediate." },
          { IDC_NP_HOST_CODE, "Session Code (Optional):", "A code the other player has to type to join. Leave it empty to let anyone join." },
          { IDC_NP_HOST, NULL, "Wait for another player to join this game." },
          { IDC_NP_LIST, NULL, "Hosts found on your network. Pick one to fill in its address." },
          { IDC_NP_FIND, NULL, "Look for hosts on your local network." },
          { IDC_NP_ADDR, "Address:", "The address of the host to join." },
          { IDC_NP_PORT, NULL, "The port the host is listening on." },
          { IDC_NP_JOIN_CODE, "Code:", "The session code, if the host set one." },
          { IDC_NP_JOIN, NULL, "Join the host at this address." },
          { IDCANCEL, NULL, "Close this window." },
        };
        gui_dialog_tips(dlg, tips, (int)(sizeof(tips) / sizeof(tips[0])));
      }
      return TRUE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
      HBRUSH br;

      /* The list of hosts: #2D2D2D in dark mode (the same shade as the other lists). */
      if (GetDlgCtrlID((HWND)lp) == IDC_NP_LIST)
        br = theme_ctlcolor_custom((HDC)wp, RGB(0x2D, 0x2D, 0x2D), 2);
      else if (msg == WM_CTLCOLOREDIT)      /* the entry boxes stand out from the dialog, as in Cheats */
        br = theme_ctlcolor_custom((HDC)wp, RGB(0x38, 0x38, 0x38), 1);
      else
        br = theme_ctlcolor((HDC)wp);
      if (br) return (LRESULT)br;
      break;
    }

    case WM_COMMAND:
    {
      char err[300], code[64], addr[64], buf[32];
      int port, delay;

      switch (LOWORD(wp))
      {
        case IDC_NP_FIND:
        {
          int i;
          HWND list = GetDlgItem(dlg, IDC_NP_LIST);
          HCURSOR old = SetCursor(LoadCursorA(NULL, IDC_WAIT));

          np_dlg_status(dlg, "Looking for hosts on your network...");
          np_host_count = netplay_discover(np_hosts, 8);
          SetCursor(old);

          SendMessage(list, LB_RESETCONTENT, 0, 0);
          for (i = 0; i < np_host_count; i++)
          {
            char line[160];
            wsprintfA(line, "%s  -  %s  (%s)%s", np_hosts[i].name, np_hosts[i].game[0] ? np_hosts[i].game : "no game",
                      np_hosts[i].addr, np_hosts[i].needs_code ? "  [code needed]" : "");
            SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)line);
          }
          if (np_host_count)
          {
            SendMessage(list, LB_SETCURSEL, 0, 0);
            SetDlgItemTextA(dlg, IDC_NP_ADDR, np_hosts[0].addr);
            wsprintfA(buf, "%u", (unsigned)np_hosts[0].port);
            SetDlgItemTextA(dlg, IDC_NP_PORT, buf);
            np_dlg_status(dlg, np_hosts[0].needs_code ? "Found a host. Enter its session code, then Join." : "Found a host. Press Join.");
          }
          else
          {
            np_dlg_status(dlg, "No hosts found. Type the host's address below, or ask them to Host first.");
          }
          return TRUE;
        }

        case IDC_NP_LIST:
          if (HIWORD(wp) == LBN_SELCHANGE)
          {
            int sel = (int)SendDlgItemMessage(dlg, IDC_NP_LIST, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < np_host_count)
            {
              SetDlgItemTextA(dlg, IDC_NP_ADDR, np_hosts[sel].addr);
              wsprintfA(buf, "%u", (unsigned)np_hosts[sel].port);
              SetDlgItemTextA(dlg, IDC_NP_PORT, buf);
            }
          }
          return TRUE;

        case IDC_NP_HOST:
          GetDlgItemTextA(dlg, IDC_NP_HOST_PORT, buf, sizeof(buf));
          port = atoi(buf);
          GetDlgItemTextA(dlg, IDC_NP_HOST_CODE, code, sizeof(code));
          delay = (int)SendDlgItemMessage(dlg, IDC_NP_DELAY, CB_GETCURSEL, 0, 0) + 1;
          if (delay < 1) delay = 2;

          if (!netplay_host(port, code, delay, err, sizeof(err)))
          {
            np_dlg_status(dlg, err);
            return TRUE;
          }
          gui.np_port = port;
          gui.np_delay = delay;
          config_save();
          gui_notify("Hosting: waiting for Player 2 (%s, port %d)", netplay_local_addresses()[0] ? netplay_local_addresses() : "this PC", port);
          EndDialog(dlg, IDOK);
          return TRUE;

        case IDC_NP_JOIN:
        {
          HCURSOR old;

          GetDlgItemTextA(dlg, IDC_NP_ADDR, addr, sizeof(addr));
          GetDlgItemTextA(dlg, IDC_NP_PORT, buf, sizeof(buf));
          port = atoi(buf);
          GetDlgItemTextA(dlg, IDC_NP_JOIN_CODE, code, sizeof(code));

          np_dlg_status(dlg, "Connecting...");
          old = SetCursor(LoadCursorA(NULL, IDC_WAIT));
          port = netplay_join(addr, port, code, err, sizeof(err));
          SetCursor(old);

          if (!port)
          {
            np_dlg_status(dlg, err);
            return TRUE;
          }
          lstrcpynA(gui.np_addr, addr, sizeof(gui.np_addr));
          config_save();
          EndDialog(dlg, IDOK);
          return TRUE;
        }

        case IDCANCEL:
          EndDialog(dlg, IDCANCEL);
          return TRUE;
      }
      return FALSE;
    }

    case WM_CLOSE:
      EndDialog(dlg, IDCANCEL);
      return TRUE;
  }

  return FALSE;
}

void dlg_netplay(HWND parent)
{
  gui_dialog_box(gui.large_ui ? IDD_NETPLAY_LARGE : IDD_NETPLAY, parent, netplay_proc);
}

/****************************************************************************
 * Manage Save States
 ****************************************************************************/

#define STATEMGR_SLOTS 10

static HBITMAP statemgr_bitmaps[STATEMGR_SLOTS];

static void statemgr_refresh_slot(HWND dlg, int slot)
{
  char state_p[GUI_PATH_LEN], thumb_p[GUI_PATH_LEN], label[96], stamp[32];
  HWND btn = GetDlgItem(dlg, IDC_STATEMGR_SLOT_BASE + slot);
  HWND lbl = GetDlgItem(dlg, IDC_STATEMGR_LABEL_BASE + slot);
  int exists;

  state_path(slot, state_p, sizeof(state_p));
  exists = GetFileAttributesA(state_p) != INVALID_FILE_ATTRIBUTES;

  if (statemgr_bitmaps[slot])
  {
    SendMessage(btn, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)NULL);
    DeleteObject(statemgr_bitmaps[slot]);
    statemgr_bitmaps[slot] = NULL;
  }

  if (exists)
  {
    thumb_path(slot, thumb_p, sizeof(thumb_p));
    statemgr_bitmaps[slot] = video_load_thumbnail(thumb_p);

    /* Three short lines -- "Slot N", time, date -- because the full
       "hh:mm:ss  DD/MM/YYYY" stamp is wider than a thumbnail column. */
    if (state_file_time(state_p, stamp, sizeof(stamp)) && lstrlenA(stamp) >= 20)
    {
      stamp[8] = '\0';
      wsprintfA(label, "Slot %d\n%s\n%s", slot, stamp, stamp + 10);
    }
    else
    {
      wsprintfA(label, "Slot %d", slot);
    }
  }
  else
  {
    wsprintfA(label, "Slot %d\nEmpty", slot);
  }

  /* Enabled whenever a state exists, even without a loadable thumbnail --
     an older save from before this dialog existed still has no .bmp next
     to it, and should still be loadable, just without a preview image. */
  SendMessage(btn, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)statemgr_bitmaps[slot]);
  EnableWindow(btn, exists);
  SetWindowTextA(lbl, label);
}

static INT_PTR CALLBACK statemgr_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
  {
    case WM_INITDIALOG:
    {
      int i;
      theme_apply_to_window(dlg);
      for (i = 0; i < STATEMGR_SLOTS; i++) statemgr_refresh_slot(dlg, i);
      return TRUE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
      HBRUSH br = theme_ctlcolor((HDC)wp);
      if (br) return (LRESULT)br;
      break;
    }

    case WM_CONTEXTMENU:
    {
      HWND target = (HWND)wp;
      int id = GetDlgCtrlID(target);

      if (id >= IDC_STATEMGR_SLOT_BASE && id < IDC_STATEMGR_SLOT_BASE + STATEMGR_SLOTS)
      {
        int slot = id - IDC_STATEMGR_SLOT_BASE;
        char state_p[GUI_PATH_LEN];
        int exists;
        HMENU menu;
        int cmd;

        state_path(slot, state_p, sizeof(state_p));
        exists = GetFileAttributesA(state_p) != INVALID_FILE_ATTRIBUTES;

        menu = CreatePopupMenu();
        AppendMenuA(menu, MF_STRING, 1, "Save to This Slot");
        AppendMenuA(menu, MF_STRING | (exists ? 0 : MF_GRAYED), 2, "Delete");

        /* Documented Windows workaround (MSDN, TrackPopupMenu remarks):
           without this, the popup can fail to dismiss correctly if this
           window isn't confirmed as the foreground one. */
        {
          /* lp is a signed screen position (negative on a monitor left of or
             above the main one), or -1 when opened from the keyboard: then
             open it at the middle of the slot's button. */
          int px = (int)(short)LOWORD(lp), py = (int)(short)HIWORD(lp);

          if (lp == (LPARAM)-1 || (px == -1 && py == -1))
          {
            RECT br;
            GetWindowRect(target, &br);
            px = (br.left + br.right) / 2;
            py = (br.top + br.bottom) / 2;
          }

          SetForegroundWindow(dlg);
          cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                               px, py, 0, dlg, NULL);
        }
        PostMessage(dlg, WM_NULL, 0, 0);
        DestroyMenu(menu);

        if (cmd == 1)
        {
          emu_save_state(slot);
          statemgr_refresh_slot(dlg, slot);
        }
        else if (cmd == 2 && exists)
        {
          state_delete(slot);
          statemgr_refresh_slot(dlg, slot);
        }
        return TRUE;
      }
      return FALSE;
    }

    case WM_COMMAND:
    {
      int id = LOWORD(wp);

      if (id >= IDC_STATEMGR_SLOT_BASE && id < IDC_STATEMGR_SLOT_BASE + STATEMGR_SLOTS &&
          HIWORD(wp) == BN_CLICKED)
      {
        int slot = id - IDC_STATEMGR_SLOT_BASE;
        char state_p[GUI_PATH_LEN];

        state_path(slot, state_p, sizeof(state_p));
        if (GetFileAttributesA(state_p) != INVALID_FILE_ATTRIBUTES)
        {
          emu_load_state(slot);
          EndDialog(dlg, IDOK);
        }
        return TRUE;
      }

      if (id == IDCANCEL)
      {
        EndDialog(dlg, IDCANCEL);
        return TRUE;
      }
      return FALSE;
    }

    case WM_DESTROY:
    {
      int i;
      for (i = 0; i < STATEMGR_SLOTS; i++)
      {
        if (statemgr_bitmaps[i])
        {
          DeleteObject(statemgr_bitmaps[i]);
          statemgr_bitmaps[i] = NULL;
        }
      }
      return FALSE;
    }

    case WM_CLOSE:
      EndDialog(dlg, IDCANCEL);
      return TRUE;
  }

  return FALSE;
}

void dlg_state_manager(HWND parent)
{
  if (!emu_running) return;
  gui_dialog_box(gui.large_ui ? IDD_STATEMGR_LARGE : IDD_STATEMGR, parent, statemgr_proc);
}
