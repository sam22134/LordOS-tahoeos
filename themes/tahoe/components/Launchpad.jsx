// Tahoe Launchpad: full-screen app grid with a frosted search field and rounded icon tiles.
// An app is pinned to the Dock by dragging it onto the Dock (the launcher steps aside as soon as the
// drag starts, and the app lands where it is dropped) or with a right click → Keep in Dock. It works
// the same for LordOS apps, .lpk packages and Linux apps (sdk/dock.js).
import { View, Text, TextInput, ScrollView, screen, t as tr, appName, refreshApps, APP_CATEGORIES, appCategory, dock, dockIdOf, useStore, settings,
  beginDockDrag, dockDragJustEnded } from '@lordos/sdk';
import { useEffect, useRef, useState } from 'react';
import { Menu } from './Menu.jsx';
import { TahoeIconForApp } from './AppIcon.jsx';

const COLS = 6, CELL = 130, ICON = 72;

// area: the display it covers (the main one; global coordinates)
export function Launchpad({ t, apps, onOpen, onClose, area = { x: 0, y: 0, w: screen.width, h: screen.height } }) {
  useEffect(refreshApps, []);
  const A = useRef(area);
  A.current = area;
  const [query, setQuery] = useState('');
  const [cat, setCat] = useState(null);                      // null: all
  const q = query.trim().toLowerCase();
  const match = (a) => [appName(a), a.name, a.id, a.genericName, a.keywords, a.comment].some((v) => v && String(v).toLowerCase().includes(q));
  const present = APP_CATEGORIES.filter((c) => apps.some((a) => appCategory(a) === c));
  const list = apps.filter((a) => (!q || match(a)) && (q || !cat || appCategory(a) === cat));
  // Adapt the grid to the actual display width: a fixed six-column row clips on compact
  // laptop panels and makes the last columns unreachable.
  const available = Math.max(1, area.w - 32);
  const cols = Math.max(1, Math.min(COLS, Math.floor(available / CELL)));
  const cell = Math.min(CELL, available / cols);
  const gridWidth = cols * cell;
  const iconSize = Math.min(ICON, Math.max(36, cell * 0.56));
  const tileSize = Math.min(88, Math.max(56, cell * 0.72));
  const searchTop = Math.min(48, Math.max(12, area.h * 0.08));
  const rows = [];
  for (let i = 0; i < list.length; i += cols) rows.push(list.slice(i, i + cols));
  const open = (id) => { onClose(); onOpen(id); };
  useStore(settings, (s) => s.dockPinned);                   // «Keep in Dock» changes immediately
  const [ctx, setCtx] = useState(null);                       // { app, x, y }

  return (
    <View onClick={onClose} style={{
      position: 'absolute', left: area.x, top: area.y, width: area.w, height: area.h, alignItems: 'center',
      backgroundColor: t.effects ? 'rgba(12,15,24,0.42)' : 'rgba(18,20,28,0.97)', backdropFilter: `blur(${t.effects ? 36 : 0}px)`,
    }}>
      <View onClick={(e) => e.stopPropagation()} style={{ marginTop: searchTop, width: Math.max(1, Math.min(360, area.w - 32)) }}>
        <TextInput value={query} onChange={setQuery} placeholder={tr('shell.launchpad.search')} autoFocus
          onSubmit={() => list[0] && open(list[0].id)} onCancel={onClose}
          style={{ backgroundColor: 'rgba(255,255,255,0.16)', border: '1px solid rgba(255,255,255,0.26)', borderRadius: 18, paddingHorizontal: 16, height: 42 }} />
      </View>
      {!q && (
        <View onClick={(e) => e.stopPropagation()} style={{ marginTop: 14, width: gridWidth, flexDirection: 'row', flexWrap: 'wrap', justifyContent: 'center', gap: 6 }}>
          {[null, ...present].map((c) => (
            <View key={c || 'all'} onClick={() => setCat(c)} style={{ paddingHorizontal: 14, paddingVertical: 7, borderRadius: 18,
              backgroundColor: c === cat ? 'rgba(255,255,255,0.32)' : 'rgba(255,255,255,0.1)' }}>
              <Text style={{ fontSize: 12, fontWeight: 600, color: '#fff' }}>{c ? tr(`shell.category.${c}`) : tr('shell.category.all')}</Text>
            </View>
          ))}
        </View>
      )}
      <ScrollView onClick={(e) => e.stopPropagation()} style={{ marginTop: q ? 18 : 10, height: Math.max(48, area.h - (q ? 112 : 158)), width: gridWidth }}>
      <View style={{ gap: 26, paddingVertical: 16 }}>
        {rows.map((row, r) => (
          <View key={r} style={{ flexDirection: 'row', justifyContent: 'center' }}>
            {row.map((a) => (
              <View key={a.id} onClick={(e) => { e.stopPropagation(); if (!dockDragJustEnded(e)) open(a.id); }}
                onMouseDown={(e) => { if (!e.button && dockIdOf(a)) beginDockDrag({ id: dockIdOf(a), Icon: a.Icon, x: e.x, y: e.y, source: 'launcher', onStart: onClose }); }}
                onContextMenu={(e) => { e.stopPropagation(); setCtx({ app: a, x: e.x, y: e.y }); }}
                style={{ width: cell, alignItems: 'center', gap: 10 }}>
                <View style={{ width: tileSize, height: tileSize, borderRadius: Math.min(24, tileSize * 0.27), alignItems: 'center', justifyContent: 'center', backgroundColor: 'rgba(255,255,255,0.10)', border: '1px solid rgba(255,255,255,0.16)', boxShadow: '0 8px 24px rgba(0,0,0,0.18)' }}><TahoeIconForApp app={a} size={iconSize} /></View>
                <Text style={{ fontSize: 13, fontWeight: 500, color: '#fff', whiteSpace: 'nowrap', maxWidth: cell - 10, overflow: 'hidden', textOverflow: 'ellipsis' }}>{appName(a)}</Text>
              </View>
            ))}
          </View>
        ))}
        {list.length === 0 && <Text style={{ fontSize: 14, color: 'rgba(255,255,255,0.7)', textAlign: 'center' }}>{tr('shell.launchpad.empty')}</Text>}
      </View>
      </ScrollView>
      {ctx && (
        <Menu x={ctx.x} y={ctx.y} onClose={() => setCtx(null)} items={[
          { label: tr('shell.dock.open'), onClick: () => open(ctx.app.id) },
          ...(dockIdOf(ctx.app) ? [{ label: tr('shell.dock.keep'), checked: dock.isPinned(ctx.app), disabled: dock.isPinned(ctx.app) && !dock.canRemove(ctx.app),
            onClick: () => (dock.isPinned(ctx.app) ? dock.unpin(ctx.app) : dock.pin(ctx.app)) }] : []),
        ]} />
      )}
    </View>
  );
}
