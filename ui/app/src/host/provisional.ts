/**
 * PROVISIONAL host methods — NOT part of ui/contracts.ts v1.
 *
 * The UI needs these to show the designed screens. Each one is documented in
 * docs/INTEGRATION_GAPS.md with request/result/error examples so the C++
 * engineer can accept, rename or reject it. Only the mock adapter implements
 * them today. The UI checks `supportedMethods` before calling any of them, so
 * a native host that omits a method gets a disabled control, not a crash.
 */
import type { EventView, HostMethods, Int64String, RegionView, SessionView, TimeBasis, UtcTimestamp } from "./contracts";

export type MusicalOutcome =
  | { kind: "sounded"; voiceId: string; noteName: string; velocity: number; pan: number; regionSends: Array<{ regionId: string; effectId: string; send: number }> }
  | { kind: "grouped"; groupId: string; groupSize: number; voiceId: string }
  | { kind: "filtered"; reason: "confidence_below_threshold" | "species_muted" | "not_soloed" | "no_voice_assigned" }
  | { kind: "suppressed"; reason: "density_cap" | "duplicate" | "baseline" | "correction" }
  | { kind: "deferred"; reason: "scheduled_to_phrase"; scheduledForUtc: UtcTimestamp }
  | { kind: "not_evaluated" };

export type MaterialRef = { kind: "selection"; selectionId: string } | { kind: "session"; sessionId: string };

export interface RegionSpeciesActivity {
  speciesId: string;
  detections: number;
  share: number; // 0..1 of detections in the region window
  voiceId: string | null;
  voiceLabel: string | null;
  muted: boolean;
  soloed: boolean;
}

export interface RegionActivity {
  regionId: string;
  windowFromUtc: UtcTimestamp;
  windowToUtc: UtcTimestamp;
  basis: TimeBasis;
  species: RegionSpeciesActivity[];
  totals: { records: number; eligible: number; sounded: number; grouped: number; suppressed: number };
}

export interface CollectionState {
  state: "idle" | "collecting" | "stopping" | "interrupted" | "complete";
  listening: boolean;            // collect-and-play vs collect-only
  requestedDurationMinutes: number | null;
  startedAtUtc: UtcTimestamp | null;
  elapsedUs: Int64String;
  recordCount: number;
  storageWarning: "none" | "near_limit" | "truncated";
}

export interface AudioState {
  status: "running" | "disabled" | "unavailable" | "device_lost";
  deviceName: string | null;
  masterGainDb: number;
  peakDbfs: number | null;       // null when no measurement is available
  meterIsSimulated: boolean;     // true for the mock — never implies real audio
  clipping: boolean;
  activeSynthVoices: number;     // currently sounding synth voices, not definitions
  maxSynthVoices: number;
}

export type VoiceType = "pluck" | "air" | "resonance" | "bell";
export type VoiceScope = "group" | "species" | "individual";

export interface VoiceParameterSpec {
  id: string;
  label: string;
  unit: "st" | "s" | "ratio" | "dB" | "pan" | "%";
  min: number;
  max: number;
  defaultValue: number;
  curve: "linear" | "log";
  proposal: true; // all DSP ranges are proposals pending engineering review
}

/** A saved voice definition (preset-like). Not a sounding synth voice. */
export interface VoiceDefinition {
  id: string;
  name: string;
  scope: VoiceScope;
  targetId: string;              // group id, species id or individual id
  inheritsFromId: string | null; // parent definition when scope is species/individual
  type: VoiceType;
  values: Record<string, number>; // normalized 0..1 per parameter id
  overriddenParameterIds: string[]; // which values differ from the parent
  savedRevision: Int64String;
  dirty: boolean;
}

export interface VoiceLibraryEntry {
  groupId: string;
  groupName: string;
  groupVoiceId: string | null;   // null = no group default (species must be assigned)
  species: Array<{
    speciesId: string;
    commonName: string;
    scientificName: string;
    voiceId: string | null;      // species-level definition, null when inheriting group
    individuals: Array<{ individualId: string; label: string; voiceId: string | null }>;
  }>;
}

export interface MappingRow {
  id: string;
  input: string;
  transform: string;
  output: string;
  enabled: boolean;
  proposal: true;
}

export interface EffectView {
  id: string;
  name: string;
  kind: "reverb" | "delay";
  summary: string;
}

export interface RenderJob {
  jobId: string;
  state: "queued" | "rendering" | "complete" | "failed" | "cancelled";
  progress: number; // 0..1
  message: string | null;
}

export interface SpeciesView {
  id: string;
  commonName: string;
  scientificName: string;
  group: "forest_bird" | "shark" | "unclassified";
}

export interface StationView { id: string; name: string; sourceId: string; latitude: number; longitude: number; positionDescription: string }
export interface SourceView { id: string; name: string; measurement: string; attribution: string }
export interface CatalogView { species: SpeciesView[]; stations: StationView[]; sources: SourceView[]; limited?: boolean }

export type HistoryFreezeResult = SessionView & { requestedFromUtc: UtcTimestamp; requestedToUtc: UtcTimestamp; materialFromUtc: UtcTimestamp | null; materialToUtc: UtcTimestamp | null };

export interface ProvisionalMethods {
  getCatalog: { request: { material?: MaterialRef }; response: CatalogView };
  getMusicalOutcomes: { request: { eventIds: string[] }; response: Record<string, MusicalOutcome> };
  getRegionActivity: { request: { regionId: string; windowMinutes: number; basis: TimeBasis; material?: MaterialRef }; response: RegionActivity };
  setSpeciesMix: { request: { speciesId: string; muted?: boolean; soloed?: boolean }; response: { speciesId: string; muted: boolean; soloed: boolean; changeRevision: Int64String } };
  getCollectionState: { request: Record<string, never>; response: CollectionState };
  setCollection: { request: { action: "start" | "stop"; durationMinutes?: number; listening?: boolean }; response: CollectionState };
  getAudioState: { request: Record<string, never>; response: AudioState };
  setAudioEnabled: { request: { enabled: boolean }; response: AudioState };
  setMasterGain: { request: { gainDb: number; gesture: "begin" | "change" | "end" }; response: { gainDb: number; changeRevision: Int64String } };
  freezeHistorySelection: { request: { selectionId: string; name: string; preserveRequestedInterval: boolean }; response: HistoryFreezeResult };
  getVoiceLibrary: { request: Record<string, never>; response: { entries: VoiceLibraryEntry[]; parameters: VoiceParameterSpec[] } };
  getVoice: { request: { voiceId: string }; response: VoiceDefinition };
  setVoiceType: { request: { voiceId: string; type: VoiceType }; response: VoiceDefinition };
  saveVoice: { request: { voiceId: string; expectedRevision: Int64String }; response: VoiceDefinition };
  revertVoice: { request: { voiceId: string }; response: VoiceDefinition };
  getMappings: { request: { voiceId: string }; response: MappingRow[] };
  setMappingEnabled: { request: { voiceId: string; mappingId: string; enabled: boolean }; response: MappingRow };
  auditionEvent: { request: { eventId: string; voiceId: string }; response: { accepted: boolean } };
  listEffects: { request: Record<string, never>; response: EffectView[] };
  setRegionEffect: { request: { regionId: string; effectId: string; enabled?: boolean; send?: number; gesture?: "begin" | "change" | "end" }; response: RegionView };
  setRegionPolicy: { request: { overlapPolicy?: RegionView["overlapPolicy"]; uncertainPosition?: "use_reported_point" | "skip_region_effects" }; response: { overlapPolicy: RegionView["overlapPolicy"]; uncertainPosition: "use_reported_point" | "skip_region_effects" } };
  getMaterialEvents: { request: { material: MaterialRef; offset: number; limit: number }; response: { total: number; events: EventView[] } };
  startRender: { request: { sessionId: string; format: "wav_stereo_48k_24"; tailPolicy: "let_ring" | "cut" }; response: RenderJob };
  getRenderJob: { request: { jobId: string }; response: RenderJob };
  cancelRender: { request: { jobId: string }; response: RenderJob };
}

export type AllMethods = HostMethods & ProvisionalMethods;
export type MethodName = keyof AllMethods;

/** Push notifications (provisional). The v1 contract has no subscription mechanism. */
export type HostNotification =
  | { type: "sourceHealthChanged" }
  | { type: "changesAvailable"; storeId: string; latestSequence: Int64String }
  | { type: "voiceParameterChanged"; voiceId: string; parameterId: string; normalizedValue: number; changeRevision: Int64String; origin: "automation" | "restore" | "ui" }
  | { type: "transportChanged"; generation: Int64String; state: "playing" | "paused" | "stopped"; positionUs: Int64String }
  | { type: "audioStateChanged" }
  | { type: "collectionChanged" }
  | { type: "renderProgress"; job: RenderJob }
  | { type: "connection"; state: "connected" | "reconnecting" | "disconnected" };
