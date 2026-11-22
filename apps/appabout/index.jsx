// About <app> (the app menu's first item, sdk/appcontrol.js): the app's icon, name, its own version
// (manifest "version"; a package's version), where it comes from and whether it runs isolated.
import { View, Text, useTheme, useAppWindow, useLauncherApps, getApp, wm, typeScale, isSystemApp, appName, t as tr } from '@lordos/sdk';
import { useEffect } from 'react';

export function Icon({ size: s }) {
  return (
    <View style={{ width: s, height: s, borderRadius: s * 0.225, background: 'linear-gradient(155deg, #65c7ff 0%, #2585e8 55%, #1456b8 100%)',
      boxShadow: `0 ${s * 0.04}px ${s * 0.12}px rgba(0,0,0,0.32)`, alignItems: 'center', justifyContent: 'center' }}>
      <View style={{ width: s * 0.58, height: s * 0.58, borderRadius: s * 0.29, background: 'linear-gradient(145deg, #fff 0%, #eaf5ff 100%)',
        alignItems: 'center', justifyContent: 'center', boxShadow: `0 ${s * 0.02}px ${s * 0.055}px rgba(0,35,90,0.25)` }}>
        <Text style={{ fontSize: s * 0.48, lineHeight: s * 0.55, fontWeight: 700, color: '#2478cf' }}>i</Text>
      </View>
    </View>
  );
}

export default function AppAbout() {
  const t = useTheme();
  const { id, args } = useAppWindow();
  const key = String(args?.key || '');
  const apps = useLauncherApps();
  const a = apps.find((x) => x.id === key) || null;
  const own = getApp(key);
  const name = a ? a.name : own ? appName(own) : key;
  useEffect(() => { if (id) wm.setTitle(id, tr('appabout.title', { app: name })); }, [id, name]);
  const AppIcon = a?.Icon || own?.Icon;
  const kind = !a ? '' : a.kind === 'linux' ? tr('appabout.linux') : a.package ? tr('appabout.package') : isSystemApp(a.id) ? tr('appabout.system') : tr('appabout.lordos');
  const isolated = !!(own?.isolated || own?.package);
  const version = own?.version || a?.version || '';
  const desc = a?.comment || own?.description || a?.genericName || '';
  return (
    <View style={{ flex: 1, alignItems: 'center', paddingHorizontal: 22, paddingTop: 22, paddingBottom: 18, gap: 6 }}>
      {AppIcon ? <AppIcon size={64} /> : <Icon size={64} />}
      <Text style={{ fontSize: typeScale.title2, fontWeight: 700, color: t.text, marginTop: 6, textAlign: 'center' }}>{name}</Text>
      {version ? <Text style={{ fontSize: typeScale.callout, color: t.textDim }}>{tr('appabout.version', { v: version })}</Text> : null}
      {kind ? <Text style={{ fontSize: typeScale.caption, color: t.textDim, textAlign: 'center' }}>{kind}</Text> : null}
      {isolated ? <Text style={{ fontSize: typeScale.caption, color: t.textDim }}>{tr('appabout.isolated')}</Text> : null}
      {desc ? <Text style={{ fontSize: typeScale.callout, color: t.text, marginTop: 8, textAlign: 'center', lineHeight: 1.4, maxWidth: 280 }}>{desc}</Text> : null}
      <View style={{ flex: 1 }} />
      <Text style={{ fontSize: typeScale.caption2 || 10, color: t.textDim }}>{a?.kind === 'linux' ? String(key).replace(/^desktop:/, '') : key}</Text>
    </View>
  );
}
