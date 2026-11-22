# MacTahoe icon theme

LordOS Tahoe uses the upstream [MacTahoe-icon-theme](https://github.com/vinceliuice/MacTahoe-icon-theme)
for freedesktop/GTK icon names used by Linux apps, tray items and status icons.

## Install

```sh
sudo bash packaging/icons/install-mactahoe.sh
```

The installer downloads a pinned upstream revision (not a moving branch), then invokes the
upstream installer to install the blue, light and dark icon variants under
`/usr/share/icons`. It also installs the matching cursor theme. For an offline checkout,
set `MACTAHOE_SOURCE_DIR=/path/to/MacTahoe-icon-theme`.

For package/image staging:

```sh
DESTDIR=/tmp/lordos-root bash packaging/icons/install-mactahoe.sh
```

The C icon resolver searches the MacTahoe variants before system Adwaita and retains the
user's `~/.local/share/icons/hicolor` overrides. GTK's system fallback is set to
`MacTahoe-blue` in `assets/gtk/settings.ini`; a user's GTK settings or GSettings preference
can still override it.

## License and attribution

The upstream project is distributed under **GPL-3.0**. The upstream installer copies its
`COPYING` and `AUTHORS` files into each installed icon theme. Keep those files and the
upstream attribution when redistributing the theme.

Pinned upstream revision: `839848b9a8a38a92a6936e30c4abe35cc6f2546d`.

## Scope

This integration supplies standard icon-theme assets to the Linux app and status-icon lookup
path. LordOS built-in applications currently draw their own app icons as React components;
those will be redesigned as Tahoe-native app icons in the Tahoe theme rather than silently
replaced with unrelated generic icons.
