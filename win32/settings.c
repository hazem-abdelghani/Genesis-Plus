/****************************************************************************
 *  Genesis Plus -- Win32 GUI frontend
 *
 *  settings.c -- Options > Settings: every option of the menus, on tabs.
 *
 *  The window holds no settings of its own. Each control stands for a menu
 *  item (or a group of them): changing it sends the same command the menu
 *  item sends, and the control then shows whatever the menu now says (its
 *  check marks), so the two can never disagree and no option is handled
 *  twice. Items that open a window of their own (Levels and Latency,
 *  Controls...) are buttons that close this window and open that one.
 ****************************************************************************/

#include <windows.h>
#include <commctrl.h>
#include <stdlib.h>
#include <string.h>

#include "shared.h"
#include "gui.h"
#include "resource.h"
#include "theme.h"
#include "toolbar.h"     /* TBS(): sizes that follow Larger UI */

enum { K_CHECK, K_CHOICE, K_BUTTON, K_FILTER, K_SHADER, K_BREAK };   /* K_BREAK: the next rows start a new column */

typedef struct
{
  int kind;
  const char *label;
  int id;                          /* command (check, button) or first command of the group (choice) */
  int n;                           /* choice: number of entries */
  const char *const *items;
  const char *tip;                 /* its tooltip */
} SRow;

typedef struct { const char *title; const SRow *rows; int count; } SPage;

#define CHECK(l, id, tip)          { K_CHECK,  l, id, 0, NULL, tip }
#define CHOICE(l, id, items, tip)  { K_CHOICE, l, id, (int)(sizeof(items) / sizeof(items[0])), items, tip }
#define BUTTON(l, id, tip)         { K_BUTTON, l, id, 0, NULL, tip }

static const char *const it_viewmode[] = { "List View", "Grid View" };
static const char *const it_theme[]    = { "System", "Light", "Dark" };
static const char *const it_scale[]    = { "1x", "2x", "3x", "4x", "5x", "6x" };
static const char *const it_lcd[]      = { "Off", "Light", "Medium", "Heavy" };
static const char *const it_scan[]     = { "Off", "25%", "50%", "75%", "100%" };
static const char *const it_ntsc[]     = { "Off", "Composite", "S-Video", "RGB" };
static const char *const it_aspect[]   = { "Square Pixels", "4:3 (As Displayed on a CRT)", "Fill the Window" };
static const char *const it_borders[]  = { "Hide All", "Top and Bottom", "Left and Right", "Show All" };
static const char *const it_interlace[] = { "Single Field", "Double Field" };
static const char *const it_frameskip[] = { "Off", "Auto", "Manual (Below 25% Audio Buffer)",
                                            "Manual (Below 33% Audio Buffer)", "Manual (Below 50% Audio Buffer)",
                                            "Manual (Below 75% Audio Buffer)" };
static const char *const it_renderer[] = { "GDI (Software)", "Direct3D 9", "Direct3D 11" };
static const char *const it_fm[]       = { "YM2612 (Nuked)", "YM3438 (Nuked)", "YM2612 (MAME, Discrete)",
                                           "YM3438 (MAME, ASIC)", "YM3438 (MAME, Enhanced)" };
static const char *const it_rate[]     = { "44100 Hz", "48000 Hz" };
static const char *const it_porta[]    = { "Nothing Connected", "Control Pad", "Mouse", "XE-1AP", "Activator",
                                           "Light Phaser", "Paddle Control", "Sports Pad", "Graphic Board",
                                           "Team Player" };
static const char *const it_portb[]    = { "Nothing Connected", "Control Pad", "Mouse", "Menacer", "Justifier",
                                           "XE-1AP", "Activator", "Light Phaser", "Paddle Control", "Team Player" };
static const char *const it_shot[]     = { "Final", "Corrected", "Raw" };
static const char *const it_region[]   = { "Detect from ROM", "USA", "Europe", "Japan (NTSC)", "Japan (PAL)" };
static const char *const it_vdp[]      = { "Auto", "NTSC (60 Hz)", "PAL (50 Hz)" };
static const char *const it_console[]  = { "Detect from ROM", "SG-1000", "SG-1000 II", "SG-1000 II + RAM Adapter",
                                           "Mark III", "Master System", "Master System II", "Game Gear",
                                           "Mega Drive / Genesis" };
static const char *const it_lockon[]   = { "None", "Game Genie", "Action Replay (Pro)", "Sonic & Knuckles" };

static const SRow rows_view[] =
{
  CHOICE("View mode:",            IDM_VIEW_LIST, it_viewmode, "How the game browser shows your games: a list or a grid of covers."),
  CHECK ("Show the Favorite column", IDM_VIEW_COL_FAV, "Show the star column in the list view. Click a star to make a game a favorite (Ctrl+D does it for the selected game)."),
  CHECK ("Show the Console column", IDM_VIEW_COL_BASE + 1, "Show the Console column in the list view."),
  CHECK ("Show the Folder column",  IDM_VIEW_COL_BASE + 2, "Show the Folder column in the list view."),
  CHECK ("Show the Size column",    IDM_VIEW_COL_BASE + 3, "Show the Size column in the list view."),
  CHOICE("Theme:",                IDM_VIDEO_THEME_BASE, it_theme, "System follows Windows; Light or Dark forces one."),
  CHECK ("Larger UI",             IDM_VIDEO_LARGE_UI, "Bigger menus, dialogs and toolbar."),
  CHECK ("Toolbar in Fullscreen", IDM_VIEW_FSTOOLBAR, "In fullscreen, show the toolbar (with the menu bar) when the mouse touches the top edge of the screen."),
  CHECK ("Show Toolbar",          IDM_VIEW_TOOLBAR, "Show the row of buttons above the game list. It stays above the game while it runs in a window."),
  CHECK ("Show Search Bar",       IDM_VIEW_SEARCHBAR, "Show the search box that narrows the game list as you type (Ctrl+F jumps to it)."),
  CHECK ("Show Filter Bar",       IDM_VIEW_FILTERBAR, "Show the bar that narrows the list by console, region and folder."),
  CHECK ("Show Favorites Only",   IDM_VIEW_FAVONLY, "Show only the games marked as favorites."),
  CHECK ("Enable Rewind",         IDM_EMU_REWIND, "Keep a history of recent play so holding Backspace runs it backwards. Uses about 200 MB and some CPU; not available during netplay."),
  CHECK ("Pause When Inactive",   IDM_EMU_PAUSE_UNFOCUSED, "Pause the game while another window has focus."),
  CHOICE("Screenshot output:",    IDM_TOOLS_SHOT_BASE, it_shot, "Final: the render filter applied, stretched to 4:3. Corrected: the plain picture at 640x480. Raw: exactly what the emulator drew, at its own size."),
};

static const SRow rows_video[] =
{
  CHOICE("Window size:",          IDM_VIDEO_SCALE_BASE, it_scale, "Size of the game window, 1x to 6x the console's picture."),
  CHECK ("Start ROM in Fullscreen", IDM_VIDEO_FULLSCREEN_START, "Start every game in fullscreen. Esc or Alt+Enter toggles it."),
  CHECK ("Always on Top",         IDM_VIDEO_ALWAYS_ON_TOP, "Keep the window above every other window."),
  { K_FILTER, "Render filter:", 0, 0, NULL, "Pixel-art scalers and CRT looks (Scale2x, Smooth, xBRZ, Scanlines, CRT...) applied before the picture is shown." },
  CHECK ("Smooth Scaling",        IDM_VIDEO_SMOOTH, "Soft edges when the picture is enlarged."),
  CHECK ("Brighten",              IDM_VIDEO_BRIGHTEN, "A gentle brightness lift."),
  CHECK ("VSync",                 IDM_VIDEO_VSYNC, "Wait for the screen's refresh after each frame so motion is even. Skipped for a frame whenever the sound buffer runs low, so it never causes crackle."),
  { K_BREAK, NULL, 0, 0, NULL, NULL },
  CHOICE("NTSC filter:",          IDM_VIDEO_NTSC_BASE, it_ntsc, "Composite, S-Video or RGB television look."),
  CHOICE("Aspect ratio:",         IDM_VIDEO_ASPECT_BASE, it_aspect, "Square pixels, 4:3 as on a CRT, or fill the window."),
  CHOICE("Borders:",              IDM_VIDEO_OVERSCAN_BASE, it_borders, "Hide or show the overscan borders (top/bottom, left/right)."),
  CHOICE("Interlaced mode:",      IDM_VIDEO_INTERLACE_BASE, it_interlace, "Single Field (the default) or Double Field, for games that use the 448-line interlaced mode."),
  CHOICE("Frameskip:",            IDM_VIDEO_FRAMESKIP_BASE, it_frameskip, "Skip drawing some frames on a slow PC: Off, Auto, or Manual by audio-buffer level. Not used during netplay or recording."),
  CHOICE("Renderer:",             IDM_VIDEO_RENDERER_BASE, it_renderer, "GDI draws with the CPU; Direct3D 9 and 11 use the graphics card (each falls back to GDI if unavailable). GPU shaders need Direct3D 11."),
  CHOICE("LCD ghosting:",         IDM_VIDEO_LCD_BASE, it_lcd, "Bright pixels fade slowly, like a handheld screen."),
  CHOICE("Scanlines:",            IDM_VIDEO_SCANLINE_BASE, it_scan, "Dark lines between rows: Off, 25, 50, 75 or 100%."),
#ifdef _WIN64
  { K_SHADER, "GPU shader:", 0, 0, NULL, "Run a librashader (.slangp) shader chain on top of the picture. Direct3D 11 only." },      /* along the bottom, under both columns */
#endif
};

static const SRow rows_audio[] =
{
  CHECK ("Mute",                  IDM_AUDIO_ENABLE, "Silence the sound; untick to hear it again."),
  CHOICE("FM chip:",              IDM_AUDIO_FMCORE_BASE, it_fm, "Which Genesis FM sound chip is emulated: YM2612 or YM3438, the Nuked versions (default YM2612 Nuked) or MAME."),
  CHOICE("Sample rate:",          IDM_AUDIO_RATE_BASE, it_rate, "Audio sample rate: 44100 or 48000 Hz."),
  CHECK ("High-Quality PSG Resampling", IDM_AUDIO_HQPSG, "Cleaner sound from the PSG (the console's square-wave chip)."),
  CHECK ("Low-Pass Filter",       IDM_AUDIO_LOWPASS, "A soft high-frequency roll-off."),
  CHECK ("Mono Output",           IDM_AUDIO_MONO, "Mix the sound to mono instead of stereo."),
  BUTTON("Levels and Latency...", IDM_AUDIO_SETTINGS, "Master volume, FM and PSG levels, CD audio, and how many frames of sound are buffered."),
  BUTTON("Advanced...",           IDM_AUDIO_ADVANCED, "Audio filter (Off, Low-Pass or 3-Band EQ), High-Quality FM, Master System FM, and Sega CD PCM volume."),
};

static const SRow rows_input[] =
{
  BUTTON("Controls...",  IDM_INPUT_P1, "Set keyboard keys and gamepad buttons for Player 1 and Player 2, the hotkeys and the stick deadzone."),
  CHOICE("Port A device:",        IDM_INPUT_PORTA_BASE, it_porta, "What is plugged into controller port A: a pad, mouse, light gun, Team Player and so on."),
  CHOICE("Port B device:",        IDM_INPUT_PORTB_BASE, it_portb, "What is plugged into controller port B: a pad, mouse, light gun, Team Player and so on."),
  CHECK ("Enable Background Input", IDM_INPUT_BACKGROUND, "Keep reading the keyboard pad keys and gamepads while another window has focus."),
};

static const SRow rows_options[] =
{
  CHOICE("Region:",               IDM_EMU_REGION_BASE, it_region, "Which region the console reports to the game: detect from the ROM, USA, Europe or Japan."),
  CHOICE("Force VDP mode:",       IDM_EMU_VDPMODE_BASE, it_vdp, "Run the video timing at 60 Hz (NTSC) or 50 Hz (PAL) whatever the region says."),
  CHOICE("Console:",              IDM_EMU_SYSTEM_BASE, it_console, "Detect the console from the game, or force a specific model."),
  CHOICE("Lock-on cartridge:",    IDM_EMU_LOCKON_BASE, it_lockon, "Attach a Game Genie, Action Replay or Sonic & Knuckles to the game."),
  CHECK ("Boot from BIOS When Available", IDM_EMU_BIOS, "Run the console's start-up ROM when a BIOS file is present in the bios folder."),
  CHECK ("Emulate Address Error Exceptions", IDM_EMU_ADDRERROR, "Hardware-accurate crashes on bad memory access; turn off only for a few hacked games."),
  CHECK ("Show Extended Game Gear Screen", IDM_VIDEO_GGEXTRA, "Show the whole Game Gear picture instead of only the part its screen displayed."),
  CHECK ("Show Master System Side Borders", IDM_VIDEO_SMSBORDER, "Show the 8-pixel columns at each side, which are cropped by default."),
  CHECK ("Show Frame Rate",       IDM_VIDEO_SHOWFPS, "Show frames per second on screen."),
};

#define PAGE(t, r) { t, r, (int)(sizeof(r) / sizeof(r[0])) }
static const SPage pages[] =
{
  PAGE("General",   rows_view),
  PAGE("Video",     rows_video),
  PAGE("Audio",     rows_audio),
  PAGE("Input",     rows_input),
  PAGE("Options",   rows_options),
};
#define NPAGES ((int)(sizeof(pages) / sizeof(pages[0])))

#define ROW_ID_BASE 4000
#define SHADER_EDIT_ID   5001      /* the shader box's file edit and its ... button */
#define SHADER_BROWSE_ID 5002
#define MAX_ROWS    96

static const SRow *s_row[MAX_ROWS];
static HWND s_ctl[MAX_ROWS];
static HWND s_page[NPAGES];
static int  s_nrows;

/****************************************************************************
 * Reading the menu
 ****************************************************************************/

/* State flags of the menu item with this command, wherever it is in the
   menus (looked for by hand, down through the submenus). */
static UINT find_state(HMENU menu, int id)
{
  int i, n = GetMenuItemCount(menu);

  for (i = 0; i < n; i++)
  {
    HMENU sub = GetSubMenu(menu, i);

    if (sub)
    {
      UINT st = find_state(sub, id);
      if (st != (UINT)-1) return st;
    }
    else if ((int)GetMenuItemID(menu, i) == id)
    {
      return GetMenuState(menu, (UINT)i, MF_BYPOSITION);
    }
  }
  return (UINT)-1;
}

static UINT menu_state(int id)
{
  return find_state(gui_main_menu(), id);
}

static int menu_checked(int id)
{
  UINT st = menu_state(id);
  return st != (UINT)-1 && (st & MF_CHECKED);
}

static void sync_shader(void);

static void sync_all(void)
{
  int i;

  for (i = 0; i < s_nrows; i++)
  {
    const SRow *r = s_row[i];
    HWND c = s_ctl[i];

    if (r->kind == K_CHECK)
    {
      UINT st = menu_state(r->id);
      SendMessage(c, BM_SETCHECK, (st != (UINT)-1 && (st & MF_CHECKED)) ? BST_CHECKED : BST_UNCHECKED, 0);
      EnableWindow(c, st == (UINT)-1 || !(st & (MF_GRAYED | MF_DISABLED)));
    }
    else if (r->kind == K_CHOICE)
    {
      int k, sel = -1;
      for (k = 0; k < r->n; k++)
        if (menu_checked(r->id + k)) { sel = k; break; }
      SendMessage(c, CB_SETCURSEL, (WPARAM)sel, 0);
    }
    else if (r->kind == K_FILTER)
    {
      int cur = video_filter_current();
      SendMessage(c, CB_SETCURSEL, (WPARAM)(cur >= 0 ? cur + 1 : 0), 0);
    }
    else if (r->kind == K_SHADER)
    {
      sync_shader();
    }
    else if (r->kind == K_BUTTON)
    {
      UINT st = menu_state(r->id);
      EnableWindow(c, st == (UINT)-1 || !(st & (MF_GRAYED | MF_DISABLED)));
    }
  }
}

/****************************************************************************
 * The GPU shader box: a frame with "Use Shader", and the preset file below
 * it (typed, or picked with the ... button). Turning it off keeps the file
 * in the box so it can be switched back on.
 ****************************************************************************/

static HWND s_shader_edit;         /* the file box */
static HWND s_shader_browse;       /* its ... button */
static HWND s_shader_check;
static RECT s_frame_rc;            /* the shader box's frame, in its page's coordinates */
static HWND s_frame_page;        /* "Use Shader" */
static char s_shader_last[GUI_PATH_LEN];

static void sync_shader(void)
{
  if (!s_shader_check) return;

  if (gui.shader_preset_path[0]) lstrcpynA(s_shader_last, gui.shader_preset_path, sizeof(s_shader_last));

  SendMessage(s_shader_check, BM_SETCHECK, gui.shader_preset_path[0] ? BST_CHECKED : BST_UNCHECKED, 0);
  SetWindowTextA(s_shader_edit, s_shader_last);

  /* The file box and its button are only usable while the shader is on. */
  EnableWindow(s_shader_edit, gui.shader_preset_path[0] != 0);
  EnableWindow(s_shader_browse, gui.shader_preset_path[0] != 0);
}

/****************************************************************************
 * The tab strip: drawn here, in the theme's colors (the system tab control
 * keeps a white body and frame in dark mode).
 ****************************************************************************/

#define TABS_CHANGED GP_TABS_CHANGED   /* notification code of WM_COMMAND, selection in the control's userdata */

static int tabs_hot = -1;

static COLORREF page_color(void);

static int tab_h(void) { return TBS(24); }      /* height of the tabs; the page frame starts here */

/* Classic tabs: a framed page with raised tabs on top, in the theme's colors. */
static void tabs_colors(COLORREF *face, COLORREF *page, COLORREF *idle, COLORREF *hot,
                        COLORREF *text, COLORREF *dim, COLORREF *border)
{
  int dark = theme_is_dark();
  *face   = dark ? RGB(32, 32, 32)    : GetSysColor(COLOR_BTNFACE);     /* behind the control */
  *page   = page_color();                                               /* the page and the selected tab */
  *idle   = dark ? RGB(44, 44, 44)    : RGB(226, 226, 226);             /* the other tabs */
  *hot    = dark ? RGB(58, 58, 58)    : RGB(238, 238, 238);             /* a tab under the mouse */
  *text   = dark ? RGB(240, 240, 240) : GetSysColor(COLOR_BTNTEXT);
  *dim    = dark ? RGB(175, 175, 175) : RGB(70, 70, 70);
  *border = dark ? RGB(95, 95, 95)    : RGB(160, 160, 160);
}

/* The strip shows the Settings pages unless a window gave it its own titles
   (settings_set_tabs). */
static int tab_count(HWND h)
{
  int n = (int)(INT_PTR)GetPropA(h, "gp_ntabs");
  return n ? n : NPAGES;
}

static const char *tab_title(HWND h, int i)
{
  const char *const *t = (const char *const *)GetPropA(h, "gp_tabs");
  return t ? t[i] : pages[i].title;
}

/* Width of tab i: its text and some room either side. */
static int tab_width(HWND h, HDC dc, int i)
{
  SIZE sz;
  GetTextExtentPoint32A(dc, tab_title(h, i), lstrlenA(tab_title(h, i)), &sz);
  return sz.cx + 2 * TBS(14);
}

#define TAB_LEFT TBS(2)           /* the first tab starts a little in from the frame */

static int tab_at(HWND h, int x, int y)
{
  HDC dc = GetDC(h);
  HFONT old = (HFONT)SelectObject(dc, (HFONT)SendMessage(GetParent(h), WM_GETFONT, 0, 0));
  int i, left = TAB_LEFT, hit = -1;

  if (y < tab_h())
    for (i = 0; i < tab_count(h); i++)
    {
      int w = tab_width(h, dc, i);
      if (x >= left && x < left + w) { hit = i; break; }
      left += w;
    }
  SelectObject(dc, old);
  ReleaseDC(h, dc);
  return hit;
}

/* Where tab `sel` is, from the strip's left edge: its page's frame leaves a gap
   there, so the tab and the page run into each other. Returns 0 without a strip. */
static HWND s_tabs;

static int tab_span(int sel, int *x0, int *x1)
{
  HDC dc;
  HFONT old;
  int i, left = TAB_LEFT;

  if (!s_tabs || sel < 0 || sel >= NPAGES) return 0;

  dc = GetDC(s_tabs);
  old = (HFONT)SelectObject(dc, (HFONT)SendMessage(GetParent(s_tabs), WM_GETFONT, 0, 0));
  for (i = 0; i < sel; i++) left += tab_width(s_tabs, dc, i);
  *x0 = left - TBS(1);
  *x1 = left + tab_width(s_tabs, dc, sel) + TBS(1);
  SelectObject(dc, old);
  ReleaseDC(s_tabs, dc);
  return 1;
}

static void tabs_select(HWND h, int sel)
{
  if (sel < 0 || sel >= tab_count(h) || sel == (int)GetWindowLongPtr(h, GWLP_USERDATA)) return;
  SetWindowLongPtr(h, GWLP_USERDATA, sel);
  InvalidateRect(h, NULL, FALSE);
  SendMessage(GetParent(h), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), TABS_CHANGED), (LPARAM)h);
}

static LRESULT CALLBACK tabs_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m)
  {
    case WM_GETDLGCODE:
      return DLGC_WANTARROWS;

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT:
    {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      RECT rc, r;
      COLORREF face, page, idle, hotc, text, dim, border;
      HBRUSH bface, bborder, bpage;
      HFONT old = (HFONT)SelectObject(dc, (HFONT)SendMessage(GetParent(h), WM_GETFONT, 0, 0));
      int i, left = TAB_LEFT, sel = (int)GetWindowLongPtr(h, GWLP_USERDATA), th = tab_h();

      GetClientRect(h, &rc);
      tabs_colors(&face, &page, &idle, &hotc, &text, &dim, &border);
      bface = CreateSolidBrush(face);
      bborder = CreateSolidBrush(border);
      bpage = CreateSolidBrush(page);

      FillRect(dc, &rc, bface);

      /* The line along the bottom of the strip: the top of the pages' frame
         continues it just below. */
      r = rc; r.top = r.bottom - 1;
      FillRect(dc, &r, bborder);

      SetBkMode(dc, TRANSPARENT);
      for (i = 0; i < tab_count(h); i++)
      {
        int tw = tab_width(h, dc, i);
        int is_sel = (i == sel);
        RECT t;
        HBRUSH bt;

        /* The selected tab is raised: taller, and open at the bottom so the
           page's frame does not run under it. */
        t.left = left; t.right = left + tw;
        t.top = is_sel ? 0 : TBS(3);
        t.bottom = th;
        if (is_sel) { t.left -= TBS(1); t.right += TBS(1); }
        if (is_sel) t.bottom = th;   /* its bottom row is page-colored below, the frame has a gap there */

        bt = CreateSolidBrush(is_sel ? page : (i == tabs_hot ? hotc : idle));
        FillRect(dc, &t, bborder);                        /* the tab's outline ... */
        { RECT in = t; in.left++; in.right--; in.top++; if (!is_sel) in.bottom--; FillRect(dc, &in, bt); }   /* ... and its face */
        DeleteObject(bt);

        SetTextColor(dc, is_sel ? text : dim);
        { RECT tx = t; if (!is_sel) tx.top += 1; DrawTextA(dc, tab_title(h, i), -1, &tx, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX); }

        if (is_sel && GetFocus() == h)                    /* the dotted focus rectangle, as in classic tabs */
        {
          RECT f = t;
          InflateRect(&f, -TBS(4), -TBS(4));
          DrawFocusRect(dc, &f);
        }
        left += tw;
      }

      DeleteObject(bface); DeleteObject(bborder); DeleteObject(bpage);
      SelectObject(dc, old);
      EndPaint(h, &ps);
      return 0;
    }

    case WM_LBUTTONDOWN:
      SetFocus(h);
      tabs_select(h, tab_at(h, (short)LOWORD(l), (short)HIWORD(l)));
      return 0;

    case WM_MOUSEMOVE:
    {
      int hot = tab_at(h, (short)LOWORD(l), (short)HIWORD(l));
      if (hot != tabs_hot)
      {
        TRACKMOUSEEVENT te;
        te.cbSize = sizeof(te); te.dwFlags = TME_LEAVE; te.hwndTrack = h; te.dwHoverTime = 0;
        TrackMouseEvent(&te);
        tabs_hot = hot;
        InvalidateRect(h, NULL, FALSE);
      }
      return 0;
    }

    case WM_MOUSELEAVE:
      tabs_hot = -1;
      InvalidateRect(h, NULL, FALSE);
      return 0;

    case WM_KEYDOWN:
      if (w == VK_LEFT)  tabs_select(h, (int)GetWindowLongPtr(h, GWLP_USERDATA) - 1);
      if (w == VK_RIGHT) tabs_select(h, (int)GetWindowLongPtr(h, GWLP_USERDATA) + 1);
      return 0;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      InvalidateRect(h, NULL, FALSE);
      return 0;
  }
  return DefWindowProc(h, m, w, l);
}

/****************************************************************************
 * Building the pages
 ****************************************************************************/

static COLORREF page_color(void)
{
  return theme_is_dark() ? RGB(32, 32, 32) : GetSysColor(COLOR_BTNFACE);
}

/* A page is a plain window that paints its own background and hands what its
   controls send to the dialog. */
static LRESULT CALLBACK page_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m)
  {
    case WM_ERASEBKGND:
    case WM_PRINTCLIENT:
    {
      RECT r;
      HBRUSH b = CreateSolidBrush(page_color());
      GetClientRect(h, &r);
      FillRect((HDC)w, &r, b);
      DeleteObject(b);

      {   /* The page's frame; its top edge has a gap under the selected tab. */
        COLORREF face, pg, idle, hotc, text, dim, border;
        HBRUSH bb;
        RECT l;
        int x0, x1;

        tabs_colors(&face, &pg, &idle, &hotc, &text, &dim, &border);
        bb = CreateSolidBrush(border);

        SetRect(&l, 0, r.bottom - 1, r.right, r.bottom);  FillRect((HDC)w, &l, bb);   /* bottom */
        SetRect(&l, 0, 0, 1, r.bottom);                   FillRect((HDC)w, &l, bb);   /* left */
        SetRect(&l, r.right - 1, 0, r.right, r.bottom);   FillRect((HDC)w, &l, bb);   /* right */

        if (tab_span((int)GetWindowLongPtr(s_tabs, GWLP_USERDATA), &x0, &x1))
        {
          SetRect(&l, 0, 0, x0 + 1, 1);                   FillRect((HDC)w, &l, bb);
          SetRect(&l, x1 - 1, 0, r.right, 1);             FillRect((HDC)w, &l, bb);
        }
        else
        {
          SetRect(&l, 0, 0, r.right, 1);                  FillRect((HDC)w, &l, bb);
        }
        DeleteObject(bb);
      }

      if (h == s_frame_page && !IsRectEmpty(&s_frame_rc))   /* the shader box's frame */
      {
        HPEN pen = CreatePen(PS_SOLID, 1, theme_is_dark() ? RGB(85, 85, 85) : RGB(205, 205, 205));
        HGDIOBJ op = SelectObject((HDC)w, pen), ob = SelectObject((HDC)w, GetStockObject(NULL_BRUSH));
        RoundRect((HDC)w, s_frame_rc.left, s_frame_rc.top, s_frame_rc.right, s_frame_rc.bottom, TBS(6), TBS(6));
        SelectObject((HDC)w, op); SelectObject((HDC)w, ob);
        DeleteObject(pen);
      }
      return 1;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
      HBRUSH br;

      /* The shader file box is #333333 in dark mode, a shade lighter than the page
         (also while it is grayed, when it is asked for as a static). */
      if ((HWND)l == s_shader_edit && s_shader_edit)
      {
        br = theme_ctlcolor_custom((HDC)w, RGB(0x33, 0x33, 0x33), 2);
        if (br) return (LRESULT)br;
      }

      br = theme_ctlcolor((HDC)w);
      if (br) return (LRESULT)br;

      /* Light theme: text boxes and drop-down lists keep their normal white. */
      if (m == WM_CTLCOLOREDIT || m == WM_CTLCOLORLISTBOX) return DefWindowProc(h, m, w, l);

      SetBkColor((HDC)w, GetSysColor(COLOR_BTNFACE));
      SetTextColor((HDC)w, GetSysColor(COLOR_BTNTEXT));
      return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_COMMAND:
      return SendMessage(GetParent(h), m, w, l);
  }
  return DefWindowProc(h, m, w, l);
}

static int row_span(const SRow *r)
{
  return (r->kind == K_SHADER || r->kind == K_BREAK) ? 0 : 1;     /* the shader box sits under the columns, outside them */
}

/* One tooltip window serves every control; it hides itself after 30 seconds. */
static HWND s_tip;
static int s_start_tab;          /* which tab the dialog opens on */
#define TIP_AUTOPOP_MS 30000

static void add_tip(HWND dlg, HWND ctl, const char *text, HFONT font)
{
  TOOLINFOA ti;

  if (!ctl || !text) return;
  if (!s_tip)
  {
    s_tip = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                            0, 0, 0, 0, dlg, NULL, g_inst, NULL);
    if (!s_tip) return;
    SendMessage(s_tip, TTM_SETMAXTIPWIDTH, 0, TBS(320));
    SendMessage(s_tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, TIP_AUTOPOP_MS);
    SendMessage(s_tip, TTM_SETDELAYTIME, TTDT_INITIAL, 400);
    SendMessage(s_tip, WM_SETFONT, (WPARAM)font, FALSE);
  }
  memset(&ti, 0, sizeof(ti));
  ti.cbSize = sizeof(ti);
  ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  ti.hwnd = dlg;
  ti.uId = (UINT_PTR)ctl;
  ti.lpszText = (LPSTR)text;
  SendMessageA(s_tip, TTM_ADDTOOLA, 0, (LPARAM)&ti);
}

static void build_page(HWND dlg, int p, const RECT *rc, HFONT font)
{
  const SPage *pg = &pages[p];
  int w = rc->right - rc->left, h = rc->bottom - rc->top;
  int m = TBS(12), rowh = TBS(30), total = 0, cols, per_col;
  int colw, i, col = 0, slot = 0, maxslot = 0;

  for (i = 0; i < pg->count; i++) total += row_span(&pg->rows[i]);
  cols = (total > 8) ? 2 : 1;
  per_col = (total + cols - 1) / cols;
  colw = (w - 2 * m - (cols - 1) * TBS(16)) / cols;

  s_page[p] = CreateWindowExA(WS_EX_CONTROLPARENT, "GPSettingsPage", "",
                              WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | (p == 0 ? WS_VISIBLE : 0),
                              rc->left, rc->top, w, h, dlg, NULL, g_inst, NULL);

  for (i = 0; i < pg->count && s_nrows < MAX_ROWS; i++)
  {
    const SRow *r = &pg->rows[i];
    int span = row_span(r), x, y, idx;
    HWND c = NULL;

    if (r->kind == K_BREAK) { if (col + 1 < cols) { col++; slot = 0; } continue; }
    idx = s_nrows++;

    if (slot + span > per_col && slot > 0 && col + 1 < cols) { col++; slot = 0; }   /* next column */
    x = m + col * (colw + TBS(16));
    y = m + slot * rowh;
    slot += span;
    if (slot > maxslot) maxslot = slot;

    s_row[idx] = r;

    if (r->kind == K_CHECK)
    {
      c = CreateWindowExA(0, "BUTTON", r->label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                          x, y, colw, TBS(22), s_page[p], (HMENU)(INT_PTR)(ROW_ID_BASE + idx), g_inst, NULL);
    }
    else if (r->kind == K_BUTTON)
    {
      c = CreateWindowExA(0, "BUTTON", r->label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                          x, y, colw * 3 / 4, TBS(24), s_page[p], (HMENU)(INT_PTR)(ROW_ID_BASE + idx), g_inst, NULL);
    }
    else if (r->kind == K_SHADER)
    {
      /* Under both columns: a frame with "Use Shader" and, below it, the
         file box and its ... button. */
      int colw = w - 2 * m;                 /* (the full width, not a column's) */
      int bh = TBS(68), bw = TBS(30);

      x = m;
      y = m + maxslot * rowh + TBS(6);

      /* The frame is painted by the page itself (see page_proc), so its line
         can be a quiet gray in dark mode; a group box draws it bright white. */
      SetRect(&s_frame_rc, x, y, x + colw, y + bh);
      s_frame_page = s_page[p];

      c = CreateWindowExA(0, "BUTTON", "Use Shader", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                          x + TBS(10), y + TBS(10), TBS(120), TBS(20), s_page[p], (HMENU)(INT_PTR)(ROW_ID_BASE + idx), g_inst, NULL);
      s_shader_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                      x + TBS(10), y + TBS(36), colw - TBS(20) - bw - TBS(6), TBS(23), s_page[p],
                                      (HMENU)(INT_PTR)(SHADER_EDIT_ID), g_inst, NULL);
      s_shader_browse = CreateWindowExA(0, "BUTTON", "...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                        x + colw - TBS(10) - bw, y + TBS(35), bw, TBS(25), s_page[p],
                                        (HMENU)(INT_PTR)(SHADER_BROWSE_ID), g_inst, NULL);
      s_shader_check = c;

      SendMessage(s_shader_edit, WM_SETFONT, (WPARAM)font, TRUE);
      SendMessage(s_shader_browse, WM_SETFONT, (WPARAM)font, TRUE);
      add_tip(dlg, s_shader_edit, "The shader preset (.slangp) to run.", font);
      add_tip(dlg, s_shader_browse, "Choose a shader preset file.", font);
    }
    else   /* K_CHOICE, K_FILTER */
    {
      int lw = colw * 2 / 5, k;
      HWND lab = CreateWindowExA(0, "STATIC", r->label, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY,
                                 x, y + TBS(4), lw - TBS(6), TBS(18), s_page[p], NULL, g_inst, NULL);
      SendMessage(lab, WM_SETFONT, (WPARAM)font, TRUE);
      add_tip(dlg, lab, r->tip, font);

      c = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                          x + lw, y, colw - lw, TBS(240), s_page[p], (HMENU)(INT_PTR)(ROW_ID_BASE + idx), g_inst, NULL);
      SendMessage(c, WM_SETFONT, (WPARAM)font, TRUE);

      if (r->kind == K_FILTER)
      {
        int n = video_filter_count();
        SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)"None");
        for (k = 0; k < n; k++) SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)video_filter_name(k));
      }
      else
      {
        for (k = 0; k < r->n; k++) SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)r->items[k]);
      }
    }

    s_ctl[idx] = c;
    add_tip(dlg, c, r->tip, font);
    if (c && r->kind != K_CHOICE && r->kind != K_FILTER) SendMessage(c, WM_SETFONT, (WPARAM)font, TRUE);
  }
}

/****************************************************************************
 * The dialog
 ****************************************************************************/

static void show_page(int p)
{
  int i;
  for (i = 0; i < NPAGES; i++) ShowWindow(s_page[i], i == p ? SW_SHOW : SW_HIDE);
}

/* Puts every setting on these tabs back to its default. The defaults are read
   the way the rows are: set_config_defaults() is run for a moment, the menu
   marks it gives are noted, the real values are put back, and then each
   row that differs is changed with the same command the menu sends -- so
   every change takes effect exactly as if it had been made by hand.
   Controls, the ROM folder, cheats and the window's place are left alone. */
static void restore_defaults(HWND dlg)
{
  int def[MAX_ROWS], i, k, theme_before = gui.theme_mode;
  t_gui_config og = gui;
  t_config oc = config;
  uint8 os0 = input.system[0], os1 = input.system[1];

  if (MessageBoxA(dlg, "Put every setting in this window back to its default?\\n\\n"
                       "Your controls, ROM folder and cheats are kept.",
                  "Restore Defaults", MB_YESNO | MB_ICONQUESTION) != IDYES)
    return;

  set_config_defaults();
  gui_update_menu();
  for (i = 0; i < s_nrows; i++)
  {
    const SRow *r = s_row[i];
    def[i] = 0;
    if (r->kind == K_CHECK) def[i] = menu_checked(r->id) ? 1 : 0;
    else if (r->kind == K_CHOICE)
      for (k = 0; k < r->n; k++)
        if (menu_checked(r->id + k)) { def[i] = k; break; }
    if (r->kind == K_CHECK && r->id == IDM_VIEW_SEARCHBAR) def[i] = 0;
    if (r->kind == K_CHECK && ((r->id > IDM_VIEW_COL_BASE && r->id < IDM_VIEW_COL_BASE + 4) || r->id == IDM_VIEW_COL_FAV)) def[i] = 1;   /* every column shown */   /* off by default, whatever the menu reads from (Ctrl+F can force it on) */
  }

  gui = og;
  config = oc;
  input.system[0] = os0;
  input.system[1] = os1;
  gui_update_menu();

  for (i = 0; i < s_nrows; i++)
  {
    const SRow *r = s_row[i];

    if (r->kind == K_CHECK)
    {
      if ((menu_checked(r->id) ? 1 : 0) != def[i]) SendMessage(g_hwnd, WM_COMMAND, MAKEWPARAM(r->id, 0), 0);
    }
    else if (r->kind == K_CHOICE)
    {
      int cur = -1;
      for (k = 0; k < r->n; k++)
        if (menu_checked(r->id + k)) { cur = k; break; }
      if (cur != def[i]) SendMessage(g_hwnd, WM_COMMAND, MAKEWPARAM(r->id + def[i], 0), 0);
    }
    else if (r->kind == K_FILTER)
    {
      if (video_filter_current() >= 0) SendMessage(g_hwnd, WM_COMMAND, MAKEWPARAM(IDM_VIDEO_FILTER_NONE, 0), 0);
    }
    else if (r->kind == K_SHADER)
    {
      if (gui.shader_preset_path[0]) gui_set_shader(NULL);
    }
  }

  if (gui.theme_mode != theme_before) PostMessage(dlg, WM_APP + 1, 0, 0);
  sync_all();
}

static INT_PTR CALLBACK settings_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
  {
    case WM_INITDIALOG:
    {
      HWND tab = GetDlgItem(dlg, IDC_SETTINGS_TAB);
      HFONT font = (HFONT)SendMessage(dlg, WM_GETFONT, 0, 0);
      RECT rc, trc;
      int p;
      s_nrows = 0;
      s_tip = NULL;
      tabs_hot = -1;
      SetRectEmpty(&s_frame_rc);
      s_frame_page = NULL;

      /* The tab strip is as tall as its tabs; the pages (each with its own
         frame) fill the rest, down to the Close button. */
      {
        RECT btn;
        s_tabs = tab;
        GetWindowRect(tab, &trc);
        GetWindowRect(GetDlgItem(dlg, IDOK), &btn);
        MapWindowPoints(NULL, dlg, (POINT *)&trc, 2);
        MapWindowPoints(NULL, dlg, (POINT *)&btn, 2);
        MoveWindow(tab, trc.left, trc.top, trc.right - trc.left, tab_h(), TRUE);
        rc.left = trc.left;
        rc.right = trc.right;
        rc.top = trc.top + tab_h();
        rc.bottom = btn.top - TBS(8);
      }

      for (p = 0; p < NPAGES; p++) build_page(dlg, p, &rc, font);

      /* Restore Defaults sits at the left, level with Close. */
      {
        RECT btn;
        HWND rd;
        GetWindowRect(GetDlgItem(dlg, IDOK), &btn);
        MapWindowPoints(NULL, dlg, (POINT *)&btn, 2);
        rd = CreateWindowExA(0, "BUTTON", "Restore Defaults", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                             trc.left, btn.top, (btn.right - btn.left) * 5 / 2, btn.bottom - btn.top, dlg,
                             (HMENU)(INT_PTR)IDC_SETTINGS_DEFAULTS, g_inst, NULL);
        SendMessage(rd, WM_SETFONT, (WPARAM)font, TRUE);
        add_tip(dlg, rd, "Put every setting in this window back to its default. Controls, ROM folder and cheats are kept.", font);
      }
      if (s_start_tab) { SetWindowLongPtr(tab, GWLP_USERDATA, s_start_tab); show_page(s_start_tab); }

      theme_apply_to_window(dlg);
      sync_all();
      return TRUE;
    }

    case WM_APP + 1:   /* re-theme after the theme was changed here */
    {
      int i;

      theme_apply_to_window(dlg);

      /* Every combo box gets its frame and contents drawn again. */
      for (i = 0; i < s_nrows; i++)
        if (s_ctl[i] && (s_row[i]->kind == K_CHOICE || s_row[i]->kind == K_FILTER))
        {
          SetWindowPos(s_ctl[i], NULL, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
          RedrawWindow(s_ctl[i], NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
        }

      RedrawWindow(dlg, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
      return TRUE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
    {
      HBRUSH br = theme_ctlcolor((HDC)wp);
      if (br) return (LRESULT)br;
      break;
    }

    case WM_COMMAND:
    {
      int id = LOWORD(wp), code = HIWORD(wp);

      if (id == IDC_SETTINGS_TAB && code == TABS_CHANGED)
      {
        show_page((int)GetWindowLongPtr((HWND)lp, GWLP_USERDATA));
        return TRUE;
      }

      if (id >= ROW_ID_BASE && id < ROW_ID_BASE + s_nrows)
      {
        const SRow *r = s_row[id - ROW_ID_BASE];

        if (r->kind == K_CHECK && code == BN_CLICKED)
        {
          SendMessage(g_hwnd, WM_COMMAND, MAKEWPARAM(r->id, 0), 0);
          sync_all();
        }
        else if ((r->kind == K_CHOICE || r->kind == K_FILTER) && code == CBN_SELCHANGE)
        {
          int sel = (int)SendMessage((HWND)lp, CB_GETCURSEL, 0, 0), cmd;

          if (sel >= 0)
          {
            if (r->kind == K_FILTER) cmd = sel == 0 ? IDM_VIDEO_FILTER_NONE : IDM_VIDEO_FILTER_BASE + sel - 1;
            else                     cmd = r->id + sel;

            SendMessage(g_hwnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);

            /* The theme changes this window too -- but not from inside the
               drop-down's own notification: re-theming a combo box while it is
               still closing leaves it blank until the mouse passes over it. */
            if (r->id == IDM_VIDEO_THEME_BASE) PostMessage(dlg, WM_APP + 1, 0, 0);
            sync_all();
          }
        }
        else if (r->kind == K_SHADER && code == BN_CLICKED)
        {
          if (SendMessage(s_shader_check, BM_GETCHECK, 0, 0) == BST_CHECKED)
          {
            char path[GUI_PATH_LEN];
            GetWindowTextA(s_shader_edit, path, sizeof(path));

            if (path[0]) gui_set_shader(path);
            else         SendMessage(dlg, WM_COMMAND, MAKEWPARAM(SHADER_BROWSE_ID, BN_CLICKED), 0);
          }
          else
          {
            gui_set_shader(NULL);
          }
          sync_shader();
        }
        else if (r->kind == K_BUTTON && code == BN_CLICKED)
        {
          /* The other window opens on top of this one, which waits (and
             stays open) until it is closed. */
          EnableWindow(dlg, FALSE);
          SendMessage(g_hwnd, WM_COMMAND, MAKEWPARAM(r->id, 0), 0);
          EnableWindow(dlg, TRUE);
          SetForegroundWindow(dlg);
          sync_all();
        }
        return TRUE;
      }

      if (id == SHADER_BROWSE_ID && code == BN_CLICKED)
      {
        EnableWindow(dlg, FALSE);
        SendMessage(g_hwnd, WM_COMMAND, MAKEWPARAM(IDM_VIDEO_SHADER_PRESET, 0), 0);   /* the file picker, then loads it */
        EnableWindow(dlg, TRUE);
        SetForegroundWindow(dlg);
        sync_shader();
        return TRUE;
      }

      if (id == IDC_SETTINGS_DEFAULTS && code == BN_CLICKED)
      {
        restore_defaults(dlg);
        return TRUE;
      }

      if (id == IDOK || id == IDCANCEL)
      {
        EndDialog(dlg, id);
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

/* For another window that wants the same strip (the controls window). */
int settings_tab_height(void) { return tab_h(); }

void settings_set_tabs(HWND tab, const char *const *titles, int n)
{
  SetPropA(tab, "gp_tabs", (HANDLE)titles);
  SetPropA(tab, "gp_ntabs", (HANDLE)(INT_PTR)n);
}

void settings_register_classes(void)
{
  WNDCLASSA wc;

  /* Both window classes have to exist before a dialog is created from a
     template that names the tab strip. */
  ZeroMemory(&wc, sizeof(wc));
  wc.hInstance = g_inst;
  wc.hCursor   = LoadCursor(NULL, IDC_ARROW);

  wc.lpfnWndProc   = page_proc;
  wc.lpszClassName = "GPSettingsPage";
  RegisterClassA(&wc);

  wc.lpfnWndProc   = tabs_proc;
  wc.lpszClassName = "GPSettingsTabs";
  RegisterClassA(&wc);
}

void dlg_settings_tab(HWND parent, int tab)
{
  s_start_tab = (tab > 0 && tab < NPAGES) ? tab : 0;
  settings_register_classes();
  gui_dialog_box(gui.large_ui ? IDD_SETTINGS_LARGE : IDD_SETTINGS, parent, settings_proc);
  gui_update_menu();
}

void dlg_settings(HWND parent)
{
  dlg_settings_tab(parent, 0);
}
