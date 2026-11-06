import type { MaterialView } from "./useMaterialView";
import type { Catalog } from "../../app/catalog";
import type { EventView } from "../../host/contracts";
import type { MapMarker } from "../../components/map/geo";

/** Display ordering retains timezone offsets and the source's microsecond precision. */
export function newestObservation(events: readonly EventView[]): EventView | null {
  let newest: EventView | null = null;
  let newestUs: bigint | null = null;
  for (const event of events) {
    const fraction = event.observedAtUtc.match(/\.(\d{1,6})(?:Z|[+-]\d{2}:\d{2})$/)?.[1] ?? "";
    const us = BigInt(Date.parse(event.observedAtUtc)) * 1000n + BigInt(fraction.padEnd(6, "0").slice(3));
    if (newestUs === null || us > newestUs) { newest = event; newestUs = us; }
  }
  return newest;
}

export function isReceiverDetection(event: EventView): boolean {
  return event.locationKind === "station_detection" || event.locationKind === "receiver_detection";
}

/** Aggregate events into bounded map symbols: one per station, one per measured fix. */
export function buildMarkers(view: MaterialView, catalog: Catalog, highlightSpeciesId: string | null, allLoaded = false): MapMarker[] {
  const recent = allLoaded ? view.allEvents : view.events;
  const stations = new Map((catalog.data?.stations ?? []).map((station) => [station.id, station]));
  // The sampled catalogue may omit a receiver present in an older loaded record.
  for (const event of recent) {
    if (event.stationId && event.position && isReceiverDetection(event) && !stations.has(event.stationId)) stations.set(event.stationId, {
      id: event.stationId, name: event.stationId, sourceId: event.sourceId,
      latitude: event.position.latitude, longitude: event.position.longitude,
      positionDescription: "Reported receiver position",
    });
  }
  const markers: MapMarker[] = [...stations.values()].map((s) => {
    const here = recent.filter((e) => e.stationId === s.id && isReceiverDetection(e));
    const highlighted = highlightSpeciesId ? here.filter((e) => e.speciesId === highlightSpeciesId).length : 0;
    const emphasis = highlighted ? "highlight" : here.length ? "normal" : "dim";
    const summary = highlightSpeciesId
      ? `${highlighted} ${catalog.speciesName(highlightSpeciesId)} of ${here.length} loaded detections`
      : `${here.length} loaded detections`;
    return { key: `station:${s.id}`, kind: "station", latitude: s.latitude, longitude: s.longitude, emphasis, label: `${s.name} receiver, ${summary}`, uncertaintyMetres: null };
  });
  const fixes = (allLoaded ? view.allEvents : view.events).filter((e) => (e.eventKind === "satellite_fix" || e.eventKind === "gps_fix") && e.position);
  for (const f of fixes) {
    markers.push({
      key: `fix:${f.id}`, kind: "fix", latitude: f.position!.latitude, longitude: f.position!.longitude,
      emphasis: highlightSpeciesId === f.speciesId ? "highlight" : "normal",
      label: `${f.individualId ?? "Individual identity unavailable"} ${f.eventKind === "gps_fix" ? "GPS" : "satellite"} fix, ${catalog.speciesName(f.speciesId)}, observed ${f.observedAtUtc}`,
      uncertaintyMetres: f.position!.uncertaintyMetres,
    });
  }
  return markers;
}

export function boundMarkers(markers: MapMarker[], capacity: number, selectedKey: string | null): MapMarker[] {
  const rank = (marker: MapMarker) => marker.key === selectedKey ? 0 : marker.emphasis === "highlight" ? 1 : marker.emphasis === "normal" ? 2 : 3;
  return [...markers].sort((a, b) => rank(a) - rank(b)).slice(0, capacity);
}
