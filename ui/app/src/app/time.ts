/**
 * Display helpers. Every timestamp crossing the bridge stays a UTC ISO string;
 * conversion happens only here, always with an explicit IANA timezone and an
 * explicit zone label in the output.
 */
export const DISPLAY_TIMEZONE = "Pacific/Auckland";

const timeFmt = new Map<string, Intl.DateTimeFormat>();
function fmt(timeZone: string, options: Intl.DateTimeFormatOptions): Intl.DateTimeFormat {
  const key = `${timeZone}|${JSON.stringify(options)}`;
  let f = timeFmt.get(key);
  if (!f) {
    f = new Intl.DateTimeFormat("en-NZ", { timeZone, ...options });
    timeFmt.set(key, f);
  }
  return f;
}

export function zoneAbbreviation(utc: string | number | Date, timeZone = DISPLAY_TIMEZONE): string {
  const parts = fmt(timeZone, { timeZoneName: "short" }).formatToParts(new Date(utc));
  return parts.find((p) => p.type === "timeZoneName")?.value ?? timeZone;
}

/** "12:02:09" — callers must show the zone label nearby (see formatClock). */
export function formatTime(utc: string | number | Date, timeZone = DISPLAY_TIMEZONE): string {
  return fmt(timeZone, { hour: "2-digit", minute: "2-digit", second: "2-digit", hourCycle: "h23" }).format(new Date(utc));
}
export function formatHm(utc: string | number | Date, timeZone = DISPLAY_TIMEZONE): string {
  return fmt(timeZone, { hour: "2-digit", minute: "2-digit", hourCycle: "h23" }).format(new Date(utc));
}
/** "07 Nov 2026 12:02:09 NZDT" */
export function formatDateTime(utc: string | number | Date, timeZone = DISPLAY_TIMEZONE): string {
  const d = new Date(utc);
  const date = fmt(timeZone, { day: "2-digit", month: "short", year: "numeric" }).format(d);
  return `${date} ${formatTime(d, timeZone)} ${zoneAbbreviation(d, timeZone)}`;
}
export function formatDate(utc: string | number | Date, timeZone = DISPLAY_TIMEZONE): string {
  return fmt(timeZone, { day: "2-digit", month: "short", year: "numeric" }).format(new Date(utc));
}

export function formatAge(fromUtc: string | null, nowMs: number): string {
  if (!fromUtc) return "unknown";
  const s = Math.max(0, Math.round((nowMs - Date.parse(fromUtc)) / 1000));
  if (s < 60) return `${s} s`;
  if (s < 3600) return `${Math.round(s / 60)} min`;
  if (s < 86400) return `${Math.round(s / 360) / 10} h`;
  return `${Math.round(s / 8640) / 10} d`;
}

/** Microsecond strings stay strings; only bounded display values become numbers. */
export function formatUs(us: string): string {
  let total: bigint;
  try { total = BigInt(us); } catch { return "—"; }
  const seconds = Number(total / 1_000_000n);
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  return `${h}:${String(m).padStart(2, "0")}:${String(s).padStart(2, "0")}`;
}

/** Convert a wall-clock date + time in `timeZone` to a UTC ISO string. */
export function zonedToUtc(dateIso: string, timeHm: string, timeZone: string): string | null {
  const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(dateIso);
  const t = /^(\d{2}):(\d{2})$/.exec(timeHm);
  if (!m || !t) return null;
  const [y, mo, d, hh, mm] = [Number(m[1]), Number(m[2]), Number(m[3]), Number(t[1]), Number(t[2])];
  if (hh > 23 || mm > 59) return null;
  // Iterate twice to settle the zone offset (handles DST transitions except the skipped hour).
  let guess = Date.UTC(y, mo - 1, d, hh, mm);
  for (let i = 0; i < 2; i += 1) {
    const parts = fmt(timeZone, { year: "numeric", month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", hourCycle: "h23" }).formatToParts(new Date(guess));
    const get = (type: string) => Number(parts.find((p) => p.type === type)?.value);
    const asUtc = Date.UTC(get("year"), get("month") - 1, get("day"), get("hour"), get("minute"));
    guess += Date.UTC(y, mo - 1, d, hh, mm) - asUtc;
  }
  return new Date(guess).toISOString();
}
