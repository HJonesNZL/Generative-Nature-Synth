import { useCallback, useEffect, useRef, useState } from "react";
import type { HostMethods } from "../host/contracts";
import { useHost } from "./HostContext";
import { formatAge } from "./time";
import { Icon } from "../components/Icon";
import { Notice } from "../components/Notice";
import { Popover } from "../components/Popover";

type LiveSourceState = HostMethods["getLiveSources"]["response"];
type LiveSource = LiveSourceState["sources"][number];
type SourceId = LiveSource["id"];

const SOURCE_NAMES: Record<SourceId, string> = { birdweather: "NZ bird detections", nzsharks: "NZ shark reports" };
const DEFAULT_SOURCES: SourceId[] = ["birdweather", "nzsharks"];

/** Status polling is read-only and bounded. Applying an explicit source selection
 * cancels/invalidate older status reads, so an old read cannot undo its receipt.
 * Native workers own collection; this React state owns only controls and feedback.
 */
function useLiveSourceState() {
  const { client, caps, connection } = useHost();
  const [status, setStatus] = useState<LiveSourceState | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [pending, setPending] = useState(false);
  const [refresh, setRefresh] = useState(0);
  const generation = useRef(0);
  const readController = useRef<AbortController | null>(null);
  const writing = useRef(false);
  const mounted = useRef(true);
  useEffect(() => { mounted.current = true; return () => { mounted.current = false; ++generation.current; readController.current?.abort(); }; }, []);
  useEffect(() => {
    if (!caps || connection !== "connected" || !client.supports("getLiveSources")) return;
    let stopped = false;
    const pull = async () => {
      if (writing.current || document.hidden) return;
      readController.current?.abort();
      const controller = new AbortController();
      readController.current = controller;
      const requestGeneration = ++generation.current;
      const reply = await client.call("getLiveSources", {}, { signal: controller.signal });
      if (stopped || controller.signal.aborted || requestGeneration !== generation.current) return;
      if (reply.ok) { setStatus(reply.value); setError(null); }
      else setError(reply.error.message);
    };
    void pull();
    const timer = window.setInterval(() => void pull(), 5000);
    return () => { stopped = true; readController.current?.abort(); ++generation.current; window.clearInterval(timer); };
  }, [client, caps, connection, refresh]);

  const apply = useCallback(async (sourceIds: SourceId[]) => {
    writing.current = true;
    readController.current?.abort();
    const requestGeneration = ++generation.current;
    setPending(true);
    const reply = await client.call("setLiveSources", { sourceIds }, { timeoutMs: 30000 });
    writing.current = false;
    if (mounted.current) {
      setPending(false);
      if (reply.ok && requestGeneration === generation.current) { setStatus(reply.value); setError(null); }
    }
    return reply;
  }, [client]);
  return { status, error, pending, apply, refresh: () => setRefresh(value => value + 1), connected: connection === "connected" };
}

export function LiveSourcesControl() {
  const { client, caps } = useHost();
  const { status, error, pending, apply, connected, refresh } = useLiveSourceState();
  const [draft, setDraft] = useState<SourceId[] | null>(null);
  const [applyError, setApplyError] = useState<string | null>(null);
  const enabled = status?.sources.filter(source => source.enabled).map(source => source.id) ?? [];
  const selected = draft ?? (enabled.length ? enabled : DEFAULT_SOURCES);
  const collecting = status?.collecting === true;
  const disabled = pending || !connected || !status || !client.supports("setLiveSources");
  const changeSources = async (sourceIds: SourceId[], close: () => void) => {
    setApplyError(null);
    const reply = await apply(sourceIds);
    if (!reply.ok) { setApplyError(reply.error.message); return; }
    setDraft(null);
    if (caps?.implementation === "native" && reply.value.libraryChanged) { window.location.reload(); return; }
    close();
  };
  return <Popover className="live-sources" label={collecting ? "Live collection options" : "Start live collection"} trigger={<span className={`collect-chip${collecting ? " is-on" : ""}`}><Icon name="collect" size={14} />{collecting ? "Collecting" : "Collect"}</span>}>
    {close => <div className="menu live-sources__menu">
      <p className="menu__title">Live sources</p>
      <p className="menu__note">Updates run only while this app is open. Each launch starts paused; choose sources and start again.</p>
      <fieldset className="live-sources__choices" disabled={disabled}>
        <legend className="sr-only">Sources to collect</legend>
        {DEFAULT_SOURCES.map(id => <label className="menu__check" key={id}>
          <input type="checkbox" checked={selected.includes(id)} onChange={event => setDraft(event.target.checked ? [...selected, id] : selected.filter(sourceId => sourceId !== id))} />
          {SOURCE_NAMES[id]}
        </label>)}
      </fieldset>
      <p className="menu__note">Starting opens the app’s live library. Collection and listening are separate controls.</p>
      {status ? <p className="menu__note" role="status">{collecting ? "Updates scheduled for selected sources." : "Live updates paused."}</p> : <p className="menu__note" role="status">{error ? "Live-source status unavailable." : "Reading live-source status…"}</p>}
      {error ? <Notice tone="problem" title="Could not read live sources" action={<button type="button" className="btn btn--quiet btn--sm" onClick={refresh}>Retry</button>}>{error}</Notice> : null}
      {applyError ? <p className="menu__error" role="alert">{applyError}</p> : null}
      <div className="menu__row">
        <button type="button" className="btn btn--primary btn--sm" disabled={disabled || (!collecting && selected.length === 0)} onClick={() => void changeSources(selected, close)}>{pending ? "Applying…" : collecting ? "Apply source selection" : "Start live updates"}</button>
        {collecting ? <button type="button" className="btn btn--outline btn--sm" disabled={disabled} onClick={() => void changeSources([], close)}>Pause updates</button> : null}
      </div>
      {!client.supports("setLiveSources") ? <p className="menu__note">This host cannot change live-source selection.</p> : null}
      <SourceDetails />
    </div>}
  </Popover>;
}

export function LiveSourcesStatus() {
  const { caps } = useHost();
  const { status, error, refresh } = useLiveSourceState();
  const now = Date.now();
  return <section className="stack-sm sources live-source-status" aria-label="Live source status">
    <span className="label">SOURCES</span>
    {status ? <>
      {!status.libraryActive ? <p className="small muted">{status.collecting ? "Live updates use the app’s live library; this view is browsing another library." : caps?.libraryAvailable === false ? "Live updates paused." : "Library browsing only. Live updates paused."} Open Collect to choose sources.</p> : !status.collecting ? <p className="small muted">Live updates paused. Open Collect to start again.</p> : null}
      {status.sources.map(source => <LiveSourceRow key={source.id} source={source} now={now} />)}
    </> : !error ? <p className="small muted" role="status">Reading live-source status…</p> : null}
    {error ? <Notice tone="problem" title="Could not read live sources" action={<button type="button" className="btn btn--quiet btn--sm" onClick={refresh}>Retry</button>}>{error}</Notice> : null}
    <SourceDetails />
  </section>;
}

function LiveSourceRow({ source, now }: { source: LiveSource; now: number }) {
  const partialReason = source.error === "pagination_limit_reached" ? "Source page limit reached." : source.error === "invalid_or_future_records" ? "Invalid or future-dated reports excluded." : null;
  const update = source.polling ? "Updating…" : !source.enabled ? "Updates paused" : partialReason ? "Partial update · coverage incomplete" : source.error ? "Update failed · retry scheduled" : !source.lastSuccessUtc ? "Waiting for first update" : `Last update ${formatAge(source.lastSuccessUtc, now)} ago`;
  return <div className={`source-row${source.error && !partialReason ? " is-problem" : ""}`}>
    <span className="source-row__name">{SOURCE_NAMES[source.id]}</span>
    <span className="source-row__line">{update}</span>
    {source.lastSuccessUtc && (!source.enabled || source.error || source.polling) ? <span className="source-row__line">Last update {formatAge(source.lastSuccessUtc, now)} ago</span> : null}
    <span className="source-row__line">{source.id === "birdweather" ? "Newest detection" : "Newest reported fix"} {source.lastObservationUtc ? `${formatAge(source.lastObservationUtc, now)} old` : "unknown"}</span>
    {source.error ? <span className={`source-row__line${partialReason ? " is-stale" : " is-problem"}`}>{partialReason ?? `Source error: ${source.error.replaceAll("_", " ")}`}</span> : null}
    {source.gap ? <span className="source-row__line is-stale">Collection gap reported</span> : null}
    {source.lastSuccessUtc && !source.complete && (!partialReason || !source.enabled || source.polling) ? <span className="source-row__line is-stale">Partial source update · coverage incomplete</span> : null}
    <details className="panel-details">
      <summary>Collection details</summary>
      <p>{source.records} collected records. {source.lastAttemptUtc ? `Last attempt ${formatAge(source.lastAttemptUtc, now)} ago.` : "No collection attempt this launch."}</p>
      {source.enabled && !source.polling ? <p>Next check in about {source.nextPollSeconds} seconds; source limits or retries may delay it.</p> : null}
      <p>Observation age describes the animal report; update age describes when the app checked the source.</p>
    </details>
  </div>;
}

function SourceDetails() {
  return <details className="panel-details live-sources__details">
    <summary>Source scope and timing</summary>
    <p>NZ birds: BirdWeather acoustic species detections at receiver stations. These are not identified individual birds or tracks. Source uploads may be delayed and coverage is best effort.</p>
    <p>NZ sharks: latest reported source-subject snapshots. Subject identity is retained as reported; identified individuals and continuous movement are not guaranteed. Intermediate fixes can be missed.</p>
    <p>Checks run no more often than once per minute for birds and once per 15 minutes for sharks. A successful check does not make an old observation current.</p>
  </details>;
}
