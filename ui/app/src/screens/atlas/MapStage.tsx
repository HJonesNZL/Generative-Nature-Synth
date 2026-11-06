/**
 * Centre of the Atlas: Three.js globe with an SVG New Zealand loupe, or the
 * full SVG map when WebGL is unavailable/lost or the user prefers 2D.
 * Selecting a station picks its most recent (highlighted-species-first) event.
 */
import { lazy, Suspense, useMemo, useState } from "react";
import { useHost, useHostQuery, valueOf } from "../../app/HostContext";
import { useEventStore } from "../../app/eventStore";
import { useAppState, useDispatch } from "../../app/store";
import type { Catalog } from "../../app/catalog";
import { Icon } from "../../components/Icon";
import { Notice } from "../../components/Notice";
import { SegmentedControl } from "../../components/SegmentedControl";
// Three.js is loaded on demand so the 2D fallback and Sound Studio never pay for it.
const GlobeView = lazy(() => import("../../components/map/GlobeView").then((m) => ({ default: m.GlobeView })));
import { Map2D } from "../../components/map/Map2D";
import { BOUNDS_NZ, BOUNDS_SW_PACIFIC, type MapRegion, type Ring } from "../../components/map/geo";
import { buildMarkers, markerKeyForEvent, type MaterialView } from "../shared/useMaterialView";

function webglAvailable(): boolean {
  try {
    const c = document.createElement("canvas");
    return !!(c.getContext("webgl2") ?? c.getContext("webgl"));
  } catch {
    return false;
  }
}

export function MapStage({ view, catalog }: { view: MaterialView; catalog: Catalog }) {
  const state = useAppState();
  const dispatch = useDispatch();
  const { liveStore } = useHost();
  const pulseVersion = useEventStore(liveStore);
  const [glOk] = useState(webglAvailable);
  const [contextLost, setContextLost] = useState(false);
  const [zoom, setZoom] = useState<{ seq: number; action: "in" | "out" | "reset" } | null>(null);
  const [regionsQ] = useHostQuery("listRegions", {}, []);
  const regions: MapRegion[] = useMemo(() => (valueOf(regionsQ) ?? []).map((r) => ({
    id: r.id, name: r.name, ring: (r.geometry.coordinates[0]?.[0] ?? []) as Ring, selected: r.id === state.selectedRegionId,
  })), [regionsQ, state.selectedRegionId]);

  const markers = useMemo(() => buildMarkers(view, catalog, state.highlightSpeciesId), [view, catalog, state.highlightSpeciesId]);
  const selected = view.store.get(state.selectedEventId);
  const selectedKey = markerKeyForEvent(selected);

  const pick = (key: string) => {
    if (key.startsWith("fix:")) { dispatch({ type: "selectEvent", eventId: key.slice(4) }); return; }
    const stationId = key.slice("station:".length);
    const here = view.events.filter((e) => e.stationId === stationId);
    const preferred = here.filter((e) => e.speciesId === state.highlightSpeciesId);
    const chosen = (preferred.length ? preferred : here).at(-1);
    dispatch({ type: "selectEvent", eventId: chosen?.id ?? null });
  };

  const use2D = state.mapView === "map2d" || !glOk || contextLost;
  const live = state.mode === "live";

  return (
    <section className="map-stage" aria-label="Map">
      <div className="map-stage__top">
        <span className="badge badge--context">{state.mode.toUpperCase()} · {state.timeBasis === "observed" ? "OBSERVED TIME" : "ARRIVAL TIME"}</span>
        <span className="small muted">Illustrative demo data</span>
        <div className="map-stage__controls">
          <SegmentedControl<"globe" | "map2d"> label="Map view" size="sm" value={use2D ? "map2d" : "globe"} onChange={(v) => { setContextLost(false); dispatch({ type: "mapView", view: v }); }} options={[{ value: "globe", label: "Globe" }, { value: "map2d", label: "2D map" }]} disabledValues={glOk ? [] : ["globe"]} />
          {!use2D ? (
            <>
              <button type="button" className="icon-btn icon-btn--sm" aria-label="Zoom in" onClick={() => setZoom({ seq: Date.now(), action: "in" })}><Icon name="plus" size={13} /></button>
              <button type="button" className="icon-btn icon-btn--sm" aria-label="Zoom out" onClick={() => setZoom({ seq: Date.now(), action: "out" })}><Icon name="minus" size={13} /></button>
              <button type="button" className="icon-btn icon-btn--sm" aria-label="Reset view" onClick={() => setZoom({ seq: Date.now(), action: "reset" })}><Icon name="recenter" size={13} /></button>
            </>
          ) : null}
          <button type="button" className={`pill pill--quiet${state.reducedMotion ? " is-current" : ""}`} aria-pressed={state.reducedMotion} onClick={() => dispatch({ type: "reducedMotion", value: !state.reducedMotion })}>Reduce motion</button>
        </div>
      </div>

      {contextLost ? <Notice tone="caution" title="3D graphics stopped" action={<button type="button" className="btn btn--quiet btn--sm" onClick={() => setContextLost(false)}>Try globe again</button>}>Showing the 2D map. Collection and sound are unaffected.</Notice> : null}
      {!glOk ? <Notice tone="info" title="2D map">WebGL is not available here, so the globe is replaced by a flat map.</Notice> : null}
      {view.feed.status === "retrying" ? <Notice tone="caution" title="Arrivals paused" live>{view.feed.error ?? "Waiting for the host"} — retrying. Nothing is lost; the feed resumes from its cursor.</Notice> : null}

      <div className="map-stage__canvas">
        {use2D ? (
          <Map2D bounds={BOUNDS_SW_PACIFIC} markers={markers} regions={regions} selectedKey={selectedKey} onPick={pick} title="South-west Pacific map" showLabels />
        ) : (
          <>
            <Suspense fallback={<p className="small muted">Loading globe…</p>}>
            <GlobeView
              markers={markers}
              regions={regions}
              selectedKey={selectedKey}
              orientation={state.globeOrientation}
              reducedMotion={state.reducedMotion}
              getPulses={(age) => (live ? liveStore.activePulses(age) : [])}
              pulseVersion={pulseVersion}
              onPick={pick}
              onOrientationChange={(o) => dispatch({ type: "globeOrientation", orientation: o })}
              onContextLost={() => setContextLost(true)}
              zoomCommand={zoom}
            />
            </Suspense>
            <div className="loupe" aria-label="New Zealand detail">
              <Map2D bounds={BOUNDS_NZ} markers={markers.filter((m) => m.kind === "station" || (m.longitude > 165 && m.latitude < -33))} regions={regions} selectedKey={selectedKey} onPick={pick} title="New Zealand detail map. Stations are buttons." />
            </div>
          </>
        )}
      </div>

      <div className="map-legend small" aria-label="Legend">
        <span><Icon name="station" size={12} /> Fixed acoustic station</span>
        <span className="legend-filled"><span className="dot" aria-hidden="true" /> Highlighted species heard (10 min)</span>
        <span><Icon name="satellite-fix" size={10} /> Satellite fix (individual)</span>
        <span><span className="dash" aria-hidden="true" /> Effect region</span>
        <span className="muted">Coastlines: Natural Earth</span>
      </div>
    </section>
  );
}
