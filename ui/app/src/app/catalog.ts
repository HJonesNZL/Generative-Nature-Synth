/** Reference data from the host (species, stations, sources) with id lookups. */
import { useMemo } from "react";
import type { CatalogView } from "../host/provisional";
import { useHostQuery, valueOf } from "./HostContext";

export interface Catalog {
  data: CatalogView | null;
  speciesName: (id: string | null) => string;
  scientificName: (id: string | null) => string;
  stationName: (id: string | null) => string;
  sourceName: (id: string | null) => string;
  status: "loading" | "ready" | "unsupported" | "error";
}

export function useCatalog(): Catalog {
  const [q] = useHostQuery("getCatalog", {}, []);
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
