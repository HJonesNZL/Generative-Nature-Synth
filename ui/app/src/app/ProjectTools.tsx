import { PieceTools } from "./PieceTools";
import { useCallback, useEffect, useId, useRef, useState } from "react";
import { CompositionPanel } from "./CompositionPanel";
import { SoundscapePanel } from "./SoundscapePanel";
import { useHost } from "./HostContext";
import { useAppState } from "./store";

/** Keep drafts, edit finalizers and render polling mounted while the panel is closed. */
export function ProjectTools() {
  const [open, setOpen] = useState(false);
  const [pieceStatus, setPieceStatus] = useState<string | null>(null);
  const { caps } = useHost();
  const state = useAppState();
  const root = useRef<HTMLDetailsElement>(null);
  const summary = useRef<HTMLElement>(null);
  const id = useId();
  const show = useCallback(() => { if (root.current) root.current.open = true; }, []);
  useEffect(() => {
    const outside = (event: MouseEvent) => {
      if (root.current && !root.current.contains(event.target as Node)) root.current.open = false;
    };
    const escape = (event: KeyboardEvent) => {
      if (event.key === "Escape" && root.current?.open) {
        root.current.open = false;
        summary.current?.focus();
      }
    };
    document.addEventListener("mousedown", outside);
    document.addEventListener("keydown", escape);
    return () => {
      document.removeEventListener("mousedown", outside);
      document.removeEventListener("keydown", escape);
    };
  }, []);
  const job = state.render.job;
  const progress = state.render.pending || job?.state === "queued" || job?.state === "rendering";
  return <details className="project-tools" ref={root} onToggle={event => { if (event.target === event.currentTarget) setOpen(event.currentTarget.open); }}>
    <summary ref={summary} id={id} className="pill" aria-label="Project" title="Soundscapes, composition and help">
      Project <span aria-hidden="true">⌄</span>
      {pieceStatus ? <span className="project-tools__state" role="status">{pieceStatus}</span> : null}
      {progress ? <span className="project-tools__state" role="status">Exporting</span> : null}
      {job?.state === "complete" ? <span className="project-tools__state" role="status">WAV ready</span> : null}
      {job?.state === "failed" ? <span className="project-tools__state" role="status">Export failed</span> : null}
    </summary>
    <div className="project-tools__panel" role="region" aria-labelledby={id}>
      <div className="project-tools__intro">
        <h2 className="h3">Project</h2>
        {caps?.libraryAvailable === false ? <p className="small muted"><strong>Scene-only mode.</strong> Import a soundscape or choose a wildlife library in the app toolbar.</p> : null}
      </div>
      <PieceTools open={open} onStatus={setPieceStatus} />
      <SoundscapePanel onAttention={show} />
      <CompositionPanel onAttention={show} />
      <details className="project-tools__help panel-details">
        <summary>Playback help</summary>
        <p className="small">Open a saved piece or soundscape, enable Listening, then press Play. Save a soundscape to preserve its events, voices and composition; WAV export uses the saved version.</p>
        {caps?.implementation === "native" && !caps.audioAvailable ? <p className="small">Audio output is unavailable in this host. Available data and export controls still work.</p> : null}
      </details>
    </div>
  </details>;
}
