import type { EventView } from "../../host/contracts";
import type { LiveSpeciesCount } from "../../app/liveSpeciesRanking";
export interface GrainLane { speciesId: string; name: string; count: number; events: EventView[] }
export function grainLanes(events: EventView[], species: {id: string; commonName: string}[], highlighted: string | null, live: boolean, totals: LiveSpeciesCount[] | null): GrainLane[] {
  const bySpecies = new Map<string, EventView[]>();
  const names = new Map(species.map(row => [row.id, row.commonName]));
  for (const event of events) {
    if (!event.speciesId) continue;
    const list = bySpecies.get(event.speciesId) ?? [];
    list.push(event); bySpecies.set(event.speciesId, list);
    if (!names.get(event.speciesId) && event.commonName) names.set(event.speciesId, event.commonName);
  }
  if (live && totals !== null) return totals.map(row => ({ speciesId: row.speciesId, name: row.commonName || names.get(row.speciesId) || row.speciesId, count: row.count, events: bySpecies.get(row.speciesId) ?? [] }));
  const order = species.map(row => row.id).filter(id => bySpecies.has(id) || id === highlighted);
  const listed = new Set(order);
  for (const id of bySpecies.keys()) if (!listed.has(id)) { order.push(id); listed.add(id); }
  const lanes = order.map(speciesId => ({ speciesId, name: names.get(speciesId) || speciesId, count: bySpecies.get(speciesId)?.length ?? 0, events: bySpecies.get(speciesId) ?? [] }));
  return live ? lanes.sort((a, b) => b.count - a.count || (a.speciesId < b.speciesId ? -1 : a.speciesId > b.speciesId ? 1 : 0)) : lanes.slice(0, 6);
}
