// Settings → Bluetooth: on/off, my devices (connect, disconnect, forget) and nearby devices
// (pair). BlueZ over the system bus, from lord-server (server/src/hw/bt.c); PIN and passkey
// requests are drawn by the system (sdk/hardware.jsx, BluetoothPrompt). Headphones and speakers
// show up in Sound through PipeWire. See docs/SETTINGS.md, "Bluetooth".
import {
  View, Text, useTheme, useLocale, Toggle, Button, admin, useBluetooth, onBluetoothResult, setBluetoothPower,
  nearbyDevices, lowEnergySide, btErrorKey,
} from '@lordos/sdk';
import { useEffect, useRef, useState } from 'react';
import { GroupBox, SettingRow, PageHeader, Note, Stack } from './ui.jsx';
import { compareNames } from '../../sdk/collate.js';

// Monochrome glyphs (the usual 🎧 🖱 📱 are color emoji on the system fonts).
const GLYPHS = [[/headset|headphones/, '♪'], [/audio/, '♪'], [/keyboard/, '⌨'], [/mouse|tablet/, '◖'], [/gaming|joystick/, '✜'],
  [/phone/, '▯'], [/computer/, '▭'], [/camera/, '◉'], [/printer/, '⎙']];
const glyphOf = (icon) => (GLYPHS.find(([re]) => re.test(icon || '')) || [null, 'ᛒ'])[1];

export function BtIcon({ size = 44 }) {
  return (
    <View style={{ width: size, height: size, borderRadius: size * 0.24, background: 'linear-gradient(180deg, #3a9bff, #0a6cd6)', alignItems: 'center', justifyContent: 'center' }}>
      <Text style={{ fontSize: size * 0.58, fontWeight: 700, color: '#fff', lineHeight: size }}>ᛒ</Text>
    </View>
  );
}

function DeviceIcon({ icon }) {
  const t = useTheme();
  return (
    <View style={{ width: 28, height: 28, borderRadius: 14, backgroundColor: t.control, alignItems: 'center', justifyContent: 'center' }}>
      <Text style={{ fontSize: 14, color: t.text }}>{glyphOf(icon)}</Text>
    </View>
  );
}

function Device({ d, tr, onAction, pending }) {
  const t = useTheme();
  const status = d.busy ? tr('prefs.bt.working') : d.connected ? tr('prefs.bt.connected') : tr('prefs.bt.notConnected');
  const battery = d.battery >= 0 ? ` · ${tr('prefs.bt.battery', { pct: d.battery })}` : '';
  if (!d.paired) {
    // a headset's low-energy side listed without its classic device (sdk/btlistmodel.js, issue #78)
    const le = lowEnergySide(d);
    const hint = le ? tr('prefs.bt.leSideHint', { name: le }) : d.rssi != null ? tr('prefs.bt.signal', { dbm: d.rssi }) : '';
    return (
      <SettingRow icon={<DeviceIcon icon={d.icon} />} label={d.name} hint={hint}>
        {d.busy || pending ? <Text style={{ fontSize: 12, color: t.textDim }}>{tr('prefs.bt.working')}</Text>
          : <Button small label={tr('prefs.bt.connect')} onClick={() => onAction(d, 'pair')} />}
      </SettingRow>
    );
  }
  return (
    <SettingRow icon={<DeviceIcon icon={d.icon} />} label={d.name} hint={`${status}${battery}`}>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 6 }}>
        {!d.busy && !pending && <Button small label={d.connected ? tr('prefs.bt.disconnect') : tr('prefs.bt.connect')}
          onClick={() => onAction(d, d.connected ? 'disconnect' : 'connect')} />}
        <Button small plain destructive label={tr('prefs.bt.forget')} disabled={!!(d.busy || pending)} onClick={() => onAction(d, 'forget')} />
      </View>
    </SettingRow>
  );
}

const SEARCH_MS = 40000;

export function BluetoothSettings() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const st = useBluetooth();
  const [err, setErr] = useState('');
  const a = st?.adapter;
  const devices = useRef([]);
  const pending = useRef(null); // { path, op }; pairing is followed by a separate Connect result
  devices.current = st?.devices || [];
  // a search of its own, for a while: the kernel doesn't listen for paired devices coming back while one runs
  const [search, setSearch] = useState(0);
  useEffect(() => {
    if (!a?.powered || typeof lord === 'undefined' || !lord.bt) return undefined;
    let accepted = false;
    try { accepted = !!lord.bt.discover(true); } catch (e) { accepted = false; }
    if (!accepted) return undefined;
    const stop = setTimeout(() => { try { lord.bt.discover(false); } catch (e) { /* adapter may have disappeared */ } }, SEARCH_MS);
    return () => { clearTimeout(stop); try { lord.bt.discover(false); } catch (e) { /* adapter may have disappeared */ } };
  }, [a?.powered, a?.path, search]);
  useEffect(() => onBluetoothResult((r) => {
    const p = pending.current;
    if (p && r?.path === p.path && r.op === p.op) {
      if (r.op === 'pair' && r.ok) { p.op = 'connect'; setErr(''); return; }
      pending.current = null;
      if (r.ok) { setErr(''); return; }
      const key = btErrorKey(r, devices.current);
      setErr(key || 'failed');
      return;
    }
    if (r?.op === 'power' && !r.ok) { setErr(btErrorKey(r, devices.current) || 'failed'); return; }
    if (r?.op === 'discover' && !r.ok) setErr(btErrorKey(r, devices.current) || 'failed');
  }), []);
  useEffect(() => {
    if (a?.powered) return;
    pending.current = null;
  }, [a?.powered, a?.path]);
  const runDeviceAction = (d, op) => {
    if (pending.current || d.busy || !a?.powered) return;
    setErr('');
    pending.current = { path: d.path, op };
    try {
      if (typeof lord === 'undefined' || !lord.bt || typeof lord.bt[op] !== 'function' || !lord.bt[op](d.path)) {
        pending.current = null;
        setErr('failed');
      }
    } catch (e) { pending.current = null; setErr('failed'); }
  };
  if (!st || !st.available || !a) {
    return (
      <Stack>
        <PageHeader icon={<BtIcon />} title={tr('prefs.bt.title')} subtitle={tr(st?.available ? 'prefs.bt.noAdapter' : 'prefs.bt.noService')} />
      </Stack>
    );
  }
  const mine = st.devices.filter((d) => d.paired).sort((x, y) => (y.connected - x.connected) || compareNames(x.name, y.name));
  const near = nearbyDevices(st.devices);
  const togglePower = (on) => {
    setErr('');
    if (!setBluetoothPower(on)) setErr('failed');
  };
  return (
    <Stack>
      <PageHeader icon={<BtIcon />} title={tr('prefs.bt.title')}
        subtitle={a.powered ? tr('prefs.bt.visibleAs', { name: a.name || a.address }) : tr('prefs.bt.offHint')}>
        <Toggle small pending on={a.powered} onChange={togglePower} />
      </PageHeader>
      {err === 'blocked' ? (
        <GroupBox>
          <SettingRow label={tr('prefs.bt.err.blocked')}>
            <Button small label={tr('prefs.bt.unblock')} onClick={() => admin.run('bt-unblock').then(() => { setErr(''); setBluetoothPower(true); }, (e) => setErr(e.message))} />
          </SettingRow>
        </GroupBox>
      ) : err ? <Note tone="warn">{tr(`prefs.bt.err.${err}`) || err}</Note> : null}
      {a.powered && (
        <GroupBox title={tr('prefs.bt.myDevices')}>
          {mine.length ? mine.map((d) => <Device key={d.path} d={d} tr={tr} onAction={runDeviceAction} pending={!!pending.current} />) : <SettingRow label={tr('prefs.bt.noDevices')} />}
        </GroupBox>
      )}
      {a.powered && (
        <GroupBox title={a.discovering ? tr('prefs.bt.nearbySearching') : tr('prefs.bt.nearby')} footer={tr('prefs.bt.nearbyHint')}>
          {near.length ? near.map((d) => <Device key={d.path} d={d} tr={tr} onAction={runDeviceAction} pending={pending.current?.path === d.path} />)
            : <SettingRow label={a.discovering ? tr('prefs.bt.searching') : tr('prefs.bt.noneNearby')} />}
        </GroupBox>
      )}
      {a.powered && !a.discovering ? <View style={{ alignItems: 'flex-end' }}><Button small label={tr('prefs.bt.searchAgain')} onClick={() => setSearch((n) => n + 1)} /></View> : null}
      {a.powered ? <Note>{tr('prefs.bt.audioHint')}</Note> : null}
      {!st.agent && a.powered ? <Note tone="warn">{tr('prefs.bt.noAgent')}</Note> : null}
    </Stack>
  );
}
