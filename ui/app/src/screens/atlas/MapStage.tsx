/**
 * Centre of the Atlas: Three.js globe with an SVG New Zealand loupe, or the
 * full SVG map when WebGL is unavailable/lost or the user prefers 2D.
 * Selecting a station picks its most recent (highlighted-species-first) event.
 */
import { lazy, Suspense, useEffect, useMemo, useState } from "react";
import { useHost, useHostQuery, valueOf } from "../../app/HostContext";
import type { EventView } from "../../host/contracts";
import { useEventStore } from "../../app/eventStore";
import { useAppState, useDispatch } from "../../app/store";
import type { Catalog } from "../../app/catalog";
import { Icon } from "../../components/Icon";
import { Notice } from "../../components/Notice";
import { Popover } from "../../components/Popover";
import { SegmentedControl } from "../../components/SegmentedControl";
// Three.js is loaded on demand so the 2D fallback and Sound Studio never pay for it.
const GlobeView = lazy(() => import("../../components/map/GlobeView").then((m) => ({ default: m.GlobeView })));
import { Map2D } from "../../components/map/Map2D";
import { NZLoupe } from "../../components/map/NZLoupe";
import { BOUNDS_WORLD, type MapRegion, type MapLocateRequest, type Ring } from "../../components/map/geo";
import { markerKeyForEvent, type MaterialView } from "../shared/useMaterialView";
import { buildMarkers, boundMarkers, newestObservation, isReceiverDetection } from "../shared/mapMarkers";
import "../../styles/map-stage.css";

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

export function MapStage({ view, catalog, latest, locateRequest }: { view: MaterialView; catalog: Catalog; latest: readonly EventView[]; locateRequest: MapLocateRequest | null }) {
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
  useEffect(() => { if (locateRequest) setScope("loaded"); }, [locateRequest]);
  const latestMarkerKey = latest[0]?.position ? markerKeyForEvent(latest[0]) : null;
  const recentMarkerKeys = useMemo(() => {
    const keys = new Set<string>();
    for (const event of latest.slice(0, 8)) {
      const key = markerKeyForEvent(event);
      if (key && event.position) keys.add(key);
    }
    return keys;
  }, [latest]);
  const mapEvents = scope === "loaded" ? view.allEvents : view.events;
  const allMarkers = useMemo(() => buildMarkers(view, catalog, state.highlightSpeciesId, scope === "loaded"), [view, catalog, state.highlightSpeciesId, scope]);
  const selected = view.store.get(state.selectedEventId);
  const selectedKey = markerKeyForEvent(selected);
  const markers = useMemo(() => boundMarkers(allMarkers, Math.min(400, caps?.maxVisibleMarkers ?? 400), selectedKey, recentMarkerKeys, latestMarkerKey), [allMarkers, caps, selectedKey, recentMarkerKeys, latestMarkerKey]);
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
    <section className="map-stage map-stage--expanded" aria-label="Map">
      <div className="map-stage__top">
        <span className="badge badge--context">{state.mode.toUpperCase()} · {state.timeBasis === "observed" ? "OBSERVED TIME" : "ARRIVAL TIME"}</span>
        <Popover className="map-details" align="start" label="Map details and coverage" trigger={<span className="pill pill--quiet">{caps?.implementation === "mock" ? "Demo map" : "Map details"}<Icon name="chevron-down" size={11} /></span>}>
          {() => <div className="map-overview small">
            <strong>{caps?.implementation === "mock" ? "Illustrative demo data" : "Reported receivers and measured fixes"}</strong>
            <SegmentedControl<"loaded" | "window"> label="Map records" size="sm" value={scope} onChange={setScope} options={[{ value: "loaded", label: "All loaded records" }, { value: "window", label: view.label }]} />
            <p>{mapEvents.length} records · {markers.length} of {allMarkers.length} locations shown.</p>
            <p>Newest loaded observation: {newest ?? "unknown"}</p>
            <details><summary>Coverage and display limits</summary><p className="muted">{caps?.implementation === "mock" ? "Illustrative demo. " : state.mode === "live" ? "Initially up to 200 records, reserving room for reported animal positions; up to 2,000 retained records. " : "Up to 2,000 loaded material records. "}Catalogue sampled from 2,000 records. Coverage is limited to available sources. Receivers locate detections, not individual animals; fixes are observations at their stated times.</p></details>
            {view.feed.displayOmitted ? <p>{view.feed.displayOmitted} records omitted from this display; stored material is unchanged.</p> : null}
            <p>Filled receiver markers show the highlighted species in the selected map records. Coastlines: Natural Earth.</p>
            <button type="button" className={`pill pill--quiet${state.reducedMotion ? " is-current" : ""}`} aria-pressed={state.reducedMotion} onClick={() => dispatch({ type: "reducedMotion", value: !state.reducedMotion })}>Reduce motion</button>
          </div>}
        </Popover>
        <div className="map-stage__controls">
          <SegmentedControl<"globe" | "map2d"> label="Map view" size="sm" value={use2D ? "map2d" : "globe"} onChange={(v) => { setContextLost(false); dispatch({ type: "mapView", view: v }); }} options={[{ value: "globe", label: "Globe" }, { value: "map2d", label: "2D map" }]} disabledValues={glOk ? [] : ["globe"]} />
          {!use2D ? (
            <>
              <button type="button" className="icon-btn icon-btn--sm" aria-label="Zoom in" onClick={() => setZoom({ seq: Date.now(), action: "in" })}><Icon name="plus" size={13} /></button>
              <button type="button" className="icon-btn icon-btn--sm" aria-label="Zoom out" onClick={() => setZoom({ seq: Date.now(), action: "out" })}><Icon name="minus" size={13} /></button>
              <button type="button" className="icon-btn icon-btn--sm" aria-label="Reset view" onClick={() => setZoom({ seq: Date.now(), action: "reset" })}><Icon name="recenter" size={13} /></button>
            </>
          ) : null}
        </div>
      </div>

      {contextLost ? <Notice tone="caution" title="3D graphics stopped" action={<button type="button" className="btn btn--quiet btn--sm" onClick={() => setContextLost(false)}>Try globe again</button>}>Showing the 2D map. Event data remains available.</Notice> : null}
      {!glOk ? <Notice tone="info" title="2D map">WebGL is not available here, so the globe is replaced by a flat map.</Notice> : null}
      {view.feed.status === "error" ? <Notice tone="problem" title="Material could not be loaded" action={<button type="button" className="btn btn--quiet btn--sm" onClick={() => dispatch({ type: "retryMaterial" })}>Retry material</button>}>{view.feed.error} Loaded records may be incomplete; retry reloads this material.</Notice> : null}
      {view.feed.status === "retrying" ? <Notice tone="caution" title="Arrivals paused" live>{view.feed.error ?? "Waiting for the host"} — retrying. Nothing is lost; the feed resumes from its cursor.</Notice> : null}

      <div className="map-stage__canvas">
        {use2D ? (
          <Map2D bounds={BOUNDS_WORLD} markers={markers} regions={regions} selectedKey={selectedKey} latestMarkerKey={latestMarkerKey} recentMarkerKeys={recentMarkerKeys} onPick={pick} title="World map" showLabels />
        ) : (
          <>
            <Suspense fallback={<p className="small muted">Loading globe…</p>}>
            <GlobeView
              markers={markers}
              regions={regions}
              selectedKey={selectedKey} latestMarkerKey={latestMarkerKey} recentMarkerKeys={recentMarkerKeys} locateRequest={locateRequest}
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
            <NZLoupe markers={markers} regions={regions} selectedKey={selectedKey} latestMarkerKey={latestMarkerKey} recentMarkerKeys={recentMarkerKeys} locateRequest={locateRequest} onPick={pick}
              pan={state.nzLoupePan} onPanChange={(pan) => dispatch({ type: "nzLoupePan", pan })} />
          </>
        )}
      </div>

      <div className="map-stage__footer">
        <div className="map-legend small" aria-label="Legend">
          {live && recentMarkerKeys.size > 0 ? <span title="Locations of the eight most recent observations. The larger ring marks the newest."><span className="latest-ring-key" aria-hidden="true" /> Latest detections</span> : null}
          <span><Icon name="station" size={12} /> Receiver</span>
          <span className="legend-filled"><span className="dot" aria-hidden="true" /> Highlighted species</span>
          <span><Icon name="satellite-fix" size={10} /> Measured fix</span>
          <span><span className="dash" aria-hidden="true" /> Effect region</span>
        </div>
        <details className="map-locations small">
          <summary>{markers.length} locations</summary>
          <div className="map-locations__panel">
            <strong>Browse displayed locations</strong>
            <ul role="list">{markers.map((marker) => <li key={marker.key}><button type="button" className="btn btn--quiet btn--sm" aria-pressed={marker.key === selectedKey} onClick={() => pick(marker.key)}>{marker.label} · {marker.latitude.toFixed(3)}, {marker.longitude.toFixed(3)}</button></li>)}</ul>
            {!markers.length ? <p>No locations in these map records.</p> : null}
          </div>
        </details>
      </div>
    </section>
  );
}
