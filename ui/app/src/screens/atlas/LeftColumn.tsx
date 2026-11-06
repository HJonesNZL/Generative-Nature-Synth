/**
 * Atlas left column. Content depends on the material context:
 * Live → species (highlight) + source health; Replay → saved pieces;
 * History → query → coverage preview → freeze.
 */
import { useEffect, useMemo, useState } from "react";
import type { SourceHealth } from "../../host/contracts";
import { useHost, useHostQuery, valueOf } from "../../app/HostContext";
import { useAppState, useDispatch } from "../../app/store";
import type { Catalog } from "../../app/catalog";
import { DISPLAY_TIMEZONE, formatAge, formatHm, formatDate, zonedToUtc, zoneAbbreviation } from "../../app/time";
import { EmptyState, Notice } from "../../components/Notice";
import { SegmentedControl } from "../../components/SegmentedControl";
import type { MaterialView } from "../shared/useMaterialView";

export function LeftColumn({ view, catalog }: { view: MaterialView; catalog: Catalog }) {
  const { mode } = useAppState();
  return (
    <aside className="atlas__left" aria-label={mode === "history" ? "History query" : mode === "replay" ? "Saved pieces" : "Species and sources"}>
      {mode === "history" ? <HistoryQueryPanel catalog={catalog} /> : mode === "replay" ? <SessionsPanel /> : <SpeciesPanel view={view} catalog={catalog} />}
      {mode !== "history" ? <SourcesPanel catalog={catalog} /> : null}
    </aside>
  );
}

function SpeciesPanel({ view, catalog }: { view: MaterialView; catalog: Catalog }) {
  const { highlightSpeciesId } = useAppState();
  const dispatch = useDispatch();
  const [search, setSearch] = useState("");
  const counts = useMemo(() => {
    const c = new Map<string, number>();
    for (const e of view.allEvents) if (e.speciesId) c.set(e.speciesId, (c.get(e.speciesId) ?? 0) + 1);
    return c;
  }, [view]);
  const species = useMemo(() => {
    const known = catalog.data?.species ?? [];
    const ids = new Set(known.map((species) => species.id));
    const extra = [...counts.keys()].filter((id) => !ids.has(id)).map((id) => ({ id, commonName: id, scientificName: "", group: "unknown" }));
    return [...known, ...extra].sort((a, b) => a.commonName.localeCompare(b.commonName));
  }, [catalog.data, counts]);
  return (
    <section className="stack-sm">
      <span className="label">SPECIES · {species.length} AVAILABLE</span>
      {catalog.status === "unsupported" ? <Notice tone="caution" title="Species list unavailable">This host does not provide a catalogue.</Notice> : null}
      {catalog.data?.limited ? <Notice tone="caution" title="Catalogue display limited">Names and stations are sampled from at most 2,000 records.</Notice> : null}
      <label className="field-label" htmlFor="species-search">Find species</label>
      <input id="species-search" type="search" className="input" value={search} onChange={(event) => setSearch(event.target.value)} />
      <button type="button" className="btn btn--quiet btn--sm" aria-pressed={highlightSpeciesId === null} onClick={() => dispatch({ type: "highlightSpecies", speciesId: null })}>All available species</button>
      <span className="small muted">Counts use all loaded records. Select a species to highlight its reported locations.</span>
      <ul className="species-list" role="list">
        {species.filter((species) => `${species.commonName} ${species.scientificName} ${species.id}`.toLocaleLowerCase().includes(search.toLocaleLowerCase())).map((s, i) => {
          const on = s.id === highlightSpeciesId;
          const n = counts.get(s.id) ?? 0;
          return (
            <li key={s.id}>
              <button type="button" className={`species-row${on ? " is-on" : ""}`} aria-pressed={on} onClick={() => dispatch({ type: "highlightSpecies", speciesId: on ? null : s.id })}>
                <span className={`swatch swatch--${s.group === "shark" ? "marine" : i % 6 + 1}`} aria-hidden="true" />
                <span className="species-row__name">{s.commonName}</span>
                <span className="species-row__count mono">{n}</span>
                <span className="sr-only">{on ? ", highlighted on map" : ", press to highlight on map"}</span>
              </button>
            </li>
          );
        })}
      </ul>
      {species.length && view.feed.status === "running" && view.events.length === 0 ? <EmptyState title="No records in the current hour">A quiet source stays quiet; nothing is invented.</EmptyState> : null}
    </section>
  );
}

function SourcesPanel({ catalog }: { catalog: Catalog }) {
  const { onNotification } = useHost();
  const [q, refresh] = useHostQuery("getSourceHealth", {}, [], { refreshMs: 10_000 });
  useEffect(() => onNotification((n) => { if (n.type === "sourceHealthChanged") refresh(); }), [onNotification, refresh]);
  const health = valueOf(q) ?? [];
  const now = Date.now();
  const allDisabled = health.length > 0 && health.every((h) => h.connection === "disabled");
  return (
    <section className="stack-sm sources">
      <span className="label">SOURCES</span>
      {q.status === "unsupported" ? <Notice tone="caution" title="Source status unavailable">This host does not report source health.</Notice> : null}
      {q.status === "error" ? <Notice tone="problem" title="Could not read source status">{q.error.message}</Notice> : null}
      {allDisabled ? <Notice tone="info" title="No sources selected">Nothing will arrive until a source is enabled in source setup.</Notice> : null}
      {health.map((h) => <SourceRow key={h.sourceId} health={h} name={catalog.sourceName(h.sourceId)} now={now} />)}
      <button type="button" className="btn btn--quiet btn--sm" disabled title="Source setup drawer needs host support (see INTEGRATION_GAPS.md)">Source setup…</button>
    </section>
  );
}

const CONNECTION_TEXT: Record<SourceHealth["connection"], { glyph: string; text: string }> = {
  connecting: { glyph: "◌", text: "Connecting" },
  connected: { glyph: "●", text: "Connected" },
  retrying: { glyph: "↻", text: "Retrying" },
  offline: { glyph: "✕", text: "Offline" },
  disabled: { glyph: "○", text: "Not selected" },
};

function SourceRow({ health, name, now }: { health: SourceHealth; name: string; now: number }) {
  const c = CONNECTION_TEXT[health.connection];
  const newestAgeMs = health.newestObservationUtc ? now - Date.parse(health.newestObservationUtc) : null;
  const stale = health.connection === "connected" && newestAgeMs !== null && newestAgeMs > 3 * 3_600_000;
  return (
    <div className={`source-row${health.connection === "offline" || health.connection === "retrying" ? " is-problem" : ""}`}>
      <span className="source-row__name">{name}</span>
      <span className="source-row__line"><span aria-hidden="true">{c.glyph}</span> {c.text}{health.lastSuccessfulFetchUtc ? ` · fetched ${formatAge(health.lastSuccessfulFetchUtc, now)} ago` : ""}</span>
      <span className={`source-row__line${stale ? " is-stale" : ""}`}>
        Newest observation {health.newestObservationUtc ? `${formatAge(health.newestObservationUtc, now)} old` : "unknown"}{stale ? " · stale" : ""}
      </span>
      {health.errorCode ? <span className="source-row__line is-problem">Error: {health.errorCode}</span> : null}
      {health.coverage !== "best_effort" ? <span className="source-row__line muted">Coverage {health.coverage}</span> : null}
    </div>
  );
}

function SessionsPanel() {
  const { client } = useHost();
  const state = useAppState();
  const dispatch = useDispatch();
  const [q, refreshSessions] = useHostQuery("listSessions", { limit: 100 }, []);
  const sessions = valueOf(q) ?? [];

  return (
    <section className="stack-sm">
      <span className="label">SAVED PIECES</span>
      {q.status === "unsupported" ? <Notice tone="caution" title="Saved pieces unavailable">This host cannot list sessions.</Notice> : null}
      {q.status === "error" ? <Notice tone="problem" title="Could not read saved pieces" action={<button type="button" className="btn btn--quiet btn--sm" onClick={refreshSessions}>Retry</button>}>{q.error.message}</Notice> : null}
      {sessions.length === 100 ? <Notice tone="caution" title="Library display limit">Showing the newest 100 pieces. Full library pagination is not implemented yet.</Notice> : null}
      {state.replay.session?.metadataIntegrity === "legacy-metadata-unverified" ? <Notice tone="caution" title="Legacy metadata unverified">Event content is checked when opened; the old saved interpretation has no metadata seal.</Notice> : null}
      {q.status === "ready" && sessions.length === 0 ? <EmptyState title="No saved pieces yet">Collect for a while, or freeze a history selection.</EmptyState> : null}
      <ul className="session-list" role="list">
        {sessions.map((s) => {
          const on = state.replay.session?.id === s.id;
          return (
            <li key={s.id}>
              <button type="button" className={`session-row${on ? " is-on" : ""}`} aria-pressed={on} onClick={() => { dispatch({ type: "replaySession", session: s });  }}>
                <span className="session-row__name">{s.name}</span>
                <span className="session-row__meta">
                  {s.origin === "imported_bundle" ? <span className="badge">Imported</span> : null}
                  {s.state === "interrupted" ? <span className="badge badge--caution">Interrupted</span> : null}
                  {s.captureTruncated ? <span className="badge badge--caution">Truncated</span> : null}
                  <span className="mono">{s.eventCount} records</span>
                </span>
              </button>
            </li>
          );
        })}
      </ul>
      {state.replay.session?.captureTruncated ? <Notice tone="caution" title="Capture truncated">The piece hit a storage limit; later records were not kept.</Notice> : null}
      {state.replay.session?.state === "interrupted" ? <Notice tone="caution" title="Interrupted capture">Collection stopped unexpectedly. The piece ends at the last durable record; no coverage is assumed after it.</Notice> : null}
      {state.transport.error ? <Notice tone="problem" title="Playback unavailable">{state.transport.error}</Notice> : null}
      {state.replay.session ? (
        <div className="stack-sm">
          <SegmentedControl<string> label="Replay speed" size="sm" value={state.replay.speedNumerator} onChange={(v) => { dispatch({ type: "replaySpeed", speedNumerator: v }); }} options={[{ value: "1", label: "1×" }, { value: "10", label: "10×" }, { value: "60", label: "60×" }, { value: "240", label: "240×" }]} />
          <SegmentedControl<"observed" | "arrival"> label="Timeline basis" size="sm" value={state.timeBasis} onChange={(b) => { dispatch({ type: "timeBasis", basis: b }); }} options={[{ value: "observed", label: "Observed" }, { value: "arrival", label: "Arrival" }]} />
          <span className="small muted">{state.transport.preparing ? "Preparing playback…" : state.transport.preparationId ? (client.supports("setTransport") ? "Ready — use Play in the header." : "Timeline verified. Audio playback is not implemented yet.") : ""}</span>
        </div>
      ) : null}
    </section>
  );
}

function HistoryQueryPanel({ catalog }: { catalog: Catalog }) {
  const { client } = useHost();
  const state = useAppState();
  const dispatch = useDispatch();
  const { draft, result, frozen } = state.history;
  const [regionsQ] = useHostQuery("listRegions", {}, []);
  const regions = valueOf(regionsQ) ?? [];
  const [busy, setBusy] = useState<"query" | "freeze" | "prepare" | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [name, setName] = useState("");
  const [preserve, setPreserve] = useState(true);

  const fromUtc = zonedToUtc(draft.date, draft.from, draft.timezone);
  const toUtc = zonedToUtc(draft.date, draft.to, draft.timezone);
  const invalid = !fromUtc || !toUtc ? "Enter a valid date and times." : Date.parse(toUtc) <= Date.parse(fromUtc) ? "End must be after start (same day)." : draft.speciesIds.length === 0 ? "Choose at least one species." : null;

  const check = async () => {
    if (invalid || !fromUtc || !toUtc) return;
    setBusy("query");
    setError(null);
    const r = await client.call("queryHistory", { speciesIds: draft.speciesIds, regionId: draft.regionId, fromUtc, toUtc, displayTimezone: draft.timezone }, { timeoutMs: 20_000 });
    setBusy(null);
    if (!r.ok) { setError(r.error.message); return; }
    dispatch({ type: "historyResult", result: { ...r.value, requestedFromUtc: fromUtc, requestedToUtc: toUtc } });
    setName(`${catalog.speciesName(draft.speciesIds[0] ?? null)} · ${formatDate(fromUtc, draft.timezone)}`);
  };

  const freeze = async () => {
    if (!result) return;
    setBusy("freeze");
    setError(null);
    const r = await client.call("freezeHistorySelection", { selectionId: result.selectionId, name, preserveRequestedInterval: preserve });
    setBusy(null);
    if (!r.ok) { setError(r.error.message); return; }
    dispatch({ type: "historyFrozen", frozen: r.value });
    setBusy("prepare");
    const p = await client.call("prepareReplay", { sessionId: r.value.id, basis: "observed", speedNumerator: "60", speedDenominator: "1" });
    setBusy(null);
    if (p.ok) dispatch({ type: "transport", transport: { preparationId: p.value.preparationId, generation: p.value.generation, durationUs: p.value.durationUs, positionUs: "0", state: "stopped" } });
    else setError(p.error.message);
  };

  const minutes = (state: string) => result ? Math.round(result.coverage.filter((c) => c.state === state).reduce((sum, c) => sum + (Date.parse(c.toUtc) - Date.parse(c.fromUtc)), 0) / 60_000) : 0;

  return (
    <section className="stack history-query">
      <span className="label">HISTORICAL SOUNDSCAPE</span>
      <fieldset className="fieldset">
        <legend className="field-label">Species</legend>
        <div className="chip-row">
          {(catalog.data?.species ?? []).map((s) => {
            const on = draft.speciesIds.includes(s.id);
            return (
              <button key={s.id} type="button" className={`chip${on ? " is-on" : ""}`} aria-pressed={on} onClick={() => dispatch({ type: "historyDraft", draft: { speciesIds: on ? draft.speciesIds.filter((x) => x !== s.id) : [...draft.speciesIds, s.id] } })}>
                {s.commonName}
              </button>
            );
          })}
        </div>
      </fieldset>
      <div className="form-grid">
        <label htmlFor="hq-region" className="field-label">Region</label>
        <select id="hq-region" className="select" value={draft.regionId} onChange={(e) => dispatch({ type: "historyDraft", draft: { regionId: e.target.value } })}>
          {regions.map((r) => <option key={r.id} value={r.id}>{r.name}</option>)}
        </select>
        <label htmlFor="hq-date" className="field-label">Date</label>
        <input id="hq-date" className="input" type="date" value={draft.date} onChange={(e) => dispatch({ type: "historyDraft", draft: { date: e.target.value } })} />
        <span className="field-label" id="hq-window">Window</span>
        <div className="input-pair" role="group" aria-labelledby="hq-window">
          <input aria-label="From" className="input mono" type="time" value={draft.from} onChange={(e) => dispatch({ type: "historyDraft", draft: { from: e.target.value } })} />
          <span aria-hidden="true">–</span>
          <input aria-label="To" className="input mono" type="time" value={draft.to} onChange={(e) => dispatch({ type: "historyDraft", draft: { to: e.target.value } })} />
        </div>
        <label htmlFor="hq-tz" className="field-label">Timezone</label>
        <select id="hq-tz" className="select" value={draft.timezone} onChange={(e) => dispatch({ type: "historyDraft", draft: { timezone: e.target.value } })}>
          <option value={DISPLAY_TIMEZONE}>New Zealand ({fromUtc ? zoneAbbreviation(fromUtc, DISPLAY_TIMEZONE) : "NZ"})</option>
          <option value="UTC">UTC</option>
        </select>
      </div>
      {invalid ? <span className="small field-error" role="status">{invalid}</span> : null}
      <button type="button" className="btn btn--outline" disabled={!!invalid || busy !== null || !client.supports("queryHistory")} onClick={() => void check()}>
        {busy === "query" ? "Checking archive…" : "Check available data"}
      </button>
      {!client.supports("queryHistory") ? <Notice tone="caution" title="History not offered">This host cannot query archives yet.</Notice> : null}
      {error ? <Notice tone="problem" title="History request failed">{error}</Notice> : null}

      {result ? (
        <div className="stack-sm history-result" aria-live="polite">
          {result.status === "unavailable" ? <Notice tone="problem" title="Archive unavailable">No permitted archive could be reached for this request. Nothing was selected.</Notice> : null}
          {result.status === "unknown" ? <Notice tone="caution" title="Coverage unknown">The source cannot say whether it was recording. Do not read silence here as absence of animals.</Notice> : null}
          {result.status === "ready" && result.eventCount === 0 ? <Notice tone="info" title="Covered, no matching records">The archive was recording, but holds no detections of these species here and then.</Notice> : null}
          {result.status === "partial" ? <Notice tone="caution" title="Partial coverage">Some of the window has no records or unknown coverage.</Notice> : null}
          <dl className="kv">
            <dt>Requested</dt><dd className="mono">{formatHm(result.requestedFromUtc, draft.timezone)}–{formatHm(result.requestedToUtc, draft.timezone)} {zoneAbbreviation(result.requestedFromUtc, draft.timezone)}</dd>
            <dt>Records</dt><dd>{result.eventCount === null ? "unknown" : result.eventCount}</dd>
            <dt>With records</dt><dd className="mono">{minutes("records_available")} min</dd>
            <dt>No detections</dt><dd className="mono">{minutes("confirmed_coverage_no_detections")} min</dd>
            <dt>Missing / unknown</dt><dd className="mono">{minutes("missing_coverage") + minutes("unknown")} min</dd>
          </dl>
          {result.status !== "unavailable" && result.eventCount !== 0 ? (
            frozen ? (
              <Notice tone="info" title={`Frozen: ${frozen.name}`}>{frozen.eventCount} records fixed. Material {frozen.materialFromUtc ? formatHm(frozen.materialFromUtc, draft.timezone) : "?"}–{frozen.materialToUtc ? formatHm(frozen.materialToUtc, draft.timezone) : "?"}. {busy === "prepare" ? "Preparing playback…" : state.transport.preparationId ? "Ready to play." : ""}</Notice>
            ) : (
              <div className="stack-sm">
                <label className="field-label" htmlFor="hq-name">Name</label>
                <input id="hq-name" className="input" value={name} onChange={(e) => setName(e.target.value)} />
                <fieldset className="fieldset">
                  <legend className="field-label">Playback length</legend>
                  <label className="radio"><input type="radio" name="hq-preserve" checked={preserve} onChange={() => setPreserve(true)} /> Keep the requested interval (gaps stay silent)</label>
                  <label className="radio"><input type="radio" name="hq-preserve" checked={!preserve} onChange={() => setPreserve(false)} /> Trim playback to first–last record</label>
                </fieldset>
                <button type="button" className="btn btn--primary" disabled={!name.trim() || busy !== null || !client.supports("freezeHistorySelection")} onClick={() => void freeze()}>
                  {busy === "freeze" ? "Freezing…" : "Freeze soundscape"}
                </button>
                {!client.supports("freezeHistorySelection") ? <span className="small muted">This host cannot freeze selections yet.</span> : null}
              </div>
            )
          ) : null}
        </div>
      ) : null}
    </section>
  );
}
