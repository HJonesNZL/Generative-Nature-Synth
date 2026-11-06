import { useMemo, useState } from "react";
import { useSpatialEditor } from "../../app/SpatialContext";
import { spatialDraft, newSpatialRegion, spatialNumbers } from "../../host/spatialSettings";
import { Map2D } from "../../components/map/Map2D";
import { BOUNDS_WORLD, type MapRegion } from "../../components/map/geo";

export function NativeSpatialEditor() {
  const { draft, accepted, ready, busy, dirty, message, edit, apply, refresh } = useSpatialEditor();
  const [selection, setSelection] = useState(0);
  const index = Math.min(selection, Math.max(0, (draft?.regions.length ?? 0) - 1));
  const region = draft?.regions[index];
  const preview = useMemo(() => {
    const regions: MapRegion[] = [];
    for (const [i, region] of (draft?.regions ?? []).entries()) {
      const numbers = [region.south, region.north, region.west, region.east];
      const south = Number(region.south), north = Number(region.north), west = Number(region.west), east = Number(region.east);
      if (numbers.some(text => !text.trim()) || ![south, north, west, east].every(Number.isFinite) || south < -90 || north > 90 || south >= north || west < -180 || west > 180 || east < -180 || east > 180 || west === east || west === 180 && east === -180) continue;
      const spans: Array<[number, number]> = west < east ? [[west, east]] : [[west, 180], [-180, east]];
      spans.forEach(([left, right], n) => regions.push({ id: `region-${i}-${n}`, name: region.name, selected: i === index, ring: [[left, south], [right, south], [right, north], [left, north], [left, south]] }));
    }
    return regions;
  }, [draft, index]);
  const changeRegion = (field: string, value: string | boolean) => edit(previous => ({ ...previous, regions: previous.regions.map((r, i) => i === index ? { ...r, [field]: value } : r) }));
  const add = () => {
    if (!draft || draft.regions.length >= 8) return;
    let suffix = 1; while (draft.regions.some(region => region.id === `region-${suffix}`)) suffix += 1;
    const next = spatialDraft({ enabled: false, amount: .25, regions: [newSpatialRegion(`region-${suffix}`, `Geographic selection ${suffix}`)] }).regions[0]!;
    setSelection(draft.regions.length); edit(previous => ({ ...previous, regions: [...previous.regions, next] }));
  };
  const move = (direction: -1 | 1) => {
    const target = index + direction; if (!draft || target < 0 || target >= draft.regions.length) return;
    edit(previous => { const regions = [...previous.regions]; const from = regions[index], to = regions[target]; if (!from || !to) return previous; [regions[index], regions[target]] = [to, from]; return { ...previous, regions }; }); setSelection(target);
  };
  return <aside className="studio__right native-spatial" aria-label="Regional spatial mixing">
    <span className="label">REGIONAL MIXING & AMBIENCE</span>
    <span className="small muted">Custom regions · first match wins</span>
    <fieldset disabled={busy || !ready}>
      <label className="spatial-switch"><input type="checkbox" aria-label="Enable spatial mixing" checked={draft?.enabled ?? false} onChange={e => edit(previous => ({ ...previous, enabled: e.target.checked }))} /> Enable spatial mixing</label>
      <label className="spatial-field">Global ambience amount <input aria-label="Global ambience amount" type="number" min={0} max={1} step={.05} value={draft?.amount ?? ".25"} onChange={e => edit(previous => ({ ...previous, amount: e.target.value }))} /></label>
      <div className="spatial-preview"><Map2D bounds={BOUNDS_WORLD} markers={[]} regions={preview} selectedKey={null} onPick={() => undefined} title="User-authored geographic selection bounds" interactiveMarkers={false} /></div>
      <ol className="spatial-order" aria-label="Region first-match order">{draft?.regions.map((region, i) => <li key={i}><button type="button" className={index === i ? "is-selected" : ""} aria-label={`Edit region ${i + 1}: ${region.name || "Unnamed"}`} aria-pressed={index === i} onClick={() => setSelection(i)}>{i + 1}. {region.name || "Unnamed"}</button></li>)}</ol>
      <details className="panel-details"><summary className="small">Region settings</summary>
      <div className="spatial-actions"><button type="button" className="btn btn--outline btn--sm" disabled={(draft?.regions.length ?? 0) >= 8} onClick={add}>Add region</button>
        <button type="button" className="btn btn--outline btn--sm" disabled={!region || index === 0} onClick={() => move(-1)}>Move region earlier</button>
        <button type="button" className="btn btn--outline btn--sm" disabled={!region || index >= (draft?.regions.length ?? 0) - 1} onClick={() => move(1)}>Move region later</button>
        <button type="button" className="btn btn--outline btn--sm" disabled={!region} onClick={() => { edit(previous => ({ ...previous, regions: previous.regions.filter((_, i) => i !== index) })); setSelection(Math.max(0, index - 1)); }}>Remove region</button></div>
      {region ? <div className="spatial-region-fields" aria-label={`Region ${index + 1} settings`}>
        <label className="spatial-field">Name <input aria-label="Region name" maxLength={128} value={region.name} onChange={e => changeRegion("name", e.target.value)} /></label>
        <div className="spatial-number-grid">{spatialNumbers.map(spec => <label className="spatial-field" key={spec.key}>{spec.label}<input aria-label={spec.label} type="number" min={spec.min} max={spec.max} step={spec.step} value={region[spec.key]} onChange={e => changeRegion(spec.key, e.target.value)} /></label>)}</div>
        <label className="spatial-switch"><input type="checkbox" aria-label="Pan by reported longitude" checked={region.positionPan} onChange={e => changeRegion("positionPan", e.target.checked)} /> Pan by reported longitude</label>
      </div> : <p className="small muted">Add up to eight regions to place sound and ambience geographically.</p>}
      </details>
      <button type="button" className="btn btn--primary btn--sm" disabled={!dirty} onClick={() => void apply()}>Apply spatial settings</button>
    </fieldset>
    {!ready ? <p className="small muted">Open a saved piece in Replay to edit its spatial recipe.</p> : null}
    {dirty ? <p className="small" role="status">Spatial edits pending</p> : null}
    {accepted?.routedNotes !== undefined ? <p className="small" aria-label="Spatial note accounting">{accepted.routedNotes} routed · {accepted.unassignedNotes ?? 0} unassigned · {accepted.wetActive ? "ambience active" : "no audible ambience"}</p> : null}
    <details className="panel-details small muted"><summary>Routing & ambience help</summary>
    <p>Custom regions are geographic selections, not animal habitats. Outside or missing positions use centred direct sound without a region send. Station records use the receiver position; individual fixes retain their reported uncertainty.</p>
    <p className="small muted">Centre pan is −3 dB per channel. West greater than east crosses the dateline. Echo / room blend: 0 is room, 1 is echo. Room decay is nominal.</p>
    <p className="small muted">Ambience applies a compensating overall level reduction; more configured regions share the return. Apply stops Listening and resets ambience. Save soundscape also applies pending composition and spatial edits.</p>
    </details>
    <span className="small muted">Apply stops Listening and resets ambience.</span>
    {message ? <p className="small" role="status">{message} <button className="btn btn--quiet btn--sm" disabled={busy} onClick={refresh}>Reload spatial settings</button></p> : null}
    <span className="small muted">Experimental ambience · listening approval pending</span>
  </aside>;
}
