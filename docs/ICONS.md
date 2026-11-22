# Icons

LordOS Tahoe uses two complementary icon paths:

1. **Native LordOS apps** draw their own app artwork as React icon components. These are part of each app's visual identity and are adapted with the Tahoe design system.
2. **Linux apps and status/tray items** use freedesktop icon names. The C resolver in `server/src/iconfind.c` finds PNG/SVG files from installed icon themes and hands SVGs to `lord-media` for rendering.

## MacTahoe icon theme

The default freedesktop icon theme is [MacTahoe-icon-theme](https://github.com/vinceliuice/MacTahoe-icon-theme), pinned to revision
`839848b9a8a38a92a6936e30c4abe35cc6f2546d`.

Install it with:

```sh
sudo bash packaging/icons/install-mactahoe.sh
```

The installer places the upstream blue, light and dark variants in `/usr/share/icons` and preserves the upstream `COPYING` and `AUTHORS` files. It can also install into an image staging root with `DESTDIR=/path/to/root`. See [packaging/icons/README.md](../packaging/icons/README.md).

The icon resolver checks the user's MacTahoe and hicolor directories before system variants, then Adwaita as a fallback. This preserves per-user hicolor icon overrides. GTK receives `MacTahoe-blue` through both the system settings.ini fallback and the Settings portal's `org.gnome.desktop.interface/icon-theme` key.

## Tahoe-native app icons

The upstream theme is a system icon theme, not a replacement for LordOS's custom app artwork. Native apps currently export React components (some in `apps/<id>/icons.jsx`, others from their app entry point); these are rendered in the Dock, launcher, switcher and app surfaces. The Tahoe theme will provide a coherent set of native app icons for Files, Settings, Terminal, Photos, Calendar, Mail, Notes, Store and the other bundled apps, while preserving each app's recognizable purpose.

Do not replace all native app icons with generic symbolic icons. Use the upstream theme for Linux desktop entries, standard actions, devices, MIME types, folders and status icons; use Tahoe-native artwork for the built-in apps.
