import { ExportHistory } from "./ExportHistory";
import { useEffect, useRef, useState } from "react";
import { useHost, useHostQuery, valueOf } from "./HostContext";
import { useAppState, useDispatch } from "./store";
import type { SoundscapeView } from "../host/contracts";

const artifactHref = (id: string | null | undefined) => id && /^[0-9a-f]{32}$/.test(id) ? `/__atlas/exports/${id}` : null;
export function SoundscapePanel({ onAttention }: { onAttention?: () => void }) {
  const { client, connection, onNotification } = useHost();
  const state = useAppState(), dispatch = useDispatch();
  const [list, refresh] = useHostQuery("listSoundscapes", { limit: 100 }, []);
  const [name, setName] = useState("");
  const [selected, setSelected] = useState<SoundscapeView | null>(null);
  const [message, setMessage] = useState<string | null>(null);
  const [renderBusy, setRenderBusy] = useState(false);
  const operation = useRef(false);
  const job = state.render.job;
  const active = job?.state === "queued" || job?.state === "rendering";
  const [jobQuery] = useHostQuery("getRenderJob", active && job ? { jobId: job.jobId } : null, [job?.jobId], { refreshMs: 500, pollWhenHidden: true });
  const polled = valueOf(jobQuery);
  useEffect(() => { if (polled && polled.jobId === job?.jobId) dispatch({ type: "renderJob", job: polled }); }, [polled, job?.jobId, dispatch]);
  useEffect(() => onNotification(notification => {
    if (notification.type === "renderProgress" && notification.job.jobId === job?.jobId) dispatch({ type: "renderJob", job: notification.job });
  }), [onNotification, job?.jobId, dispatch]);
  useEffect(() => { if (message || list.status === "error" || job?.state === "failed") onAttention?.(); }, [message, list.status, job?.state, onAttention]);
  if (!client.supports("listSoundscapes")) return null;
  const busy = state.soundscape.operation !== null || connection !== "connected";
  const items = valueOf(list)?.items ?? [];
  const unmanagedFiles = valueOf(list)?.unmanagedFiles ?? 0;
  const selectValue = selected?.id ?? "";
  const save = async () => {
    if (operation.current || !name.trim() || !state.transport.preparationId || !client.edits.begin()) return;
    operation.current = true; dispatch({ type: "soundscapeOperation", operation: "save" }); setMessage(null);
    try {
      if (!await client.edits.drain()) { setMessage("A sound edit did not finish successfully. Resolve the edit error before saving a soundscape."); return; }
      const result = await client.call("saveSoundscape", { name: name.trim() }, { timeoutMs: 30000 });
      if (!result.ok) { setMessage(result.error.message); return; }
      setSelected(result.value); setName(""); refresh();
      setMessage(`Saved “${result.value.name}” with all sound settings.`);
    } finally { operation.current = false; client.edits.end(); dispatch({ type: "soundscapeOperation", operation: null }); }
  };
  const recall = async () => {
    if (operation.current || !selected?.valid || !client.edits.begin()) return;
    operation.current = true; dispatch({ type: "soundscapeOperation", operation: "recall" }); setMessage(null);
    try {
      // Drain old queues before native recall replaces the complete draft recipe.
      await client.edits.drain(false);
      const result = await client.call("recallSoundscape", { soundscapeId: selected.id }, { timeoutMs: 30000 });
      if (!result.ok) { setMessage(result.error.message); return; }
      dispatch({ type: "soundscapeRecalled", scene: result.value.soundscape, preparation: result.value.preparation });
      setSelected(result.value.soundscape);
      setMessage(`Opened “${result.value.soundscape.name}”. Drafts replaced. Enable Listening, then Play.`);
    } finally { operation.current = false; client.edits.end(); dispatch({ type: "soundscapeOperation", operation: null }); }
  };
  const render = async () => {
    if (!selected?.valid || active || operation.current) return;
    operation.current = true; dispatch({ type: "renderPending", pending: true }); setMessage(null);
    // Only the selected immutable scene is submitted. Current edit drafts are not an export input.
    const result = await client.call("startSoundscapeRender", { soundscapeId: selected.id });
    operation.current = false; dispatch({ type: "renderPending", pending: false });
    if (result.ok) dispatch({ type: "renderStarted", job: result.value }); else setMessage(result.error.message);
  };
  const cancel = async () => {
    if (!job || renderBusy || !client.supports("cancelRender")) return;
    setRenderBusy(true);
    const result = await client.call("cancelRender", { jobId: job.jobId });
    setRenderBusy(false);
    if (result.ok) dispatch({ type: "renderJob", job: result.value }); else setMessage(result.error.message);
  };
  const download = !state.render.pending && job?.state === "complete" ? artifactHref(job.artifactId) : null;
  return <section className="soundscape-panel" aria-label="Soundscapes">
    <div className="soundscape-panel__row">
      <span className="label">SOUNDSCAPES</span>
      <input aria-label="Soundscape name" placeholder="Name this soundscape" maxLength={160} value={name} disabled={busy} onChange={event => setName(event.target.value)} />
      <button type="button" className="btn btn--primary btn--sm" disabled={busy || !name.trim() || !state.transport.preparationId || !client.supports("saveSoundscape")} onClick={() => void save()}>{state.soundscape.operation === "save" ? "Saving…" : "Save soundscape"}</button>
      <select aria-label="Saved soundscape" value={selectValue} disabled={busy} onChange={event => { setSelected(items.find(item => item.id === event.target.value) ?? null); setMessage(null); }}>
        <option value="">Choose a saved soundscape</option>
        {selected && !items.some(item => item.id === selected.id) ? <option value={selected.id}>{selected.name}</option> : null}
        {items.map(item => <option key={item.id} value={item.id} disabled={!item.valid}>{item.name}{item.valid ? "" : " (unavailable)"}</option>)}
      </select>
      <button type="button" className="btn btn--outline btn--sm" disabled={busy || !selected?.valid || !client.supports("recallSoundscape")} onClick={() => void recall()}>{state.soundscape.operation === "recall" ? "Opening…" : "Open soundscape"}</button>
      <button type="button" className="btn btn--outline btn--sm" disabled={busy || !selected?.valid || active || state.render.pending || !client.supports("startSoundscapeRender")} onClick={() => void render()}>{state.render.pending ? "Starting export…" : "Export saved WAV"}</button>
      {selected?.valid && client.implementation === "native" && (window.__ATLAS_NATIVE_FILES__ || import.meta.env.DEV) ? <a className="btn btn--outline btn--sm" href={`/__atlas/scenes/${selected.id}`} data-scene-id={selected.id} download={`Animal-Atlas-${selected.id}.atlas-scene`}>Export scene</a> : null}
    </div>
    <div className="soundscape-panel__details small muted">
      <span>{selected ? `“${selected.name}” · ${selected.eventCount} records. Export uses this saved version.` : "Open a soundscape, or select a piece in Replay to save a new one."}</span>
      {selected && !selected.contentVerified ? <span>Contents will be verified when opened or exported.</span> : null}
      {valueOf(list)?.limited ? <span>Showing up to 100 soundscapes.</span> : null}
      {unmanagedFiles ? <span>{unmanagedFiles} {unmanagedFiles === 1 ? "file uses an unsupported name and cannot be opened here." : "files use unsupported names and cannot be opened here."}</span> : null}
      {items.filter(item => !item.valid).map(item => <span role="alert" key={item.id}>{item.name}: {item.error ?? "Unavailable soundscape"}</span>)}
      {list.status === "error" ? <span role="alert">{list.error.message} <button className="btn btn--quiet btn--sm" onClick={refresh}>Retry soundscapes</button></span> : null}
      {message ? <span role="status">{message}</span> : null}
      {active && job ? <><span role="status">{job.state === "queued" ? "WAV export queued" : "Exporting saved soundscape"}</span><progress max={1} value={job.progress} aria-label="WAV export progress" /><button className="btn btn--quiet btn--sm" disabled={renderBusy || !client.supports("cancelRender")} onClick={() => void cancel()}>{renderBusy ? "Cancelling…" : "Cancel export"}</button></> : null}
      {!state.render.pending && job?.state === "complete" ? <span role="status">WAV export complete. {download ? <a href={download} data-artifact-id={job.artifactId} download={job.filename ?? undefined}>Download WAV</a> : "Download is unavailable."}</span> : null}
      {job?.state === "failed" ? <span role="alert">WAV export failed: {job.message}</span> : null}
      {job?.state === "cancelled" ? <span role="status">WAV export cancelled.</span> : null}
      {active && jobQuery.status === "error" ? <span role="alert">Export status unavailable: {jobQuery.error.message}</span> : null}
    </div>
    <ExportHistory />
  </section>;
}
