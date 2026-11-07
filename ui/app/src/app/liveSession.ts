import type { EventView, LiveCollectionSession } from "../host/contracts";
import { observedMicroseconds } from "./displayCoverage.ts";
import type { CatalogView } from "../host/provisional";
/** Native owns admission; this additional view gate rejects old or stale replies. */
export function currentSessionEvents(events: readonly EventView[], session: LiveCollectionSession | null, storeSessionId: string | null): EventView[] {
  if (!session || session.id !== storeSessionId) return [];
  const from = observedMicroseconds(session.observedFromUtc);
  if (from === null) return [];
  return events.filter(event => event.provenance === "live_source" && BigInt(event.revisionSequence) > BigInt(session.baselineRevision) && (observedMicroseconds(event.observedAtUtc) ?? -1n) >= from);
}
/** Metadata may name current records; it never supplies records or extra species. */
export function liveSessionCatalog(metadata: CatalogView | null, events: readonly EventView[]): CatalogView {
  const species = new Map(metadata?.species.map(row => [row.id, row]));
  const stations = new Map(metadata?.stations.map(row => [row.id, row]));
  const sources = new Map(metadata?.sources.map(row => [row.id, row]));
  const currentSpecies = new Map<string, CatalogView["species"][number]>();
  const currentStations = new Map<string, CatalogView["stations"][number]>();
  const stationTimes = new Map<string, bigint>();
  const currentSources = new Map<string, CatalogView["sources"][number]>();
  for (const event of events) {
    if (event.speciesId && !currentSpecies.has(event.speciesId)) currentSpecies.set(event.speciesId, species.get(event.speciesId) ?? {
      id: event.speciesId, commonName: event.commonName || event.speciesId, scientificName: "", group: "unclassified"});
    const stationTime = observedMicroseconds(event.observedAtUtc) ?? -1n;
    if (event.stationId && event.position && ["station_detection", "receiver_detection"].includes(event.locationKind) && stationTime >= (stationTimes.get(event.stationId) ?? -1n)) {
      const metadata = stations.get(event.stationId);
      currentStations.set(event.stationId, {id: event.stationId, name: metadata?.name ?? event.stationId, sourceId: event.sourceId,
        latitude: event.position.latitude, longitude: event.position.longitude, positionDescription: "Reported receiver position"});
      stationTimes.set(event.stationId, stationTime);
    }
    if (sources.has(event.sourceId)) currentSources.set(event.sourceId, sources.get(event.sourceId)!);
  }
  return {species: [...currentSpecies.values()], stations: [...currentStations.values()], sources: [...currentSources.values()], limited: false};
}
