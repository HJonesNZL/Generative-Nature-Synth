/** Reference data from the host (species, stations, sources) with id lookups. */
import { useMemo } from "react";
import type { CatalogView } from "../host/provisional";
import { useAppState } from "./store";
import { useHost, useHostQuery, valueOf } from "./HostContext";

export interface Catalog {
  data: CatalogView | null;
  speciesName: (id: string | null) => string;
  scientificName: (id: string | null) => string;
  stationName: (id: string | null) => string;
  sourceName: (id: string | null) => string;
  status: "loading" | "ready" | "unsupported" | "error";
}

export function useCatalog(): Catalog {
  const state = useAppState();
  const { caps } = useHost();
  const sessionId = state.mode === "replay" ? state.replay.session?.id : state.mode === "history" ? state.history.frozen?.id : undefined;
  const [q] = useHostQuery("getCatalog", sessionId ? { material: { kind: "session", sessionId } } : caps?.libraryAvailable === false ? null : {}, [sessionId], { refreshMs: 30000 });
  const data = valueOf(q);
  return useMemo(() => {
    const species = new Map(data?.species.map((s) => [s.id, s]) ?? []);
    const stations = new Map(data?.stations.map((s) => [s.id, s]) ?? []);
    const sources = new Map(data?.sources.map((s) => [s.id, s]) ?? []);
    return {
      data,
      speciesName: (id) => (id ? species.get(id)?.commonName ?? id : "Unknown species"),
      scientificName: (id) => (id ? species.get(id)?.scientificName ?? "" : ""),
      stationName: (id) => (id ? stations.get(id)?.name ?? id : "No station"),
      sourceName: (id) => (id ? sources.get(id)?.name ?? id : "Unknown source"),
      status: q.status === "ready" ? "ready" : q.status === "unsupported" ? "unsupported" : q.status === "error" ? "error" : "loading",
    };
  }, [data, q.status]);
}
