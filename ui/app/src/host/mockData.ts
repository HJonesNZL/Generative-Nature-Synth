/**
 * DEMO DATA for the mock adapter only. Station positions, individual IDs,
 * scores and counts are ILLUSTRATIVE. They are not records from BirdWeather,
 * the NZ Great White app or any other provider and must never be presented
 * as real wildlife observations.
 */
import type { EffectView, MappingRow, SpeciesView, VoiceDefinition, VoiceParameterSpec } from "./provisional";

export const DEMO_TIMEZONE = "Pacific/Auckland";

export const SPECIES: SpeciesView[] = [
  { id: "tui", commonName: "Tūī", scientificName: "Prosthemadera novaeseelandiae", group: "forest_bird" },
  { id: "korimako", commonName: "Korimako", scientificName: "Anthornis melanura", group: "forest_bird" },
  { id: "kaka", commonName: "Kākā", scientificName: "Nestor meridionalis", group: "forest_bird" },
  { id: "riroriro", commonName: "Riroriro", scientificName: "Gerygone igata", group: "forest_bird" },
  { id: "kereru", commonName: "Kererū", scientificName: "Hemiphaga novaeseelandiae", group: "forest_bird" },
  { id: "white_shark", commonName: "White shark", scientificName: "Carcharodon carcharias", group: "shark" },
];

export interface DemoStation { id: string; name: string; latitude: number; longitude: number; weights: Record<string, number> }

/** Fixed acoustic stations (illustrative positions near real places). */
export const STATIONS: DemoStation[] = [
  { id: "stn-waitakere", name: "Waitākere", latitude: -36.95, longitude: 174.55, weights: { tui: 4, korimako: 2, kereru: 1, riroriro: 1 } },
  { id: "stn-rotorua", name: "Rotorua", latitude: -38.14, longitude: 176.25, weights: { tui: 6, korimako: 2, kaka: 1, riroriro: 1 } },
  { id: "stn-te-urewera", name: "Te Urewera", latitude: -38.75, longitude: 177.0, weights: { tui: 2, kaka: 2, korimako: 1, kereru: 1 } },
  { id: "stn-pureora", name: "Pureora", latitude: -38.52, longitude: 175.57, weights: { tui: 3, kaka: 2, korimako: 2, riroriro: 1 } },
  { id: "stn-tongariro", name: "Tongariro", latitude: -39.2, longitude: 175.6, weights: { riroriro: 2, korimako: 1 } },
  { id: "stn-zealandia", name: "Zealandia", latitude: -41.29, longitude: 174.75, weights: { tui: 2, korimako: 4, kaka: 2 } },
  { id: "stn-kaikoura", name: "Kaikōura", latitude: -42.4, longitude: 173.68, weights: { korimako: 2, riroriro: 1 } },
  { id: "stn-arthurs-pass", name: "Arthur’s Pass", latitude: -42.94, longitude: 171.56, weights: { korimako: 3, riroriro: 1 } },
  { id: "stn-orokonui", name: "Orokonui", latitude: -45.77, longitude: 170.6, weights: { korimako: 3, tui: 1, riroriro: 2 } },
  { id: "stn-rakiura", name: "Rakiura", latitude: -46.9, longitude: 168.1, weights: { korimako: 2, kaka: 1 } },
];

/** Satellite fixes for two illustrative tagged individuals (measured points only). */
export const SHARK_FIXES: Array<{ individualId: string; deploymentId: string; latitude: number; longitude: number; uncertaintyMetres: number }> = [
  { individualId: "WS-07", deploymentId: "dep-ws07-2026", latitude: -46.75, longitude: 168.2, uncertaintyMetres: 4000 },
  { individualId: "WS-07", deploymentId: "dep-ws07-2026", latitude: -47.25, longitude: 168.55, uncertaintyMetres: 1500 },
  { individualId: "WS-12", deploymentId: "dep-ws12-2026", latitude: -44.0, longitude: -176.5, uncertaintyMetres: 2500 },
  { individualId: "WS-12", deploymentId: "dep-ws12-2026", latitude: -21.2, longitude: -175.2, uncertaintyMetres: 9000 },
];

export const SOURCE_IDS = { birdweather: "birdweather", sos: "nz-great-white-sos", archive: "archive-import" } as const;
export const SOURCE_NAMES: Record<string, string> = {
  birdweather: "BirdWeather",
  "nz-great-white-sos": "NZ Great White · SOS",
  "archive-import": "Archive import",
};

const ring = (points: Array<[number, number]>) => [...points, points[0] as [number, number]];

export const REGIONS = [
  {
    id: "region-central-forest",
    name: "Central North Island forest",
    shortName: "Central forest",
    priority: 2,
    ring: ring([[175.2, -37.6], [177.6, -37.8], [177.9, -39.2], [176.4, -39.9], [175.0, -39.4]]),
    effect: { effectId: "fx-forest-space", send: 0.6 },
  },
  {
    id: "region-foveaux",
    name: "Foveaux coast",
    shortName: "Foveaux coast",
    priority: 1,
    ring: ring([[167.3, -46.2], [169.3, -46.1], [169.1, -47.7], [167.2, -47.6]]),
    effect: { effectId: "fx-coastal-echo", send: 0.4 },
  },
];

export const EFFECTS: EffectView[] = [
  { id: "fx-forest-space", name: "Forest Space", kind: "reverb", summary: "reverb · 3.8 s" },
  { id: "fx-coastal-echo", name: "Coastal Echo", kind: "delay", summary: "delay · fb 0.28" },
];

/** Parameter ranges are PROPOSALS for DSP review, not settled specifications. */
export const VOICE_PARAMETERS: VoiceParameterSpec[] = [
  { id: "tuning", label: "Tuning", unit: "st", min: -24, max: 24, defaultValue: 0, curve: "linear", proposal: true },
  { id: "attack", label: "Attack", unit: "s", min: 0.001, max: 2, defaultValue: 0.02, curve: "log", proposal: true },
  { id: "decay", label: "Decay", unit: "s", min: 0.01, max: 4, defaultValue: 0.4, curve: "log", proposal: true },
  { id: "sustain", label: "Sustain", unit: "ratio", min: 0, max: 1, defaultValue: 0.7, curve: "linear", proposal: true },
  { id: "release", label: "Release", unit: "s", min: 0.01, max: 8, defaultValue: 1.2, curve: "log", proposal: true },
  { id: "tone", label: "Tone", unit: "ratio", min: 0, max: 1, defaultValue: 0.68, curve: "linear", proposal: true },
  { id: "gain", label: "Gain", unit: "dB", min: -48, max: 6, defaultValue: -6, curve: "linear", proposal: true },
  { id: "pan", label: "Pan", unit: "pan", min: -1, max: 1, defaultValue: 0, curve: "linear", proposal: true },
];

function toNormalizedMock(spec: VoiceParameterSpec, value: number): number {
  if (spec.curve === "log") return Math.log(value / spec.min) / Math.log(spec.max / spec.min);
  return (value - spec.min) / (spec.max - spec.min);
}

function values(overrides: Record<string, number>): Record<string, number> {
  const out: Record<string, number> = {};
  for (const spec of VOICE_PARAMETERS) out[spec.id] = toNormalizedMock(spec, overrides[spec.id] ?? spec.defaultValue);
  return out;
}

export function initialVoices(): VoiceDefinition[] {
  return [
    { id: "voice-group-forest", name: "Forest birds default", scope: "group", targetId: "forest_bird", inheritsFromId: null, type: "pluck", values: values({}), overriddenParameterIds: [], savedRevision: "11", dirty: false },
    { id: "voice-group-sharks", name: "Sharks default", scope: "group", targetId: "shark", inheritsFromId: null, type: "resonance", values: values({ attack: 0.3, release: 4, tone: 0.35, tuning: -12 }), overriddenParameterIds: [], savedRevision: "4", dirty: false },
    { id: "voice-tui", name: "Voice A · Pluck", scope: "species", targetId: "tui", inheritsFromId: "voice-group-forest", type: "pluck", values: values({ attack: 0.18, decay: 0.42, sustain: 0.72, release: 1.2 }), overriddenParameterIds: ["attack", "decay", "sustain"], savedRevision: "12", dirty: false },
    { id: "voice-korimako", name: "Voice B · Air", scope: "species", targetId: "korimako", inheritsFromId: "voice-group-forest", type: "air", values: values({ attack: 0.4, release: 2.5, tone: 0.5 }), overriddenParameterIds: ["attack", "release", "tone"], savedRevision: "7", dirty: false },
    { id: "voice-riroriro", name: "Voice C · Bell", scope: "species", targetId: "riroriro", inheritsFromId: "voice-group-forest", type: "bell", values: values({ decay: 1.6, sustain: 0.1, tuning: 12 }), overriddenParameterIds: ["decay", "sustain", "tuning"], savedRevision: "3", dirty: false },
    { id: "voice-ws07", name: "WS-07 override", scope: "individual", targetId: "WS-07", inheritsFromId: "voice-group-sharks", type: "resonance", values: values({ attack: 0.3, release: 4, tone: 0.5, tuning: -7 }), overriddenParameterIds: ["tone", "tuning"], savedRevision: "2", dirty: false },
  ];
}

export function initialMappings(): MappingRow[] {
  return [
    { id: "map-species-motif", input: "Species", transform: "fixed motif per species", output: "phrase choice", enabled: true, proposal: true },
    { id: "map-interval", input: "Interval since previous event", transform: "curve 0–120 s → 1–0", output: "articulation", enabled: true, proposal: true },
    { id: "map-longitude", input: "Published longitude", transform: "region-relative, bounded ±0.6", output: "pan", enabled: true, proposal: true },
    { id: "map-confidence", input: "Provider confidence", transform: "provider-specific, ≥ threshold only", output: "velocity 48–96", enabled: false, proposal: true },
    { id: "map-solar", input: "Local solar time", transform: "dawn/dusk window", output: "register shift", enabled: false, proposal: true },
  ];
}

export const SCALE_NOTES = ["G3", "A3", "B♭3", "C4", "D4", "E♭4", "F4", "G4"];
