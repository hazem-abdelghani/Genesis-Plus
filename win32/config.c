/****************************************************************************
 *  Genesis Plus -- Win32 GUI frontend
 *
 *  config.c -- default settings and INI persistence.
 *
 *  Settings live in genesis-plus.ini next to the executable, so the whole thing stays
 *  portable: copy the folder to a USB stick and the configuration travels
 *  with it.
 ****************************************************************************/

#include <windows.h>
#include <shlwapi.h>

#include "shared.h"
#include "gui.h"

t_config     config;
t_gui_config gui;

/* XInput button masks, declared here so config.c does not need xinput.h. */
#define XPAD_DPAD_UP        0x0001
#define XPAD_DPAD_DOWN      0x0002
#define XPAD_DPAD_LEFT      0x0004
#define XPAD_DPAD_RIGHT     0x0008
#define XPAD_START          0x0010
#define XPAD_BACK           0x0020
#define XPAD_LEFT_SHOULDER  0x0100
#define XPAD_RIGHT_SHOULDER 0x0200
#define XPAD_A              0x1000
#define XPAD_B              0x2000
#define XPAD_X              0x4000
#define XPAD_Y              0x8000
#define XPAD_LEFT_TRIGGER   0x00010000
#define XPAD_RIGHT_TRIGGER  0x00020000

static char ini_path[GUI_PATH_LEN];

static const char *pad_key_names[PAD_KEYS] =
{
  "up", "down", "left", "right",
  "a", "b", "c", "start",
  "x", "y", "z", "mode"
};

/****************************************************************************
 * Defaults
 ****************************************************************************/

static void set_pad_defaults(int player)
{
  t_pad_map *p = &gui.pad[player];
  int i;

  p->turbo = 0;
  for (i = 0; i < PAD_KEYS; i++)
  {
    p->key[i] = 0;
  }

  /* A modern pad laid out the way a 6-button Control Pad expects. */
  p->button[PAD_UP]    = XPAD_DPAD_UP;
  p->button[PAD_DOWN]  = XPAD_DPAD_DOWN;
  p->button[PAD_LEFT]  = XPAD_DPAD_LEFT;
  p->button[PAD_RIGHT] = XPAD_DPAD_RIGHT;
  p->button[PAD_A]     = XPAD_X;
  p->button[PAD_B]     = XPAD_A;
  p->button[PAD_C]     = XPAD_B;
  p->button[PAD_START] = XPAD_START;
  p->button[PAD_X]     = XPAD_Y;
  p->button[PAD_Y]     = XPAD_RIGHT_SHOULDER;
  p->button[PAD_Z]     = XPAD_RIGHT_TRIGGER;
  p->button[PAD_MODE]  = XPAD_BACK;

  p->device = player;
  p->joy_name[0] = '\0';

  if (player == 0)
  {
    p->key[PAD_UP]    = VK_UP;
    p->key[PAD_DOWN]  = VK_DOWN;
    p->key[PAD_LEFT]  = VK_LEFT;
    p->key[PAD_RIGHT] = VK_RIGHT;
    p->key[PAD_A]     = 'Z';
    p->key[PAD_B]     = 'X';
    p->key[PAD_C]     = 'C';
    p->key[PAD_START] = VK_RETURN;
    p->key[PAD_X]     = 'A';
    p->key[PAD_Y]     = 'S';
    p->key[PAD_Z]     = 'D';
    p->key[PAD_MODE]  = VK_RSHIFT;
  }
}

void set_config_defaults(void)
{
  int i;

  /* --- sound --- */
  config.psg_preamp     = 150;
  config.fm_preamp      = 100;
  config.cdda_volume    = 100;
  config.pcm_volume     = 100;
  config.hq_fm          = 1;
  config.hq_psg         = 1;
  config.filter         = 1;
  config.low_freq       = 200;
  config.high_freq      = 8000;
  config.lg             = 100;
  config.mg             = 100;
  config.hg             = 100;
  config.lp_range       = 0x9999;
  config.ym2612         = YM2612_DISCRETE;
  config.ym2413         = 2;
  config.ym3438         = 1;   /* Nuked */
  config.opll           = 0;
  config.mono           = 0;

  /* --- system --- */
  config.system         = 0;
  config.region_detect  = 0;
  config.vdp_mode       = 0;
  config.master_clock   = 0;
  config.force_dtack    = 0;
  config.addr_error     = 1;
  config.bios           = 0;
  config.lock_on        = 0;
  config.add_on         = 0;
  config.cd_latency     = 1;
  config.hot_swap       = 0;

  /* --- display --- */
  config.overscan       = 0;
  config.gg_extra       = 0;
  config.render         = 0;   /* single field: half height. Was double field (both fields, full
                                   height) by default before; changed on request. */
  config.ntsc           = 0;
  config.lcd            = 0;
  config.enhanced_vscroll = 0;
  config.enhanced_vscroll_limit = 8;

  /* --- controllers --- */
  input.system[0]       = SYSTEM_GAMEPAD;
  input.system[1]       = SYSTEM_GAMEPAD;
  config.gun_cursor[0]  = 1;
  config.gun_cursor[1]  = 1;
  config.invert_mouse   = 0;
  for (i = 0; i < MAX_INPUTS; i++)
  {
    config.input[i].padtype = DEVICE_PAD2B | DEVICE_PAD3B | DEVICE_PAD6B;
  }

  /* --- frontend --- */
  gui.scale               = 3;
  gui.aspect              = ASPECT_43;
  gui.smooth              = 0;
  gui.scanline_pct        = 0;
  gui.brighten            = 0;
  gui.background_input    = 0;
  gui.frameskip           = 0;
  gui.shot_mode           = 2;
  gui.sms_show_border     = 0;
  gui.nuked_ym2612        = 1;   /* as a YM2612 */
  gui.np_port             = 55455;
  gui.np_delay            = 2;
  gui.np_addr[0]          = '\0';
  gui.filter_console[0] = gui.filter_genre[0] = gui.filter_region[0] = '\0';
  gui.filter_genre_on = 0;
  gui.browser_col_hidden  = 0;
  gui.browser_col_extra   = 0;
  gui.clean_names         = 0;
  gui.browser_gridlines   = 0;
  gui.scan_subfolders     = 1;
  gui.browser_sort_column = 0;   /* Name */
  gui.browser_sort_ascending = 1;
  gui.browser_col_pct[0] = 64;
  gui.browser_col_pct[1] = 36;
  gui.renderer            = RENDERER_D3D11;
  gui.vsync               = 0;
  gui.theme_mode          = 0;   /* THEME_AUTO */
  gui.large_ui            = 0;
  gui.cheats_enabled      = 0;
  gui.browser_grid_view   = 0;
  gui.hide_toolbar        = 0;
  gui.hide_searchbar      = 1;   /* the search bar and the filter bar start switched off */
  gui.hide_filterbar      = 1;
  gui.browser_grid_size   = 96;
  gui.deadzone[0]         = 30;   /* close to this app's own prior fixed threshold (XPAD_DEADZONE
                                      10000 out of a possible 32767, about 30%), so upgrading
                                      changes nothing by default -- only touching the slider does */
  gui.deadzone[1]         = 30;
  gui.fullscreen          = 0;
  gui.fullscreen_on_load  = 0;
  gui.show_fps            = 0;
  gui.favorites_only      = 0;
  gui.recent_mode         = 0;
  gui.fullscreen_toolbar  = 0;
  gui.integer_scale       = 0;
  { int k; for (k = 0; k < padhk_count(); k++) gui.padhk[k] = padhk_default(k); }
  gui.slowmo              = 0;
  {
    int k;
    for (k = 0; k < hotkeys_count(); k++) gui.hotkey[k] = hotkey_default(k);
  }
  gui.mute_fm             = 0;
  gui.mute_psg            = 0;
  gui.autosave            = 0;
  gui.resume_choice       = 0;
  gui.osd                 = 1;

  /*
   * Sentinel for "never saved". GetPrivateProfileIntA is documented to
   * return UINT, and relying on a value at the very edge of the int range
   * to round-trip through that is asking for trouble; -32000 is far enough
   * from any real coordinate to be unambiguous, and matches the value
   * Windows itself uses for an off-screen/iconic window position.
   */
  gui.window_x            = -32000;
  gui.window_y            = -32000;
  gui.window_maximized    = 0;

  gui.render_filter[0]    = '\0';
  gui.sound_enabled       = 1;
  gui.volume              = 100;
  gui.sample_rate         = 48000;
  gui.latency             = 4;
  gui.rewind              = 1;
  gui.pause_on_focus_loss = 0;
  gui.always_on_top       = 0;
  gui.fast_forward_ratio  = 4;
  gui.state_slot          = 0;
  gui.rom_dir[0]          = '\0';
  gui.shader_preset_path[0] = '\0';

  for (i = 0; i < GUI_RECENT_MAX; i++)
  {
    gui.recent[i][0] = '\0';
  }

  set_pad_defaults(0);
  set_pad_defaults(1);
}

/****************************************************************************
 * INI helpers
 ****************************************************************************/

static void ini_init_path(void)
{
  if (ini_path[0] == '\0')
  {
    lstrcpynA(ini_path, osd_path("genesis-plus.ini"), sizeof(ini_path));
  }
}

static int ini_get(const char *section, const char *key, int fallback)
{
  return (int)GetPrivateProfileIntA(section, key, fallback, ini_path);
}

/* Saving collects every setting in memory and then writes the whole file in
   one go (to a temporary file that then replaces genesis-plus.ini). Writing each key
   with WritePrivateProfileString rewrote the file about 130 times per save, and
   a crash part-way through could leave it half written. */
#define INI_MAX_ITEMS 3000

typedef struct
{
  char section[24];
  char key[40];
  char value[GUI_PATH_LEN];
} ini_item;

#define GUI_FAV_MAX 400
static char fav_list[GUI_FAV_MAX][GUI_PATH_LEN];
static int  fav_n;

static ini_item *ini_q;
static int       ini_n;

static void ini_queue_reset(void)
{
  if (!ini_q) ini_q = (ini_item *)malloc(sizeof(ini_item) * INI_MAX_ITEMS);
  ini_n = 0;
}

static void ini_put_str(const char *section, const char *key, const char *value)
{
  ini_item *it;

  if (!ini_q || ini_n >= INI_MAX_ITEMS) return;
  it = &ini_q[ini_n++];
  lstrcpynA(it->section, section, sizeof(it->section));
  lstrcpynA(it->key, key, sizeof(it->key));
  lstrcpynA(it->value, value ? value : "", sizeof(it->value));
}

static void ini_put(const char *section, const char *key, int value)
{
  char buf[32];
  wsprintfA(buf, "%d", value);
  ini_put_str(section, key, buf);
}

/* Writes the queued settings, grouped by section in the order the sections
   first appeared. On any failure the old file is left as it was. */
static void ini_flush(void)
{
  char tmp[GUI_PATH_LEN + 8];
  FILE *fp;
  int i, j, ok = 1;

  if (!ini_q) return;

  wsprintfA(tmp, "%s.tmp", ini_path);
  fp = fopen(tmp, "wb");
  if (!fp) return;

  for (i = 0; i < ini_n && ok; i++)
  {
    int seen = 0;

    for (j = 0; j < i; j++)
      if (!lstrcmpA(ini_q[j].section, ini_q[i].section)) { seen = 1; break; }
    if (seen) continue;

    if (i > 0) ok = ok && fputs("\r\n", fp) >= 0;
    ok = ok && fprintf(fp, "[%s]\r\n", ini_q[i].section) >= 0;

    for (j = i; j < ini_n && ok; j++)
    {
      if (lstrcmpA(ini_q[j].section, ini_q[i].section)) continue;
      ok = ok && fprintf(fp, "%s=%s\r\n", ini_q[j].key, ini_q[j].value) >= 0;
    }
  }

  if (fflush(fp) != 0) ok = 0;
  if (fclose(fp) != 0) ok = 0;

  if (!ok || !MoveFileExA(tmp, ini_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    DeleteFileA(tmp);
}

static void ini_get_str(const char *section, const char *key, char *dst, int len)
{
  GetPrivateProfileStringA(section, key, "", dst, len, ini_path);
}

/* Deleting a key: settings are written afresh each time, so a key that is not
   put is simply gone. */
static void ini_del(const char *section, const char *key)
{
  (void)section;
  (void)key;
}

static int clampi(int v, int lo, int hi)
{
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

/****************************************************************************
 * Load / save
 ****************************************************************************/

void config_load(void)
{
  char key[64];
  char buf[GUI_PATH_LEN];
  int player, i;

  set_config_defaults();
  ini_init_path();

  if (GetFileAttributesA(ini_path) == INVALID_FILE_ATTRIBUTES)
  {
    /* First run: keep the defaults and write them out so the file exists. */
    config_save();
    return;
  }

  /* --- video --- */
  gui.scale      = clampi(ini_get("video", "scale", gui.scale), 1, 6);
  gui.aspect     = clampi(ini_get("video", "aspect", gui.aspect), 0, 2);
  gui.smooth     = ini_get("video", "smooth", gui.smooth) ? 1 : 0;
  gui.scanline_pct = clampi(ini_get("video", "scanline_pct", gui.scanline_pct), 0, 100);
  gui.brighten   = ini_get("video", "brighten", gui.brighten) ? 1 : 0;
  gui.background_input = ini_get("input", "background_input", gui.background_input) ? 1 : 0;
  gui.frameskip        = clampi(ini_get("video", "frameskip", gui.frameskip), 0, 5);
  gui.shot_mode        = clampi(ini_get("video", "screenshot_output", gui.shot_mode), 0, 2);
  /* Was stored the other way round ("sms_hide_side_border") until the option
     was inverted; carry an old "hide" choice over. */
  gui.sms_show_border  = ini_get("video", "sms_show_side_border",
                                 ini_get("video", "sms_hide_side_border", 0) ? 0 : gui.sms_show_border) ? 1 : 0;
  /* The side borders are now off by default. Settings saved before that hold
     the old default (on) whether or not it was a choice, so move them across
     once; anything chosen afterwards is kept. */
  if (ini_get("general", "defaults_rev", 0) < 3) gui.sms_show_border = 0;
  gui.np_port    = clampi(ini_get("netplay", "port", gui.np_port), 1024, 65535);
  gui.np_delay   = clampi(ini_get("netplay", "delay", gui.np_delay), 1, 4);
  ini_get_str("netplay", "address", gui.np_addr, sizeof(gui.np_addr));
  ini_get_str("general", "filter_console", gui.filter_console, sizeof(gui.filter_console));
  ini_get_str("general", "filter_genre", gui.filter_genre, sizeof(gui.filter_genre));
  gui.filter_genre_on = ini_get("general", "filter_genre_on", 0) ? 1 : 0;
  ini_get_str("general", "filter_region", gui.filter_region, sizeof(gui.filter_region));
  gui.browser_col_hidden = ini_get("video", "browser_col_hidden", gui.browser_col_hidden);
  gui.browser_col_extra = ini_get("video", "browser_col_extra", gui.browser_col_extra) & 1;   /* Genre only */
  gui.clean_names = ini_get("general", "clean_names", gui.clean_names) ? 1 : 0;
  gui.browser_gridlines = ini_get("general", "browser_row_lines", gui.browser_gridlines) ? 1 : 0;
  gui.scan_subfolders = ini_get("general", "scan_subfolders", gui.scan_subfolders) ? 1 : 0;
  gui.browser_sort_column = ini_get("video", "browser_sort_column", gui.browser_sort_column);
  if (ini_get("video", "browser_layout_rev", 0) < 2)
  {
    /* The Folder and Size columns are gone: Favorite and Genre moved up (were 4 and 5), and the
       column widths start over. */
    int old_hidden = gui.browser_col_hidden;
    gui.browser_col_hidden = (old_hidden & 2) | ((old_hidden & 16) ? 4 : 0);   /* Console, Favorite */
    switch (gui.browser_sort_column)
    {
      case 4: gui.browser_sort_column = 2; break;
      case 5: gui.browser_sort_column = 3; break;
      case 0: case 1: break;
      default: gui.browser_sort_column = 0; break;
    }
  }
  if (gui.browser_sort_column < 0 || gui.browser_sort_column > 3) gui.browser_sort_column = 0;
  gui.browser_sort_ascending = ini_get("video", "browser_sort_ascending", gui.browser_sort_ascending) ? 1 : 0;
  gui.browser_col_pct[0] = clampi(ini_get("video", "browser_col0_pct", gui.browser_col_pct[0]), 5, 90);
  gui.browser_col_pct[1] = clampi(ini_get("video", "browser_col1_pct", gui.browser_col_pct[1]), 5, 90);
  if (ini_get("video", "browser_layout_rev", 0) < 2) { gui.browser_col_pct[0] = 64; gui.browser_col_pct[1] = 36; }   /* old widths belonged to other columns */
  {
    /* "renderer" is the current key; an existing settings file from before
       this had three choices only has the old on/off "hw_accel" key, which
       is read here as the fallback default for "renderer" so upgrading
       preserves whatever the person already had (GDI stays GDI, D3D9 stays
       D3D9) rather than silently resetting everyone to the new default. */
    int legacy_default = ini_get("video", "hw_accel", gui.renderer != RENDERER_GDI) ? RENDERER_D3D9 : RENDERER_GDI;
    gui.renderer = clampi(ini_get("video", "renderer", legacy_default), RENDERER_GDI, RENDERER_D3D11);
  }
  gui.vsync = ini_get("video", "vsync", gui.vsync) ? 1 : 0;
  gui.theme_mode = clampi(ini_get("video", "theme_mode", gui.theme_mode), 0, 2);
  gui.large_ui = ini_get("video", "large_ui", gui.large_ui) ? 1 : 0;
  gui.cheats_enabled = ini_get("general", "cheats_enabled", gui.cheats_enabled) ? 1 : 0;
  gui.browser_grid_view = ini_get("general", "browser_grid_view", gui.browser_grid_view) ? 1 : 0;
  gui.hide_toolbar   = ini_get("general", "hide_toolbar", gui.hide_toolbar) ? 1 : 0;
  gui.hide_searchbar = ini_get("general", "hide_searchbar", gui.hide_searchbar) ? 1 : 0;
  gui.hide_filterbar = ini_get("general", "hide_filterbar", gui.hide_filterbar) ? 1 : 0;
  gui.browser_grid_size = clampi(ini_get("general", "browser_grid_size", gui.browser_grid_size), 48, 256);
  gui.show_fps   = ini_get("video", "show_fps", gui.show_fps) ? 1 : 0;
  gui.favorites_only = ini_get("general", "favorites_only", gui.favorites_only) ? 1 : 0;
  gui.recent_mode = ini_get("general", "recent_mode", gui.recent_mode) ? 1 : 0;
  gui.fullscreen_toolbar = ini_get("general", "fullscreen_toolbar", gui.fullscreen_toolbar) ? 1 : 0;
  gui.integer_scale = ini_get("video", "integer_scale", gui.integer_scale) ? 1 : 0;
  {
    int k;
    for (k = 0; k < padhk_count(); k++)
    {
      char key[32];
      padhk_ini_key(k, key);
      gui.padhk[k] = ini_get("input", key, gui.padhk[k]);
    }
  }
  {
    int k;
    for (k = 0; k < hotkeys_count(); k++)
    {
      char hkey[32];
      hotkey_ini_key(k, hkey, sizeof(hkey));
      gui.hotkey[k] = ini_get("hotkeys", hkey, gui.hotkey[k]);
    }
    /* Fast Forward and Rewind keys used to be set in Controls: carry the old choice over. */
    if (ini_get("hotkeys", "rev", 0) < 5)
    {
      for (k = 0; k < hotkeys_count(); k++)
      {
        if (!lstrcmpA(hotkey_name(k), "Fast Forward (hold)")) gui.hotkey[k] = ini_get("input", "key_fast_forward", hotkey_default(k));
        if (!lstrcmpA(hotkey_name(k), "Rewind (hold)"))       gui.hotkey[k] = ini_get("input", "key_rewind", hotkey_default(k));
      }
    }
    /* Slow Motion got Ctrl+Tab after it was saved with no key. */
    if (ini_get("hotkeys", "rev", 0) < 6)
    {
      for (k = 0; k < hotkeys_count(); k++)
      {
        int j, used = 0;
        if (lstrcmpA(hotkey_name(k), "Slow Motion") || gui.hotkey[k]) continue;
        for (j = 0; j < hotkeys_count(); j++) if (gui.hotkey[j] == hotkey_default(k)) used = 1;
        if (!used) gui.hotkey[k] = hotkey_default(k);
      }
    }
    /* Newer default keys: applied once to settings saved with the older ones. */
    if (ini_get("hotkeys", "rev", 0) < 4)
    {
      static const char *const moved[] = { "Reset", "Hard Reset", "ROMs Directory", "Mute FM Sound", "Mute PSG Sound",
                                           "Show / Hide the Search Bar", "Undo Load State", "Undo Save State" };
      int m, j;
      for (m = 0; m < (int)(sizeof(moved) / sizeof(moved[0])); m++)
        for (k = 0; k < hotkeys_count(); k++)
          if (!lstrcmpA(hotkey_name(k), moved[m]))
          {
            for (j = 0; j < hotkeys_count(); j++)       /* another action holding the key lets go of it */
              if (j != k && gui.hotkey[j] == hotkey_default(k)) gui.hotkey[j] = 0;
            gui.hotkey[k] = hotkey_default(k);
          }
    }
    /* Mute got Ctrl+M after the list was first saved with it empty: give it the key once. */
    if (ini_get("hotkeys", "rev", 0) < 2)
    {
      for (k = 0; k < hotkeys_count(); k++)
      {
        int j, used = 0;
        if (lstrcmpA(hotkey_name(k), "Mute") || gui.hotkey[k]) continue;
        for (j = 0; j < hotkeys_count(); j++) if (gui.hotkey[j] == hotkey_default(k)) used = 1;
        if (!used) gui.hotkey[k] = hotkey_default(k);
      }
    }
  }
  gui.mute_fm  = ini_get("audio", "mute_fm", gui.mute_fm) ? 1 : 0;
  gui.mute_psg = ini_get("audio", "mute_psg", gui.mute_psg) ? 1 : 0;
  gui.autosave = ini_get("general", "autosave", gui.autosave) ? 1 : 0;
  gui.resume_choice = ini_get("general", "resume_choice", gui.resume_choice);
  if (gui.resume_choice < 0 || gui.resume_choice > 2) gui.resume_choice = 0;
  gui.osd      = ini_get("general", "osd", gui.osd) ? 1 : 0;
  gui.fullscreen = ini_get("video", "fullscreen", gui.fullscreen) ? 1 : 0;
  gui.fullscreen_on_load = ini_get("video", "fullscreen_on_load", gui.fullscreen_on_load) ? 1 : 0;
  config.ntsc    = (uint8)clampi(ini_get("video", "ntsc_filter", config.ntsc), 0, 3);
  config.overscan= (uint8)clampi(ini_get("video", "overscan", config.overscan), 0, 3);
  config.render  = (uint8)(ini_get("video", "interlaced_double_height", config.render) ? 1 : 0);
  /* Single field is the default again (rev 4) -- it was double field for a
     while (rev 2), but that is being reversed, so this now does the exact
     opposite of what the rev-2 migration below it used to: anyone below
     rev 4 (which includes everyone the rev-2 check ever touched) moves to
     single field once; anything chosen afterwards is kept regardless. */
  if (ini_get("general", "defaults_rev", 0) < 4) config.render = 0;
  config.gg_extra= (uint8)(ini_get("video", "gg_extended_screen", config.gg_extra) ? 1 : 0);
  config.lcd     = (uint8)clampi(ini_get("video", "lcd_ghosting", config.lcd), 0, 255);

  /* --- window geometry --- */
  gui.window_x         = ini_get("window", "x", gui.window_x);
  gui.window_y         = ini_get("window", "y", gui.window_y);
  gui.window_maximized = ini_get("window", "maximized", gui.window_maximized) ? 1 : 0;

  /* --- render filter --- */
  ini_get_str("video", "render_filter", gui.render_filter, sizeof(gui.render_filter));
  if (!gui.render_filter[0])
  {
    /* Settings written by the older build that loaded .rpi plugins. The value
       there was a filename ("Scale2x.rpi"); filter_find() ignores the
       extension, so a plugin that shared a name with a built-in carries over
       and anything else simply falls back to no filter. */
    ini_get_str("video", "rpi_filter", gui.render_filter, sizeof(gui.render_filter));
  }

  /* --- audio --- */
  gui.sound_enabled  = ini_get("audio", "enabled", gui.sound_enabled) ? 1 : 0;
  gui.volume         = clampi(ini_get("audio", "volume", gui.volume), 0, 100);
  gui.sample_rate    = (ini_get("audio", "sample_rate", gui.sample_rate) == 44100) ? 44100 : 48000;
  gui.latency        = clampi(ini_get("audio", "buffered_frames", gui.latency), 2, 8);
  config.fm_preamp   = (int16)clampi(ini_get("audio", "fm_preamp", config.fm_preamp), 0, 200);
  config.psg_preamp  = (int16)clampi(ini_get("audio", "psg_preamp", config.psg_preamp), 0, 200);
  config.cdda_volume = (int16)clampi(ini_get("audio", "cdda_volume", config.cdda_volume), 0, 200);
  config.pcm_volume  = (int16)clampi(ini_get("audio", "pcm_volume", config.pcm_volume), 0, 200);
  config.filter      = (uint8)clampi(ini_get("audio", "low_pass_filter", config.filter), 0, 2);
  config.hq_psg      = (uint8)(ini_get("audio", "hq_psg", config.hq_psg) ? 1 : 0);
  config.hq_fm       = (uint8)(ini_get("audio", "hq_fm", config.hq_fm) ? 1 : 0);
  config.mono        = (uint8)(ini_get("audio", "mono", config.mono) ? 1 : 0);
  config.ym2612      = (uint8)clampi(ini_get("audio", "ym2612_core", config.ym2612), 0, 2);
  gui.nuked_ym2612   = ini_get("audio", "nuked_ym2612", gui.nuked_ym2612) ? 1 : 0;

  /* Advanced audio: filter strength, equaliser, Master System FM. */
  {
    int pct = clampi(ini_get("audio", "lowpass_percent", (int)((config.lp_range * 100u + 32768u) / 65536u)), 0, 99);
    config.lp_range = (uint32)((pct * 65536) / 100);
  }
  config.lg        = (int16)clampi(ini_get("audio", "eq_low_gain",  config.lg), 0, 200);
  config.mg        = (int16)clampi(ini_get("audio", "eq_mid_gain",  config.mg), 0, 200);
  config.hg        = (int16)clampi(ini_get("audio", "eq_high_gain", config.hg), 0, 200);
  config.low_freq  = (int16)clampi(ini_get("audio", "eq_low_freq",  config.low_freq), 20, 2000);
  config.high_freq = (int16)clampi(ini_get("audio", "eq_high_freq", config.high_freq), 1000, 20000);
  if (config.low_freq >= config.high_freq) { config.low_freq = 200; config.high_freq = 8000; }
  config.ym2413    = (uint8)clampi(ini_get("audio", "ym2413_mode", config.ym2413), 0, 2);
  config.opll      = (uint8)(ini_get("audio", "opll_nuked", config.opll) ? 1 : 0);
  config.ym3438      = (uint8)(ini_get("audio", "ym3438", config.ym3438) ? 1 : 0);

  /* --- system --- */
  /* Unlike every field around it, this one has no contiguous valid range to
     clampi() against (the SYSTEM_* constants are sparse: 0, 0x01-0x03,
     0x10, 0x20-0x21, 0x40, 0x80 -- see core/system.h), so it's checked
     against the exact set the System menu can actually produce (main.c's
     own systems[] table) instead. Anything else -- a hand-edited or
     corrupted ini -- falls back to 0 (auto-detect) rather than being
     handed to the core as-is; loadrom.c uses a nonzero config.system to
     override auto-detection unconditionally, with no validation of its
     own. */
  {
    static const uint8 valid_console[] =
    {
      0, SYSTEM_SG, SYSTEM_SGII, SYSTEM_SGII_RAM_EXT, SYSTEM_MARKIII,
      SYSTEM_SMS, SYSTEM_SMS2, SYSTEM_GG, SYSTEM_MD
    };
    int val = ini_get("system", "console", config.system);
    int j, ok = 0;

    for (j = 0; j < (int)(sizeof(valid_console) / sizeof(valid_console[0])); j++)
    {
      if (val == valid_console[j]) { ok = 1; break; }
    }
    config.system = (uint8)(ok ? val : 0);
  }
  config.region_detect = (uint8)clampi(ini_get("system", "region", config.region_detect), 0, 4);
  config.vdp_mode      = (uint8)clampi(ini_get("system", "vdp_mode", config.vdp_mode), 0, 2);
  config.bios          = (uint8)clampi(ini_get("system", "bios", config.bios), 0, 3);
  config.lock_on       = (uint8)clampi(ini_get("system", "lock_on", config.lock_on), 0, 3);
  config.addr_error    = (uint8)(ini_get("system", "address_error", config.addr_error) ? 1 : 0);
  config.cd_latency    = (uint8)(ini_get("system", "cd_latency", config.cd_latency) ? 1 : 0);

  /* --- behaviour --- */
  gui.pause_on_focus_loss = ini_get("general", "pause_on_focus_loss", gui.pause_on_focus_loss) ? 1 : 0;
  gui.always_on_top       = ini_get("general", "always_on_top", gui.always_on_top) ? 1 : 0;
  gui.rewind              = ini_get("general", "rewind_enabled", gui.rewind) ? 1 : 0;
  gui.state_slot          = clampi(ini_get("general", "state_slot", gui.state_slot), 0, GUI_SLOT_MAX - 1);
  /* Always starts at 0 on launch, on request -- read above (and still
     written on save, below) only so the key round-trips cleanly for
     anything else that might inspect the ini file, not because the loaded
     value is ever actually used to pick up where a previous session left
     off. */
  gui.state_slot          = 0;
  gui.fast_forward_ratio  = clampi(ini_get("general", "fast_forward_ratio", gui.fast_forward_ratio), 2, 16);
  ini_get_str("general", "rom_dir", gui.rom_dir, sizeof(gui.rom_dir));
  ini_get_str("general", "shader_preset_path", gui.shader_preset_path, sizeof(gui.shader_preset_path));

  /* --- ports --- */
  input.system[0] = (uint8)clampi(ini_get("input", "port_a", input.system[0]), 0, 14);
  input.system[1] = (uint8)clampi(ini_get("input", "port_b", input.system[1]), 0, 14);

  gui.deadzone[0]      = clampi(ini_get("input", "deadzone_p1", gui.deadzone[0]), 0, 100);
  gui.deadzone[1]      = clampi(ini_get("input", "deadzone_p2", gui.deadzone[1]), 0, 100);

  /* --- pads --- */

  /* DirectInput itself only initializes lazily on first use (see
     gui_input_refresh_joysticks()), so unlike XInput's four fixed slots,
     nothing has enumerated its device list yet at this point in startup --
     without this call the by-name re-match just below would find nothing
     and silently fall back to whatever raw index was last saved, on every
     single launch. Read-only (device names and GUIDs only): it does not
     open or acquire anything, so it can't be the auto-acquire-every-pad
     issue gui_input_refresh_joysticks()'s own comment describes. */
  gui_input_refresh_joysticks();

  for (player = 0; player < 2; player++)
  {
    char section[16];
    wsprintfA(section, "player%d", player + 1);

    gui.pad[player].device = clampi(ini_get(section, "device", gui.pad[player].device),
                                     -1, GUI_INPUT_XPAD_COUNT + GUI_INPUT_DI_MAX - 1);
    config.input[player].padtype = (uint8)ini_get(section, "pad_type", config.input[player].padtype);

    ini_get_str(section, "joy_name", buf, sizeof(buf));
    lstrcpynA(gui.pad[player].joy_name, buf, sizeof(gui.pad[player].joy_name));

    /* Re-matches a DirectInput device by name against what was just
       enumerated above. Keeps a saved mapping on the same physical
       joystick even if it came up at a different index this time (a
       different USB port, another joystick plugged in first, and so on).
       Player 2 avoids whatever player 1 was just resolved to, so two
       identically-named controllers don't both resolve to the same one. */
    gui.pad[player].device = gui_input_resolve_device(gui.pad[player].device, gui.pad[player].joy_name,
                                                        (player == 1) ? gui.pad[0].device : -1);

    for (i = 0; i < PAD_KEYS; i++)
    {
      wsprintfA(key, "key_%s", pad_key_names[i]);
      gui.pad[player].key[i] = ini_get(section, key, gui.pad[player].key[i]);

      wsprintfA(key, "btn_%s", pad_key_names[i]);
      gui.pad[player].button[i] = ini_get(section, key, gui.pad[player].button[i]);
    }
    gui.pad[player].turbo = (unsigned)ini_get(section, "turbo", (int)gui.pad[player].turbo) & 0x3F0u;

    /* Player 1's keyboard defaults changed (A S D / Q W E  ->  Z X C / A S D).
       A saved config that still holds exactly the old defaults was never
       customised, so move it to the new layout; anything else is left alone. */
    if (player == 0)
    {
      t_pad_map *p = &gui.pad[0];

      if (p->key[PAD_UP] == VK_UP && p->key[PAD_DOWN] == VK_DOWN &&
          p->key[PAD_LEFT] == VK_LEFT && p->key[PAD_RIGHT] == VK_RIGHT &&
          p->key[PAD_A] == 'A' && p->key[PAD_B] == 'S' && p->key[PAD_C] == 'D' &&
          p->key[PAD_X] == 'Q' && p->key[PAD_Y] == 'W' && p->key[PAD_Z] == 'E' &&
          p->key[PAD_START] == VK_RETURN && p->key[PAD_MODE] == VK_RSHIFT)
      {
        p->key[PAD_A] = 'Z';  p->key[PAD_B] = 'X';  p->key[PAD_C] = 'C';
        p->key[PAD_X] = 'A';  p->key[PAD_Y] = 'S';  p->key[PAD_Z] = 'D';
      }
    }
  }

  /* --- recent files --- */
  for (i = 0; i < GUI_RECENT_MAX; i++)
  {
    wsprintfA(key, "file%d", i);
    ini_get_str("recent", key, buf, sizeof(buf));
    lstrcpynA(gui.recent[i], buf, GUI_PATH_LEN);
  }

  /* --- favorites --- */
  fav_n = 0;
  {
    int n = ini_get("favorites", "count", 0);
    if (n > GUI_FAV_MAX) n = GUI_FAV_MAX;
    for (i = 0; i < n; i++)
    {
      wsprintfA(key, "game%d", i);
      ini_get_str("favorites", key, buf, sizeof(buf));
      if (buf[0]) config_fav_set(buf, 1);
    }
  }
}


void config_save(void)
{
  char key[64];
  int player, i;

  ini_init_path();
  ini_queue_reset();

  ini_put("video", "scale", gui.scale);
  ini_put("video", "aspect", gui.aspect);
  ini_put("video", "smooth", gui.smooth);
  ini_put("video", "scanline_pct", gui.scanline_pct);
  ini_put("video", "brighten", gui.brighten);
  ini_put("input", "background_input", gui.background_input);
  ini_put("video", "frameskip", gui.frameskip);
  ini_put("video", "screenshot_output", gui.shot_mode);
  ini_put("video", "sms_show_side_border", gui.sms_show_border);
  ini_del("video", "sms_hide_side_border");
  /* Run-ahead was removed: it depended on a core-side hook (saving/restoring
     the audio filters' internal memory around a run-ahead reload) that this
     GUI has no business adding to core/ -- see the "Run-Ahead" menu it used
     to sit under, gone the same commit as this line. */
  ini_del("system", "run_ahead");
  ini_put("netplay", "port", gui.np_port);
  ini_put("netplay", "delay", gui.np_delay);
  ini_put_str("netplay", "address", gui.np_addr);
  ini_put_str("general", "filter_console", gui.filter_console);
  ini_put_str("general", "filter_genre", gui.filter_genre);
  ini_put("general", "filter_genre_on", gui.filter_genre_on);
  ini_put_str("general", "filter_region", gui.filter_region);
  ini_put("video", "browser_col_hidden", gui.browser_col_hidden);
  ini_put("video", "browser_col_extra", gui.browser_col_extra);
  ini_put("general", "clean_names", gui.clean_names);
  ini_put("general", "browser_row_lines", gui.browser_gridlines);
  ini_put("general", "scan_subfolders", gui.scan_subfolders);
  ini_put("video", "browser_sort_column", gui.browser_sort_column);
  ini_put("video", "browser_sort_ascending", gui.browser_sort_ascending);
  ini_put("video", "browser_layout_rev", 2);
  ini_put("video", "browser_col0_pct", gui.browser_col_pct[0]);
  ini_put("video", "browser_col1_pct", gui.browser_col_pct[1]);
  ini_put("video", "renderer", gui.renderer);
  ini_put("video", "vsync", gui.vsync);
  ini_put("video", "theme_mode", gui.theme_mode);
  ini_put("video", "large_ui", gui.large_ui);
  ini_put("general", "cheats_enabled", gui.cheats_enabled);
  ini_put("general", "browser_grid_view", gui.browser_grid_view);
  ini_put("general", "hide_toolbar", gui.hide_toolbar);
  ini_put("general", "hide_searchbar", gui.hide_searchbar);
  ini_put("general", "hide_filterbar", gui.hide_filterbar);
  ini_put("general", "browser_grid_size", gui.browser_grid_size);
  ini_put("video", "show_fps", gui.show_fps);
  ini_put("general", "favorites_only", gui.favorites_only);
  ini_put("general", "recent_mode", gui.recent_mode);
  ini_put("general", "fullscreen_toolbar", gui.fullscreen_toolbar);
  ini_put("video", "integer_scale", gui.integer_scale);
  {
    int k;
    for (k = 0; k < padhk_count(); k++)
    {
      char key[32];
      padhk_ini_key(k, key);
      ini_put("input", key, gui.padhk[k]);
    }
  }
  {
    int k;
    for (k = 0; k < hotkeys_count(); k++)
    {
      char hkey[32];
      hotkey_ini_key(k, hkey, sizeof(hkey));
      ini_put("hotkeys", hkey, gui.hotkey[k]);
    }
    ini_put("hotkeys", "rev", 6);
  }
  ini_put("audio", "mute_fm", gui.mute_fm);
  ini_put("audio", "mute_psg", gui.mute_psg);
  ini_put("general", "autosave", gui.autosave);
  ini_put("general", "resume_choice", gui.resume_choice);
  ini_put("general", "osd", gui.osd);
  ini_put("video", "fullscreen", gui.fullscreen);
  ini_put("video", "fullscreen_on_load", gui.fullscreen_on_load);
  ini_put("video", "ntsc_filter", config.ntsc);
  ini_put("video", "overscan", config.overscan);
  ini_put("video", "interlaced_double_height", config.render);
  ini_put("general", "defaults_rev", 4);
  ini_put("video", "gg_extended_screen", config.gg_extra);
  ini_put("video", "lcd_ghosting", config.lcd);

  ini_put("window", "x", gui.window_x);
  ini_put("window", "y", gui.window_y);
  ini_put("window", "maximized", gui.window_maximized);

  ini_put_str("video", "render_filter", gui.render_filter);

  /* Obsolete keys from the .rpi-plugin build: remove them so the file does
     not keep stale entries that nothing reads. */
  ini_del("video", "rpi_filter");
  ini_del("video", "rpi_scale");

  ini_put("audio", "enabled", gui.sound_enabled);
  ini_put("audio", "volume", gui.volume);
  ini_put("audio", "sample_rate", gui.sample_rate);
  ini_put("audio", "buffered_frames", gui.latency);
  ini_put("audio", "fm_preamp", config.fm_preamp);
  ini_put("audio", "psg_preamp", config.psg_preamp);
  ini_put("audio", "cdda_volume", config.cdda_volume);
  ini_put("audio", "pcm_volume", config.pcm_volume);
  ini_put("audio", "low_pass_filter", config.filter);
  ini_put("audio", "hq_psg", config.hq_psg);
  ini_put("audio", "hq_fm", config.hq_fm);
  ini_put("audio", "mono", config.mono);
  ini_put("audio", "ym2612_core", config.ym2612);
  ini_put("audio", "nuked_ym2612", gui.nuked_ym2612);
  ini_put("audio", "lowpass_percent", (int)((config.lp_range * 100u + 32768u) / 65536u));
  ini_put("audio", "eq_low_gain",  config.lg);
  ini_put("audio", "eq_mid_gain",  config.mg);
  ini_put("audio", "eq_high_gain", config.hg);
  ini_put("audio", "eq_low_freq",  config.low_freq);
  ini_put("audio", "eq_high_freq", config.high_freq);
  /* The core sets bit 0 itself when "Auto" turns the FM unit on at run time. */
  ini_put("audio", "ym2413_mode", (config.ym2413 & 2) ? 2 : (config.ym2413 & 1));
  ini_put("audio", "opll_nuked", config.opll);
  ini_put("audio", "ym3438", config.ym3438);

  ini_put("system", "console", config.system);
  ini_put("system", "region", config.region_detect);
  ini_put("system", "vdp_mode", config.vdp_mode);
  ini_put("system", "bios", config.bios);
  ini_put("system", "lock_on", config.lock_on);
  ini_put("system", "address_error", config.addr_error);
  ini_put("system", "cd_latency", config.cd_latency);

  ini_put("general", "pause_on_focus_loss", gui.pause_on_focus_loss);
  ini_put("general", "always_on_top", gui.always_on_top);
  ini_put("general", "rewind_enabled", gui.rewind);
  ini_put("general", "state_slot", gui.state_slot);
  ini_put("general", "fast_forward_ratio", gui.fast_forward_ratio);
  ini_put_str("general", "rom_dir", gui.rom_dir);
  ini_put_str("general", "shader_preset_path", gui.shader_preset_path);

  ini_put("input", "port_a", input.system[0]);
  ini_put("input", "port_b", input.system[1]);
  ini_put("input", "deadzone_p1", gui.deadzone[0]);
  ini_put("input", "deadzone_p2", gui.deadzone[1]);

  for (player = 0; player < 2; player++)
  {
    char section[16];
    wsprintfA(section, "player%d", player + 1);

    ini_put(section, "device", gui.pad[player].device);
    ini_put_str(section, "joy_name", gui.pad[player].joy_name);
    ini_put(section, "pad_type", config.input[player].padtype);

    for (i = 0; i < PAD_KEYS; i++)
    {
      wsprintfA(key, "key_%s", pad_key_names[i]);
      ini_put(section, key, gui.pad[player].key[i]);

      wsprintfA(key, "btn_%s", pad_key_names[i]);
      ini_put(section, key, gui.pad[player].button[i]);
    }
    ini_put(section, "turbo", (int)gui.pad[player].turbo);
  }

  for (i = 0; i < GUI_RECENT_MAX; i++)
  {
    wsprintfA(key, "file%d", i);
    ini_put_str("recent", key, gui.recent[i]);
  }

  ini_put("favorites", "count", fav_n);
  for (i = 0; i < fav_n; i++)
  {
    wsprintfA(key, "game%d", i);
    ini_put_str("favorites", key, fav_list[i]);
  }

  /* When each game was last opened (history.c): "<Unix time>|<path>". */
  {
    int n = history_count();
    ini_put("history", "count", n);
    for (i = 0; i < n; i++)
    {
      char val[GUI_PATH_LEN];
      wsprintfA(key, "game%d", i);
      wsprintfA(val, "%lu|", (unsigned long)history_entry_last(i));   /* wsprintf has no %llu */
      lstrcpynA(val + lstrlenA(val), history_entry_path(i), (int)sizeof(val) - lstrlenA(val));
      ini_put_str("history", key, val);
    }
  }

  ini_flush();
}

/* Reads the [history] section back into history.c. */
void config_history_load(void)
{
  int i, n;
  char key[40], val[GUI_PATH_LEN];

  ini_init_path();
  n = ini_get("history", "count", 0);
  for (i = 0; i < n && i < 1000; i++)
  {
    char *bar;
    wsprintfA(key, "game%d", i);
    ini_get_str("history", key, val, sizeof(val));
    bar = strchr(val, '|');
    if (!bar || !bar[1]) continue;
    *bar = '\0';
    history_restore(bar + 1, strtoull(val, NULL, 10));
  }
}

/****************************************************************************
 * Favorites
 ****************************************************************************/

int config_fav_has(const char *path)
{
  int i;
  if (!path) return 0;
  for (i = 0; i < fav_n; i++)
    if (!lstrcmpiA(fav_list[i], path)) return 1;
  return 0;
}

int config_fav_set(const char *path, int on)
{
  int i;

  if (!path || !path[0]) return 0;
  for (i = 0; i < fav_n; i++)
    if (!lstrcmpiA(fav_list[i], path)) break;

  if (on && i == fav_n)
  {
    if (fav_n >= GUI_FAV_MAX) return 0;
    lstrcpynA(fav_list[fav_n++], path, GUI_PATH_LEN);
  }
  else if (!on && i < fav_n)
  {
    memmove(fav_list[i], fav_list[i + 1], (size_t)(fav_n - i - 1) * GUI_PATH_LEN);
    fav_n--;
  }
  return 1;
}

/****************************************************************************
 * Recent files
 ****************************************************************************/

void config_add_recent(const char *path)
{
  char moved[GUI_RECENT_MAX][GUI_PATH_LEN];
  int count = 0;
  int i;

  if (!path || !path[0]) return;

  lstrcpynA(moved[count++], path, GUI_PATH_LEN);

  for (i = 0; i < GUI_RECENT_MAX && count < GUI_RECENT_MAX; i++)
  {
    if (!gui.recent[i][0]) continue;
    if (lstrcmpiA(gui.recent[i], path) == 0) continue;
    lstrcpynA(moved[count++], gui.recent[i], GUI_PATH_LEN);
  }

  for (i = 0; i < GUI_RECENT_MAX; i++)
  {
    if (i < count) lstrcpynA(gui.recent[i], moved[i], GUI_PATH_LEN);
    else gui.recent[i][0] = '\0';
  }
}
