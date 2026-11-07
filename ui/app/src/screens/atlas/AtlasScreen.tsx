/** Screen 1 — Atlas (approved design: H2 · Region Mix). */
import { useMemo, useState } from "react";
import type { EventView } from "../../host/contracts";
import type { MapLocateRequest } from "../../components/map/geo";
import { latestObservations } from "../shared/mapMarkers";
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
  const { mode } = useAppState();
  const dispatch = useDispatch();
  const latest = useMemo(() => mode === "live" ? latestObservations(view.allEvents) : [], [mode, view.allEvents]);
  const [locateRequest, setLocateRequest] = useState<MapLocateRequest | null>(null);
  const locate = (event: EventView) => {
    dispatch({ type: "selectEvent", eventId: event.id });
    const key = markerKeyForEvent(event);
    if (!key || !event.position) return;
    const { latitude, longitude } = event.position;
    setLocateRequest(previous => ({ key, latitude, longitude, seq: (previous?.seq ?? 0) + 1 }));
  };
  return (
    <main className="screen atlas" id="main" aria-label="Atlas">
      <LeftColumn view={view} catalog={catalog} />
      <MapStage view={view} catalog={catalog} latest={latest} locateRequest={mode === "live" ? locateRequest : null} />
      <RightColumn view={view} catalog={catalog} latest={latest} onLocate={locate} />
      <GrainPanel view={view} catalog={catalog} aside={mode === "history" ? <CoverageLegend /> : null} />
    </main>
  );
}
