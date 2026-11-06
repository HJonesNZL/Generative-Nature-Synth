/**
 * Screen 2 — Sound Studio (first pass in the H2 language; NOT yet reviewed
 * as a canvas design). Same continuous ground, same Grain panel as Atlas.
 * Shares selection, material, transport and region with the Atlas.
 */
import { useCallback, useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import { useCatalog } from "../../app/catalog";
import { useHost, useHostQuery, valueOf } from "../../app/HostContext";
import { useAppState, useDispatch } from "../../app/store";
import { GrainPanel } from "../shared/GrainPanel";
import { useMaterialView } from "../shared/useMaterialView";
import { RegionEffectsPanel } from "./RegionEffectsPanel";
import { VoiceEditor } from "./VoiceEditor";
import { VoiceLibrary } from "./VoiceLibrary";

export function SoundStudioScreen() {
  const view = useMaterialView();
  const catalog = useCatalog();
  const state = useAppState();
  const [libQ, refreshLib] = useHostQuery("getVoiceLibrary", {}, []);
  const lib = valueOf(libQ);
  const [dirty, setDirty] = useState<Set<string>>(new Set());
  const onDirtyChange = useCallback((voiceId: string, isDirty: boolean) => {
    setDirty((d) => {
      if (d.has(voiceId) === isDirty) return d;
      const next = new Set(d);
      if (isDirty) next.add(voiceId); else next.delete(voiceId);
      return next;
    });
  }, []);
  useEffect(() => { refreshLib(); }, [dirty.size, refreshLib]);
  const voiceName = useMemo(() => {
    const id = state.selectedVoiceId;
    if (!id || !lib) return null;
    for (const g of lib.entries) {
      if (g.groupVoiceId === id) return `${g.groupName} default`;
      for (const s of g.species) {
        if (s.voiceId === id) return `${s.commonName} voice`;
        for (const i of s.individuals) if (i.voiceId === id) return `${i.label} override`;
      }
    }
    return id;
  }, [lib, state.selectedVoiceId]);
  const libStatus = libQ.status === "ready" ? "ready" : libQ.status === "unsupported" ? "unsupported" : libQ.status === "error" ? "error" : "loading";

  return (
    <main className="screen studio" id="main" aria-label="Sound Studio">
      <ContextStrip actions={<StudioActions selectedEventId={state.selectedEventId} />} />
      <VoiceLibrary entries={lib?.entries ?? []} dirtyIds={dirty} status={libStatus} />
      <VoiceEditor key={state.selectedVoiceId ?? "no-voice"} parameters={lib?.parameters ?? []} catalog={catalog} onDirtyChange={onDirtyChange} />
      <RegionEffectsPanel voiceName={voiceName} />
      <GrainPanel view={view} catalog={catalog} />
    </main>
  );
}

function ContextStrip({ actions }: { actions: ReactNode }) {
  const { caps } = useHost();
  const state = useAppState();
  const catalog = useCatalog();
  const text = caps?.implementation === "native" && !caps.audioAvailable ? "Data preview — sound design is not available from this host." : state.mode === "live"
    ? "Editing while listening live — changes apply to new arrivals."
    : state.mode === "replay"
      ? state.replay.session ? `Editing for saved piece “${state.replay.session.name}”${state.replay.session.origin === "imported_bundle" ? " (imported)" : ""}.` : "Replay — no piece open."
      : state.history.frozen ? `Editing for frozen history “${state.history.frozen.name}”.` : state.history.result ? "History selection not yet frozen." : "History — no selection yet.";
  return (
    <div className="context-strip" role="status">
      <span className="badge badge--context">{state.mode.toUpperCase()}</span>
      <span className="small">{text}</span>
      {state.highlightSpeciesId ? <span className="small muted">Highlighted: {catalog.speciesName(state.highlightSpeciesId)}</span> : null}
      <div className="context-strip__actions">{actions}</div>
    </div>
  );
}

function StudioActions({ selectedEventId }: { selectedEventId: string | null }) {
  const { client, onNotification } = useHost();
  const state = useAppState();
  const dispatch = useDispatch();
  const job = state.render.job;
  const currentJobId = useRef(job?.jobId);
  currentJobId.current = job?.jobId;
  const active = job?.state === "queued" || job?.state === "rendering";
  const starting = useRef(false);
  const [cancelling, setCancelling] = useState(false);
  const [jobQ] = useHostQuery("getRenderJob", active && job ? { jobId: job.jobId } : null, [job?.jobId], { refreshMs: 1000 });
  const polled = valueOf(jobQ);
  useEffect(() => {
    if (polled && polled.jobId === job?.jobId) dispatch({ type: "renderJob", job: polled });
  }, [polled, job?.jobId, dispatch]);
  const [message, setMessage] = useState<string | null>(null);
  useEffect(() => onNotification((n) => { if (n.type === "renderProgress" && n.job.jobId === job?.jobId) dispatch({ type: "renderJob", job: n.job }); }), [onNotification, job?.jobId, dispatch]);
  const renderSessionId = state.mode === "replay" ? state.replay.session?.id : state.mode === "history" ? state.history.frozen?.id : undefined;

  const audition = async () => {
    if (!selectedEventId || !state.selectedVoiceId) return;
    const r = await client.call("auditionEvent", { eventId: selectedEventId, voiceId: state.selectedVoiceId });
    setMessage(r.ok ? (r.value.accepted ? "Auditioning selected event (host plays it; demo host is silent)." : "Audio is off or unavailable.") : r.error.message);
  };
  const render = async () => {
    if (!renderSessionId || starting.current || state.render.pending || active) return;
    starting.current = true;
    dispatch({ type: "renderPending", pending: true });
    setMessage(null);
    const r = await client.call("startRender", { sessionId: renderSessionId, format: "wav_stereo_48k_24", tailPolicy: "let_ring" });
    starting.current = false;
    dispatch({ type: "renderPending", pending: false });
    if (r.ok) dispatch({ type: "renderStarted", job: r.value }); else setMessage(r.error.message);
  };
  const cancel = async () => {
    if (!job || cancelling || !client.supports("cancelRender")) return;
    setCancelling(true);
    setMessage(null);
    const r = await client.call("cancelRender", { jobId: job.jobId });
    setCancelling(false);
    if (currentJobId.current !== job.jobId) return;
    if (r.ok) dispatch({ type: "renderJob", job: r.value }); else setMessage(r.error.message);
  };
  return (
    <div className="studio-actions">
      <button type="button" className="btn btn--outline btn--sm" disabled={!selectedEventId || !state.selectedVoiceId || !client.supports("auditionEvent")} onClick={() => void audition()}>Audition selected event</button>
      <button type="button" className="btn btn--outline btn--sm" disabled={!renderSessionId || !client.supports("startRender") || state.render.pending || active} onClick={() => void render()} title={renderSessionId ? "Render the frozen piece offline to WAV" : "Open a saved piece or freeze a history selection first"}>{state.render.pending ? "Starting render…" : "Render WAV…"}</button>
      {job ? (
        <div className="render-status" role="status">
          {active ? (
            <>
              <span className="small">{job.state === "queued" ? "Render queued." : "Rendering…"}</span>
              <progress max={1} value={job.progress} aria-label="Render progress" />
              <button type="button" className="btn btn--quiet btn--sm" disabled={cancelling || !client.supports("cancelRender")} onClick={() => void cancel()}>{cancelling ? "Cancelling…" : "Cancel"}</button>
            </>
          ) : null}
          {job.state === "complete" ? <span className="small"><strong>Render complete.</strong> {job.message}</span> : null}
          {job.state === "failed" ? <span className="small warn" role="alert"><strong>Render failed.</strong> {job.message}</span> : null}
          {job.state === "cancelled" ? <span className="small"><strong>Render cancelled.</strong> {job.message}</span> : null}
        </div>
      ) : null}
      {active && jobQ.status === "error" ? <span className="small" role="alert">Render status could not be refreshed: {jobQ.error.message}</span> : null}
      {active && !client.supports("getRenderJob") ? <span className="small muted">Status polling is not offered; waiting for host progress notifications.</span> : null}
      {message ? <span className="small" role="status">{message}</span> : null}
    </div>
  );
}
