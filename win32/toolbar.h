/****************************************************************************
 *  Genesis Plus -- Win32 GUI frontend
 *
 *  toolbar.h -- the flat, icon-only buttons of the ROM browser's toolbar.
 *  The icons are drawn in code (no picture files), in the theme's text
 *  color, so they follow the light and dark themes.
 ****************************************************************************/

#ifndef _TOOLBAR_H_
#define _TOOLBAR_H_

enum { TI_OPEN, TI_REFRESH, TI_PLAY, TI_STOP, TI_RESET, TI_PAUSE, TI_FULLSCREEN,
       TI_SCREENSHOT, TI_SETTINGS, TI_CONTROLS, TI_N };

/* Sizes follow the Larger UI setting. */
#define TBS(v) (gui.large_ui ? (((v) * 1175 + 500) / 1000) : (v))

/* A flat button showing `icon`; `tip` is its tooltip (must stay valid). */
HWND tb_button(HWND parent, int id, int icon, const char *tip);

/* A thin vertical divider between groups of buttons. */
HWND tb_separator(HWND parent);

/* The pressed look of a switch (e.g. the current view). */
void tb_set_checked(HWND button, int on);

/* From WM_DRAWITEM: 1 when the item was one of the toolbar's. */
int tb_draw(const DRAWITEMSTRUCT *di);

/* The face color the toolbar, search bar and filter bar sit on. */
COLORREF tb_face_color(void);

#endif
