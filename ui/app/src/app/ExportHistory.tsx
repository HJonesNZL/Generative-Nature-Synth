import { useEffect } from "react";
import { useAppState, useDispatch } from "./store";
import { useHost, useHostQuery, valueOf } from "./HostContext";

export function ExportHistory() {
  const { client, connection } = useHost();
  const { render } = useAppState();
  const dispatch = useDispatch();
  const [query, refresh] = useHostQuery("listExports", { limit: 100 }, [render.job?.state]);
  const history = valueOf(query);
  useEffect(() => {
    const active = history?.activeRender;
    if (active && !render.pending && !render.job) dispatch({ type: "renderStarted", job: active });
  }, [history?.activeRender, render.pending, render.job, dispatch]);
  if (!client.supports("listExports")) return null;
  return <details className="export-history">
    <summary>Saved audio exports{history ? ` · ${history.items.length}` : ""}</summary>
    <div className="export-history__body">
      <button className="btn btn--quiet btn--sm" disabled={connection !== "connected"} onClick={refresh}>Refresh exports</button>
      {query.status === "error" ? <p role="alert">{query.error.message}</p> : null}
      {history?.items.length === 0 ? <p className="small muted">Completed WAV files will appear here, including after restarting.</p> : null}
      {history?.items.map(item => <div className="export-history__item" key={item.artifactId}>
        <span><strong>{item.sceneName}</strong><small>{(Number(item.durationUs) / 1000000).toFixed(1)} s · {item.sampleRate / 1000} kHz · 24-bit</small></span>
        <a href={`/__atlas/exports/${item.artifactId}`} data-artifact-id={item.artifactId} download={`Animal-Atlas-${item.artifactId}.wav`}>Save WAV</a>
        {window.__ATLAS_NATIVE_FILES__ ? <button className="btn btn--quiet btn--sm" data-reveal-artifact-id={item.artifactId}>Show file</button> : null}
      </div>)}
      {history?.limited ? <p className="small muted">Showing a bounded selection. Use File / Project → Show app data for the full folder.</p> : null}
      {history && history.unavailableFiles > 0 ? <p role="status" className="small">{history.unavailableFiles} unavailable WAV files were excluded.</p> : null}
      {history && history.incompleteRenders > 0 ? <p role="status" className="small">{history.incompleteRenders} unfinished render files. These may belong to an active export.</p> : null}
    </div>
  </details>;
}
