/**
 * Atlas right column — the approved H2 "Region Mix" panel above the event
 * inspector / incoming list.
 */
import { useEffect, useMemo, useState } from "react";
import type { EventView } from "../../host/contracts";
import type { MaterialRef, MusicalOutcome, RegionSpeciesActivity } from "../../host/provisional";
import { useHost, useHostQuery, valueOf } from "../../app/HostContext";
import { useAppState, useDispatch } from "../../app/store";
import { useCatalog, type Catalog } from "../../app/catalog";
import { formatDateTime, formatTime, zoneAbbreviation } from "../../app/time";
import { EmptyState, Notice } from "../../components/Notice";
import type { MaterialView } from "../shared/useMaterialView";

export function RightColumn({ view, catalog }: { view: MaterialView; catalog: Catalog }) {
  return (
    <aside className="atlas__right" aria-label="Region mix and selected event">
      <RegionMixPanel />
      <EventPanel view={view} catalog={catalog} />
    </aside>
  );
}

function useMaterialRef(): MaterialRef | undefined {
  const { mode, replay, history } = useAppState();
  if (mode === "replay" && replay.session) return { kind: "session", sessionId: replay.session.id };
  if (mode === "history" && history.result?.selectionId) return { kind: "selection", selectionId: history.result.selectionId };
  return undefined;
}

export function RegionMixPanel() {
  const { client, liveStore, materialStore } = useHost();
  const state = useAppState();
  const dispatch = useDispatch();
  const catalog = useCatalog();
  const speciesLabel = catalog.speciesName;
  const material = useMaterialRef();
  const live = state.mode === "live";
  const [regionsQ] = useHostQuery("listRegions", {}, []);
  const [effectsQ] = useHostQuery("listEffects", {}, []);
  const regions = valueOf(regionsQ) ?? [];
  const effects = valueOf(effectsQ) ?? [];
  const args = live || material ? { regionId: state.selectedRegionId, windowMinutes: 60, basis: state.timeBasis, material } : null;
  const [q, refresh] = useHostQuery("getRegionActivity", args, [state.selectedRegionId, state.timeBasis, JSON.stringify(material ?? null)], { refreshMs: live ? 5000 : undefined });
  const activity = valueOf(q);
  const [pending, setPending] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const region = regions.find((r) => r.id === state.selectedRegionId);
  const assignment = region?.effectAssignments[0];
  const effect = effects.find((e) => e.id === assignment?.effectId);

  const setMix = async (row: RegionSpeciesActivity, change: { muted?: boolean; soloed?: boolean }) => {
    const key = `${row.speciesId}:${Object.keys(change)[0]}`;
    setPending(key);
    setError(null);
    const r = await client.call("setSpeciesMix", { speciesId: row.speciesId, ...change });
    setPending(null);
    if (!r.ok) { setError(r.error.message); return; }
    refresh();
    (live ? liveStore : materialStore).invalidateOutcomes(client);
  };

  const totals = activity?.totals;
  const mutedNames = activity?.species.filter((s) => s.muted).map((s) => speciesLabel(s.speciesId)) ?? [];

  return (
    <section className="region-mix" aria-labelledby="region-mix-title">
      <div className="row-between">
        <span className="label">REGION MIX · {live ? "60 MIN" : "THIS MATERIAL"}</span>
        <label className="link-select">
          <span className="sr-only">Region</span>
          <select value={state.selectedRegionId} onChange={(e) => dispatch({ type: "selectRegion", regionId: e.target.value })} aria-label="Change region">
            {regions.map((r) => <option key={r.id} value={r.id}>{r.name}</option>)}
          </select>
          <span aria-hidden="true">Change region</span>
        </label>
      </div>
      <h2 id="region-mix-title" className="h3">{region?.name ?? "Region"}</h2>
      {q.status === "unsupported" ? <Notice tone="caution" title="Region mix unavailable">This host does not report per-region activity.</Notice> : null}
      {q.status === "error" ? <Notice tone="problem" title="Region mix not updated">{q.error.message}</Notice> : null}
      {!args ? <EmptyState title="Nothing selected">Open a saved piece or check a history window to see its region mix.</EmptyState> : null}
      {activity ? (
        <div className="mix-table" role="table" aria-label={`Species contributions in ${region?.name ?? "region"}`}>
          {activity.species.map((row) => {
            const share = Math.round(row.share * 100);
            const max = Math.max(...activity.species.map((s) => s.share), 0.0001);
            const highlighted = row.speciesId === state.highlightSpeciesId;
            return (
              <div role="row" key={row.speciesId} className={`mix-row${highlighted ? " is-highlighted" : ""}${row.muted ? " is-muted" : ""}`}>
                <div role="cell" className="mix-row__name">
                  <span className="mix-row__species">{speciesLabel(row.speciesId)}</span>
                  <span className={`mix-row__voice mono${row.voiceId ? "" : " is-missing"}`}>{row.voiceLabel ?? "no voice"}</span>
                </div>
                <div role="cell" className="mix-row__bar" aria-label={`${share} percent, ${row.detections} detections`}>
                  <span className="grainbar" style={{ width: `${(row.share / max) * 100}%` }} />
                </div>
                <div role="cell" className="mix-row__pct mono">{share}%</div>
                <div role="cell" className="mix-row__actions">
                  {row.voiceId ? (
                    <>
                      <button type="button" className={`round-toggle${row.soloed ? " is-on" : ""}`} aria-pressed={row.soloed} aria-busy={pending === `${row.speciesId}:soloed` || undefined} aria-label={`Solo ${speciesLabel(row.speciesId)}`} disabled={!client.supports("setSpeciesMix")} onClick={() => void setMix(row, { soloed: !row.soloed })}>S</button>
                      <button type="button" className={`round-toggle${row.muted ? " is-on" : ""}`} aria-pressed={row.muted} aria-busy={pending === `${row.speciesId}:muted` || undefined} aria-label={`Mute ${speciesLabel(row.speciesId)}`} disabled={!client.supports("setSpeciesMix")} onClick={() => void setMix(row, { muted: !row.muted })}>M</button>
                    </>
                  ) : (
                    <button type="button" className="pill pill--dashed" onClick={() => { dispatch({ type: "screen", screen: "studio" }); dispatch({ type: "selectVoice", voiceId: null }); dispatch({ type: "highlightSpecies", speciesId: row.speciesId }); }}>Assign</button>
                  )}
                </div>
              </div>
            );
          })}
        </div>
      ) : q.status === "loading" ? <p className="small muted">Loading region mix…</p> : null}
      {error ? <Notice tone="problem" title="Mix change not applied">{error}</Notice> : null}
      {activity ? (
        <p className="small muted">
          {mutedNames.length ? `${mutedNames.join(", ")} muted — still recorded, not sounded. ` : ""}
          {totals ? `${totals.records} records · ${totals.sounded} sounded · ${totals.grouped} grouped · ${totals.suppressed} held back. ` : ""}
          {effect && assignment ? `Region send: ${effect.name} ${assignment.enabled ? assignment.send.toFixed(2) : "(bypassed)"}.` : ""}
        </p>
      ) : null}
    </section>
  );
}

function outcomeText(o: MusicalOutcome | undefined): { short: string; long: string } {
  if (!o) return { short: "…", long: "Musical outcome not yet reported by the host." };
  switch (o.kind) {
    case "sounded": return { short: "sounded", long: `Sounded on ${o.voiceId.replace("voice-", "voice ")} — ${o.noteName}, velocity ${o.velocity}, pan ${o.pan >= 0 ? "R" : "L"}${Math.abs(Math.round(o.pan * 50))}${o.regionSends.length ? ` · region send ${o.regionSends.map((s) => s.send.toFixed(2)).join(", ")}` : " · direct only"}` };
    case "grouped": return { short: `grouped ×${o.groupSize}`, long: `Grouped with ${o.groupSize - 1} other call(s) into one phrase on ${o.voiceId}.` };
    case "filtered": return { short: o.reason === "species_muted" ? "muted" : o.reason === "not_soloed" ? "not soloed" : o.reason === "no_voice_assigned" ? "no voice" : "below threshold", long: { species_muted: "Not sounded: species is muted. The record is kept.", not_soloed: "Not sounded: another species is soloed.", no_voice_assigned: "Not sounded: no voice is assigned to this species.", confidence_below_threshold: "Not sounded: provider confidence is below the threshold." }[o.reason] };
    case "suppressed": return { short: o.reason === "density_cap" ? "held back" : o.reason, long: { density_cap: "Held back by the density limit.", duplicate: "Duplicate of an earlier record.", baseline: "Baseline record: present before listening started, never sounded as new.", correction: "Correction of an earlier record; it does not re-trigger a note." }[o.reason] };
    case "deferred": return { short: "deferred", long: `Scheduled into a phrase at ${formatTime(o.scheduledForUtc)}.` };
    default: return { short: "—", long: "Not evaluated by the musical engine." };
  }
}

function EventPanel({ view, catalog }: { view: MaterialView; catalog: Catalog }) {
  const state = useAppState();
  const dispatch = useDispatch();
  const { client } = useHost();
  const [tab, setTab] = useState<"selected" | "incoming">("selected");
  const selected = view.store.get(state.selectedEventId);
  useEffect(() => { if (selected) setTab("selected"); }, [selected?.id]);
  useEffect(() => { if (selected) view.store.requestOutcomes(client, [selected.id]); }, [selected, client, view.store]);
  const incoming = useMemo(() => [...view.events].sort((a, b) => view.timeOf(b) - view.timeOf(a)).slice(0, 40), [view]);
  useEffect(() => { view.store.requestOutcomes(client, incoming.map((e) => e.id)); }, [incoming, client, view.store]);

  return (
    <section className="event-panel">
      <div className="tabs" role="tablist" aria-label="Event views">
        <button type="button" role="tab" id="tab-selected" aria-selected={tab === "selected"} aria-controls="panel-selected" tabIndex={tab === "selected" ? 0 : -1} className="tab" onClick={() => setTab("selected")} onKeyDown={(e) => { if (e.key === "ArrowRight") { setTab("incoming"); (document.getElementById("tab-incoming") as HTMLElement | null)?.focus(); } }}>Selected</button>
        <button type="button" role="tab" id="tab-incoming" aria-selected={tab === "incoming"} aria-controls="panel-incoming" tabIndex={tab === "incoming" ? 0 : -1} className="tab" onClick={() => setTab("incoming")} onKeyDown={(e) => { if (e.key === "ArrowLeft") { setTab("selected"); (document.getElementById("tab-selected") as HTMLElement | null)?.focus(); } }}>
          {state.mode === "live" ? "Incoming" : "Events"} <span className="mono">{view.events.length}</span>
        </button>
      </div>
      {tab === "selected" ? (
        <div role="tabpanel" id="panel-selected" aria-labelledby="tab-selected" className="event-panel__body">
          {selected ? <Inspector event={selected} outcome={view.store.outcome(selected.id)} catalog={catalog} /> : <EmptyState title="No event selected">Pick a station on the map, a line in Grain, or an item under {state.mode === "live" ? "Incoming" : "Events"}.</EmptyState>}
        </div>
      ) : (
        <div role="tabpanel" id="panel-incoming" aria-labelledby="tab-incoming" className="event-panel__body">
          {incoming.length === 0 ? <EmptyState title="Nothing here yet">{state.mode === "live" ? "New arrivals appear as the host receives them." : "No events in this material."}</EmptyState> : (
            <ul className="incoming" role="list">
              {incoming.map((e) => (
                <li key={e.id}>
                  <button type="button" className={`incoming__row${e.id === state.selectedEventId ? " is-on" : ""}`} aria-pressed={e.id === state.selectedEventId} onClick={() => dispatch({ type: "selectEvent", eventId: e.id })}>
                    <span className="mono">{formatTime(view.timeOf(e))}</span>
                    <span>{catalog.speciesName(e.speciesId)} · {e.stationId ? catalog.stationName(e.stationId) : e.individualId ?? "fix"}</span>
                    <span className="muted">{outcomeText(view.store.outcome(e.id)).short}</span>
                  </button>
                </li>
              ))}
            </ul>
          )}
        </div>
      )}
    </section>
  );
}

const KIND_TEXT: Record<EventView["eventKind"], string> = {
  acoustic_species_detection: "Acoustic species detection",
  acoustic_receiver_detection: "Acoustic receiver detection (tag near receiver)",
  satellite_fix: "Satellite position fix",
  gps_fix: "GPS position fix",
  human_observation: "Human observation",
};

function Inspector({ event, outcome, catalog }: { event: EventView; outcome: MusicalOutcome | undefined; catalog: Catalog }) {
  const dispatch = useDispatch();
  const { client } = useHost();
  const [libQ] = useHostQuery("getVoiceLibrary", {}, []);
  const lib = valueOf(libQ);
  const voiceId = outcome && "voiceId" in outcome ? outcome.voiceId : lib?.entries.flatMap((g) => g.species.map((s) => ({ s, g }))).find(({ s }) => s.speciesId === event.speciesId)?.s.voiceId ?? null;
  const o = outcomeText(outcome);
  const identity = event.individualId ? `Individual ${event.individualId}${event.deploymentId ? ` (deployment ${event.deploymentId})` : ""}` : "Species only — individual unknown";
  const location = !event.position ? "No usable position" : event.stationId ? `${catalog.stationName(event.stationId)} station position (not the bird’s position)` : `Estimated position ±${event.position.uncertaintyMetres != null ? `${(event.position.uncertaintyMetres / 1000).toFixed(1)} km` : "unknown"}`;
  return (
    <div className="inspector">
      <div className="row-between">
        <span className="h2 accent">{catalog.speciesName(event.speciesId).toUpperCase()}</span>
        <span className="label">{event.stationId ? catalog.stationName(event.stationId).toUpperCase() : event.individualId}</span>
      </div>
      <span className="small italic muted">{catalog.scientificName(event.speciesId)}</span>
      {event.provenance !== "live_source" ? <span className="badge badge--demo">{event.provenance === "demo" ? "Demo record" : "Imported record"}</span> : null}
      <dl className="kv kv--tight">
        <dt>Kind</dt><dd>{KIND_TEXT[event.eventKind]}</dd>
        <dt>Observed</dt><dd className="mono">{formatDateTime(event.observedAtUtc)}</dd>
        <dt>Arrived</dt><dd className="mono">{formatTime(event.firstSeenAtUtc)} {zoneAbbreviation(event.firstSeenAtUtc)}</dd>
        <dt>Identity</dt><dd>{identity}</dd>
        <dt>Location</dt><dd>{location}</dd>
        <dt>Confidence</dt><dd>{event.confidence ? `${event.confidence.value.toFixed(2)} — ${event.confidence.meaning}` : "Not provided"}</dd>
        <dt>Source</dt><dd>{catalog.sourceName(event.sourceId)}{event.attribution ? ` · ${event.attribution}` : ""}</dd>
      </dl>
      <p className="small"><span aria-hidden="true">→ </span>{o.long}</p>
      <button
        type="button"
        className="btn btn--primary"
        disabled={!voiceId && !event.speciesId}
        onClick={() => { dispatch({ type: "selectVoice", voiceId }); if (event.speciesId) dispatch({ type: "highlightSpecies", speciesId: event.speciesId }); dispatch({ type: "screen", screen: "studio" }); }}
      >
        {voiceId ? `Open ${catalog.speciesName(event.speciesId)} voice in Sound Studio →` : "Assign a voice in Sound Studio →"}
      </button>
      {!client.supports("getMusicalOutcomes") ? <span className="small muted">This host does not report musical outcomes.</span> : null}
    </div>
  );
}
