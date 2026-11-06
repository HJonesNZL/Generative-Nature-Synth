import { useEffect, useRef, useState } from "react";
import type { CompositionSettings, CompositionState } from "../host/contracts";
import { recipeStillCurrent } from "./recipeIdentity";
import { useHost, useHostQuery, valueOf } from "./HostContext";
import { useAppState, useDispatch } from "./store";

/** One persistent draft above screen changes. Save finalizes it after voice queues. */
export function CompositionPanel({ onAttention }: { onAttention?: () => void }) {
  const { client, connection } = useHost();
  const state = useAppState(), dispatch = useDispatch();
  const [query, refresh] = useHostQuery("getComposition", {}, [state.transport.preparationId, state.soundscape.epoch]);
  const [draft, setDraft] = useState<CompositionSettings | null>(null);
  const [accepted, setAccepted] = useState<CompositionState | null>(null);
  const [message, setMessage] = useState<string | null>(null);
  const [applying, setApplying] = useState(false);
  const numeric = useRef({ tempoBpm: "90", seed: "1" });
  const [numericText, setNumericText] = useState(numeric.current);
  const current = useRef(state.transport); current.current = state.transport;
  const draftRef = useRef(draft); draftRef.current = draft;
  const acceptedRef = useRef(accepted); acceptedRef.current = accepted;
  const loadedId = useRef<string | null>(null);
  const inFlight = useRef(false);
  const epoch = useRef(state.soundscape.epoch);
  const value = valueOf(query);
  useEffect(() => {
    if (query.status !== "ready" || !value || value.preparation?.preparationId !== current.current.preparationId) return;
    const previous = acceptedRef.current;
    const preserve = previous && epoch.current === state.soundscape.epoch &&
      previous.preparation?.session?.id === value.preparation.session?.id && JSON.stringify(previous.settings) === JSON.stringify(value.settings);
    epoch.current = state.soundscape.epoch; acceptedRef.current = value;
    loadedId.current = value.preparation.preparationId; setAccepted(value);
    if (preserve) return;
    setMessage(null); draftRef.current = value.settings;
    numeric.current = { tempoBpm: String(value.settings.tempoBpm), seed: String(value.settings.seed) };
    setNumericText(numeric.current);
    setDraft(value.settings);
  }, [query, value]);
  const flush = useRef<() => Promise<boolean>>(async () => true);
  flush.current = async () => {
    const settings = draftRef.current, previous = acceptedRef.current;
    if (!settings || !previous) return true;
    const tempo = Number(numeric.current.tempoBpm), seed = Number(numeric.current.seed);
    if (!numeric.current.tempoBpm.trim() || !Number.isInteger(tempo) || tempo < 40 || tempo > 180) {
      setMessage("Enter a whole tempo from 40 to 180 BPM before applying or saving."); return false;
    }
    if (!numeric.current.seed.trim() || !Number.isInteger(seed) || seed < 0 || seed > 4294967295) {
      setMessage("Enter a whole seed from 0 to 4294967295 before applying or saving."); return false;
    }
    if (JSON.stringify(settings) === JSON.stringify(previous.settings)) {
      numeric.current = { tempoBpm: String(settings.tempoBpm), seed: String(settings.seed) };
      setNumericText(numeric.current); return true;
    }
    if (inFlight.current || !current.current.preparationId) {
      setMessage("Wait for the current piece settings before applying composition."); return false;
    }
    inFlight.current = true; setApplying(true); setMessage(null);
    try {
      // Submitted mutations are never cancelled on a screen change.
      const fresh = await client.call("getComposition", {});
      if (!fresh.ok || !fresh.value.preparation || !recipeStillCurrent(fresh.value, previous)) {
        setMessage(fresh.ok ? "The source piece or composition recipe changed. Reload its settings before applying." : fresh.error.message); return false;
      }
      const preparation = fresh.value.preparation;
      const result = await client.call("setComposition", { settings, preparationId: preparation.preparationId, generation: preparation.generation }, { timeoutMs: 30000 });
      if (!result.ok || !result.value.preparation) {
        setMessage(result.ok ? "Composition did not return a preparation." : result.error.message); return false;
      }
      const next = result.value;
      acceptedRef.current = next; draftRef.current = next.settings; loadedId.current = next.preparation!.preparationId;
      numeric.current = { tempoBpm: String(next.settings.tempoBpm), seed: String(next.settings.seed) };
      setNumericText(numeric.current);
      setAccepted(next); setDraft(next.settings);
      dispatch({ type: "recipePrepared", preparation: next.preparation! });
      setMessage("Composition applied. Listening is off; enable it and press Play to hear this candidate.");
      return true;
    } finally { inFlight.current = false; setApplying(false); }
  };
  useEffect(() => client.edits.registerFinalizer(() => flush.current()), [client]);
  const apply = async () => {
    if (!client.edits.begin()) return;
    dispatch({ type: "soundscapeOperation", operation: "composition" });
    try { if (await client.edits.drain(true, false)) await flush.current(); }
    finally { client.edits.end(); dispatch({ type: "soundscapeOperation", operation: null }); }
  };
  useEffect(() => { if (message || (query.status === "error" && state.transport.preparationId)) onAttention?.(); }, [message, query.status, state.transport.preparationId, onAttention]);
  if (!client.supports("getComposition") || !client.supports("setComposition")) return null;
  const busy = state.soundscape.operation !== null || connection !== "connected" || applying;
  const ready = !!draft && query.status === "ready" && !!state.transport.preparationId && loadedId.current === state.transport.preparationId;
  const change = (patch: Partial<CompositionSettings>) => {
    if (!draftRef.current || client.edits.locked) return;
    const next = { ...draftRef.current, ...patch }; draftRef.current = next; setDraft(next); setMessage(null);
  };
  const changeNumber = (field: "tempoBpm" | "seed", text: string) => {
    if (client.edits.locked) return;
    numeric.current = { ...numeric.current, [field]: text }; setNumericText(numeric.current); setMessage(null);
    const value = Number(text), min = field === "tempoBpm" ? 40 : 0, max = field === "tempoBpm" ? 180 : 4294967295;
    if (text.trim() && Number.isInteger(value) && value >= min && value <= max) change({ [field]: value });
  };
  const dirty = !!draft && !!accepted && (JSON.stringify(draft) !== JSON.stringify(accepted.settings) ||
    numericText.tempoBpm !== String(draft.tempoBpm) || numericText.seed !== String(draft.seed));
  return <section className="composition-panel" aria-label="Scene composition">
    <div className="row-between"><span className="label">SCENE COMPOSITION</span><span className="small muted">Phrases</span></div>
    <fieldset disabled={busy || !ready} className="composition-panel__controls">
      <label><input type="checkbox" aria-label="Enable composition" checked={draft?.enabled ?? false} onChange={e => change({ enabled: e.target.checked })} /> Compose phrases</label>
      <label>Tempo <input aria-label="Composition tempo BPM" type="number" min={40} max={180} step={1} value={numericText.tempoBpm} onChange={e => changeNumber("tempoBpm", e.target.value)} /></label>
      <label>Key <select aria-label="Composition tonic" value={draft?.tonicPitchClass ?? 0} onChange={e => change({ tonicPitchClass: Number(e.target.value) })}>{["C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"].map((name, i) => <option key={i} value={i}>{name}</option>)}</select></label>
      <label>Scale <select aria-label="Composition scale" value={draft?.scale ?? "majorPentatonic"} onChange={e => change({ scale: e.target.value as CompositionSettings["scale"] })}><option value="majorPentatonic">Major pentatonic</option><option value="minorPentatonic">Minor pentatonic</option><option value="major">Major</option><option value="naturalMinor">Natural minor</option></select></label>
      <label>Density <select aria-label="Composition density" value={draft?.density ?? 1} onChange={e => change({ density: Number(e.target.value) as CompositionSettings["density"] })}><option value={0}>Sparse</option><option value={1}>Balanced</option><option value={2}>Active</option></select></label>
      <label>Variation <input aria-label="Composition variation" type="range" min={0} max={1} step={0.01} value={draft?.variation ?? .25} onChange={e => change({ variation: Number(e.target.value) })} /><span className="mono small">{Math.round((draft?.variation ?? .25) * 100)}%</span></label>
      <label>Seed <input aria-label="Composition seed" type="number" min={0} max={4294967295} step={1} value={numericText.seed} onChange={e => changeNumber("seed", e.target.value)} /></label>
      <label><input type="checkbox" aria-label="Quantize composition" checked={draft?.quantize ?? true} onChange={e => change({ quantize: e.target.checked })} /> Quantize phrase timing</label>
      <button type="button" className="btn btn--outline btn--sm" disabled={!dirty} onClick={() => void apply()}>Apply composition</button>
    </fieldset>
    <div className="small muted">{ready ? "Apply stops playback. Saving also applies pending edits." : "Open a piece to compose."}</div>
    {accepted?.generatedNotes !== undefined ? <details className="panel-details"><summary>Note accounting</summary><div className="small" aria-label="Composition note accounting">{accepted.generatedNotes} generated musical notes · {accepted.preparation?.eventCount ?? "Verified"} source records retained{accepted.suppressedSourceEvents !== undefined ? ` · ${accepted.suppressedSourceEvents} source records omitted from music by density` : ""}{accepted.suppressed !== undefined ? ` · ${accepted.suppressed} additional phrase notes omitted` : ""}.</div></details> : null}
    {dirty ? <span className="small" role="status">Composition edits pending</span> : null}
    {query.status === "error" ? <span className="small" role="alert">{query.error.message} <button onClick={refresh}>Reload composition</button></span> : null}
    {message ? <span className="small" role="status">{message}</span> : null}
  </section>;
}
