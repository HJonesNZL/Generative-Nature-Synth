import type { CompositionSettings } from "../host/contracts";
import { useLiveSettings } from "./LiveSettingsContext";
import { useHost } from "./HostContext";
export function LiveCompositionPanel() {
  const {client} = useHost(), live = useLiveSettings();
  const draft = live.compositionDraft;
  if (!client.supports("setLiveSettings")) return null;
  const change = live.editComposition;
  if (live.state?.mode === "smoothed") return <p className="small muted">Smoothed uses each species voice and pitch. Key, scale and tempo are used by Musical mode. Edit regions below to shape the space.</p>;
  return <section className="composition-panel" aria-label="Live composition">
    <span className="label">LIVE COMPOSITION</span>
    <fieldset disabled={live.busy || !draft} className="composition-panel__controls">
      <label>Key <select aria-label="Live tonic" value={draft?.tonicPitchClass ?? 0} onChange={e => change({tonicPitchClass: Number(e.target.value)})}>{["C","C♯","D","E♭","E","F","F♯","G","A♭","A","B♭","B"].map((name,i) => <option key={i} value={i}>{name}</option>)}</select></label>
      <label>Scale <select aria-label="Live scale" value={draft?.scale ?? "majorPentatonic"} onChange={e => change({scale: e.target.value as CompositionSettings["scale"]})}><option value="majorPentatonic">Major pentatonic</option><option value="minorPentatonic">Minor pentatonic</option><option value="major">Major</option><option value="naturalMinor">Natural minor</option></select></label>
      {live.state?.mode === "musical" ? <label>Tempo <input aria-label="Live tempo BPM" type="number" min={40} max={180} step={1} value={draft?.tempoBpm ?? 90} onChange={e => {const n = Number(e.target.value); if(Number.isInteger(n) && n >= 40 && n <= 180) change({tempoBpm: n});}} /></label> : null}
      <button className="btn btn--outline btn--sm" disabled={!draft || JSON.stringify(draft) === JSON.stringify(live.state?.composition)} onClick={() => {if(draft) void live.change({composition:draft});}}>Apply live composition</button>
    </fieldset>
    <p className="small muted">Musical mode uses key, scale and tempo for upcoming phrases. Choose a mode in Live listening status.</p>
    {live.error ? <p className="small" role="alert">{live.error}</p> : null}
  </section>;
}
