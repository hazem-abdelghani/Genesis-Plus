/****************************************************************************
 *  Genesis Plus -- Win32 GUI frontend
 *
 *  browser.c -- a library view over a folder of ROMs, built into the main
 *  window itself rather than a separate dialog: when nothing is running,
 *  this panel occupies the video area instead of a black screen, and it
 *  steps aside the moment a game starts.
 *
 *  Extension list matches what the core can actually open, verified against
 *  core/loadrom.c and sdl/fileio.c rather than assumed: load_archive() only
 *  ever unpacks a ZIP (sniffed by its "PK" magic bytes) or falls through to
 *  zlib's gzopen(), which transparently handles both real .gz files and, as
 *  a side effect, plain uncompressed ones. .7z is handled separately, by
 *  this frontend rather than the core -- see sevenzip.c and main.c's
 *  emu_load_rom() -- and only when built with CHD=1 (the default), since it
 *  reuses that feature's LZMA decoder; #ifdef USE_LIBCHDR below matches
 *  Makefile.win32's own gate for it. There is no .m3u playlist support --
 *  that's a libretro frontend feature (it parses the playlist itself), not
 *  something core/loadrom.c understands.
 ****************************************************************************/

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <stdlib.h>
#include <stdio.h>

#include "shared.h"
#include "gui.h"
#include "theme.h"
#include "resource.h"
#include "coverart.h"
#include "sevenzip.h"
#include "toolbar.h"

#define BROWSER_MAX_ENTRIES 4000
#define BROWSER_MAX_DEPTH   6
#define BROWSER_COLUMNS     4

typedef struct
{
  char  full_path[GUI_PATH_LEN];
  char  name[128];
  char  console[40];
  char  folder[GUI_PATH_LEN];
  char  region[24];        /* guessed from the file name, for the filter bar */
  DWORD size_bytes;
  FILETIME mtime;          /* with size_bytes: tells a rescan the file is unchanged */
  signed char cover_ext;   /* COVERART_UNCHECKED until the grid first looks this ROM's cover up, then cached */
  int   img_slot;          /* grid view: index in the image list, valid while img_gen == grid_gen */
  int   img_gen;
} browser_entry_t;

static browser_entry_t *entries;
static int entry_count;

/* The previous scan's entries. A rescan (which happens every time the browser
   is shown again) reuses their console label for archives that have not
   changed instead of opening every .zip again. */
static browser_entry_t *spare;
static int spare_count;

/* Console label of an unchanged archive from the previous scan, or NULL. */
static const char *cached_archive_console(const char *full, const WIN32_FIND_DATAA *fd)
{
  int k;

  for (k = 0; k < spare_count; k++)
  {
    if (spare[k].size_bytes == fd->nFileSizeLow &&
        CompareFileTime(&spare[k].mtime, &fd->ftLastWriteTime) == 0 &&
        !lstrcmpA(spare[k].full_path, full))
      return spare[k].console;
  }
  return NULL;
}

/* Which entries match the current search text, in the order they're shown
   -- the ListView's row N is entries[filtered_index[N]], not entries[N],
   whenever a filter is active. */
static int filtered_index[BROWSER_MAX_ENTRIES];
static int filtered_count;

/* The panel's own child controls, created once and reused for the life of
   the app -- shown when nothing is running, hidden the moment it is. The
   toolbar, search bar and filter bar sit inside panel_bar, one container
   window that fills its own background and passes its children's messages on
   to the main window. */
static HWND panel_toolbar;   /* the toolbar: also shown while a game runs in a window */
static HWND panel_bar;       /* search bar and filter bar */
static HWND panel_search;
static HWND panel_search_label;
static HWND panel_search_close;
static HWND panel_filter[3];       /* console, region, folder */
static HWND panel_filter_reset;
static HWND panel_list;
static HWND panel_choose;
static int  panel_visible;

/* Toolbar buttons. */
static HWND tb_open, tb_refresh, tb_play, tb_stop, tb_reset, tb_pause, tb_fs, tb_shot,
            tb_set, tb_ctl, tb_sep[2];

#define TOOLBAR_H TBS(40)

/* Ctrl+F shows the search bar for now even when View > Show Search Bar is
   off; the Close button and the menu item both go through
   browser_toggle_searchbar(), so they always agree. */
static int  search_forced;

/* Column layout: widths are recomputed proportionally on every relayout,
   so "remembered width" for a hidden column is just whatever it would be
   at the current window size, not a stale value from before a resize. */
static int col_hidden[BROWSER_COLUMNS];
static int last_list_w = 300;
static const char *col_names[BROWSER_COLUMNS] = { "Name", "Console", "Folder", "Size" };

/****************************************************************************
 * Classification
 ****************************************************************************/

static const char *classify_extension(const char *ext)
{
  if (!lstrcmpiA(ext, "md")  || !lstrcmpiA(ext, "gen") || !lstrcmpiA(ext, "bin") ||
      !lstrcmpiA(ext, "smd") || !lstrcmpiA(ext, "mdx") || !lstrcmpiA(ext, "68k"))
    return "Mega Drive / Genesis";

  if (!lstrcmpiA(ext, "sms")) return "Master System";
  if (!lstrcmpiA(ext, "gg"))  return "Game Gear";
  if (!lstrcmpiA(ext, "sg"))  return "SG-1000";

  if (!lstrcmpiA(ext, "cue") || !lstrcmpiA(ext, "iso") || !lstrcmpiA(ext, "chd"))
    return "Mega CD / Sega CD";

  if (!lstrcmpiA(ext, "zip") || !lstrcmpiA(ext, "gz")
#ifdef USE_LIBCHDR
      || !lstrcmpiA(ext, "7z")
#endif
      )
    return "Archive";

  return NULL;
}

#ifdef USE_LIBCHDR
static int is_7z_path(const char *full_path)
{
  const char *dot = strrchr(full_path, '.');
  return dot && !lstrcmpiA(dot, ".7z");
}
#endif

/* Placeholder so browser_console_for_path (below) can use classify_archive. */
static const char *classify_archive(const char *full_path);

/* Console for a ROM path -- by extension, looking inside zip archives -- or
   NULL when it isn't recognised. Used for the Open Recent labels. */
const char *browser_console_for_path(const char *full_path)
{
  const char *name = full_path, *p, *dot;
  const char *console;

  if (!full_path) return NULL;
  for (p = full_path; *p; p++)
    if (*p == '\\' || *p == '/') name = p + 1;

  dot = strrchr(name, '.');
  if (!dot || !dot[1]) return NULL;

  console = classify_extension(dot + 1);
  if (console && !lstrcmpiA(console, "Archive")) console = classify_archive(full_path);
  if (console && !lstrcmpiA(console, "Archive")) return NULL;
  return console;
}

#ifdef USE_LIBCHDR
/* Same idea as classify_archive's zip branch below, for a .7z archive. */
static const char *classify_7z(const char *full_path)
{
  sevenzip_t *sz = sevenzip_open(full_path);
  int i, count;

  if (!sz) return "Archive";

  count = sevenzip_count(sz);
  for (i = 0; i < count; i++)
  {
    char inner_name[MAX_PATH];
    const char *inner_dot;

    if (sevenzip_entry_is_dir(sz, i)) continue;

    sevenzip_entry_name(sz, i, inner_name, sizeof(inner_name));
    inner_dot = strrchr(inner_name, '.');
    if (inner_dot && inner_dot[1])
    {
      const char *console = classify_extension(inner_dot + 1);
      if (console && lstrcmpiA(console, "Archive") != 0)
      {
        sevenzip_close(sz);
        return console;
      }
    }
  }

  sevenzip_close(sz);
  return "Archive";
}
#endif

/* Peeks inside a zip or 7z archive to find what console it actually holds,
   based on the first entry whose own extension classify_extension
   recognizes -- multi-entry archives (a ROM plus a manual, or several
   discs) are still handled since every entry is checked in order, not
   just the first one. Falls back to plain "Archive" if the file can't
   be opened, isn't a real archive, or nothing inside is recognized. */
static const char *classify_archive(const char *full_path)
{
  unzFile zf;
  int result;

#ifdef USE_LIBCHDR
  if (is_7z_path(full_path)) return classify_7z(full_path);
#endif

  zf = unzOpen(full_path);
  if (!zf) return "Archive";

  result = unzGoToFirstFile(zf);
  while (result == UNZ_OK)
  {
    char inner_name[MAX_PATH];
    const char *inner_dot;

    if (unzGetCurrentFileInfo(zf, NULL, inner_name, sizeof(inner_name), NULL, 0, NULL, 0) == UNZ_OK)
    {
      inner_dot = strrchr(inner_name, '.');
      if (inner_dot && inner_dot[1])
      {
        const char *console = classify_extension(inner_dot + 1);
        /* Recognized and not itself another archive -- zip-in-zip isn't
           worth recursing into for a file browser column. */
        if (console && lstrcmpiA(console, "Archive") != 0)
        {
          unzClose(zf);
          return console;
        }
      }
    }

    result = unzGoToNextFile(zf);
  }

  unzClose(zf);
  return "Archive";
}

static void format_size(DWORD bytes, char *out, int out_len)
{
  if (bytes >= 1024 * 1024)
  {
    wsprintfA(out, "%lu.%01lu MB",
              (unsigned long)(bytes / (1024 * 1024)),
              (unsigned long)(((bytes % (1024 * 1024)) * 10) / (1024 * 1024)));
  }
  else
  {
    wsprintfA(out, "%lu KB", (unsigned long)((bytes + 1023) / 1024));
  }
  (void)out_len;
}

/* Case-insensitive substring test -- lstrcmpiA compares whole strings, not
   what's needed for "does the search text appear anywhere in this name". */
static int contains_ci(const char *haystack, const char *needle)
{
  int hlen, nlen, i;

  if (!needle[0]) return 1;

  hlen = lstrlenA(haystack);
  nlen = lstrlenA(needle);
  if (nlen > hlen) return 0;

  for (i = 0; i <= hlen - nlen; i++)
  {
    if (CompareStringA(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                        haystack + i, nlen, needle, nlen) == CSTR_EQUAL)
    {
      return 1;
    }
  }
  return 0;
}

/* Region from the file name's parenthesized tags -- "(USA)", "(Europe)",
   "(Japan, USA)", or the short forms "(U)", "(E)", "(J)", "(UE)" -- or
   "Unknown" when the name carries none. */
static const char *const region_words[] =
  { "USA", "Europe", "Japan", "World", "Asia", "Korea", "Brazil", "Australia", "Germany",
    "France", "Spain", "Italy", "Sweden", "Netherlands", "China", "Taiwan", "Canada",
    "UK", "Russia", "Hong Kong", NULL };

static int is_region_word(const char *s, int n)
{
  int i;
  for (i = 0; region_words[i]; i++)
    if ((int)lstrlenA(region_words[i]) == n &&
        CompareStringA(LOCALE_USER_DEFAULT, NORM_IGNORECASE, s, n, region_words[i], n) == CSTR_EQUAL)
      return 1;
  return 0;
}

static void classify_region(const char *name, char *out, int out_len)
{
  const char *p = name;

  lstrcpynA(out, "Unknown", out_len);

  while ((p = strchr(p, '(')) != NULL)
  {
    const char *end = strchr(p, ')');
    char group[40];
    int n, ok = 1, i, start;

    if (!end) break;
    n = (int)(end - p - 1);
    if (n < 1 || n >= (int)sizeof(group)) { p = end + 1; continue; }
    memcpy(group, p + 1, (size_t)n);
    group[n] = '\0';
    p = end + 1;

    /* Long form: every comma-separated word is a region name. */
    start = 0;
    for (i = 0; i <= n && ok; i++)
    {
      if (group[i] == ',' || group[i] == '\0')
      {
        int s = start, e = i;
        while (s < e && group[s] == ' ') s++;
        while (e > s && group[e - 1] == ' ') e--;
        if (e <= s || !is_region_word(group + s, e - s)) ok = 0;
        start = i + 1;
      }
    }
    if (ok) { lstrcpynA(out, group, out_len); return; }

    /* Short form: one to three letters, each a region. */
    if (n <= 3)
    {
      char text[40] = "";
      for (i = 0; i < n; i++)
      {
        const char *w = NULL;
        switch (group[i])
        {
          case 'U': w = "USA"; break;
          case 'E': w = "Europe"; break;
          case 'J': w = "Japan"; break;
          case 'W': w = "World"; break;
          case 'K': w = "Korea"; break;
          case 'B': w = "Brazil"; break;
        }
        if (!w) { text[0] = '\0'; break; }
        if (text[0]) lstrcatA(text, ", ");
        lstrcatA(text, w);
      }
      if (text[0]) { lstrcpynA(out, text, out_len); return; }
    }
  }
}

/****************************************************************************
 * Scanning
 *
 * Bounded two ways: a depth cap so a directory junction loop can't recurse
 * forever, and a total entry cap so a huge tree can't hang the app. Both
 * are generous for how people actually organise a ROM collection.
 ****************************************************************************/

static void scan_dir(const char *dir, const char *rel, int depth)
{
  WIN32_FIND_DATAA fd;
  HANDLE h;
  char pattern[GUI_PATH_LEN];

  if (depth > BROWSER_MAX_DEPTH) return;
  if (entry_count >= BROWSER_MAX_ENTRIES) return;

  gui_snprintf(pattern, (int)sizeof(pattern), "%s\\*", dir);
  h = FindFirstFileA(pattern, &fd);
  if (h == INVALID_HANDLE_VALUE) return;

  do
  {
    char full[GUI_PATH_LEN];

    if (entry_count >= BROWSER_MAX_ENTRIES) break;
    if (!lstrcmpA(fd.cFileName, ".") || !lstrcmpA(fd.cFileName, "..")) continue;

    gui_snprintf(full, (int)sizeof(full), "%s\\%s", dir, fd.cFileName);

    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
      char newrel[GUI_PATH_LEN];

      if (rel[0]) gui_snprintf(newrel, (int)sizeof(newrel), "%s\\%s", rel, fd.cFileName);
      else lstrcpynA(newrel, fd.cFileName, sizeof(newrel));

      scan_dir(full, newrel, depth + 1);
    }
    else
    {
      const char *dot = strrchr(fd.cFileName, '.');
      const char *console;
      browser_entry_t *e;

      if (!dot || !dot[1]) continue;

      console = classify_extension(dot + 1);
      if (!console) continue;
      if (!lstrcmpiA(console, "Archive"))
      {
        const char *known = cached_archive_console(full, &fd);
        console = known ? known : classify_archive(full);
      }

      /* A standalone .bin belongs to a .cue in the same folder if one
         exists -- show the .cue only, since that's the actual entry point. */
      if (!lstrcmpiA(dot, ".bin"))
      {
        char cue_path[GUI_PATH_LEN];
        char base[128];
        int base_len = (int)(dot - fd.cFileName);

        if (base_len >= (int)sizeof(base)) base_len = sizeof(base) - 1;
        memcpy(base, fd.cFileName, (size_t)base_len);
        base[base_len] = '\0';

        gui_snprintf(cue_path, (int)sizeof(cue_path), "%s\\%s.cue", dir, base);
        if (GetFileAttributesA(cue_path) != INVALID_FILE_ATTRIBUTES) continue;
      }

      e = &entries[entry_count++];
      lstrcpynA(e->full_path, full, sizeof(e->full_path));
      lstrcpynA(e->name, fd.cFileName, sizeof(e->name));
      lstrcpynA(e->console, console, sizeof(e->console));
      lstrcpynA(e->folder, rel, sizeof(e->folder));
      e->size_bytes = fd.nFileSizeLow;   /* every supported format fits well under 4 GB */
      e->mtime = fd.ftLastWriteTime;
      e->cover_ext = COVERART_UNCHECKED;
      classify_region(fd.cFileName, e->region, (int)sizeof(e->region));
      e->img_slot = -1;
      e->img_gen = 0;

      /* A big library takes a while the first time: show that it is working. */
      if ((entry_count & 63) == 0)
      {
        char progress[48];
        wsprintfA(progress, "Scanning... %d", entry_count);
        gui_status_slot(progress);
        if (g_status) UpdateWindow(g_status);
      }
    }
  }
  while (FindNextFileA(h, &fd) && entry_count < BROWSER_MAX_ENTRIES);

  FindClose(h);
}

/****************************************************************************
 * Folder picker
 ****************************************************************************/

static int pick_folder(HWND parent, char *out, int out_len)
{
  BROWSEINFOA bi;
  LPITEMIDLIST pidl;
  int ok = 0;

  ZeroMemory(&bi, sizeof(bi));
  bi.hwndOwner = parent;
  bi.lpszTitle = "Choose a folder to scan for ROMs";
  bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

  {
    int c = gui_cursor_show();   /* visible even if fullscreen has hidden it */
    pidl = SHBrowseForFolderA(&bi);
    gui_cursor_restore(c);
  }
  if (pidl)
  {
    if (SHGetPathFromIDListA(pidl, out)) ok = 1;
    CoTaskMemFree(pidl);
  }

  (void)out_len;
  return ok;
}

/****************************************************************************
 * List population and filtering
 ****************************************************************************/

static int search_shown(void)
{
  return search_forced || !gui.hide_searchbar;
}

/* The filter bar: what the scanned games really have, one drop-down list
   each. The lists point into entries[] and are rebuilt after every scan; the
   choice is remembered by its text, so it survives a rescan. */
#define FILTER_MAX 64
static const char *f_list[3][FILTER_MAX];
static int  f_count[3];
static char sel_con[40];
static char sel_reg[24];
static char sel_fol[GUI_PATH_LEN];
static int  sel_fol_on;               /* sel_fol is "" for the top level, so it needs its own flag */

static int cmp_text(const void *a, const void *b)
{
  return lstrcmpiA(*(const char *const *)a, *(const char *const *)b);
}

static void filter_add(int which, const char *s)
{
  int i;
  for (i = 0; i < f_count[which]; i++)
    if (!lstrcmpiA(f_list[which][i], s)) return;
  if (f_count[which] < FILTER_MAX) f_list[which][f_count[which]++] = s;
}

static void fill_filters(void)
{
  static const char *const all_text[3] = { "All Consoles", "All Regions", "All Folders" };
  int i, k;

  if (!panel_filter[0]) return;

  f_count[0] = f_count[1] = f_count[2] = 0;
  for (i = 0; i < entry_count; i++)
  {
    filter_add(0, entries[i].console);
    filter_add(1, entries[i].region);
    filter_add(2, entries[i].folder);
  }
  for (k = 0; k < 3; k++) qsort(f_list[k], (size_t)f_count[k], sizeof(f_list[k][0]), cmp_text);

  for (k = 0; k < 3; k++)
  {
    int sel = 0;

    SendMessageA(panel_filter[k], CB_RESETCONTENT, 0, 0);
    SendMessageA(panel_filter[k], CB_ADDSTRING, 0, (LPARAM)all_text[k]);
    for (i = 0; i < f_count[k]; i++)
    {
      const char *s = f_list[k][i];
      SendMessageA(panel_filter[k], CB_ADDSTRING, 0, (LPARAM)((k == 2 && !s[0]) ? "(top level)" : s));

      if (k == 0 && sel_con[0] && !lstrcmpiA(s, sel_con)) sel = i + 1;
      if (k == 1 && sel_reg[0] && !lstrcmpiA(s, sel_reg)) sel = i + 1;
      if (k == 2 && sel_fol_on && !lstrcmpiA(s, sel_fol)) sel = i + 1;
    }
    SendMessageA(panel_filter[k], CB_SETCURSEL, (WPARAM)sel, 0);

    if (!sel)   /* what was chosen is gone from the folder */
    {
      if (k == 0) sel_con[0] = '\0';
      if (k == 1) sel_reg[0] = '\0';
      if (k == 2) { sel_fol[0] = '\0'; sel_fol_on = 0; }
    }
  }
}

static void filter_chosen(int k)
{
  int idx = (int)SendMessageA(panel_filter[k], CB_GETCURSEL, 0, 0);
  const char *s = (idx > 0 && idx <= f_count[k]) ? f_list[k][idx - 1] : NULL;

  if (k == 0) lstrcpynA(sel_con, s ? s : "", sizeof(sel_con));
  if (k == 1) lstrcpynA(sel_reg, s ? s : "", sizeof(sel_reg));
  if (k == 2) { lstrcpynA(sel_fol, s ? s : "", sizeof(sel_fol)); sel_fol_on = (s != NULL); }
}

static void filters_reset(void)
{
  int k;
  sel_con[0] = sel_reg[0] = sel_fol[0] = '\0';
  sel_fol_on = 0;
  for (k = 0; k < 3; k++) SendMessageA(panel_filter[k], CB_SETCURSEL, 0, 0);
}

static void update_status_count(void)
{
  char count_text[64];

  if (!panel_visible) return;

  if (entry_count >= BROWSER_MAX_ENTRIES)
  {
    wsprintfA(count_text, "%d+ games (capped)", entry_count);
  }
  else if (filtered_count != entry_count)
  {
    wsprintfA(count_text, "%d of %d game%s", filtered_count, entry_count,
              (entry_count == 1) ? "" : "s");
  }
  else
  {
    wsprintfA(count_text, "%d game%s found", entry_count, (entry_count == 1) ? "" : "s");
  }

  gui_status_slot(count_text);
}

/* Compares two entries (by index into entries[]) on whichever column and
   direction is currently active -- used to sort filtered_index[] after
   filtering, before any rows are actually inserted, so rows always go
   into the ListView already in the right order rather than needing a
   second re-sort pass afterward. */
static int compare_entries(const void *a, const void *b)
{
  int ia = *(const int *)a;
  int ib = *(const int *)b;
  const browser_entry_t *ea = &entries[ia];
  const browser_entry_t *eb = &entries[ib];
  int result;

  switch (gui.browser_sort_column)
  {
    case 1:  result = lstrcmpiA(ea->console, eb->console); break;
    case 2:  result = lstrcmpiA(ea->folder, eb->folder);   break;
    case 3:  result = (ea->size_bytes > eb->size_bytes) - (ea->size_bytes < eb->size_bytes); break;
    default: result = lstrcmpiA(ea->name, eb->name);        break;
  }

  /* Ties broken by name, ascending, regardless of the sort's own
     direction -- otherwise every console/folder/size tie (there are a
     lot of these; most ROMs share a folder) would fall back to
     whatever order they happened to be discovered in, undoing the
     point of sorting by that column at all. */
  if (result == 0 && gui.browser_sort_column != 0)
    result = lstrcmpiA(ea->name, eb->name);

  return gui.browser_sort_ascending ? result : -result;
}

#define BROWSER_ICON_SIZE_MIN  48
#define BROWSER_ICON_SIZE_MAX  256
#define BROWSER_ICON_SIZE_STEP 32

static HIMAGELIST grid_imagelist;

/* Used for any entry with no cover art on disk -- a plain, theme-aware
   square rather than requiring a bundled placeholder resource file.
   Kept deliberately plain (no text, no icon glyph) since at small grid
   sizes anything more detailed just turns to noise. */
static HBITMAP make_placeholder_bitmap(int size)
{
  BITMAPINFO bmi;
  HBITMAP hbmp;
  unsigned char *bits;
  int dark = theme_is_dark();
  unsigned char fill = dark ? 70 : 190;
  unsigned char border = dark ? 110 : 140;
  int x, y;

  ZeroMemory(&bmi, sizeof(bmi));
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = size;
  bmi.bmiHeader.biHeight = -size;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  hbmp = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
  if (!hbmp) return NULL;

  for (y = 0; y < size; y++)
  {
    for (x = 0; x < size; x++)
    {
      unsigned char *px = bits + ((size_t)y * size + x) * 4;
      int on_border = (x < 2 || y < 2 || x >= size - 2 || y >= size - 2);
      unsigned char shade = on_border ? border : fill;

      px[0] = shade;
      px[1] = shade;
      px[2] = shade;
      px[3] = 255;
    }
  }

  return hbmp;
}

/* Grid view fills the image list lazily: an entry's picture is only read from
   disk and decoded when the list view first draws that entry (LVN_GETDISPINFO),
   not for every game in the library each time the list changes. Slot 0 of the
   list is the plain placeholder used for games without a cover. grid_gen
   changes whenever the list is rebuilt (zoom, rescan, cover changed), which
   makes every entry's remembered slot stale at once. */
static int grid_gen;
static int grid_placeholder_index;

void browser_rebuild_grid_images(void)
{
  int size = gui.browser_grid_size;
  HBITMAP placeholder;

  if (!panel_list) return;

  if (grid_imagelist)
  {
    ListView_SetImageList(panel_list, NULL, LVSIL_NORMAL);
    ImageList_Destroy(grid_imagelist);
    grid_imagelist = NULL;
  }

  grid_imagelist = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, 64, 64);
  if (!grid_imagelist) return;

  grid_placeholder_index = 0;
  placeholder = make_placeholder_bitmap(size);
  if (placeholder)
  {
    int idx = ImageList_Add(grid_imagelist, placeholder, NULL);
    grid_placeholder_index = (idx >= 0) ? idx : 0;
    DeleteObject(placeholder);
  }

  grid_gen++;

  ListView_SetImageList(panel_list, grid_imagelist, LVSIL_NORMAL);
  ListView_SetIconSpacing(panel_list, size + 24, size + 34);
  ListView_Arrange(panel_list, LVA_DEFAULT);
  RedrawWindow(panel_list, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static void browser_rebuild_grid_images_fwd(void)
{
  browser_rebuild_grid_images();
}

/* The image-list index to show for an entry, loading its cover the first time. */
static int cover_image_index(browser_entry_t *e)
{
  HBITMAP hbmp;
  int idx;

  if (e->img_gen == grid_gen && e->img_slot >= 0) return e->img_slot;
  e->img_gen = grid_gen;

  hbmp = coverart_load_cached(e->full_path, e->console, gui.browser_grid_size, &e->cover_ext);
  if (!hbmp)
  {
    e->img_slot = grid_placeholder_index;
    return e->img_slot;
  }

  idx = ImageList_Add(grid_imagelist, hbmp, NULL);
  DeleteObject(hbmp);
  e->img_slot = (idx >= 0) ? idx : grid_placeholder_index;
  return e->img_slot;
}

/* Refreshes just one entry's thumbnail after its cover was changed or
   removed via the context menu -- rebuilding the whole image list for
   a single-item change would be wasteful for a large library, but
   ImageList has no "replace in place" for a DIB source, so the
   practical equivalent is a full rebuild anyway; this at least skips
   it entirely when grid view isn't even active. */
static void browser_grid_refresh_cover(int item_index)
{
  /* The cover file for this entry just changed: forget what was remembered
     about it (including "no cover"), so it is looked up again. */
  if (item_index >= 0 && item_index < filtered_count)
    entries[filtered_index[item_index]].cover_ext = COVERART_UNCHECKED;

  if (gui.browser_grid_view) browser_rebuild_grid_images();
}

/* Switches the underlying ListView between report (list) and icon
   (grid) presentation. Both modes share the exact same items/columns
   underneath -- LVS_ICON just displays iImage/text instead of the
   detail columns, so no separate data structure is needed for grid
   view at all. */
void browser_apply_view_mode(void)
{
  if (!panel_list) return;

  ListView_SetView(panel_list, gui.browser_grid_view ? LV_VIEW_ICON : LV_VIEW_DETAILS);

  if (gui.browser_grid_view)
  {
    browser_rebuild_grid_images();
  }
  else if (grid_imagelist)
  {
    ListView_SetImageList(panel_list, NULL, LVSIL_NORMAL);
    ImageList_Destroy(grid_imagelist);
    grid_imagelist = NULL;
  }
}

#define GRID_ZOOM_TIMER_ID 1

/* Ctrl+wheel while grid view is active: grow/shrink the thumbnail size
   by one step. delta follows the usual WM_MOUSEWHEEL sign convention
   (positive = away from the user = zoom in). Ignored entirely outside
   grid view, so the main window doesn't need to track view mode itself
   before deciding whether to forward wheel events here.

   The actual rebuild (icon spacing plus every thumbnail) is deferred
   to a debounced WM_TIMER rather than done on every single tick, so
   scrolling fast triggers it once after scrolling settles instead of
   once per tick. Everything about the visual change -- spacing and
   images together -- lands in that one rebuild rather than being
   split across ticks: an earlier version applied icon spacing
   immediately on each tick for a bit of extra responsiveness, but
   that only partially repositions items internally, and racing that
   partial layout against a manual repaint was what caused a stale
   ghost of the previous size to briefly show during rapid scrolling.
   With the rebuild itself now fast (well under 150ms even for a large
   library), that split isn't worth the risk anymore. See WM_TIMER in
   browser_list_subclass_proc. */
void browser_grid_zoom(int delta)
{
  int new_size;

  if (!gui.browser_grid_view) return;

  new_size = gui.browser_grid_size + ((delta > 0) ? BROWSER_ICON_SIZE_STEP : -BROWSER_ICON_SIZE_STEP);
  if (new_size < BROWSER_ICON_SIZE_MIN) new_size = BROWSER_ICON_SIZE_MIN;
  if (new_size > BROWSER_ICON_SIZE_MAX) new_size = BROWSER_ICON_SIZE_MAX;
  if (new_size == gui.browser_grid_size) return;

  gui.browser_grid_size = new_size;
  SetTimer(panel_list, GRID_ZOOM_TIMER_ID, 150, NULL);
}

static void browser_apply_filter(void)
{
  char search[128];
  int i, row;

  if (!panel_list) return;

  if (search_shown())
    GetWindowTextA(panel_search, search, sizeof(search));
  else
    search[0] = '\0';

  filtered_count = 0;
  for (i = 0; i < entry_count; i++)
  {
    if (!contains_ci(entries[i].name, search)) continue;
    if (!gui.hide_filterbar)
    {
      if (sel_con[0] && lstrcmpiA(entries[i].console, sel_con)) continue;
      if (sel_reg[0] && lstrcmpiA(entries[i].region, sel_reg)) continue;
      if (sel_fol_on && lstrcmpiA(entries[i].folder, sel_fol)) continue;
    }
    filtered_index[filtered_count++] = i;
  }

  qsort(filtered_index, filtered_count, sizeof(filtered_index[0]), compare_entries);

  SendMessage(panel_list, WM_SETREDRAW, FALSE, 0);
  SendMessage(panel_list, LVM_DELETEALLITEMS, 0, 0);

  for (row = 0; row < filtered_count; row++)
  {
    LVITEMA item;
    char sizebuf[32];
    int idx = filtered_index[row];

    ZeroMemory(&item, sizeof(item));
    item.mask     = LVIF_TEXT | LVIF_IMAGE;
    item.iItem    = row;
    item.iSubItem = 0;
    item.iImage   = I_IMAGECALLBACK;    /* supplied on demand, see cover_image_index() */
    item.pszText  = entries[idx].name;
    SendMessageA(panel_list, LVM_INSERTITEMA, 0, (LPARAM)&item);

    item.iSubItem = 1;
    item.pszText  = entries[idx].console;
    SendMessageA(panel_list, LVM_SETITEMTEXTA, (WPARAM)row, (LPARAM)&item);

    item.iSubItem = 2;
    item.pszText  = entries[idx].folder[0] ? entries[idx].folder : "(top level)";
    SendMessageA(panel_list, LVM_SETITEMTEXTA, (WPARAM)row, (LPARAM)&item);

    format_size(entries[idx].size_bytes, sizebuf, sizeof(sizebuf));
    item.iSubItem = 3;
    item.pszText  = sizebuf;
    SendMessageA(panel_list, LVM_SETITEMTEXTA, (WPARAM)row, (LPARAM)&item);
  }

  SendMessage(panel_list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(panel_list, NULL, TRUE);

  update_status_count();
  browser_toolbar_update();
}

static void browser_rebuild_grid_images_fwd(void);

static void browser_rescan(void)
{
  HCURSOR old_cursor;

  if (!entries || !spare) return;

  /* Keep the last scan to reuse its archive labels, and scan into the other
     buffer. */
  {
    browser_entry_t *t = spare;
    spare = entries;
    spare_count = entry_count;
    entries = t;
  }
  entry_count = 0;

  if (gui.rom_dir[0])
  {
    old_cursor = SetCursor(LoadCursorA(NULL, IDC_WAIT));
    scan_dir(gui.rom_dir, "", 0);
    SetCursor(old_cursor);
  }

  /* The entries are new objects: start the grid's images over. */
  if (gui.browser_grid_view) browser_rebuild_grid_images_fwd();

  fill_filters();
  browser_apply_filter();
}

void browser_panel_change_folder(void)
{
  char picked[GUI_PATH_LEN];
  if (pick_folder(g_hwnd, picked, sizeof(picked)))
  {
    lstrcpynA(gui.rom_dir, picked, sizeof(gui.rom_dir));
    config_save();

    if (panel_visible)
    {
      /* Switches from the Choose Folder button to the normal search+list
         view (or just re-lays-out the same view if a folder was already
         set and this came from the File menu instead of the button). */
      browser_relayout();
      browser_panel_show(1);
    }
  }
}

static void browser_launch_selected(void)
{
  int sel = ListView_GetNextItem(panel_list, -1, LVNI_SELECTED);
  char path[GUI_PATH_LEN];

  if (sel < 0 || sel >= filtered_count) return;

  /* Copy before hiding: hiding the panel doesn't touch entries[], but
     there's no reason to rely on that not changing before emu_load_rom()
     actually reads it back out. */
  lstrcpynA(path, entries[filtered_index[sel]].full_path, sizeof(path));

  emu_load_rom(path);
}

/****************************************************************************
 * Column visibility
 ****************************************************************************/

static void apply_column_widths(void)
{
  int i;

  for (i = 0; i < BROWSER_COLUMNS; i++)
  {
    int w = (last_list_w * gui.browser_col_pct[i]) / 100;
    SendMessage(panel_list, LVM_SETCOLUMNWIDTH, (WPARAM)i, (LPARAM)(col_hidden[i] ? 0 : w));
  }
}

int browser_column_hidden(int col)
{
  return (col > 0 && col < BROWSER_COLUMNS) ? col_hidden[col] : 0;
}

/* Shows or hides one list column (Name, column 0, is always shown). */
void browser_toggle_column(int col)
{
  int i;

  if (col <= 0 || col >= BROWSER_COLUMNS) return;

  col_hidden[col] = !col_hidden[col];
  apply_column_widths();

  gui.browser_col_hidden = 0;
  for (i = 0; i < BROWSER_COLUMNS; i++)
  {
    if (col_hidden[i]) gui.browser_col_hidden |= (1 << i);
  }
  config_save();
}

static void show_column_menu(int screen_x, int screen_y)
{
  HMENU menu = CreatePopupMenu();
  int i, cmd;

  for (i = 0; i < BROWSER_COLUMNS; i++)
  {
    UINT flags = MF_STRING | (col_hidden[i] ? MF_UNCHECKED : MF_CHECKED);
    if (i == 0) flags |= MF_GRAYED;   /* Name is the row's identity, always shown */
    AppendMenuA(menu, flags, (UINT_PTR)(2000 + i), col_names[i]);
  }

  /* Documented Windows workaround (MSDN, TrackPopupMenu remarks): if the
     owning window isn't confirmed as the foreground window, the popup can
     fail to dismiss/behave correctly when an item is chosen. The trailing
     WM_NULL is the other half -- without it, some message-loop timing can
     still leave the menu visually stuck even though the selection itself
     went through. */
  SetForegroundWindow(g_hwnd);

  cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                       screen_x, screen_y, 0, g_hwnd, NULL);
  DestroyMenu(menu);

  PostMessage(g_hwnd, WM_NULL, 0, 0);

  if (cmd >= 2001 && cmd < 2000 + BROWSER_COLUMNS)
  {
    browser_toggle_column(cmd - 2000);
    gui_update_menu();
  }
}

/****************************************************************************
 * Panel lifecycle
 ****************************************************************************/

static void update_sort_arrow(void);

/* Ctrl+wheel zooms the grid; plain wheel is left alone entirely (passed
   through to the control's own default scroll handling) so normal
   scrolling in both list and grid view is completely unaffected. */
static LRESULT CALLBACK browser_list_subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                                    UINT_PTR subclass_id, DWORD_PTR ref_data)
{
  (void)subclass_id;
  (void)ref_data;

  if (msg == WM_MOUSEWHEEL && (LOWORD(wp) & MK_CONTROL))
  {
    browser_grid_zoom((short)HIWORD(wp));
    return 0;
  }

  if (msg == WM_TIMER && wp == GRID_ZOOM_TIMER_ID)
  {
    KillTimer(hwnd, GRID_ZOOM_TIMER_ID);
    browser_rebuild_grid_images();
    config_save();
    return 0;
  }

  return DefSubclassProc(hwnd, msg, wp, lp);
}

/* The container of the toolbar, search bar and filter bar: paints its own
   background in the theme's color and hands what its children send (clicks,
   drawing requests, colors) to the main window, which is where the rest of
   the program looks for them. */
static LRESULT CALLBACK bar_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
  {
    case WM_ERASEBKGND:
    {
      RECT r;
      HBRUSH br = CreateSolidBrush(tb_face_color());
      GetClientRect(hwnd, &r);
      FillRect((HDC)wp, &r, br);
      DeleteObject(br);
      return 1;
    }

    case WM_COMMAND:
    {
      LRESULT r = SendMessage(GetParent(hwnd), msg, wp, lp);

      /* While a game runs, a toolbar button must not keep the keyboard: the
         Enter key is the pad's Start button and would press it again. */
      if (hwnd == panel_toolbar && emu_running) SetFocus(GetParent(hwnd));
      return r;
    }

    case WM_DRAWITEM:
    case WM_NOTIFY:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
      return SendMessage(GetParent(hwnd), msg, wp, lp);
  }
  return DefWindowProc(hwnd, msg, wp, lp);
}

void browser_panel_create(HWND parent)
{
  HFONT font = gui_get_ui_font();
  LVCOLUMNA col;
  int i;

  entries = (browser_entry_t *)malloc(sizeof(browser_entry_t) * BROWSER_MAX_ENTRIES);
  spare   = (browser_entry_t *)malloc(sizeof(browser_entry_t) * BROWSER_MAX_ENTRIES);
  if (!entries || !spare)
  {
    free(entries); free(spare);
    entries = spare = NULL;
    MessageBoxA(parent, "Not enough memory for the ROM browser.",
                APP_NAME, MB_OK | MB_ICONERROR);
    return;
  }

  {
    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = bar_proc;
    wc.hInstance     = g_inst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "GPBrowserBar";
    RegisterClassA(&wc);
  }

  panel_bar = CreateWindowExA(WS_EX_CONTROLPARENT, "GPBrowserBar", "",
                              WS_CHILD | WS_CLIPCHILDREN,
                              0, 0, 0, 0, parent, (HMENU)(UINT_PTR)IDC_BROWSER_BAR,
                              g_inst, NULL);

  panel_toolbar = CreateWindowExA(WS_EX_CONTROLPARENT, "GPBrowserBar", "",
                                  WS_CHILD | WS_CLIPCHILDREN,
                                  0, 0, 0, 0, parent, NULL, g_inst, NULL);

  tb_open    = tb_button(panel_toolbar, IDM_FILE_OPEN, TI_OPEN, "Open ROM (Ctrl+O)");
  tb_refresh = tb_button(panel_toolbar, IDC_TB_REFRESH, TI_REFRESH, "Refresh the ROM list");
  tb_sep[0]  = tb_separator(panel_toolbar);
  tb_play    = tb_button(panel_toolbar, IDC_TB_PLAY, TI_PLAY, "Play the selected game");
  tb_stop    = tb_button(panel_toolbar, IDM_EMU_STOP, TI_STOP, "Stop (F3)");
  tb_reset   = tb_button(panel_toolbar, IDM_EMU_RESET, TI_RESET, "Reset (Ctrl+R)");
  tb_pause   = tb_button(panel_toolbar, IDM_EMU_PAUSE, TI_PAUSE, "Pause (F2)");
  tb_fs      = tb_button(panel_toolbar, IDM_VIDEO_FULLSCREEN_START, TI_FULLSCREEN, "Start ROM in Fullscreen");
  tb_shot    = tb_button(panel_toolbar, IDM_FILE_SCREENSHOT, TI_SCREENSHOT, "Save a Screenshot (F12)");
  tb_sep[1]  = tb_separator(panel_toolbar);
  tb_set     = tb_button(panel_toolbar, IDM_OPTIONS_SETTINGS, TI_SETTINGS, "Settings");
  tb_ctl     = tb_button(panel_toolbar, IDM_INPUT_P1, TI_CONTROLS, "Configure Player 1");

  panel_search_label = CreateWindowExA(0, "STATIC", "Search:", WS_CHILD | SS_LEFT,
                                       0, 0, 0, 0, panel_bar, (HMENU)(UINT_PTR)IDC_BROWSER_SEARCH_LABEL,
                                       g_inst, NULL);
  panel_search = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                 WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP,
                                 0, 0, 0, 0, panel_bar, (HMENU)(UINT_PTR)IDC_BROWSER_SEARCH,
                                 g_inst, NULL);
  panel_search_close = CreateWindowExA(0, "BUTTON", "Close", WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
                                       0, 0, 0, 0, panel_bar, (HMENU)(UINT_PTR)IDC_BROWSER_SEARCH_CLOSE,
                                       g_inst, NULL);

  for (i = 0; i < 3; i++)
  {
    panel_filter[i] = CreateWindowExA(0, "COMBOBOX", "",
                                      WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                      0, 0, 0, 0, panel_bar,
                                      (HMENU)(UINT_PTR)(IDC_BROWSER_FILTER_CONSOLE + i), g_inst, NULL);
  }
  panel_filter_reset = CreateWindowExA(0, "BUTTON", "Reset Filters", WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
                                       0, 0, 0, 0, panel_bar, (HMENU)(UINT_PTR)IDC_BROWSER_FILTER_RESET,
                                       g_inst, NULL);

  panel_list = CreateWindowExA(WS_EX_CLIENTEDGE, "SysListView32", "",
                               WS_CHILD | LVS_REPORT | LVS_SINGLESEL |
                               LVS_SHOWSELALWAYS | LVS_AUTOARRANGE | WS_TABSTOP,
                               0, 0, 0, 0, parent, (HMENU)(UINT_PTR)IDC_BROWSER_LIST,
                               g_inst, NULL);
  SetWindowSubclass(panel_list, browser_list_subclass_proc, 1, 0);

  panel_choose = CreateWindowExA(0, "BUTTON", "Choose ROMs Directory",
                                 WS_CHILD | WS_TABSTOP,
                                 0, 0, 0, 0, parent, (HMENU)(UINT_PTR)IDC_BROWSER_CHOOSE,
                                 g_inst, NULL);

  SendMessage(panel_search, WM_SETFONT, (WPARAM)font, TRUE);
  SendMessage(panel_search_label, WM_SETFONT, (WPARAM)font, TRUE);
  SendMessage(panel_search_close, WM_SETFONT, (WPARAM)font, TRUE);
  SendMessage(panel_filter_reset, WM_SETFONT, (WPARAM)font, TRUE);
  for (i = 0; i < 3; i++)
  {
    SendMessage(panel_filter[i], WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageA(panel_filter[i], CB_ADDSTRING, 0,
                 (LPARAM)(i == 0 ? "All Consoles" : i == 1 ? "All Regions" : "All Folders"));
    SendMessage(panel_filter[i], CB_SETCURSEL, 0, 0);
  }
  SendMessage(panel_list, WM_SETFONT, (WPARAM)font, TRUE);
  SendMessage(panel_choose, WM_SETFONT, (WPARAM)font, TRUE);

  SendMessage(panel_list, LVM_SETEXTENDEDLISTVIEWSTYLE,
              LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);

  for (i = 0; i < BROWSER_COLUMNS; i++)
  {
    col_hidden[i] = (gui.browser_col_hidden & (1 << i)) ? 1 : 0;
  }
  col_hidden[0] = 0;   /* Name is the row's identity, never actually toggleable */

  ZeroMemory(&col, sizeof(col));
  col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

  for (i = 0; i < BROWSER_COLUMNS; i++)
  {
    col.iSubItem = i;
    col.cx = col_hidden[i] ? 0 : (last_list_w * gui.browser_col_pct[i]) / 100;
    col.pszText = (LPSTR)col_names[i];
    SendMessageA(panel_list, LVM_INSERTCOLUMNA, (WPARAM)i, (LPARAM)&col);
  }
  update_sort_arrow();
  browser_toolbar_update();
}

/* Shows the toolbar, search bar and filter bar rows that are switched on
   (and none of them while the "Choose ROMs Directory" button is what is shown). */
static void show_bars(void)
{
  int on = panel_visible && gui.rom_dir[0];
  int sb = on && search_shown(), fb = on && !gui.hide_filterbar;
  int i;

  ShowWindow(panel_bar, (sb || fb) ? SW_SHOWNA : SW_HIDE);
  ShowWindow(panel_search_label, sb ? SW_SHOWNA : SW_HIDE);
  ShowWindow(panel_search, sb ? SW_SHOWNA : SW_HIDE);
  ShowWindow(panel_search_close, sb ? SW_SHOWNA : SW_HIDE);
  for (i = 0; i < 3; i++) ShowWindow(panel_filter[i], fb ? SW_SHOWNA : SW_HIDE);
  ShowWindow(panel_filter_reset, fb ? SW_SHOWNA : SW_HIDE);
}

/* Places the bar and its rows; returns the height it takes. */
static int layout_bar(int x, int y, int w)
{
  int m = TBS(4), rows = 0, ry = TBS(2);

  if (search_shown())
  {
    int lw = TBS(54), cw = TBS(72), eh = TBS(22);
    MoveWindow(panel_search_label, m, ry + TBS(4), lw, TBS(18), TRUE);
    MoveWindow(panel_search, m + lw, ry, w - 2 * m - lw - cw - TBS(6), eh, TRUE);
    MoveWindow(panel_search_close, w - m - cw, ry - 1, cw, eh + 2, TRUE);
    ry += eh + TBS(6);
    rows++;
  }

  if (!gui.hide_filterbar)
  {
    int rw = TBS(96), g3 = TBS(6), eh = TBS(22), k;
    int cw = (w - 2 * m - rw - 3 * g3) / 3;
    if (cw < 40) cw = 40;
    for (k = 0; k < 3; k++)
      MoveWindow(panel_filter[k], m + k * (cw + g3), ry, cw, TBS(220), TRUE);
    MoveWindow(panel_filter_reset, w - m - rw, ry - 1, rw, eh + 2, TRUE);
    ry += eh + TBS(6);
    rows++;
  }

  if (!rows) { MoveWindow(panel_bar, x, y, w, 0, TRUE); return 0; }

  MoveWindow(panel_bar, x, y, w, ry, TRUE);
  return ry;
}

int browser_panel_visible(void)
{
  return panel_visible;
}

void browser_panel_show(int show)
{
  int has_folder = gui.rom_dir[0] != '\0';

  panel_visible = show ? 1 : 0;

  ShowWindow(panel_list, (show && has_folder) ? SW_SHOW : SW_HIDE);
  ShowWindow(panel_choose, (show && !has_folder) ? SW_SHOW : SW_HIDE);
  show_bars();

  if (show)
  {
    browser_apply_view_mode();
    if (has_folder) browser_rescan();
    InvalidateRect(g_hwnd, NULL, TRUE);

    /* Coming back from a game: bring the browser's windows above whatever
       the renderer left behind and repaint every one of them, so the filter
       bar and the toolbar are drawn from scratch. */
    {
      HWND w[] = { panel_list, panel_bar, panel_toolbar };
      int i;
      for (i = 0; i < 3; i++)
        if (w[i])
        {
          SetWindowPos(w[i], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
          RedrawWindow(w[i], NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
    }
  }
}

/* Lays the panel out within the given area -- same region video would
   otherwise occupy (below the menu, above the status bar). */
void browser_panel_layout(const RECT *area)
{
  int x = area->left;
  int w = area->right - area->left;
  int y = area->top;

  if (w < 40) w = 40;

  if (!gui.rom_dir[0])
  {
    int btn_w = gui.large_ui ? 300 : 200;
    int btn_h = gui.large_ui ? 39 : 26;
    int cx = (area->left + area->right) / 2;
    int cy = (area->top + area->bottom) / 2;

    MoveWindow(panel_choose, cx - btn_w / 2, cy - btn_h / 2, btn_w, btn_h, TRUE);
    return;
  }

  y += layout_bar(x, y, w);

  {
    int list_h = area->bottom - y;
    if (list_h < 40) list_h = 40;

    MoveWindow(panel_list, x, y, w, list_h, TRUE);

    if (gui.browser_grid_view) ListView_Arrange(panel_list, LVA_DEFAULT);

    /* Vertical scrollbar (~18px) eats into the usable width regardless of
       column sizing, so it's subtracted up front rather than left to force
       a horizontal scrollbar that fixed pixel widths would risk causing. */
    last_list_w = w - 18;
    if (last_list_w < 100) last_list_w = 100;
    apply_column_widths();
  }
}

/****************************************************************************
 * Message routing -- called from main.c's window procedure. Control IDs
 * here (1060+) never overlap the menu command range, so there's no
 * ambiguity in claiming them before the menu dispatch sees them.
 ****************************************************************************/

int browser_panel_handle_command(WORD id, WORD notify_code)
{
  switch (id)
  {
    case IDC_BROWSER_SEARCH:
      if (notify_code == EN_CHANGE) browser_apply_filter();

      /* A search bar brought up with Ctrl+F while it is switched off goes
         away again once it is left empty. */
      if (notify_code == EN_KILLFOCUS && search_forced && GetWindowTextLengthA(panel_search) == 0)
      {
        search_forced = 0;
        browser_bars_changed();
      }
      return 1;

    case IDC_BROWSER_SEARCH_CLOSE:
      browser_toggle_searchbar();
      gui_update_menu();
      return 1;

    case IDC_BROWSER_FILTER_CONSOLE:
    case IDC_BROWSER_FILTER_REGION:
    case IDC_BROWSER_FILTER_FOLDER:
      if (notify_code == CBN_SELCHANGE)
      {
        filter_chosen(id - IDC_BROWSER_FILTER_CONSOLE);
        browser_apply_filter();
      }
      return 1;

    case IDC_BROWSER_FILTER_RESET:
      filters_reset();
      browser_apply_filter();
      return 1;

    case IDC_TB_REFRESH:
      browser_rescan();
      return 1;

    case IDC_TB_PLAY:
      browser_launch_selected();
      return 1;

    case IDC_BROWSER_CHOOSE:
      browser_panel_change_folder();
      return 1;
  }

  return 0;
}

/* Reflects the current sort column/direction as the little up/down arrow
   glyph in that column's header -- standard Explorer-list behaviour,
   rendered by the header control itself (comctl32 v6, already required
   by this app's manifest) once told which column and which way via
   HDF_SORTUP/HDF_SORTDOWN. */
static void update_sort_arrow(void)
{
  HWND header = ListView_GetHeader(panel_list);
  int i;

  if (!header) return;

  for (i = 0; i < BROWSER_COLUMNS; i++)
  {
    HDITEMA hdi;
    hdi.mask = HDI_FORMAT;
    SendMessageA(header, HDM_GETITEMA, i, (LPARAM)&hdi);
    hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
    if (i == gui.browser_sort_column)
      hdi.fmt |= gui.browser_sort_ascending ? HDF_SORTUP : HDF_SORTDOWN;
    SendMessageA(header, HDM_SETITEMA, i, (LPARAM)&hdi);
  }
}

int browser_panel_handle_notify(NMHDR *hdr)
{
  /* Grid view asks for each visible entry's picture as it draws it. */
  if (hdr->idFrom == IDC_BROWSER_LIST &&
      (hdr->code == LVN_GETDISPINFOA || hdr->code == LVN_GETDISPINFOW))
  {
    NMLVDISPINFOA *di = (NMLVDISPINFOA *)hdr;

    if ((di->item.mask & LVIF_IMAGE) && grid_imagelist &&
        di->item.iItem >= 0 && di->item.iItem < filtered_count)
    {
      di->item.iImage = cover_image_index(&entries[filtered_index[di->item.iItem]]);
    }
    return 1;
  }

  if (hdr->idFrom == IDC_BROWSER_LIST && hdr->code == LVN_ITEMCHANGED)
  {
    NMLISTVIEW *nm = (NMLISTVIEW *)hdr;
    if (nm->uChanged & LVIF_STATE) browser_toolbar_update();
    return 0;
  }

  if (hdr->idFrom == IDC_BROWSER_LIST && hdr->code == NM_DBLCLK)
  {
    browser_launch_selected();
    return 1;
  }

  if (hdr->code == HDN_ITEMCLICKA || hdr->code == HDN_ITEMCLICKW)
  {
    NMHEADERA *nmh = (NMHEADERA *)hdr;
    int col = nmh->iItem;

    if (col < 0 || col >= BROWSER_COLUMNS) return 0;

    if (col == gui.browser_sort_column)
      gui.browser_sort_ascending = !gui.browser_sort_ascending;
    else
    {
      gui.browser_sort_column = col;
      gui.browser_sort_ascending = 1;
    }

    update_sort_arrow();
    browser_apply_filter();
    config_save();
    return 1;
  }

  if (hdr->code == HDN_ENDTRACKA || hdr->code == HDN_ENDTRACKW)
  {
    NMHEADERA *nmh = (NMHEADERA *)hdr;
    int col = nmh->iItem;
    int new_width;

    if (col < 0 || col >= BROWSER_COLUMNS || col_hidden[col] || last_list_w <= 0)
      return 0;

    new_width = (int)SendMessage(panel_list, LVM_GETCOLUMNWIDTH, (WPARAM)col, 0);
    {
      int pct = (new_width * 100) / last_list_w;
      if (pct < 5) pct = 5;
      if (pct > 90) pct = 90;
      gui.browser_col_pct[col] = pct;
    }
    config_save();
    return 0;   /* FALSE allows the resize the user just made to actually take effect */
  }

  return 0;
}

/* WM_CONTEXTMENU reports the ListView itself as its target for a header
   right-click too, not the header's own child handle -- confirmed by
   direct testing, not assumed. Checking whether the click coordinates
   actually land inside the header's screen rectangle is what reliably
   tells the two apart. */
/* Extracts the base name (no directory, no extension) the same way
   coverart.c and main.c's rom_base both do, for locating this ROM's
   save state files without needing it to be the currently-loaded one. */
static void guess_base_name(const char *path, char *out, int out_len)
{
  const char *slash = strrchr(path, '\\');
  const char *name = slash ? slash + 1 : path;
  const char *dot = strrchr(name, '.');
  int len = dot ? (int)(dot - name) : (int)lstrlenA(name);
  if (len >= out_len) len = out_len - 1;
  memcpy(out, name, (size_t)len);
  out[len] = '\0';
}

/* Loads this ROM only if it isn't already the one running -- ROM
   Information and Edit Cheats both need a loaded ROM to have anything
   meaningful to show, but there's no reason to interrupt/reset a game
   already in progress if the user right-clicked the same entry.
   When nothing at all is running, uses the non-disruptive peek (no
   audio, no system reset, browser stays visible) rather than actually
   launching the game just to look at its header or cheat list -- a
   full load is only used as a last resort, when a genuinely different
   ROM is already mid-game and there's no safe way to inspect this one
   without touching the memory the running game is using. */
static void ensure_rom_loaded(const char *path)
{
  if (emu_running && lstrcmpiA(emu_rom_path(), path) == 0) return;
  if (!emu_running && emu_peek_rom(path)) return;
  emu_load_rom(path);
}

static void show_item_context_menu(int screen_x, int screen_y, int item_index)
{
  HMENU menu, state_menu;
  int cmd, i;
  char path[GUI_PATH_LEN];
  char base[160];
  char cover_path[GUI_PATH_LEN];
  char console[40];
  int has_cover;
  int peeked;

  if (item_index < 0 || item_index >= filtered_count) return;
  lstrcpynA(path, entries[filtered_index[item_index]].full_path, sizeof(path));
  lstrcpynA(console, entries[filtered_index[item_index]].console, sizeof(console));
  guess_base_name(path, base, sizeof(base));

  menu = CreatePopupMenu();
  state_menu = CreatePopupMenu();

  /* Save states are named after the game's data, so the game has to be
     inspected (without starting it) to find them. Nothing is running while
     the browser is shown. If it cannot be inspected, fall back to the plain
     name older versions used. */
  peeked = (!emu_running && emu_peek_rom(path));

  for (i = 0; i < GUI_SLOT_MAX; i++)
  {
    char state_file[GUI_PATH_LEN];
    char label[64], stamp[32];
    UINT flags = MF_STRING;

    if (peeked) state_path(i, state_file, sizeof(state_file));
    else        gui_snprintf(state_file, (int)sizeof(state_file), "%s\\%s.gp%d", osd_path("states"), base, i);
    if (state_file_time(state_file, stamp, sizeof(stamp)))
    {
      wsprintfA(label, "Slot %d - %s", i, stamp);
    }
    else
    {
      flags |= MF_GRAYED;
      wsprintfA(label, "Slot %d - Empty", i);
    }
    AppendMenuA(state_menu, flags, (UINT_PTR)(IDM_BROWSER_PLAY_STATE_BASE + i), label);
  }

  has_cover = coverart_find(path, console, cover_path, sizeof(cover_path));

  AppendMenuA(menu, MF_STRING, IDM_BROWSER_PLAY, "Play Game");
  AppendMenuA(menu, MF_STRING | MF_POPUP, (UINT_PTR)state_menu, "Play Game with State");
  AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
  AppendMenuA(menu, MF_STRING, IDM_BROWSER_REFRESH, "Refresh ROMs Directory");
  AppendMenuA(menu, MF_STRING, IDM_BROWSER_CHANGE_DIR, "Change ROMs Directory");
  AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
  AppendMenuA(menu, MF_STRING, IDM_BROWSER_ROMINFO, "ROM Information");
  AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
  AppendMenuA(menu, MF_STRING, IDM_BROWSER_CHEATS, "Edit Cheats");
  AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
  AppendMenuA(menu, MF_STRING, IDM_BROWSER_SET_COVER, "Change Cover Image");
  AppendMenuA(menu, MF_STRING | (has_cover ? 0 : (UINT)MF_GRAYED), IDM_BROWSER_REMOVE_COVER, "Remove Cover Image");

  /* Same owner-draw system the main menu bar uses for large_ui -- these
     are standard WM_MEASUREITEM/WM_DRAWITEM messages, delivered to
     g_hwnd regardless of which menu they originate from, so the
     handlers already wired up for the main menu pick these up with no
     extra plumbing. Dark/light coloring for a *non*-owner-drawn popup
     (large_ui off) is handled by Windows itself via the system theme
     this app already sets up; nothing extra is needed here for that
     case. */
  if (gui.large_ui)
  {
    theme_ownerdraw_menu(menu);
    theme_ownerdraw_menu(state_menu);
  }

  SetForegroundWindow(g_hwnd);
  cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                       screen_x, screen_y, 0, g_hwnd, NULL);
  DestroyMenu(menu);   /* also destroys state_menu, since it's attached as a submenu */
  PostMessage(g_hwnd, WM_NULL, 0, 0);

  if (cmd == 0) return;

  ListView_SetItemState(panel_list, item_index, LVIS_SELECTED | LVIS_FOCUSED,
                        LVIS_SELECTED | LVIS_FOCUSED);

  if (cmd == IDM_BROWSER_PLAY)
  {
    emu_load_rom(path);
  }
  else if (cmd >= IDM_BROWSER_PLAY_STATE_BASE && cmd < IDM_BROWSER_PLAY_STATE_BASE + GUI_SLOT_MAX)
  {
    int slot = cmd - IDM_BROWSER_PLAY_STATE_BASE;
    emu_load_rom(path);
    emu_load_state(slot);
  }
  else if (cmd == IDM_BROWSER_REFRESH)
  {
    browser_rescan();
  }
  else if (cmd == IDM_BROWSER_CHANGE_DIR)
  {
    browser_panel_change_folder();
  }
  else if (cmd == IDM_BROWSER_ROMINFO)
  {
    ensure_rom_loaded(path);
    dlg_rom_info(g_hwnd);
  }
  else if (cmd == IDM_BROWSER_CHEATS)
  {
    ensure_rom_loaded(path);
    dlg_cheats(g_hwnd);
  }
  else if (cmd == IDM_BROWSER_SET_COVER)
  {
    char picked[GUI_PATH_LEN];
    char covers_dir[GUI_PATH_LEN];
    picked[0] = '\0';

    /* Always start in ./covers (next to the exe); created on demand in
       case it was deleted while the app was running. */
    CreateDirectoryA(osd_path("covers"), NULL);
    lstrcpynA(covers_dir, osd_path("covers"), sizeof(covers_dir));

    if (gui_pick_file(g_hwnd, GUI_PICK_COVER, "Choose Cover Image",
                      "Image files\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0All files\0*.*\0\0",
                      covers_dir, picked, sizeof(picked)))
    {
      coverart_set(path, console, picked);
      browser_grid_refresh_cover(item_index);
    }
  }
  else if (cmd == IDM_BROWSER_REMOVE_COVER)
  {
    coverart_remove(path, console);
    browser_grid_refresh_cover(item_index);
  }
}

int browser_panel_handle_contextmenu(HWND target, int x, int y)
{
  HWND header;
  RECT header_rect;
  POINT pt;
  LVHITTESTINFO hit;

  if (!panel_visible || !panel_list) return 0;
  if (target != panel_list) return 0;

  header = ListView_GetHeader(panel_list);
  if (header)
  {
    GetWindowRect(header, &header_rect);
    if (x >= header_rect.left && x < header_rect.right &&
        y >= header_rect.top  && y < header_rect.bottom)
    {
      show_column_menu(x, y);
      return 1;
    }
  }

  /* Not on the header -- check whether it landed on a row (client
     coordinates, since that's what LVM_HITTEST expects) and show the
     per-item menu if so. A right-click on empty list space below the
     last row is left alone entirely (no menu), matching Explorer's own
     "right-click empty space does something different" convention
     rather than showing a menu for whatever the previous selection
     happened to be. */
  if (x == -1 && y == -1)
  {
    /* Keyboard-triggered (Shift+F10 / the Menu key) rather than an
       actual mouse click -- Windows deliberately sends (-1,-1) here to
       mean "you pick a sensible spot", based on the current selection
       rather than any cursor position. */
    int sel = ListView_GetNextItem(panel_list, -1, LVNI_SELECTED);
    RECT item_rect;

    if (sel < 0) return 1;
    ListView_GetItemRect(panel_list, sel, &item_rect, LVIR_LABEL);
    pt.x = item_rect.left + 8;
    pt.y = (item_rect.top + item_rect.bottom) / 2;
    ClientToScreen(panel_list, &pt);

    show_item_context_menu(pt.x, pt.y, sel);
    return 1;
  }

  pt.x = x;
  pt.y = y;
  ScreenToClient(panel_list, &pt);
  hit.pt = pt;
  if (ListView_HitTest(panel_list, &hit) < 0) return 1;   /* still "handled" -- suppress the default menu */
  if (!(hit.flags & LVHT_ONITEM)) return 1;

  show_item_context_menu(x, y, hit.iItem);
  return 1;
}

HBRUSH browser_panel_ctlcolor(HWND ctrl, HDC hdc)
{
  if (ctrl == panel_search_label)
  {
    if (theme_is_dark()) return theme_ctlcolor(hdc);

    SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
    SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
    return GetSysColorBrush(COLOR_BTNFACE);
  }

  if (ctrl != panel_search) return NULL;

  if (theme_is_dark())
    return theme_ctlcolor_custom(hdc, RGB(0x38, 0x38, 0x38), 0);

  SetBkMode(hdc, OPAQUE);
  SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
  SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
  return (HBRUSH)(COLOR_WINDOW + 1);
}

/* Enter, pressed anywhere in the panel (including the search box), plays
   the selected -- or first, if nothing is explicitly selected -- entry. */
int browser_panel_handle_return(void)
{
  HWND f;

  if (!panel_visible) return 0;

  /* Enter plays the game only from the list or the search box. From the
     toolbar, the filter drop-downs and the bar's buttons it belongs to that
     control (pick an item, press the button). */
  f = GetFocus();
  if (f && f != panel_list && f != panel_search && !IsChild(panel_list, f) && f != g_hwnd)
    return 0;

  if (ListView_GetNextItem(panel_list, -1, LVNI_SELECTED) < 0 && filtered_count > 0)
  {
    ListView_SetItemState(panel_list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
  }
  browser_launch_selected();
  return 1;
}

/****************************************************************************
 * Toolbar, search bar and filter bar: called from main.c
 ****************************************************************************/

/* Brings the bars in line with the View menu's switches and lays out again. */
void browser_bars_changed(void)
{
  if (!panel_bar) return;

  if (!search_shown()) SetWindowTextA(panel_search, "");
  show_bars();
  browser_toolbar_layout();
  browser_relayout();
  if (panel_visible) browser_apply_filter();
}

/* Ctrl+F: shows the search bar if it is hidden, and puts the cursor in it. */
void browser_focus_search(void)
{
  if (!panel_visible || !gui.rom_dir[0]) return;

  if (!search_shown())
  {
    search_forced = 1;
    browser_bars_changed();
  }
  SetFocus(panel_search);
  SendMessage(panel_search, EM_SETSEL, 0, -1);
}

/* The pressed look of the toolbar's switches. */
void browser_toolbar_update(void)
{
  if (!tb_fs) return;

  tb_set_checked(tb_fs, gui.fullscreen_on_load);

  /* These act on the running game, so they are grayed while there is none. */
  /* Play needs a game picked in the list. */
  EnableWindow(tb_play, !emu_running && panel_list && ListView_GetSelectedCount(panel_list) > 0);

  EnableWindow(tb_stop, emu_running);
  EnableWindow(tb_reset, emu_running);
  EnableWindow(tb_pause, emu_running);
  EnableWindow(tb_shot, emu_running);
  tb_set_checked(tb_pause, emu_running && emu_paused);
}

/* The toolbar is shown above the game too, in a window; in fullscreen it
   goes away. 0 when it is not shown. */
int browser_toolbar_height(void)
{
  if (!panel_toolbar || gui.hide_toolbar || gui.fullscreen) return 0;
  return TOOLBAR_H;
}

/* Places the toolbar (or hides it) and moves the picture's area with it. */
void browser_toolbar_layout(void)
{
  static int last_h = -1;
  int h = browser_toolbar_height();
  RECT c;

  if (!panel_toolbar) return;

  if (h)
  {
    HWND row[] = { tb_open, tb_refresh, tb_sep[0], tb_play, tb_stop, tb_reset, tb_pause, tb_fs, tb_shot,
                   tb_sep[1], tb_set, tb_ctl };
    int bw = TBS(36), bh = TBS(32), gap = TBS(2), sep = TBS(14), bx = TBS(4), by = (h - bh) / 2, k;

    GetClientRect(g_hwnd, &c);
    MoveWindow(panel_toolbar, 0, 0, c.right, h, TRUE);

    for (k = 0; k < (int)(sizeof(row) / sizeof(row[0])); k++)
    {
      int is_sep = (row[k] == tb_sep[0] || row[k] == tb_sep[1]);
      int iw = is_sep ? sep : bw;
      MoveWindow(row[k], bx, by, iw, bh, TRUE);
      ShowWindow(row[k], SW_SHOWNA);
      bx += iw + (is_sep ? 0 : gap);
    }
    ShowWindow(panel_toolbar, SW_SHOWNA);
  }
  else
  {
    ShowWindow(panel_toolbar, SW_HIDE);
  }

  if (h != last_h)
  {
    last_h = h;
    video_invalidate();
    InvalidateRect(g_hwnd, NULL, TRUE);
  }
}

/* Whether the search bar is up (the View menu's check mark follows this). */
int browser_search_shown(void)
{
  return search_shown();
}

/* View > Show Search Bar and the bar's Close button: shown <-> hidden, and
   remembered. */
void browser_toggle_searchbar(void)
{
  gui.hide_searchbar = search_shown() ? 1 : 0;
  search_forced = 0;
  browser_bars_changed();
  config_save();
}
