/* iconfind.h — the file for an icon name of a Linux app (a .desktop's Icon=, a tray item's IconName), in the
 * installed icon themes. apps.c uses it for the Dock, the launcher and the tray; build/iconfind-test checks the
 * order over a made-up tree.
 *
 * PNGs first: the user's MacTahoe variants and hicolor, then the system's MacTahoe variants and hicolor,
 * preferring high-resolution PNGs suitable for the Dock, then /usr/share/pixmaps. Then SVG, which lord-media draws (media/svgicon.c): category/size
 * and category/scalable (MacTahoe), scalable/category (hicolor), then Adwaita and /usr/share/pixmaps; an app's
 * AppStream PNG; and last symbolic/ icons, which
 * the bar recolors to its text color. */
#ifndef LORD_ICONFIND_H
#define LORD_ICONFIND_H

#include <limits.h>
#include <stddef.h>

#define ICONFIND_THEMES 9

typedef struct {
    char themes[ICONFIND_THEMES][PATH_MAX];     /* MacTahoe variants, hicolor and Adwaita, in lookup order */
    int nthemes, npng;                          /* the first npng are searched for PNGs too */
    char pixmaps[PATH_MAX];                     /* /usr/share/pixmaps */
    char appstream[PATH_MAX];                   /* the package engine's 64 px AppStream PNGs (<package>_<icon>.png) */
} IconRoots;

/* MacTahoe-blue variants plus hicolor in the user's and system icon directories, then system Adwaita.
 * prefix is the filesystem root ("" outside tests). */
void iconfind_roots(IconRoots *r, const char *home, const char *prefix);
/* The active MacTahoe variant follows the system's dark/light appearance. Invalid names are ignored. */
void iconfind_set_variant(const char *variant);

/* A .desktop's Icon=: a name (no "..") or an absolute .png/.svg path. A tray's IconName: a plain name of
 * [A-Za-z0-9._+-], at most 128 characters, looked for in status/ before apps/ at the bar's sizes. 1 if found
 * (out = the file), else 0 and out = "". */
int iconfind_app(const IconRoots *r, const char *name, char *out, size_t n);
int iconfind_status(const IconRoots *r, const char *name, char *out, size_t n);

/* Is the file (already through realpath) one lord-server may hand lord-media as an SVG icon? A .svg under the
 * system's or the user's icon folders, /usr/share, /usr/local/share or /opt (absolute Icon= paths). */
int iconfind_svg_allowed(const char *real, const char *home);

#endif
