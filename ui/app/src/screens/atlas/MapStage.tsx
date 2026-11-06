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
import { BOUNDS_NZ, BOUNDS_WORLD, type MapRegion, type Ring } from "../../components/map/geo";
import { markerKeyForEvent, type MaterialView } from "../shared/useMaterialView";
import { buildMarkers, boundMarkers, newestObservation, isReceiverDetection } from "../shared/mapMarkers";

function webglAvailable(): boolean {
  try {
    const c = document.createElement("canvas");
    const gl = c.getContext("webgl2") ?? c.getContext("webgl");
    gl?.getExtension("WEBGL_lose_context")?.loseContext();
    return !!gl;
  } catch {
    return false;
  }
}

export function MapStage({ view, catalog }: { view: MaterialView; catalog: Catalog }) {
  const state = useAppState();
  const dispatch = useDispatch();
  const { liveStore, caps } = useHost();
  const pulseVersion = useEventStore(liveStore);
  const [glOk] = useState(webglAvailable);
  const [contextLost, setContextLost] = useState(false);
  const [zoom, setZoom] = useState<{ seq: number; action: "in" | "out" | "reset" } | null>(null);
  const [regionsQ] = useHostQuery("listRegions", {}, []);
  const regions: MapRegion[] = useMemo(() => (valueOf(regionsQ) ?? []).map((r) => ({
    id: r.id, name: r.name, ring: (r.geometry.coordinates[0]?.[0] ?? []) as Ring, selected: r.id === state.selectedRegionId,
  })), [regionsQ, state.selectedRegionId]);

  const [scope, setScope] = useState<"loaded" | "window">("loaded");
  const mapEvents = scope === "loaded" ? view.allEvents : view.events;
  const allMarkers = useMemo(() => buildMarkers(view, catalog, state.highlightSpeciesId, scope === "loaded"), [view, catalog, state.highlightSpeciesId, scope]);
  const selected = view.store.get(state.selectedEventId);
  const selectedKey = markerKeyForEvent(selected);
  const markers = useMemo(() => boundMarkers(allMarkers, Math.min(400, caps?.maxVisibleMarkers ?? 400), selectedKey), [allMarkers, caps, selectedKey]);
  const newest = newestObservation(mapEvents)?.observedAtUtc;

  const pick = (key: string) => {
    if (key.startsWith("fix:")) { dispatch({ type: "selectEvent", eventId: key.slice(4) }); return; }
    const stationId = key.slice("station:".length);
    const here = mapEvents.filter((e) => e.stationId === stationId && isReceiverDetection(e));
    const preferred = here.filter((e) => e.speciesId === state.highlightSpeciesId);
    const chosen = newestObservation(preferred.length ? preferred : here);
    dispatch({ type: "selectEvent", eventId: chosen?.id ?? null });
  };

  const use2D = state.mapView === "map2d" || !glOk || contextLost;
  const live = state.mode === "live";

  return (
    <section className="map-stage" aria-label="Map">
      <div className="map-stage__top">
        <span className="badge badge--context">{state.mode.toUpperCase()} · {state.timeBasis === "observed" ? "OBSERVED TIME" : "ARRIVAL TIME"}</span>
        <span className="small muted">{caps?.implementation === "mock" ? "Illustrative demo data" : "Reported receivers and measured fixes"}</span>
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

      <div className="map-overview small">
        <SegmentedControl<"loaded" | "window"> label="Map records" size="sm" value={scope} onChange={setScope} options={[{ value: "loaded", label: "All loaded records" }, { value: "window", label: view.label }]} />
        <p>{mapEvents.length} records · {markers.length} of {allMarkers.length} locations shown.</p>
        <p>Newest loaded observation: {newest ?? "unknown"}</p>
        <details><summary>Coverage and display limits</summary><p className="muted">{caps?.implementation === "mock" ? "Illustrative demo. " : state.mode === "live" ? "Initially newest 200 revisions, up to 2,000 retained records. " : "Up to 2,000 loaded material records. "}Catalogue sampled from 2,000 records. Coverage is limited to available sources. Receivers locate detections, not individual animals; fixes are observations at their stated times.</p></details>
        {view.feed.displayOmitted ? <p>{view.feed.displayOmitted} records omitted from this display; stored material is unchanged.</p> : null}
      </div>
      {contextLost ? <Notice tone="caution" title="3D graphics stopped" action={<button type="button" className="btn btn--quiet btn--sm" onClick={() => setContextLost(false)}>Try globe again</button>}>Showing the 2D map. Event data remains available.</Notice> : null}
      {!glOk ? <Notice tone="info" title="2D map">WebGL is not available here, so the globe is replaced by a flat map.</Notice> : null}
      {view.feed.status === "retrying" ? <Notice tone="caution" title="Arrivals paused" live>{view.feed.error ?? "Waiting for the host"} — retrying. Nothing is lost; the feed resumes from its cursor.</Notice> : null}

      <div className="map-stage__canvas">
        {use2D ? (
          <Map2D bounds={BOUNDS_WORLD} markers={markers} regions={regions} selectedKey={selectedKey} onPick={pick} title="World map" showLabels />
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
              <Map2D bounds={BOUNDS_NZ} markers={markers.filter((m) => m.longitude >= BOUNDS_NZ.west && m.longitude <= BOUNDS_NZ.east && m.latitude >= BOUNDS_NZ.south && m.latitude <= BOUNDS_NZ.north)} regions={regions} selectedKey={selectedKey} onPick={pick} title="New Zealand detail map. Stations are buttons." />
            </div>
          </>
        )}
      </div>

      <details className="map-locations small">
        <summary>Browse {markers.length} displayed locations</summary>
        <ul role="list">{markers.map((marker) => <li key={marker.key}><button type="button" className="btn btn--quiet btn--sm" aria-pressed={marker.key === selectedKey} onClick={() => pick(marker.key)}>{marker.label} · {marker.latitude.toFixed(3)}, {marker.longitude.toFixed(3)}</button></li>)}</ul>
      </details>
      <div className="map-legend small" aria-label="Legend">
        <span><Icon name="station" size={12} /> Reported receiver</span>
        <span className="legend-filled"><span className="dot" aria-hidden="true" /> Highlighted species in selected map records</span>
        <span><Icon name="satellite-fix" size={10} /> Measured GPS / satellite fix</span>
        <span><span className="dash" aria-hidden="true" /> Effect region</span>
        <span className="muted">Coastlines: Natural Earth</span>
      </div>
    </section>
  );
}
