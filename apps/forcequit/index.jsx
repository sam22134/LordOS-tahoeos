// Force Quit (the crown menu, sdk/appcontrol.js): every app with a window; the chosen one ends without
// being asked. Linux and isolated apps get SIGKILL from lord-server (only their own process), built-in
// apps close without their close guard.
import { View, Text, Button, ScrollView, useTheme, useWindows, runningApps, forceQuit, windowIcon, errorText, typeScale, t as tr } from '@lordos/sdk';
import { useState } from 'react';

export function Icon({ size: s }) {
  return (
    <View style={{ width: s, height: s, borderRadius: s * 0.225, background: 'linear-gradient(155deg, #ff6b63 0%, #e32636 58%, #b91c2c 100%)',
      boxShadow: `0 ${s * 0.04}px ${s * 0.12}px rgba(0,0,0,0.35)`, alignItems: 'center', justifyContent: 'center', overflow: 'hidden' }}>
      <View style={{ width: s * 0.58, height: s * 0.58, borderRadius: s * 0.18, background: 'linear-gradient(145deg, #fff 0%, #f2f2f7 100%)',
        alignItems: 'center', justifyContent: 'center', boxShadow: `0 ${s * 0.025}px ${s * 0.06}px rgba(80,0,0,0.28)` }}>
        <Text style={{ fontSize: s * 0.62, lineHeight: s * 0.66, fontWeight: 700, color: '#d92335' }}>×</Text>
      </View>
    </View>
  );
}

export default function ForceQuit() {
  const t = useTheme();
  useWindows();                                  // the list follows windows opening and closing
  const list = runningApps();
  const [sel, setSel] = useState(null);
  const [err, setErr] = useState('');
  const chosen = list.find((g) => g.key === sel) ? sel : null;
  const quit = () => { if (!chosen) return; const e = forceQuit(chosen); setErr(e ? errorText(e) : ''); setSel(null); };
  return (
    <View style={{ flex: 1, padding: 16, gap: 10 }}>
      <Text style={{ fontSize: typeScale.callout, color: t.text, textAlign: 'start', lineHeight: 1.4 }}>{tr('forcequit.intro')}</Text>
      <View style={{ flex: 1, borderRadius: 8, border: `1px solid ${t.separator}`, overflow: 'hidden' }}>
        <ScrollView style={{ flex: 1 }}>
          {list.length ? list.map((g) => {
            const I = windowIcon(g.windows[0]);
            const on = g.key === chosen;
            return (
              <View key={g.key} onClick={() => setSel(g.key)} onDoubleClick={() => { setSel(g.key); }}
                style={{ flexDirection: 'row', alignItems: 'center', gap: 10, height: 34, paddingHorizontal: 10, backgroundColor: on ? t.accent : 'transparent' }}>
                {I ? <I size={20} /> : <View style={{ width: 20 }} />}
                <Text style={{ flex: 1, fontSize: 13, color: on ? '#fff' : t.text, textAlign: 'start', whiteSpace: 'nowrap' }}>{g.name}</Text>
                {g.windows.length > 1 ? <Text style={{ fontSize: 11, color: on ? '#fff' : t.textDim }}>{tr('forcequit.windows', { count: g.windows.length })}</Text> : null}
              </View>
            );
          }) : <Text style={{ padding: 12, fontSize: 13, color: t.textDim }}>{tr('forcequit.none')}</Text>}
        </ScrollView>
      </View>
      {err ? <Text style={{ fontSize: typeScale.caption, color: '#ff9f0a', textAlign: 'start' }}>{err}</Text> : null}
      <View style={{ flexDirection: 'row', alignItems: 'center' }}>
        <Text style={{ flex: 1, fontSize: typeScale.caption, color: t.textDim, textAlign: 'start' }}>{tr('forcequit.shortcut')}</Text>
        <Button destructive label={tr('forcequit.quit')} disabled={!chosen} onClick={quit} />
      </View>
    </View>
  );
}
