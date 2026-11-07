import type { LiveActivityPage, LiveActivitySnapshot, LiveActivityVoice } from "../host/contracts";
export const ACTIVITY_HISTORY_US = 30_000_000;
const MAX_VOICES = 4096;
/** One bounded incremental journal cache. Publish only after hasMore drains. */
export class LiveActivityCache {
  private voices = new Map<string, LiveActivityVoice>();
  private ordered: LiveActivityVoice[] | null = null;
  nextSequence = "0";
  apply(page: LiveActivityPage) {
    if (page.reset) { this.voices.clear(); this.ordered = null; }
    for (const voice of page.voices) { this.voices.set(voice.token, voice); this.ordered = null; }
    const cutoff = BigInt(page.clockUs) - BigInt(ACTIVITY_HISTORY_US);
    for (const [token, voice] of this.voices) if (voice.endUs !== null && BigInt(voice.endUs) < cutoff) { this.voices.delete(token); this.ordered = null; }
    if (this.voices.size > MAX_VOICES) throw Error("Playback activity exceeded its 4096-note history capacity.");
    this.nextSequence = page.nextSequence;
  }
  snapshot(page: LiveActivityPage): LiveActivitySnapshot {
    const voices = this.ordered ?? [...this.voices.values()].sort((a, b) => {
      const first = BigInt(a.onsetUs), second = BigInt(b.onsetUs);
      return first === second ? a.token.localeCompare(b.token) : first < second ? -1 : 1;
    });
    this.ordered = voices;
    return {sessionId: page.sessionId, sessionActive: page.sessionActive, clockUs: page.clockUs, dropped: page.dropped, voices};
  }
}
/** Steady native time anchored on receipt; this never creates note onsets. */
export class LiveActivityClock {
  private anchored = false;
  private anchorUs = 0;
  private anchorMs = 0;
  private sampledUs = 0;
  private listeners = new Set<() => void>();
  sampleNowUs = () => this.anchored ? this.anchorUs + Math.max(0, performance.now() - this.anchorMs) * 1000 : 0;
  getSnapshot = () => this.sampledUs;
  subscribe = (listener: () => void) => { this.listeners.add(listener); return () => { this.listeners.delete(listener); }; };
  anchor(clockUs: string) { this.anchored = true; this.anchorUs = Number(clockUs); this.anchorMs = performance.now(); }
  tick = () => { this.sampledUs = this.sampleNowUs(); for (const listener of this.listeners) listener(); };
  reset() { this.anchored = false; this.anchorUs = 0; this.anchorMs = 0; this.tick(); }
}
export function activityIsSounding(voice: LiveActivityVoice, nowUs: number) {
  return Number(voice.onsetUs) <= nowUs && (voice.endUs === null || nowUs < Number(voice.endUs));
}
export function activitySpeciesName(voice: LiveActivityVoice) { return voice.commonName || voice.speciesId || "Species not reported"; }
export function activityVoiceName(voice: LiveActivityVoice) { return voice.voiceName || voice.voiceType; }
function compactPlace(value: string | null, stationId: string | null) {
  if (!value) return "";
  let label = value.trim();
  const prefix = /^(?:receiver|measured fix|reported location|location)\b\s*(?:[·:—–-]\s*)?/i;
  for (let i = 0; i < 4 && prefix.test(label); i++) label = label.replace(prefix, "").trim();
  if (/^(acoustic receiver location|reported individual fix(?: \(approximate\))?|measured fix|receiver position)$/i.test(label)) return "";
  if (!label || label === stationId || /^(?:[a-z0-9_-]+:)?(?:station|receiver|fix):/i.test(label) || /^(unknown|unassigned|not assigned|no region|unavailable|world|global)$/i.test(label)) return "";
  return label;
}
export function activityAccuracy(voice: LiveActivityVoice) {
  const metres = voice.position?.uncertaintyMetres;
  if (metres == null) return "accuracy not supplied";
  return metres >= 1000 ? `±${Number((metres / 1000).toFixed(2))} km` : `±${Number(metres.toFixed(2))} m`;
}
export function activityLocation(voice: LiveActivityVoice) {
  const receiver = ["station_detection", "receiver_detection"].includes(voice.locationKind);
  const meaning = receiver ? "Receiver" : ["gps_fix", "satellite_fix"].includes(voice.eventKind) ? "Measured fix" : "Reported location";
  // Explicit source privacy takes precedence over a region or canonical ID.
  if (/unavailable or withheld/i.test(voice.locationLabel)) return `${meaning} · location unavailable or withheld`;
  if (/withheld|redacted|hidden for privacy|sensitive location/i.test(voice.locationLabel)) return `${meaning} · location withheld`;
  let place = compactPlace(voice.locationLabel, voice.stationId) || compactPlace(voice.region, voice.stationId);
  if (!place && voice.position) {
    const {latitude, longitude} = voice.position;
    place = `${Math.round(Math.abs(latitude))}° ${latitude < 0 ? "S" : "N"}, ${Math.round(Math.abs(longitude))}° ${longitude < 0 ? "W" : "E"}`;
  }
  if (!place) return `${meaning} · location unavailable`;
  const uncertainty = voice.position?.uncertaintyMetres;
  return `${meaning} · ${place}${/approximate/i.test(voice.locationLabel) ? " · approximate" : ""}${uncertainty == null ? "" : ` · ${activityAccuracy(voice)}`}`;
}
export function activityColour(voice: LiveActivityVoice) {
  const id = voice.speciesId || voice.eventId;
  let hash = 0; for (let i = 0; i < id.length; i++) hash = (hash * 31 + id.charCodeAt(i)) >>> 0;
  return ["#1f5a32", "#4f8a5b", "#5e8f66", "#6e9a72", "#86b08a", "#3d5a6c"][hash % 6]!;
}
