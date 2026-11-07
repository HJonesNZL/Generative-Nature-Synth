/**
 * Bottom panel shared by both screens ("Grain"): every detection in the
 * active window, one line each, one lane per species. Layout is fixed by the
 * approved H2 design. History mode adds the coverage band; replay adds the
 * host-reported playhead. Lines that were filtered or suppressed are drawn
 * half-height so the distinction is not colour-only.
 */
import { useMemo, type MouseEvent, type ReactNode } from "react";
import type { CoverageInterval, EventView } from "../../host/contracts";
import { useHost } from "../../app/HostContext";
import { useAppState, useDispatch } from "../../app/store";
import type { Catalog } from "../../app/catalog";
import { formatHm, zoneAbbreviation } from "../../app/time";
import { useLiveSpeciesCounts } from "../../app/LiveSpeciesCountsContext";
import { grainLanes } from "./grainLanes";
import "./GrainPanel.css";
import type { MaterialView } from "./useMaterialView";

const LANE_COLORS = ["var(--c-sp-1)", "var(--c-sp-2)", "var(--c-sp-3)", "var(--c-sp-4)", "var(--c-sp-5)", "var(--c-sp-6)"];

export function GrainPanel({ view, catalog, aside }: { view: MaterialView; catalog: Catalog; aside?: ReactNode }) {
  const state = useAppState();
  const dispatch = useDispatch();
  const { client } = useHost();
  const span = Math.max(1, view.windowToMs - view.windowFromMs);
  const x = (t: number) => ((t - view.windowFromMs) / span) * 1000;

  const live = state.mode === "live";
  const ranking = useLiveSpeciesCounts();
  const lanes = useMemo(() => grainLanes(view.events, catalog.data?.species ?? [], state.highlightSpeciesId, live, live && ranking.supported ? ranking.rows ?? [] : null), [view.events, catalog.data, state.highlightSpeciesId, live, ranking.rows, ranking.supported]);

  const selected = view.allEvents.find(event => event.id === state.selectedEventId) ?? null;
  const coverage = state.mode === "history" ? state.history.result?.coverage ?? [] : [];

  const pick = (e: MouseEvent<SVGSVGElement>, events: EventView[]) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const t = view.windowFromMs + ((e.clientX - rect.left) / rect.width) * span;
    let best: EventView | null = null;
    let bestDist = Infinity;
    for (const ev of events) {
      const d = Math.abs(view.timeOf(ev) - t);
      if (d < bestDist) { best = ev; bestDist = d; }
    }
    if (best && bestDist < span / 60) dispatch({ type: "selectEvent", eventId: best.id });
  };

  const ticks = Array.from({ length: 5 }, (_, i) => view.windowFromMs + (span * i) / 4);
  const playhead = state.mode !== "live" && state.transport.durationUs && state.transport.durationUs !== "0"
    ? Number((BigInt(state.transport.positionUs) * 1000n) / BigInt(state.transport.durationUs)) / 1000
    : null;

  return (
    <section className={`grain${live ? " grain--live" : ""}`} aria-label="Detections by species">
      <div className="grain__intro">
        <span className="label">GRAIN · {view.label.toUpperCase()}</span>
        <span className="grain__title">{live ? "Species in this collection" : "Every detection in view, one line each"}</span>
        <details className="panel-details small muted"><summary>{state.timeBasis === "observed" ? "Observation time" : "Arrival time"} · details</summary>
          <p>Short lines were filtered or held back.{state.mode === "live" ? " Counts cover this collection session; ticks show retained detections in the visible time window. Coverage may be incomplete." : ""} No records do not establish that animals were absent or silent.</p>
          {!client.supports("getMusicalOutcomes") ? <p>Musical outcomes unavailable; all lines are full height.</p> : null}
        </details>
        {view.feed.displayOmitted > 0 ? <span className="small is-caution">Display limit: {view.store.size} shown · {view.feed.displayOmitted} omitted</span> : null}
        {(view.feed.status === "retrying" || view.feed.status === "error") ? <span className="small is-problem" role="alert">{view.feed.error}</span> : null}
        {live && ranking.error ? <span className="small is-problem" role="alert">Species counts: {ranking.error}</span> : null}
        {live && !ranking.supported ? <span className="small muted">Counts cover retained display records.</span> : null}
        {aside}
      </div>
      <div className="grain__lanes">
        {coverage.length ? <CoverageBand coverage={coverage} x={x} /> : null}
        <div className={live ? "grain__scroll" : "grain__rows"} role={live ? "region" : undefined} aria-label={live ? "Species ranked by detection count" : undefined} tabIndex={live ? 0 : undefined} aria-busy={live && ranking.loading}>
        {lanes.length === 0 ? (
          <p className="grain__empty">
            {live && ranking.loading ? "Loading species counts…" : view.feed.status === "loading" ? "Loading detections…" : state.mode === "history" && !state.history.result ? "Choose species, place and time, then check available data." : state.mode === "replay" && !state.replay.session ? "Open a saved piece to see its detections." : "No records in this window · coverage may be incomplete."}
          </p>
        ) : lanes.map((lane, i) => {
          const full: string[] = [];
          const short: string[] = [];
          for (const e of lane.events) {
            const o = view.store.outcome(e.id);
            const px = x(view.timeOf(e)).toFixed(1);
            if (o && (o.kind === "filtered" || o.kind === "suppressed")) short.push(`M${px} 9v8`);
            else full.push(`M${px} 0v26`);
          }
          const highlighted = lane.speciesId === state.highlightSpeciesId;
          return (
            <div className="grain__lane" key={lane.speciesId}>
              <button type="button" className={`grain__name${highlighted ? " is-highlighted" : ""}`} aria-pressed={highlighted} onClick={() => dispatch({ type: "highlightSpecies", speciesId: highlighted ? null : lane.speciesId })}>
                {lane.name}
                <span className="sr-only">, {lane.count} detections{highlighted ? ", highlighted" : ""}</span>
              </button>
              <svg viewBox="0 0 1000 26" preserveAspectRatio="none" className="grain__ticks" aria-hidden="true" onClick={(e) => pick(e, lane.events)}>
                <path d={full.join("")} stroke={LANE_COLORS[i % LANE_COLORS.length]} strokeWidth={highlighted ? 2 : 1.4} vectorEffect="non-scaling-stroke" />
                <path d={short.join("")} stroke={LANE_COLORS[i % LANE_COLORS.length]} strokeOpacity={0.6} strokeWidth={1.2} vectorEffect="non-scaling-stroke" />
                {selected && selected.speciesId === lane.speciesId ? <path d={`M${x(view.timeOf(selected)).toFixed(1)} 0v26`} stroke="var(--c-warn)" strokeWidth={2.5} vectorEffect="non-scaling-stroke" /> : null}
              </svg>
              {live ? <span className="grain__count" aria-label={`${lane.count} detections`}>{lane.count.toLocaleString()}</span> : null}
            </div>
          );
        })}
        </div>
        <div className="grain__axis" aria-hidden="true">
          {ticks.map((t, i) => <span key={i}>{i === 4 && state.mode === "live" ? "now" : formatHm(t)}</span>)}
        </div>
        <span className="grain__zone label">{zoneAbbreviation(view.windowToMs)}</span>
        {playhead !== null ? <span className="grain__playhead" style={{ left: `calc(92px + (100% - 92px) * ${playhead})` }} aria-hidden="true" /> : null}
      </div>
    </section>
  );
}

const COVERAGE_LABEL: Record<CoverageInterval["state"], string> = {
  records_available: "Records",
  confirmed_coverage_no_detections: "Covered, no detections",
  missing_coverage: "Missing coverage",
  unknown: "Coverage unknown",
};

function CoverageBand({ coverage, x }: { coverage: CoverageInterval[]; x: (t: number) => number }) {
  return (
    <div className="grain__lane grain__lane--coverage">
      <span className="grain__name grain__name--static">Coverage</span>
      <div className="coverage" role="list" aria-label="Coverage of the requested window">
        {coverage.map((c, i) => {
          const left = x(Date.parse(c.fromUtc)) / 10;
          const width = (x(Date.parse(c.toUtc)) - x(Date.parse(c.fromUtc))) / 10;
          return (
            <span key={i} role="listitem" className={`coverage__seg coverage__seg--${c.state}`} style={{ left: `${left}%`, width: `${width}%` }} title={`${COVERAGE_LABEL[c.state]} ${formatHm(c.fromUtc)}–${formatHm(c.toUtc)}`}>
              <span className="sr-only">{COVERAGE_LABEL[c.state]} from {formatHm(c.fromUtc)} to {formatHm(c.toUtc)}</span>
            </span>
          );
        })}
      </div>
    </div>
  );
}

export function CoverageLegend() {
  return (
    <div className="coverage-legend" aria-hidden="true">
      <span><i className="coverage__seg--records_available" />Records</span>
      <span><i className="coverage__seg--confirmed_coverage_no_detections" />Covered, none detected</span>
      <span><i className="coverage__seg--missing_coverage" />Missing</span>
      <span><i className="coverage__seg--unknown" />Unknown</span>
    </div>
  );
}
