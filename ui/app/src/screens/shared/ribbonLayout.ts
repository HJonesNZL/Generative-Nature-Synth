import type { LiveActivityVoice } from "../../host/contracts";
import { ACTIVITY_HISTORY_US, activityIsSounding, activityLocation, activitySpeciesName } from "../../app/liveActivity.ts";
export interface RibbonLane { key: string; name: string; location: string; voices: LiveActivityVoice[] }
export function ribbonLanes(voices: readonly LiveActivityVoice[], nowUs: number): RibbonLane[] {
  const lanes = new Map<string, RibbonLane>();
  for (const voice of voices) {
    if (voice.endUs !== null && Number(voice.endUs) < nowUs - ACTIVITY_HISTORY_US) continue;
    const location = activityLocation(voice);
    const key = JSON.stringify([voice.speciesId, voice.commonName, location, voice.voiceName, voice.voiceType]);
    const lane = lanes.get(key) ?? {key, name: activitySpeciesName(voice), location, voices: []};
    lane.voices.push(voice); lanes.set(key, lane);
  }
  return [...lanes.values()].sort((a, b) => Number(b.voices.at(-1)!.onsetUs) - Number(a.voices.at(-1)!.onsetUs) || a.key.localeCompare(b.key));
}
/** Positions use real elapsed time; there is no beat grid or invented envelope. */
export function ribbonInterval(voice: LiveActivityVoice, nowUs: number) {
  const x = (us: number) => Math.max(0, Math.min(100, (us - nowUs + ACTIVITY_HISTORY_US) / ACTIVITY_HISTORY_US * 100));
  const start = x(Number(voice.onsetUs));
  const end = x(voice.endUs === null ? nowUs : Math.min(nowUs, Number(voice.endUs)));
  return {start, width: Math.max(.25, end - start), active: activityIsSounding(voice, nowUs)};
}
