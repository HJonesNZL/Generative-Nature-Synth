/** Native bounded spatial editor when advertised; otherwise the illustrative legacy region view. */
import { NativeSpatialEditor } from "./NativeSpatialEditor";
import { useMemo, useRef, useState } from "react";
import type { RegionView } from "../../host/contracts";
import { useHost, useHostQuery, valueOf } from "../../app/HostContext";
import { useAppState, useDispatch } from "../../app/store";
import { useCoalescedGesture } from "../../app/useCoalescedGesture";
import { Map2D } from "../../components/map/Map2D";
import { BOUNDS_NZ, type MapRegion, type Ring } from "../../components/map/geo";
import { Notice } from "../../components/Notice";
import { Toggle } from "../../components/Toggle";

export function RegionEffectsPanel({ voiceName }: { voiceName: string | null }) {
  const { client } = useHost();
  return client.supports("getSpatial") && client.supports("setSpatial") ? <NativeSpatialEditor /> : <LegacyRegionEffectsPanel voiceName={voiceName} />;
}
function LegacyRegionEffectsPanel({ voiceName }: { voiceName: string | null }) {
  const { client } = useHost();
  const state = useAppState();
  const dispatch = useDispatch();
  const [regionsQ, refreshRegions] = useHostQuery("listRegions", {}, []);
  const [effectsQ] = useHostQuery("listEffects", {}, []);
  const [policy, setPolicy] = useState<{ overlapPolicy: RegionView["overlapPolicy"]; uncertainPosition: "use_reported_point" | "skip_region_effects" } | null>(null);
  const [error, setError] = useState<string | null>(null);
  const regions = valueOf(regionsQ) ?? [];
  const effects = valueOf(effectsQ) ?? [];
  const mapRegions: MapRegion[] = useMemo(() => regions.map((r) => ({ id: r.id, name: r.name, ring: (r.geometry.coordinates[0]?.[0] ?? []) as Ring, selected: r.id === state.selectedRegionId })), [regions, state.selectedRegionId]);
  const overlap = policy?.overlapPolicy ?? regions[0]?.overlapPolicy ?? "highest_priority";
  const uncertain = policy?.uncertainPosition ?? "use_reported_point";
  const selectedRegion = regions.find((r) => r.id === state.selectedRegionId);
  const selectedEffect = effects.find((e) => e.id === selectedRegion?.effectAssignments[0]?.effectId);

  const setRegionPolicy = async (change: { overlapPolicy?: RegionView["overlapPolicy"]; uncertainPosition?: "use_reported_point" | "skip_region_effects" }) => {
    const r = await client.call("setRegionPolicy", change);
    if (r.ok) { setPolicy(r.value); refreshRegions(); setError(null); } else setError(r.error.message);
  };

  if (regionsQ.status === "unsupported") return (
    <aside className="studio__right" aria-label="Region effects">
      <span className="label">REGION EFFECTS</span>
      <Notice tone="caution" title="Region effects unavailable">This host does not provide regions or audio effects yet.</Notice>
    </aside>
  );

  return (
    <aside className="studio__right" aria-label="Region effects">
      <div className="row-between">
        <span className="label">REGION EFFECTS</span>
        <button type="button" className="link-button" disabled title="Region drawing is unavailable from this host">+ Draw region</button>
      </div>
      <div className="region-effects__top">
        <div className="region-thumb">
          <Map2D bounds={BOUNDS_NZ} markers={[]} regions={mapRegions} selectedKey={null} onPick={() => undefined} title="Effect regions on New Zealand" interactiveMarkers={false} />
        </div>
        <ol className="region-list">
          {regions.map((r) => {
            const assignment = r.effectAssignments[0];
            const effect = effects.find((e) => e.id === assignment?.effectId);
            return (
              <li key={r.id} className={r.id === state.selectedRegionId ? "is-on" : ""}>
                <RegionRow region={r} effectName={effect ? `${effect.name} · ${effect.summary}` : "No effect"} onSelect={() => dispatch({ type: "selectRegion", regionId: r.id })} onChanged={refreshRegions} onError={setError} />
              </li>
            );
          })}
        </ol>
      </div>
      <p className="small muted">Outside regions → direct sound</p>
      <div className="form-grid">
        <label className="field-label" htmlFor="re-overlap">Overlap</label>
        <select id="re-overlap" className="select" value={overlap} disabled={!client.supports("setRegionPolicy")} onChange={(e) => void setRegionPolicy({ overlapPolicy: e.target.value as RegionView["overlapPolicy"] })}>
          <option value="highest_priority">Highest priority wins</option>
          <option value="blend">Blend sends</option>
        </select>
        <label className="field-label" htmlFor="re-uncertain">Uncertain position</label>
        <select id="re-uncertain" className="select" value={uncertain} disabled={!client.supports("setRegionPolicy")} onChange={(e) => void setRegionPolicy({ uncertainPosition: e.target.value as "use_reported_point" | "skip_region_effects" })}>
          <option value="use_reported_point">Use reported point</option>
          <option value="skip_region_effects">Skip region effects</option>
        </select>
      </div>
      {error ? <Notice tone="problem" title="Region change not applied">{error}</Notice> : null}
      <div className="routing" aria-label="Routing for the selected voice">
        <span className="label">ROUTING</span>
        <div className="routing__chain">
          <span className="chip">{voiceName ?? "Voice"}</span><span aria-hidden="true">→</span>
          <span className="chip is-on">{selectedEffect?.name ?? "Region FX"}</span><span aria-hidden="true">→</span>
          <span className="chip">Master</span>
        </div>
        <details className="panel-details small muted"><summary>Routing help</summary><p>Direct sound is kept; send adds the selected region effect. Station detections use the station’s position, not an estimated animal position.</p></details>
      </div>
    </aside>
  );
}

function RegionRow({ region, effectName, onSelect, onChanged, onError }: { region: RegionView; effectName: string; onSelect: () => void; onChanged: () => void; onError: (m: string | null) => void }) {
  const { client } = useHost();
  const assignment = region.effectAssignments[0];
  const [local, setLocal] = useState<number | null>(null);
  const active = useRef(false);
  const begin = (v: number) => { if (!active.current) { active.current = true; send.push("begin", v); } };
  const end = (v: number) => { if (active.current) { active.current = false; send.push("end", v); } };
  const send = useCoalescedGesture(async (value, gesture) => {
    const r = await client.call("setRegionEffect", { regionId: region.id, effectId: assignment!.effectId, send: value, gesture });
    if (r.ok && gesture === "end") { setLocal(null); onChanged(); }
    return r.ok ? { ok: true as const, value: {} } : r;
  });
  if (!assignment) return <button type="button" className="region-row" onClick={onSelect}>{region.name}<span className="small muted">No effect</span></button>;
  const shown = local ?? assignment.send;
  return (
    <div className="region-row">
      <button type="button" className="region-row__name" onClick={onSelect} aria-label={`Select ${region.name}`}>
        <span className="region-row__index mono" title={`Priority ${region.priority}`} aria-hidden="true">{region.priority}</span>{region.name}<span className="sr-only">, priority {region.priority}</span>
      </button>
      <div className="row-between">
        <span className="small muted">{effectName}</span>
        <Toggle label={`${effectName} on ${region.name}`} checked={assignment.enabled} disabled={!client.supports("setRegionEffect")} onChange={async (enabled) => { const r = await client.call("setRegionEffect", { regionId: region.id, effectId: assignment.effectId, enabled }); if (r.ok) { onChanged(); onError(null); } else onError(r.error.message); }} />
      </div>
      <label className="send-row">
        <span className="mono small">send</span>
        <input
          type="range" min={0} max={1} step={0.01} value={shown} className="range"
          disabled={!client.supports("setRegionEffect") || !assignment.enabled}
          aria-valuetext={`${shown.toFixed(2)}${assignment.enabled ? "" : ", bypassed"}`}
          onPointerDown={() => begin(shown)}
          onKeyDown={() => begin(shown)}
          onChange={(e) => { const v = Number(e.target.value); setLocal(v); begin(shown); send.push("change", v); }}
          onPointerUp={(e) => end(Number((e.target as HTMLInputElement).value))}
          onKeyUp={(e) => end(Number((e.target as HTMLInputElement).value))}
          onBlur={(e) => end(Number(e.target.value))}
        />
        <span className="mono small">{shown.toFixed(2)}</span>
      </label>
    </div>
  );
}
