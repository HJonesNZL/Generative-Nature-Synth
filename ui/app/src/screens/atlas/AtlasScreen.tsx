/** Screen 1 — Atlas (approved design: H2 · Region Mix). */
import { useMemo, useState } from "react";
import type { EventView } from "../../host/contracts";
import type { MapLocateRequest } from "../../components/map/geo";
import { latestObservations, sourceType, type SourceType } from "../shared/mapMarkers";
import { useLiveCollection } from "../../app/LiveCollectionContext";
import { useCatalog } from "../../app/catalog";
import { useAppState, useDispatch } from "../../app/store";
import { CoverageLegend, GrainPanel } from "../shared/GrainPanel";
import { markerKeyForEvent, useMaterialView } from "../shared/useMaterialView";
import { LeftColumn } from "./LeftColumn";
import { MapStage } from "./MapStage";
import { RightColumn } from "./RightColumn";

export function AtlasScreen() {
  const view = useMaterialView();
  const catalog = useCatalog();
  const {session} = useLiveCollection();
  const { mode } = useAppState();
  const dispatch = useDispatch();
  const [sourceTypes, setSourceTypes] = useState<SourceType[]>(["acoustic", "tracking", "reported"]);
  const filteredView = useMemo(() => mode === "live" ? { ...view, events: view.events.filter(event => sourceTypes.includes(sourceType(event))), allEvents: view.allEvents.filter(event => sourceTypes.includes(sourceType(event))) } : view, [mode, view, sourceTypes]);
  const exactLatest = useMemo(() => latestObservations(filteredView.allEvents, false).slice(0, 8), [filteredView.allEvents]);
  const latest = useMemo(() => mode === "live" ? latestObservations(filteredView.allEvents) : [], [mode, filteredView.allEvents]);
  const [locateRequest, setLocateRequest] = useState<MapLocateRequest | null>(null);
  const [locateSessionId, setLocateSessionId] = useState<string | null>(null);
  const locate = (event: EventView) => {
    setLocateSessionId(session?.id ?? null);
    dispatch({ type: "selectEvent", eventId: event.id });
    if (!sourceTypes.includes(sourceType(event))) setSourceTypes(types => [...types, sourceType(event)]);
    const key = markerKeyForEvent(event);
    if (!key || !event.position) return;
    const { latitude, longitude } = event.position;
    setLocateRequest(previous => ({ key, latitude, longitude, seq: (previous?.seq ?? 0) + 1 }));
  };
  return (
    <main className="screen atlas" id="main" aria-label="Atlas">
      <LeftColumn view={view} catalog={catalog} />
      <MapStage view={filteredView} catalog={catalog} latest={exactLatest} sourceTypes={sourceTypes} onSourceTypes={setSourceTypes} locateRequest={mode === "live" && locateSessionId === (session?.id ?? null) && view.allEvents.some(event => markerKeyForEvent(event) === locateRequest?.key) ? locateRequest : null} />
      <RightColumn view={filteredView} catalog={catalog} latest={latest} onLocate={locate} />
      <GrainPanel view={view} catalog={catalog} aside={mode === "history" ? <CoverageLegend /> : null} />
    </main>
  );
}
