import { useEffect, useRef, useState } from "react";
import type { SessionView } from "../../host/contracts";
import { useHost, useHostQuery, valueOf } from "../../app/HostContext";
import { useAppState, useDispatch } from "../../app/store";
import { EmptyState, Notice } from "../../components/Notice";
import { acceptSessionPage, firstSessionPage, nextSessionPage, previousSessionPage, SESSION_PAGE_SIZE, type SessionPageTarget } from "./sessionBrowserState";

/** React owns paging. One successful page survives screen changes in AppState.
 * Requests abort on navigation/unmount; only the current request may publish.
 * Library changes invalidate cursors explicitly and never change replay selection.
 */
export function SessionBrowser() {
  const { caps, client } = useHost();
  if (caps?.libraryAvailable === false) return <p className="small muted">Choose a wildlife library to list its pieces. Saved soundscapes remain available above.</p>;
  if (!caps) return <p className="small muted" role="status">Connecting to saved pieces…</p>;
  return client.supports("listSessionsPage") ? <PagedSessionBrowser /> : <LegacySessionBrowser />;
}

function PagedSessionBrowser() {
  const { client, caps, connection } = useHost();
  const { sessionBrowser: snapshot, replay } = useAppState();
  const dispatch = useDispatch();
  const [target, setTarget] = useState<SessionPageTarget>(() => snapshot ? { cursor: snapshot.cursor, previousCursors: snapshot.previousCursors, pageNumber: snapshot.pageNumber } : firstSessionPage());
  const [refresh, setRefresh] = useState(0);
  const [busy, setBusy] = useState(true);
  const [error, setError] = useState<{ code: string; message: string } | null>(null);
  const snapshotRef = useRef(snapshot);
  const requestGeneration = useRef(0);
  snapshotRef.current = snapshot;

  useEffect(() => {
    if (!caps || connection !== "connected") return;
    const controller = new AbortController();
    const generation = ++requestGeneration.current;
    const previous = snapshotRef.current;
    setBusy(true);
    setError(null);
    void client.call("listSessionsPage", { limit: SESSION_PAGE_SIZE, cursor: target.cursor }, { signal: controller.signal }).then(result => {
      if (controller.signal.aborted || generation !== requestGeneration.current) return;
      setBusy(false);
      if (!result.ok) { setError(result.error); return; }
      const browser = acceptSessionPage(result.value, target, previous);
      if (!browser) { setError({ code: "stale_generation", message: "The wildlife library changed while browsing. Refresh pieces to start at the newest page." }); return; }
      dispatch({ type: "sessionBrowser", browser });
    });
    return () => controller.abort();
  }, [client, caps, connection, target, refresh, dispatch]);

  const restart = () => { setTarget(firstSessionPage()); setRefresh(value => value + 1); };
  const unavailable = connection !== "connected";
  const previous = snapshot ? previousSessionPage(snapshot) : null;
  const next = snapshot ? nextSessionPage(snapshot) : null;
  const selectedOutsidePage = replay.session && !snapshot?.page.items.some(item => item.id === replay.session?.id);
  return <div className="stack-sm session-browser" aria-busy={busy}>
    <div className="session-browser__navigation" role="group" aria-label="Saved piece pages">
      <button type="button" className="btn btn--quiet btn--sm" disabled={busy || unavailable || !previous || !!error} onClick={() => previous && setTarget(previous)}>Previous</button>
      <button type="button" className="btn btn--quiet btn--sm" disabled={busy || unavailable || !next || !!error} onClick={() => next && setTarget(next)}>Next</button>
      <button type="button" className="btn btn--quiet btn--sm" disabled={unavailable} onClick={restart}>Refresh pieces</button>
    </div>
    <p className="small muted" role="status">{busy ? "Loading saved pieces…" : snapshot ? `Page ${snapshot.pageNumber} · ${snapshot.page.items.length} pieces${snapshot.page.nextCursor === null ? " · End of list" : " · More available"}. Total unknown.` : "Saved-piece total unknown."}</p>
    {snapshot && snapshot.pageNumber > 1 && snapshot.previousCursors.length === 0 ? <p className="small muted">Earlier pages are outside the back history. Refresh pieces to return to the newest page.</p> : null}
    {error ? <Notice tone="problem" title="Could not read saved pieces">{error.message} {error.code === "stale_generation" || error.code === "invalid_request" ? "Refresh pieces to restart this list." : "Use Refresh pieces to retry."}</Notice> : null}
    {selectedOutsidePage ? <p className="small muted">Selected: <strong>{replay.session!.name}</strong> · outside this page.</p> : null}
    {!busy && !error && snapshot?.page.items.length === 0 ? <EmptyState title="No saved pieces yet">Collect for a while, or freeze a history selection.</EmptyState> : null}
    <SessionRows sessions={snapshot?.page.items ?? []} disabled={unavailable || !!error} paged />
  </div>;
}

function LegacySessionBrowser() {
  const [query, refresh] = useHostQuery("listSessions", { limit: 100 }, []);
  const sessions = valueOf(query) ?? [];
  return <div className="stack-sm">
    {query.status === "unsupported" ? <Notice tone="caution" title="Saved pieces unavailable">This host cannot list sessions.</Notice> : null}
    {query.status === "error" ? <Notice tone="problem" title="Could not read saved pieces" action={<button type="button" className="btn btn--quiet btn--sm" onClick={refresh}>Retry</button>}>{query.error.message}</Notice> : null}
    {query.status === "loading" ? <p className="small muted" role="status">Loading saved pieces…</p> : null}
    {query.status === "ready" ? <p className="small muted">{sessions.length} pieces shown. This host does not support paging; the total and further pieces are unknown.</p> : null}
    {sessions.length === 100 ? <Notice tone="caution" title="Library display limit">Showing at most the newest 100 pieces. Update the host to browse older pieces.</Notice> : null}
    {query.status === "ready" && sessions.length === 0 ? <EmptyState title="No saved pieces yet">Collect for a while, or freeze a history selection.</EmptyState> : null}
    <SessionRows sessions={sessions} />
  </div>;
}

function SessionRows({ sessions, disabled = false, paged = false }: { sessions: SessionView[]; disabled?: boolean; paged?: boolean }) {
  const { replay, transport } = useAppState();
  const dispatch = useDispatch();
  return <ul className="session-list" role="list">
    {sessions.map(session => {
      const selected = replay.session?.id === session.id;
      return <li key={session.id}>
        <button type="button" className={`session-row${selected ? " is-on" : ""}`} aria-pressed={selected} disabled={disabled} onClick={() => { if (!paged || !selected || (!transport.preparationId && !transport.preparing)) dispatch({ type: "replaySession", session }); }}>
          <span className="session-row__name">{session.name}</span>
          <span className="session-row__meta">
            {session.origin === "imported_bundle" ? <span className="badge">Imported</span> : null}
            {paged && session.state === "frozen" ? <span className="badge">Frozen</span> : null}
            {session.state === "interrupted" ? <span className="badge badge--caution">Interrupted</span> : null}
            {session.captureTruncated ? <span className="badge badge--caution">Truncated</span> : null}
            <span className="mono">{session.eventCount} records</span>
          </span>
        </button>
      </li>;
    })}
  </ul>;
}
