/****************************************************************************
 *  Genesis Plus -- Win32 GUI frontend
 *
 *  history.c -- when each game was last opened (View > Recently Played First).
 *
 *  Kept in history.dat next to the program, one line per game:
 *    last opened (Unix time) <TAB> full path
 *  (Older files with two more numbers in front are still read.)
 ****************************************************************************/

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "shared.h"
#include "gui.h"

typedef struct
{
  char path[GUI_PATH_LEN];
  unsigned long long last;
} hist_t;

static hist_t *h_list;
static int h_n, h_cap;
static int h_current = -1;      /* the game being played, or -1 */
static int h_dirty;

static int h_find(const char *path)
{
  int i;
  for (i = 0; i < h_n; i++)
    if (!lstrcmpiA(h_list[i].path, path)) return i;
  return -1;
}

static int h_add(const char *path)
{
  if (h_n >= h_cap)
  {
    int cap = h_cap ? h_cap * 2 : 256;
    hist_t *n = (hist_t *)realloc(h_list, sizeof(hist_t) * (size_t)cap);
    if (!n) return -1;
    h_list = n;
    h_cap = cap;
  }
  memset(&h_list[h_n], 0, sizeof(hist_t));
  lstrcpynA(h_list[h_n].path, path, GUI_PATH_LEN);
  return h_n++;
}

static void history_file(char *out, int len)
{
  lstrcpynA(out, osd_path("history.dat"), len);
}

void history_load(void)
{
  char file[GUI_PATH_LEN], line[GUI_PATH_LEN + 80];
  FILE *fp;

  h_n = 0;
  h_current = -1;
  history_file(file, sizeof(file));
  fp = fopen(file, "rb");
  if (!fp) return;

  while (fgets(line, sizeof(line), fp))
  {
    unsigned long long last;
    char *p = line, *path;
    int idx, tabs = 0;

    for (path = line; *path; path++) if (*path == '\t') tabs++;
    if (tabs >= 3)                       /* the old layout: seconds, times, last, path */
    {
      strtoull(p, &p, 10);
      if (*p != '\t') continue;
      strtoull(p + 1, &p, 10);
      if (*p != '\t') continue;
      p++;
    }
    last = strtoull(p, &p, 10);
    if (*p != '\t') continue;
    path = p + 1;
    path[strcspn(path, "\r\n")] = '\0';
    if (!path[0]) continue;

    idx = h_add(path);
    if (idx < 0) break;
    h_list[idx].last = last;
  }
  fclose(fp);
  h_dirty = 0;
}

void history_save(void)
{
  char file[GUI_PATH_LEN], tmp[GUI_PATH_LEN + 8];
  FILE *fp;
  int i, ok = 1;

  if (!h_dirty) return;
  history_file(file, sizeof(file));
  wsprintfA(tmp, "%s.tmp", file);
  fp = fopen(tmp, "wb");
  if (!fp) return;

  for (i = 0; i < h_n && ok; i++)
    ok = fprintf(fp, "%llu\t%s\r\n", h_list[i].last, h_list[i].path) >= 0;
  if (fflush(fp) != 0) ok = 0;
  if (fclose(fp) != 0) ok = 0;

  if (ok && MoveFileExA(tmp, file, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) h_dirty = 0;
  else DeleteFileA(tmp);
}

/* A game was opened: now is when it was last played. */
void history_game_started(const char *path)
{
  int i;

  if (!path || !path[0]) return;
  i = h_find(path);
  if (i < 0) i = h_add(path);
  if (i < 0) return;

  h_list[i].last = (unsigned long long)time(NULL);
  h_current = i;
  h_dirty = 1;
  history_save();
}

void history_game_ended(void)
{
  h_current = -1;
  history_save();
}

unsigned long long history_last(const char *path)
{
  int i = h_find(path);
  return i < 0 ? 0 : h_list[i].last;
}
