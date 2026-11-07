/** Bounded display sampling. Geographic sectors describe coverage, not animal tracks. */
export interface LocatedSource { sourceId?: string; latitude?: number; longitude?: number; position?: { latitude: number; longitude: number } | null }
export function coverageGroup(row: LocatedSource): string {
  const p = row.position ?? row;
  const sector = p.latitude == null || p.longitude == null ? "unlocated" : `${Math.floor((p.latitude + 90) / 30)}:${Math.floor((p.longitude + 180) / 60)}`;
  return `${row.sourceId ?? "unknown"}|${sector}`;
}
/** Input is priority/newest first. Keep leading rows, then spread half the slots across sources and geographic sectors. */
export function sampleCoverage<T extends LocatedSource>(ordered: readonly T[], capacity: number, leading = 8): T[] {
  if (ordered.length <= capacity) return [...ordered];
  const chosen = new Set(ordered.slice(0, Math.min(leading, capacity)));
  const sources = new Map<string, Map<string, T>>();
  for (const row of ordered) {
    const source = row.sourceId ?? "unknown";
    if (!sources.has(source)) sources.set(source, new Map());
    const sectors = sources.get(source)!;
    const sector = coverageGroup(row);
    if (!sectors.has(sector)) sectors.set(sector, row);
  }
  const groups = [...sources.values()].map(sectors => [...sectors.values()]);
  const reserve = Math.min(capacity, chosen.size + Math.floor(capacity / 2));
  for (let sector = 0; chosen.size < reserve; sector++) {
    let found = false;
    for (const rows of groups) {
      const representative = rows[sector];
      if (!representative) continue;
      found = true; chosen.add(representative);
      if (chosen.size >= reserve) break;
    }
    if (!found) break;
  }
  for (const row of ordered) { if (chosen.size >= capacity) break; chosen.add(row); }
  return ordered.filter(row => chosen.has(row));
}

/** Preserve timezone offsets and the source's microsecond precision. */
export function observedMicroseconds(timestamp: string): bigint | null {
  const ms = Date.parse(timestamp);
  if (!Number.isFinite(ms)) return null;
  const fraction = timestamp.match(/\.(\d{1,6})(?:Z|[+-]\d{2}:\d{2})$/)?.[1] ?? "";
  return BigInt(ms) * 1000n + BigInt(fraction.padEnd(6, "0").slice(3));
}
