// Tahoe visual tokens. The shared base keys are the app contract (sdk/theme.js).
// Lord Flow's native shell components are reused; this file supplies a distinct material system.
import { lordosWallpapers } from '@lordos/sdk';

// Tahoe-curated order: lead with calmer, light/glass-friendly landscapes and abstract art,
// while retaining the full LordOS collection and per-display wallpaper support.
const tahoeWallpaperOrder = ['lumo-celestial', 'lumo-aurora', 'royal-waves', 'crystal-crown',
  'eternal-dawn', 'violet-horizon', 'royal-flow', 'solar-odyssey', 'event-horizon',
  'infinite-galaxy', 'stellar-voyage'];
const tahoeWallpapers = [...lordosWallpapers].sort((a, b) => {
  const ai = tahoeWallpaperOrder.indexOf(a.id), bi = tahoeWallpaperOrder.indexOf(b.id);
  return (ai < 0 ? 999 : ai) - (bi < 0 ? 999 : bi);
});

export const accents = {
  azul: '#0a84ff',
  violeta: '#bf5af2',
  rosa: '#ff375f',
  naranja: '#ff9f0a',
  verde: '#30d158',
  grafito: '#8e8e93',
};

export function makeTheme({ mode, accent, effects }) {
  const dark = mode === 'dark';
  const glass = effects !== false;
  const a = accents[accent] || accents.azul;
  return {
    mode,
    accent: a,
    accentName: accents[accent] ? accent : 'azul',
    accents,
    wallpapers: tahoeWallpapers,
    effects: glass,
    text: dark ? '#f5f5f7' : '#1d1d1f',
    textDim: dark ? 'rgba(235,235,245,0.58)' : 'rgba(60,60,67,0.62)',
    windowBg: dark ? '#202126' : '#fbfbfd',
    sidebarBg: dark ? '#292b32' : '#f0f1f5',
    separator: dark ? 'rgba(255,255,255,0.11)' : 'rgba(35,38,48,0.12)',
    hover: dark ? 'rgba(255,255,255,0.075)' : 'rgba(35,38,48,0.055)',
    control: dark ? '#393c45' : '#e5e7ed',
    inputBg: dark ? '#292c34' : '#ffffff',

    // Tahoe materials: translucent when effects are enabled, opaque otherwise.
    menuBg: dark ? (glass ? 'rgba(31,33,40,0.76)' : '#252830') : (glass ? 'rgba(250,251,254,0.82)' : '#f6f7fa'),
    menuBorder: dark ? 'rgba(255,255,255,0.13)' : 'rgba(35,38,48,0.10)',
    menuShadow: dark ? '0 5px 22px rgba(0,0,0,0.22)' : '0 5px 22px rgba(35,45,65,0.10)',
    menuText: dark ? '#f5f5f7' : '#1d1d1f',
    menuTextDim: dark ? 'rgba(245,245,247,0.58)' : 'rgba(29,31,38,0.52)',
    menuHover: dark ? 'rgba(255,255,255,0.14)' : 'rgba(20,30,50,0.08)',
    panelBg: dark ? (glass ? 'rgba(37,40,49,0.78)' : '#292c34') : (glass ? 'rgba(247,248,252,0.84)' : '#f1f2f6'),
    popupBg: dark ? (glass ? 'rgba(37,40,49,0.94)' : '#292c34') : (glass ? 'rgba(250,251,254,0.96)' : '#f6f7fa'),
    windowBorder: dark ? 'rgba(255,255,255,0.14)' : 'rgba(35,38,48,0.14)',
    windowShadow: dark ? '0 24px 64px rgba(0,0,0,0.52)' : '0 24px 64px rgba(25,35,55,0.22)',
    windowShadowInactive: dark ? '0 12px 32px rgba(0,0,0,0.34)' : '0 12px 32px rgba(25,35,55,0.13)',
    titleBg: dark ? 'rgba(43,46,56,0.96)' : 'rgba(242,244,249,0.96)',
    dockBg: dark ? (glass ? 'rgba(45,48,59,0.62)' : '#292c34') : (glass ? 'rgba(248,249,253,0.68)' : '#f0f1f5'),
    dockBorder: dark ? 'rgba(255,255,255,0.22)' : 'rgba(255,255,255,0.78)',
    dockShadow: dark ? '0 12px 36px rgba(0,0,0,0.42)' : '0 12px 36px rgba(35,45,65,0.18)',
    panelShadow: dark ? '0 18px 48px rgba(0,0,0,0.48)' : '0 18px 48px rgba(35,45,65,0.20)',
    lightOff: dark ? '#555965' : '#c7cad3',
    scrollThumb: dark ? 'rgba(255,255,255,0.34)' : 'rgba(20,25,35,0.28)',
    scrollThumbHover: dark ? 'rgba(255,255,255,0.52)' : 'rgba(20,25,35,0.44)',
    blur: glass ? 28 : 0,
    shadow: dark ? '0 18px 48px rgba(0,0,0,0.42)' : '0 18px 48px rgba(25,35,55,0.18)',

    // Shared controls: consistent rounded geometry across LordOS apps.
    controlRadius: 11,
    controlRadiusSmall: 8,
    groupRadius: 14,
    menuRadius: 13,
    menuItemRadius: 8,
    menuHighlight: 'accent',
    selection: 'fill',
    inputFocus: 'ring',
    buttonBorder: dark ? 'rgba(255,255,255,0.12)' : 'rgba(35,38,48,0.12)',
    focusRing: a,
  };
}
