/** Display-only projection of actual native onsets/ends. Arrival time never
 * starts a light. Shared clock microseconds preserve overlap and stop timing. */
export const SOUND_FADE_US = 2_000_000;
export const MAX_SOUND_ORIGINS = 4096;
export interface SoundingVoice {
  token: string;
  eventId: string;
  commonName: string;
  locationLabel: string;
  eventKind: string;
  locationKind: string;
  position: {latitude: number; longitude: number} | null;
  onsetUs: string;
  endUs: string | null;
}
export interface SoundingOrigin {
  key: string;
  latitude: number;
  longitude: number;
  level: number;
  activeVoices: number;
  tokens: string[];
  label: string;
}
export function soundingSourceType(voice: Pick<SoundingVoice, "eventKind" | "locationKind">): "acoustic" | "reported" | "tracking" {
  return voice.locationKind === "station_detection" || voice.locationKind === "receiver_detection" ? "acoustic" : voice.eventKind === "human_observation" || voice.locationKind === "reported_location" ? "reported" : "tracking";
}
export function soundingOrigins(voices: readonly SoundingVoice[], nowUs: number, sourceTypes: readonly string[]) {
  const origins = new Map<string, SoundingOrigin>();
  let unmapped = 0;
  for (const voice of voices) {
    if (!sourceTypes.includes(soundingSourceType(voice))) continue;
    const onset = Number(voice.onsetUs), end = voice.endUs === null ? null : Number(voice.endUs);
    if (!Number.isFinite(nowUs) || !Number.isFinite(onset) || nowUs < onset || end !== null && (!Number.isFinite(end) || end < onset)) continue;
    const active = end === null || nowUs < end;
    const level = active ? 1 : Math.max(0, 1 - (nowUs - end!) / SOUND_FADE_US);
    if (level <= 0) continue;
    const point = voice.position;
    if (!point || !Number.isFinite(point.latitude) || !Number.isFinite(point.longitude) || Math.abs(point.latitude) > 90 || Math.abs(point.longitude) > 180) {
      if (active) ++unmapped;
      continue;
    }
    const key = `${point.latitude}:${point.longitude}`;
    const existing = origins.get(key);
    if (existing) {
      existing.level = Math.max(existing.level, level);
      existing.activeVoices += active ? 1 : 0;
      existing.tokens.push(voice.token);
    } else origins.set(key, {key, ...point, level, activeVoices: active ? 1 : 0, tokens: [voice.token], label: `${voice.commonName || "Animal voice"} · ${voice.locationLabel || "Reported sound origin"}`});
  }
  return {origins: [...origins.values()], unmapped};
}
