# Tahoe Design System

## First implementation slice

The first Tahoe desktop theme lives in `themes/tahoe/` and is independently selectable from Lord Flow.

- `theme.json`: registers the theme as **Tahoe**.
- `index.jsx`: owns the complete desktop composition, window work area, menu-bar/Dock layers, app switcher, overview, launchpad and system panels. It starts the shell through the same supported SDK entry point as the bundled themes.
- `tokens.js`: defines Tahoe's color, material, border, shadow, blur and shared-control shape tokens.

The first slice deliberately reuses Lord Flow's existing native shell components from `themes/default/components/` instead of copying or forking their event handling. This keeps window management, Dock interactions, context menus, Control Center, notifications, multi-display behavior, widgets and system prompts on the existing tested paths while Tahoe establishes its own visual token layer.

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
- Native components are shared with Lord Flow for now. Tahoe-specific implementations of the menu bar, Dock, window chrome and Control Center can be introduced one at a time after visual review, without duplicating their system behavior.
- Tahoe-specific wallpapers, icon artwork and sound assets have not yet been added. The theme currently offers the LordOS wallpaper collection.
- A source-level registration is not a build or runtime validation. Run `npm run build`, switch to Tahoe in Settings → Appearance → Desktop style, and test multi-display, RTL, Reduce Motion, fullscreen and low-effects configurations before calling the theme validated.
- Themes not present in the signed release manifest remain development-build-only, as described in `docs/THEMES-GUIDE.md`. A release must include the new theme in the verified-code signing pipeline.
