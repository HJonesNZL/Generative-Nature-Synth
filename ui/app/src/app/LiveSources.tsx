import { useState } from "react";
import { useLiveCollection } from "./LiveCollectionContext";
import type { HostMethods } from "../host/contracts";
import { useHost } from "./HostContext";
import { formatAge } from "./time";
import { Icon } from "../components/Icon";
import { Notice } from "../components/Notice";
import { Popover } from "../components/Popover";

type LiveSourceState = HostMethods["getLiveSources"]["response"];
type LiveSource = LiveSourceState["sources"][number];
type SourceId = LiveSource["id"];

const SOURCE_NAMES: Record<SourceId, string> = { birdweather: "NZ bird detections", nzsharks: "NZ shark reports", birdweather_world: "Worldwide bird detections", inaturalist: "Worldwide reported sightings" };
const DEFAULT_SOURCES: SourceId[] = ["birdweather", "nzsharks"];

/** Status polling is read-only and bounded. Applying an explicit source selection
 * cancels/invalidate older status reads, so an old read cannot undo its receipt.
 * Native workers own collection; this React state owns only controls and feedback.
 */

export function LiveSourcesControl() {
  const { client, caps } = useHost();
  const { status, error, pending, apply, connected, refresh } = useLiveCollection();
  const [draft, setDraft] = useState<SourceId[] | null>(null);
  const [applyError, setApplyError] = useState<string | null>(null);
  const enabled = status?.sources.filter(source => source.enabled).map(source => source.id) ?? [];
  const defaults: SourceId[] = status?.sources.some(source => source.id === "birdweather_world") ? ["birdweather_world", "nzsharks"] : DEFAULT_SOURCES;
  const selected = draft ?? (enabled.length ? enabled : defaults);
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
  return <><Popover className="live-sources" label={collecting ? "Live collection options" : "Start live collection"} trigger={<span className={`collect-chip${collecting ? " is-on" : ""}`}><Icon name="collect" size={14} />{collecting ? "Collecting" : "Collect"}</span>}>
    {close => <div className="menu live-sources__menu">
      <p className="menu__title">Live sources</p>
      <p className="menu__note">Updates run only while this app is open. Each launch starts paused; choose sources and start again.</p>
      <fieldset className="live-sources__choices" disabled={disabled}>
        <legend className="sr-only">Sources to collect</legend>
        {(status?.sources.map(source => source.id) ?? DEFAULT_SOURCES).map(id => <label className="menu__check" key={id}>
          <input type="checkbox" checked={selected.includes(id)} onChange={event => setDraft(event.target.checked ? [...selected.filter(other => !(id === "birdweather" && other === "birdweather_world" || id === "birdweather_world" && other === "birdweather")), id] : selected.filter(sourceId => sourceId !== id))} />
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
  </Popover>{status?.sessionError ? <span className="header-error" role="alert">{status.sessionError}</span> : null}</>;
}

export function LiveSourcesStatus() {
  const { caps } = useHost();
  const { status, error, refresh } = useLiveCollection();
  const now = Date.now();
  return <section className="stack-sm sources live-source-status" aria-label="Live source status">
    <span className="label">SOURCES</span>
    {status ? <>
      {!status.libraryActive ? <p className="small muted">{status.collecting ? "Live updates use the app’s live library; this view is browsing another library." : caps?.libraryAvailable === false ? "Live updates paused." : "Library browsing only. Live updates paused."} Open Collect to choose sources.</p> : !status.collecting ? <p className="small muted">Live updates paused. Open Collect to start again.</p> : null}
      {status.sources.filter(source => status.collecting ? source.enabled : source.lastAttemptUtc || source.id === "nzsharks" || (status.sources.length === 2 ? source.id === "birdweather" : source.id === "birdweather_world")).map(source => <LiveSourceRow key={source.id} source={source} now={now} />)}
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
    {source.error ? <span className={`source-row__line${partialReason ? " is-stale" : " is-problem"}`}>{partialReason ?? `Source error: ${source.error.replaceAll("_", " ")}`}</span> : null}
    {source.gap ? <span className="source-row__line is-stale">Collection gap reported</span> : null}
    {source.lastSuccessUtc && !source.complete && (!partialReason || !source.enabled || source.polling) ? <span className="source-row__line is-stale">Partial source update · coverage incomplete</span> : null}
    <details className="panel-details">
      <summary>Collection details</summary>
      <p>{source.lastAttemptUtc ? `Last attempt ${formatAge(source.lastAttemptUtc, now)} ago.` : "No collection attempt this launch."}</p>
      {source.enabled && !source.polling ? <p>Next check in about {source.nextPollSeconds} seconds; source limits or retries may delay it.</p> : null}
      {source.regions?.length ? <ul aria-label="Bird region coverage">{source.regions.map(region => <li key={region.id}>{region.id.replaceAll("_", " ")} · {region.error || region.gap ? "partial coverage" : region.lastAttemptUtc ? "checked" : "awaiting check"}</li>)}</ul> : null}
      <p>Update age describes when the app checked the source.</p>
    </details>
  </div>;
}

function SourceDetails() {
  return <details className="panel-details live-sources__details">
    <summary>Source scope and timing</summary>
    <p>NZ birds: BirdWeather acoustic species detections at receiver stations. These are not identified individual birds or tracks. Source uploads may be delayed and coverage is best effort.</p>
    <p>NZ sharks: latest reported source-subject snapshots. Subject identity is retained as reported; identified individuals and continuous movement are not guaranteed. Intermediate fixes can be missed.</p>
    <p>Worldwide birds rotate through listening regions under the same four-request budget as NZ birds. Choose one bird coverage option.</p>
    <p>Worldwide sightings sample public CC0 and CC BY animal observations from iNaturalist every 15 minutes. Times and accuracy come from the observer. A sighting represents a report at a place.</p>
    <p>Checks run no more often than once per minute for birds and once per 15 minutes for sharks. A successful check does not make an old observation current.</p>
  </details>;
}
