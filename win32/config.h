/****************************************************************************
 *  Genesis Plus -- Win32 GUI frontend
 *
 *  config.h -- emulation settings (t_config, read directly by the core) and
 *              frontend settings (t_gui_config, used only by the GUI).
 *
 *  t_config must keep the same fields the core expects; add frontend options
 *  to t_gui_config instead.
 ****************************************************************************/

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "types.h"

/****************************************************************************
 * Emulation settings -- shared with the core
 ****************************************************************************/

typedef struct
{
  uint8 padtype;
} t_input_config;

typedef struct
{
  uint8 hq_fm;
  uint8 filter;
  uint8 hq_psg;
  uint8 ym2612;
  uint8 ym2413;
  uint8 ym3438;
  uint8 opll;
  uint8 cd_latency;
  int16 psg_preamp;
  int16 fm_preamp;
  int16 cdda_volume;
  int16 pcm_volume;
  uint32 lp_range;
  int16 low_freq;
  int16 high_freq;
  int16 lg;
  int16 mg;
  int16 hg;
  uint8 mono;
  uint8 system;
  uint8 region_detect;
  uint8 vdp_mode;
  uint8 master_clock;
  uint8 force_dtack;
  uint8 addr_error;
  uint8 bios;
  uint8 lock_on;
  uint8 add_on;
  uint8 hot_swap;
  uint8 invert_mouse;
  uint8 gun_cursor[2];
  uint8 overscan;
  uint8 gg_extra;
  uint8 ntsc;
  uint8 lcd;
  uint8 render;
  uint8 enhanced_vscroll;
  uint8 enhanced_vscroll_limit;
  uint8 h40_extra_columns;   /* widescreen: extra 8px tile columns to draw beyond the normal 320px H40 width, 0 = off (see Genesis-Plus-GX-Wide) */
  t_input_config input[MAX_INPUTS];
} t_config;

/****************************************************************************
 * Frontend settings -- GUI only
 ****************************************************************************/

#define GUI_PATH_LEN    512
#define GUI_RECENT_MAX  10
#define GUI_SLOT_MAX    10

/* Indices into t_pad_map.key[] and t_pad_map.button[] */
enum
{
  PAD_UP = 0,
  PAD_DOWN,
  PAD_LEFT,
  PAD_RIGHT,
  PAD_A,
  PAD_B,
  PAD_C,
  PAD_X,
  PAD_Y,
  PAD_Z,
  PAD_START,
  PAD_MODE,
  PAD_KEYS
};

typedef struct
{
  int key[PAD_KEYS];    /* Windows virtual-key code per button */
  int button[PAD_KEYS]; /* button/POV/stick mask per button (namespace depends
                            on device: XInput below GUI_INPUT_XPAD_COUNT,
                            DirectInput at or above it), 0 = unassigned */
  unsigned turbo;       /* bit per PAD_* index: that button fires on and off while held (A B C X Y Z) */
  int device;           /* -1 keyboard only, else an XInput slot or a
                            DirectInput joystick -- see GUI_INPUT_XPAD_COUNT
                            in gui.h */
  char joy_name[64];     /* DirectInput product name device was last set to,
                            empty for keyboard/XInput -- lets a saved mapping
                            find the same physical joystick again even if it
                            re-enumerates at a different index */
} t_pad_map;

/* video.aspect */
enum { ASPECT_SQUARE = 0, ASPECT_43, ASPECT_STRETCH };

/* gui.renderer */
enum { RENDERER_GDI = 0, RENDERER_D3D9 = 1, RENDERER_D3D11 = 2 };

typedef struct
{
  /* video */
  int scale;              /* 1-4, integer window scale at 320x224 */
  int aspect;             /* one of the ASPECT_* values */
  int smooth;             /* 1 = smooth scaling (HALFTONE), 0 = nearest */
  int scanline_pct;       /* 0/25/50/75/100 -- darkens alternating output rows */
  int nuked_ym2612;       /* Nuked FM core: 1 = behave as a YM2612, 0 = as a YM3438 */
  int shot_mode;          /* screenshot output: 0 final, 1 corrected (640x480), 2 raw */
  int frameskip;          /* 0 off, 1 auto, 2-5 manual (25/33/50/75% audio buffer) */
  int sms_show_border;    /* 1 = show the Master System's 8-pixel side borders (0 crops them) */
  int background_input;   /* 1 = controller input keeps working while the window is not focused */
  int brighten;           /* 1 = lift the picture's brightness (offsets scanline darkening) */

  /* netplay (LAN) -- last values used in the Netplay dialog */
  int  np_port;
  int  np_delay;          /* input delay in frames, 1-4 */
  char np_addr[64];       /* last host address joined */
  int browser_col_hidden; /* bitmask, one bit per ROM browser list column (Name, Console, Favorite) */
  int browser_col_extra;  /* bitmask of the extra columns that are shown: Genre, Last Played, Play Time, Plays */
  int browser_sort_column; /* which column the ROM browser list is sorted by */
  int browser_sort_ascending;
  int browser_col_pct[2]; /* user-adjustable column width, as a percent of the list's width */
  int renderer;            /* which output path to use: RENDERER_GDI, RENDERER_D3D9 or
                               RENDERER_D3D11 -- always falls back to GDI automatically
                               if the chosen one is unavailable or fails */
  int vsync;                /* wait for the compositor's refresh after each frame (Direct3D
                               or GDI). Off by default: the app is paced by audio, so this
                               only helps if the display refresh is close to the game's rate;
                               skipped automatically when the sound buffer is running low */
  int fullscreen;
  int theme_mode;          /* THEME_AUTO/LIGHT/DARK, see theme.h */
  int large_ui;            /* 0 = normal size, 1 = ~15-20% larger throughout */
  int cheats_enabled;      /* master toggle -- 0 disables every cheat regardless of each one's own checkbox */
  int hide_toolbar;        /* the ROM browser's toolbar / search bar / filter bar are hidden */
  int hide_searchbar;
  int hide_filterbar;
  int browser_grid_view;   /* 0 = report/list view, 1 = icon/grid view with cover art */
  int browser_grid_size;   /* icon size in pixels, adjustable via Ctrl+wheel while in grid view */
  int fullscreen_on_load;  /* persists across runs, unlike fullscreen itself --
                               enters fullscreen automatically whenever a ROM loads */
  int show_fps;
  int browser_gridlines;   /* lines between the columns of the game list */
  int scan_subfolders;     /* the ROMs directory is searched including its subfolders */
  char filter_console[40]; /* the filter bar choices, kept between runs ("" = all) */
  char filter_genre[28];
  int  filter_genre_on;    /* filter_genre is "" for an unknown genre, so it needs its own flag */
  char filter_region[24];
  int clean_names;         /* the game list shows names without the (...) and [...] tags */
  int recent_mode;         /* the last few games are grouped at the top of the list */
  int favorites_only;      /* the game list shows only the favorites */
  int hotkey[64];          /* the keyboard shortcuts: virtual key + 0x10000 Ctrl / 0x20000 Shift / 0x40000 Alt (hotkeys.c) */
  int padhk[16];           /* the pad button (XInput mask) for each gamepad hotkey action, 0 = none; the first two are Fast Forward and Rewind (hotkeys.c) */
  int slowmo;              /* half speed, without sound */
  int integer_scale;       /* the picture is enlarged by whole multiples only (sharp pixels) */
  int mute_fm;             /* silence the FM chip alone */
  int mute_psg;            /* silence the PSG chip alone */
  int resume_choice;       /* the Auto Resume question: 0 = ask, 1 = always resume, 2 = never (set by "Do not ask again") */
  int autosave;            /* save the game when it is closed and offer to resume it next time */
  int osd;                 /* messages over the picture (saved, slot, screenshot ...) */
  int fullscreen_toolbar;  /* the toolbar slides in at the top edge of the screen in fullscreen, like the menu bar */

  /* window geometry, restored on the next launch */
  int window_x;            /* top-left of the window, INT_MIN = "not set" */
  int window_y;
  int window_maximized;

  /* Built-in CPU render filter (filters.c). Stored by name rather than by
     table position so adding or reordering filters never silently changes
     what an existing settings file selects. */
  char render_filter[64];   /* empty = none */

  /* audio */
  int sound_enabled;
  int volume;             /* 0-100, applied on the mixed output */
  int sample_rate;        /* 44100 or 48000 */
  int latency;            /* number of queued frame buffers, 2-8 */

  /* behaviour */
  int rewind;             /* 1 = keep the history Backspace rewinds through (uses ~200 MB) */
  int pause_on_focus_loss;
  int always_on_top;
  int fast_forward_ratio; /* frames run per displayed frame in fast forward */
  int state_slot;         /* 0-9 */

  /* input */
  t_pad_map pad[2];
  int deadzone[2];        /* 0-100%, how far a thumbstick has to move off-center before it
                              registers at all -- per player, since two different controllers
                              (or the same model with worn-in sticks) can drift by different
                              amounts. Applied both to the stick-as-D-pad digital directions and
                              to the analog reading itself (for the few games/peripherals that
                              read the stick as an analog axis rather than a D-pad). */

  /* paths */
  char rom_dir[GUI_PATH_LEN];
  char recent[GUI_RECENT_MAX][GUI_PATH_LEN];
  char shader_preset_path[GUI_PATH_LEN];   /* .slangp GPU shader (D3D11 only); "" = none */
} t_gui_config;

/* Global variables */
extern t_config config;
extern t_gui_config gui;

extern void set_config_defaults(void);
extern void config_load(void);
extern void config_save(void);
extern void config_add_recent(const char *path);

/* Favorite games, kept by full path in genesis-plus.ini ([favorites]). */
extern int  config_fav_has(const char *path);
extern int  config_fav_set(const char *path, int on);   /* 1 when it is now as asked (0: the list is full); does not save */

#endif /* _CONFIG_H_ */
