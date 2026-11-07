import type { HostMethods } from "../host/contracts";
export type LiveSpeciesCount = HostMethods["getLiveSpeciesCounts"]["response"]["species"][number];
type SpeciesPage = HostMethods["getLiveSpeciesCounts"]["response"];

// Assemble one complete committed frontier. Never present a partial ranking or
// combine pages whose ordering changed while new detections were committed.
export async function collectSpeciesPages(sessionId: string, fetchPage: (cursor?: string) => Promise<SpeciesPage>, cancelled: () => boolean = () => false): Promise<LiveSpeciesCount[] | null> {
  const rows: LiveSpeciesCount[] = [];
  const seen = new Set<string>();
  const cursors = new Set<string>();
  let cursor: string | undefined;
  let revision: string | undefined;
  do {
    const page = await fetchPage(cursor);
    if (cancelled() || page.sessionId !== sessionId || !page.sessionActive) return null;
    if (revision !== undefined && page.revision !== revision) return null;
    revision = page.revision;
    for (const row of page.species) {
      if (seen.has(row.speciesId)) throw Error("Species ranking repeated an identity");
      seen.add(row.speciesId);
      rows.push(row);
    }
    cursor = page.nextCursor ?? undefined;
    if (cursor !== undefined) {
      if (cursors.has(cursor)) throw Error("Species ranking repeated a page");
      cursors.add(cursor);
    }
  } while (cursor !== undefined);
  return rows.sort((a, b) => b.count - a.count || (a.speciesId < b.speciesId ? -1 : a.speciesId > b.speciesId ? 1 : 0));
}
