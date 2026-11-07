import { useEffect, useRef, useState } from "react";
import type { PieceJob, SessionView } from "../host/contracts";
import { useHost, useHostQuery, valueOf } from "./HostContext";
import { useAppState, useDispatch } from "./store";

declare global { interface Window { atlasPendingReplayPiece?: SessionView } }
const utcInput = (milliseconds: number) => new Date(milliseconds).toISOString().slice(0, 19);

/** Mounted with Project: UTC drafts persist closed; one bounded host job, serial polling, explicit open. */
export function PieceTools({ open: isOpen, onStatus }: { open: boolean; onStatus: (status: string | null) => void }) {
  const { client, connection, caps } = useHost();
  const state = useAppState(), dispatch = useDispatch();
  const [name, setName] = useState("");
  const [start, setStart] = useState(() => utcInput(Date.now() - 3600000));
  const [end, setEnd] = useState(() => utcInput(Date.now()));
  const [sourceId, setSourceId] = useState("");
  const [speciesId, setSpeciesId] = useState("");
  const [job, setJob] = useState<PieceJob | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const operation = useRef(false);
  const retry = useRef<{ signature: string; requestId: string } | null>(null);
  const [catalog] = useHostQuery("getCatalog", isOpen && client.supports("startPieceFreeze") ? {} : null, []);
  const [sources] = useHostQuery("getLiveSources", isOpen && client.supports("getLiveSources") ? {} : null, [], { refreshMs: 1000 });
  const catalogueSnapshot = useRef(valueOf(catalog));
  const sourcesSnapshot = useRef(valueOf(sources));
  if (valueOf(catalog)) catalogueSnapshot.current = valueOf(catalog);
  if (valueOf(sources)) sourcesSnapshot.current = valueOf(sources);
  const catalogData = catalogueSnapshot.current;
  const sourceData = sourcesSnapshot.current;
  const active = job?.status === "running";
  const supported = client.supports("getPieceJob");
  useEffect(() => {
    if (!supported || connection !== "connected") return;
    let stopped = false, timer = 0;
    const poll = async () => {
      const result = await client.call("getPieceJob", {}, { timeoutMs: 10000 });
      if (stopped) return;
      if (result.ok) {
        setJob(previous => {
          const next = result.value;
          if (!next) return previous;
          if (previous?.id === next.id && previous.status !== "running" && next.status === "running") return previous;
          return next;
        });
        if (result.value?.status === "running") timer = window.setTimeout(() => void poll(), 500);
      } else setError(result.error.message);
    };
    void poll();
    return () => { stopped = true; window.clearTimeout(timer); };
  }, [client, supported, connection, active]);
  useEffect(() => {
    onStatus(busy ? "Piece working" : active ? "Saving piece" : error || job?.status === "failed" ? "Piece needs attention" : job?.status === "completed" ? "Piece ready" : null);
  }, [busy, active, error, job?.status, onStatus]);
  useEffect(() => {
    if (!supported || connection !== "connected") return;
    const session = window.atlasPendingReplayPiece;
    if (!session) return;
    delete window.atlasPendingReplayPiece;
    dispatch({ type: "mode", mode: "replay" });
    dispatch({ type: "replaySession", session });
  }, [connection, supported, dispatch]);
  if (!supported) return null;
  const disabled = busy || active || connection !== "connected";
  const run = async (kind: "freeze" | "import") => {
    if (operation.current || disabled) return;
    setError(null);
    const startMs = Date.parse(`${start}Z`), endMs = Date.parse(`${end}Z`);
    if (kind === "freeze" && (!name.trim() || new TextEncoder().encode(name.trim()).length > 128 || !Number.isFinite(startMs) || !Number.isFinite(endMs) || endMs <= startMs)) {
      setError("Enter a piece name of at most 128 UTF-8 bytes and a valid UTC interval with the end after the start."); return;
    }
    if (kind === "import" && !state.replay.session) { setError("Select an external saved piece in Replay first."); return; }
    operation.current = true; setBusy(true);
    try {
      const signature = JSON.stringify([name.trim(), start, end, sourceId, speciesId]);
      if (!retry.current || retry.current.signature !== signature) retry.current = { signature, requestId: crypto.randomUUID().replaceAll("-", "") };
      const result = kind === "freeze"
        ? await client.call("startPieceFreeze", { name: name.trim(), startUtc: new Date(startMs).toISOString(), endUtc: new Date(endMs).toISOString(), ...(sourceId ? { sourceId } : {}), ...(speciesId ? { speciesId } : {}), requestId: retry.current.requestId })
        : await client.call("startPieceImport", { sessionId: state.replay.session!.id });
      if (result.ok) { setJob(result.value); retry.current = null; } else setError(result.error.message);
    } finally { operation.current = false; setBusy(false); }
  };
  const cancel = async () => {
    if (!job || busy) return;
    setBusy(true);
    const result = await client.call("cancelPieceJob", { jobId: job.id });
    if (result.ok) setJob(result.value); else setError(result.error.message);
    setBusy(false);
  };
  const open = async () => {
    if (!job?.piece || busy) return;
    setBusy(true); setError(null);
    const result = await client.call("openManagedPiece", { sessionId: job.piece.id });
    if (result.ok) {
      if (result.value.libraryChanged) return;
      dispatch({ type: "mode", mode: "replay" });
      dispatch({ type: "replaySession", session: result.value.session });
    } else setError(result.error.message);
    setBusy(false);
  };
  const external = caps?.libraryAvailable !== false && sourceData?.libraryActive === false;
  return <section className="soundscape-panel stack-sm" aria-label="Saved piece tools">
    <span className="label">SAVED PIECES</span>
    {sourceData?.libraryActive ? <details className="panel-details"><summary>Freeze collected interval</summary>
      <div className="stack-sm">
        <p className="small muted">Save managed collected records by observation time. UTC; the end is excluded. Empty intervals retain the requested time bounds. Stored records do not establish wildlife coverage.</p>
        <label>Piece name<input className="input" maxLength={128} value={name} disabled={disabled} onChange={e => setName(e.target.value)} /></label>
        <label>Start (UTC)<input className="input" type="datetime-local" step="1" value={start} disabled={disabled} onChange={e => setStart(e.target.value)} /></label>
        <label>End (UTC)<input className="input" type="datetime-local" step="1" value={end} disabled={disabled} onChange={e => setEnd(e.target.value)} /></label>
        <button className="btn btn--quiet btn--sm" disabled={disabled} onClick={() => { const now = Date.now(); setStart(utcInput(now - 3600000)); setEnd(utcInput(now)); }}>Last hour</button>
        <label>Source<select aria-label="Source" value={sourceId} disabled={disabled} onChange={e => setSourceId(e.target.value)}><option value="">All sources</option>{sourceId && !catalogData?.sources.some(s => s.id === sourceId) ? <option value={sourceId}>{sourceId}</option> : null}{(catalogData?.sources ?? []).map(s => <option key={s.id} value={s.id}>{s.name}</option>)}</select></label>
        <label>Species<select aria-label="Species" value={speciesId} disabled={disabled} onChange={e => setSpeciesId(e.target.value)}><option value="">All species</option>{speciesId && !catalogData?.species.some(s => s.id === speciesId) ? <option value={speciesId}>{speciesId}</option> : null}{(catalogData?.species ?? []).map(s => <option key={s.id} value={s.id}>{s.commonName}</option>)}</select></label>
        {catalogData?.limited || !catalogData?.sources.length || !catalogData?.species.length ? <details className="panel-details"><summary>Missing source or species</summary>
          <label>Exact source ID<input className="input" value={sourceId} maxLength={128} disabled={disabled} onChange={e => setSourceId(e.target.value)} /></label>
          <label>Exact species ID<input className="input" value={speciesId} maxLength={512} disabled={disabled} onChange={e => setSpeciesId(e.target.value)} /></label>
        </details> : null}
        <button className="btn btn--primary btn--sm" disabled={disabled || !client.supports("startPieceFreeze")} onClick={() => void run("freeze")}>Freeze interval</button>
      </div>
    </details> : <p className="small muted">Choose the managed collection library to freeze an interval.</p>}
    {external ? <div className="stack-sm"><span className="small">Selected external piece: {state.replay.session?.name ?? "Choose a saved piece in Replay"}</span><button className="btn btn--outline btn--sm" disabled={disabled || !state.replay.session || state.mode !== "replay"} onClick={() => void run("import")}>Import selected piece to managed library</button></div> : null}
    {job ? <div className="stack-sm" role="status">
      <span>{job.operation === "freeze" ? "Freeze" : "Import"}: {job.status}{active && job.stage ? ` · ${job.stage}` : ""} · {job.processed}{job.total !== null ? ` / ${job.total}` : ""} records</span>
      {active ? <><progress aria-label="Saved piece progress" max={job.total ?? undefined} value={job.total === null ? undefined : Math.min(job.processed, job.total)} /><button className="btn btn--quiet btn--sm" disabled={busy} onClick={() => void cancel()}>Cancel piece job</button></> : null}
      {job.status === "completed" && job.piece ? <><span>Saved “{job.piece.name}” · {job.piece.id} · {job.piece.eventCount} records{job.piece.eventCount === 0 ? " · No observations in the requested interval" : ""}</span><button className="btn btn--outline btn--sm" disabled={busy || !client.supports("openManagedPiece")} onClick={() => void open()}>Open saved piece</button></> : null}
      {job.error ? <span role="alert">{job.error}</span> : null}
    </div> : null}
    {error ? <p className="small" role="alert">{error}</p> : null}
  </section>;
}
