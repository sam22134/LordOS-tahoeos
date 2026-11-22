// Tahoe native app-icon accents. Only built-in LordOS app IDs are mapped;
// external desktop/Flatpak/web app icons continue to render through their own providers.
import { View, Text } from '@lordos/runtime';

const ICONS = {
  files: ['📁','#65a8f4'], settings: ['⚙️','#8e929a'], terminal: ['⌘','#292b32'],
  photos: ['🌈','#ffffff'], calendar: ['📅','#ffffff'], mail: ['✉️','#4d9bf7'],
  notes: ['📝','#f8d75b'], store: ['🛍️','#45b4e7'], contacts: ['👤','#f6c85e'],
  clock: ['⏱️','#2c3142'], calculator: ['🧮','#444955'], camera: ['📷','#33343a'],
  reminders: ['☑️','#f5f5f5'], passwords: ['🔑','#b7b8c2'], pdf: ['📕','#f2f2f2'],
  editor: ['✍️','#32343b'], screenshots: ['🖼️','#9c8cf0'], recorder: ['🎙️','#f35b62'],
  player: ['▶️','#2d3440'], disks: ['💽','#8b929b'], monitor: ['📊','#414754'],
  fonts: ['🔤','#b5c7e8'], appabout: ['','#454954'], about: ['','#454954'],
  imager: ['🖼️','#8e8cf0'], scanner: ['📄','#4d9bf7'], help: ['❔','#6a9de8'],
  sysinfo: ['ℹ️','#3f84d5'], sysprobe: ['📈','#354454'], backup: ['🗄️','#64748b'],
  cleaner: ['🧹','#60a5fa'], installer: ['📦','#3d8bdb'], databases: ['🗃️','#5c6b7d'],
  characters: ['😀','#f4c34e'], machines: ['🖥️','#39465b'], 'remote-desk': ['🧑‍💻','#4f8fcf'],
  logs: ['📋','#586476'], forcequit: ['⏹️','#414852'],
};
export function TahoeAppIcon({ id, Base, size = 32 }) {
  const data = ICONS[id];
  if (!data) return Base ? <Base size={size} /> : null;
  const [glyph, bg] = data;
  return (
    <View style={{ width: size, height: size, borderRadius: size * 0.23, background: bg,
      border: '1px solid rgba(255,255,255,0.36)', boxShadow: '0 2px 5px rgba(0,0,0,0.20)',
      alignItems: 'center', justifyContent: 'center', overflow: 'hidden' }}>
      <Text style={{ fontSize: size * (id === 'terminal' || id === 'about' ? 0.56 : 0.68),
        fontWeight: 700, color: id === 'terminal' || id === 'about' ? '#fff' : undefined }}>{glyph}</Text>
    </View>
  );
}
export function TahoeIconForApp({ app, size = 32 }) {
  return <TahoeAppIcon id={app?.id} Base={app?.Icon} size={size} />;
}
