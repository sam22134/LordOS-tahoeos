# Tahoe Design System

## First implementation slice

The first Tahoe desktop theme lives in `themes/tahoe/` and is independently selectable from Lord Flow.

- `theme.json`: registers the theme as **Tahoe**.
- `index.jsx`: owns the complete desktop composition, window work area, menu-bar/Dock layers, app switcher, overview, launchpad and system panels. It starts the shell through the same supported SDK entry point as the bundled themes.
- `tokens.js`: defines Tahoe's color, material, border, shadow, blur and shared-control shape tokens.
- `components/MenuBar.jsx`: Tahoe's 32 px translucent top bar, with softer rounded menu-item highlights, a subtle lower edge and shadow.
- `components/Window.jsx`: Tahoe's 46 px title bar, 16 px frame corners, compact traffic lights and theme-driven active/inactive window shadows. Window movement, resizing, focus, CSD integration and title context menus remain the LordOS implementations.
- `components/Dock.jsx`: Tahoe glass Dock with its own elevated shadow token; magnification, drag/reorder, minimized-window tiles, context menus and multi-edge placement remain shared behavior.
- `components/ControlCenter.jsx`: Tahoe quick-settings panel with a 20 px outer radius, 16 px tiles and theme-driven panel shadow.
- `components/NotificationCenter.jsx`: Tahoe notification/calendar panel with matching glass, rounded notification cards and the same panel elevation token.
- `components/Launchpad.jsx`: Tahoe app launcher with a frosted search field, rounded category chips and individual elevated icon tiles; app discovery, search, drag-to-Dock and context actions remain the SDK implementations.
- `components/Menu.jsx`: Tahoe menu surface with 27 px rows, larger rounded highlights, popup material and theme-driven shadow. Keyboard navigation, safe submenu placement, focus restoration and accessibility behavior remain unchanged.

The shell now has Tahoe-specific presentation for its Menu Bar, Dock, window frame, Control Center, Notification Center, Launchpad and menus. These components retain LordOS's existing interaction and window-management paths instead of forking system behavior.

## Visual contract

- Accent defaults to Tahoe blue (`#0a84ff`), while the user's saved accent selection is preserved.
- Light and dark appearances have separate surfaces, separators, text and shadow values.
- When desktop effects are enabled, menu, panel and Dock surfaces use translucent materials and a 28 px blur token. When effects are disabled, the same surfaces become opaque and blur is zero.
- Window and shared-control geometry uses rounded corners: 11 px controls, 14 px groups and 13 px menus.
- Shared app controls receive the theme's shape, selection, focus-ring and border tokens through the existing `ThemeProvider` contract.
- Tahoe's shell shortcut table selects `Alt+Tab`; the SDK's user-configurable switcher preferences remain authoritative.

## Boundaries and follow-up

- This is a new theme, not a replacement for Lord Flow or Lord Classic.
- No compositor, Wayland, window-manager, application runtime, MIME or security code is changed by this visual slice.
- The menu bar, Dock, window chrome, Control Center, Notification Center, Launchpad and general menus now have Tahoe-specific presentation components. The underlying shell services and interaction contracts remain shared with LordOS.
- The freedesktop/GTK MacTahoe icon theme is already integrated into LordOS packaging and Linux-app/status-icon lookup (`packaging/icons/` and `docs/ICONS.md`). Tahoe-native artwork for built-in React apps is still pending; third-party Linux app icons should continue using their installed icon theme.
- Tahoe-specific wallpapers and sound assets have not yet been added. The theme currently offers the LordOS wallpaper collection.
- A source-level registration is not a build or runtime validation. Run `npm run build`, switch to Tahoe in Settings → Appearance → Desktop style, and test multi-display, RTL, Reduce Motion, fullscreen and low-effects configurations before calling the theme validated.
- Themes not present in the signed release manifest remain development-build-only, as described in `docs/THEMES-GUIDE.md`. A release must include the new theme in the verified-code signing pipeline.


## Tahoe native application icons

The Tahoe shell has a mapped icon accent component for selected built-in LordOS app IDs, rendered in Launchpad and Dock. It falls back to the app's own icon for unmapped IDs, including third-party desktop, Flatpak and web apps. This initial set uses lightweight runtime primitives rather than a separate image dependency.
