/**
 * Shared host boundary for React/native integration, version 1.
 * These presentation DTOs include implemented and proposed methods.
 * Every consumer must check the host capability list before sending a method.
 * No provider credentials, SQL, raw filesystem access, or audio sample arrays cross it.
 */
export type UtcTimestamp = string;
export type Int64String = string;
export type TimeBasis = "observed" | "arrival";
export type MaterialMode = "live" | "replay" | "history" | "imported" | "demo";

export interface HostCapabilities {
  protocolVersion: 1;
  implementation: "mock" | "native";
  supportedMethods: string[];
  audioAvailable: boolean;
  maxEventBatch: number;
  maxVisibleMarkers: number;
}

export interface TransportState {
  preparationId: string | null;
  generation: Int64String;
  state: "playing" | "paused" | "stopped";
  positionUs: Int64String;
  /** Native playback timeline, including bounded voice/effect tails. */
  durationUs: Int64String;
}

export interface AudioState {
  status: "running" | "disabled" | "unavailable" | "device_lost";
  deviceName: string | null;
  masterGainDb: number;
  peakDbfs: number | null;
  meterIsSimulated: boolean;
  clipping: boolean;
  activeSynthVoices: number | null;
  maxSynthVoices: number;
  sampleRate?: number | null;
  error?: string | null;
}

export interface EventView {
  id: string;
  revisionSequence: Int64String;
  sourceId: string;
  speciesId: string | null;
  commonName: string | null;
  individualId: string | null;
  deploymentId: string | null;
  stationId: string | null;
  eventKind: "gps_fix" | "satellite_fix" | "acoustic_receiver_detection" | "acoustic_species_detection" | "human_observation";
  locationKind: string;
  position: { latitude: number; longitude: number; uncertaintyMetres: number | null } | null;
  observedAtUtc: UtcTimestamp;
  firstSeenAtUtc: UtcTimestamp;
  confidence: { value: number; meaning: string } | null;
  attribution: string | null;
  qualityFlags: string[];
  // Data remains its original provenance even while the VIEW is in replay/history mode.
  provenance: "live_source" | "imported" | "demo";
}

export interface SourceHealth {
  sourceId: string;
  connection: "connecting" | "connected" | "retrying" | "offline" | "disabled";
  lastSuccessfulFetchUtc: UtcTimestamp | null;
  newestObservationUtc: UtcTimestamp | null;
  publicationLagSeconds: number | null;
  coverage: "best_effort" | "partial" | "unknown";
  permissionsStatus: string;
  errorCode: string | null;
}

export interface SessionView {
  id: string;
  name: string;
  state: "recording" | "frozen" | "interrupted";
  origin: "collection" | "archive_snapshot" | "imported_bundle";
  eventCount: number;
  captureTruncated: boolean;
  metadataIntegrity?: string;
  observedWindowPolicy?: string;
  observedBoundsUs?: [Int64String, Int64String] | null;
  arrivalBoundsUs?: [Int64String, Int64String] | null;
}

export interface CoverageInterval {
  fromUtc: UtcTimestamp;
  toUtc: UtcTimestamp;
  state: "records_available" | "confirmed_coverage_no_detections" | "missing_coverage" | "unknown";
  sourceIds: string[];
  resolutionDescription: string | null;
}

export interface RegionView {
  id: string;
  name: string;
  // WGS84 longitude/latitude, unlike EventView's explicitly named position fields.
  geometry: { type: "MultiPolygon"; coordinates: number[][][][] };
  priority: number;
  overlapPolicy: "highest_priority" | "blend";
  effectAssignments: Array<{ effectId: string; enabled: boolean; send: number }>;
}

export type VoiceType = "struck" | "whistle" | "reed" | "bell" | "air" | "pulse" | "pluck" | "resonance";
export type VoiceScope = "group" | "species" | "individual";

export interface VoiceParameterSpec {
  id: string;
  label: string;
  unit: "st" | "s" | "ratio" | "dB" | "pan" | "%" | "Hz";
  min: number;
  max: number;
  defaultValue: number;
  curve: "linear" | "log";
  proposal: boolean; // true only for unimplemented design proposals
  step?: number; // display-unit quantization, when supplied by the host
}

/** A saved voice definition (preset-like). Not a sounding synth voice. */
export interface VoiceDefinition {
  id: string;
  name: string;
  scope: VoiceScope;
  targetId: string;              // group id, species id or individual id
  inheritsFromId: string | null; // native species definitions are independent (null)
  type: VoiceType;
  values: Record<string, number>; // normalized 0..1 per parameter id
  overriddenParameterIds: string[]; // which values differ from the parent
  savedRevision: Int64String;
  dirty: boolean;
  changeRevision: Int64String;
}

export interface VoiceLibraryEntry {
  groupId: string;
  groupName: string;
  groupVoiceId: string | null;   // null = no group default (species must be assigned)
  species: Array<{
    speciesId: string;
    commonName: string;
    scientificName: string;
    voiceId: string | null;      // native species definition; null uses fallback
    individuals: Array<{ individualId: string; label: string; voiceId: string | null }>;
  }>;
}

export interface VoiceTypeSpec { id: VoiceType; label: string; proposal: boolean }

export interface SoundscapeView {
  id: string;
  name: string;
  valid: boolean;
  contentVerified: boolean;
  error: string | null;
  contentHash: string | null;
  sourceSessionId: string | null;
  eventCount: number;
  durationUs: Int64String | null;
  basis: TimeBasis | null;
  speedNumerator: Int64String | null;
  speedDenominator: Int64String | null;
}

export interface RenderJob {
  jobId: string;
  state: "queued" | "rendering" | "complete" | "failed" | "cancelled";
  progress: number;
  message: string | null;
  artifactId?: string | null;
  filename?: string | null;
  soundscapeId?: string;
}

export interface VoicePresetView {
  id: string;
  name: string;
  speciesId: string | null;
  scientificName: string;
  description: string;
  evidenceStatus: "research_candidate" | "ecological_metaphor" | "unresolved_taxon";
  researchPath: string;
}

export interface HostMethods {
  listVoicePresets: { request: { voiceId: string }; response: { items: VoicePresetView[]; listeningApproved: false } };
  applyVoicePreset: { request: { voiceId: string; presetId: string }; response: VoiceDefinition };
  listSoundscapes: { request: { limit: number }; response: { items: SoundscapeView[]; limited: boolean; unmanagedFiles?: number } };
  saveSoundscape: { request: { name: string }; response: SoundscapeView };
  recallSoundscape: { request: { soundscapeId: string }; response: { soundscape: SoundscapeView; preparation: HostMethods["prepareReplay"]["response"] } };
  startSoundscapeRender: { request: { soundscapeId: string }; response: RenderJob };
  getCapabilities: { request: Record<string, never>; response: HostCapabilities };
  getSourceHealth: { request: Record<string, never>; response: SourceHealth[] };
  getChanges: {
    request: { storeId: string; afterSequence: Int64String; limit: number };
    response: { storeId: string; nextSequence: Int64String; events: Array<{ event: EventView; liveEligible: boolean; change: "baseline" | "new" | "corrected" }> };
  };
  listSessions: { request: { limit: number }; response: SessionView[] };
  prepareReplay: {
    request: { sessionId: string; basis: TimeBasis; speedNumerator: Int64String; speedDenominator: Int64String };
    response: { preparationId: string; generation: Int64String; durationUs: Int64String; eventCount: number; peakEventsPerSecond: number; session?: SessionView };
  };
  queryHistory: {
    request: { speciesIds: string[]; regionId: string; fromUtc: UtcTimestamp; toUtc: UtcTimestamp; displayTimezone: string };
    response: { selectionId: string; eventCount: number | null; coverage: CoverageInterval[]; status: "ready" | "partial" | "unavailable" | "unknown" };
  };
  getTransport: { request: Record<string, never>; response: TransportState };
  setTransport: {
    request: { action: "play" | "pause" | "seek"; preparationId: string; generation: Int64String; positionUs?: Int64String }
      | { action: "stop" | "panic"; preparationId?: string; generation?: Int64String };
    response: TransportState;
  };
  getAudioState: { request: Record<string, never>; response: AudioState };
  setAudioEnabled: { request: { enabled: boolean }; response: AudioState };
  setMasterGain: { request: { gainDb: number; gesture: "begin" | "change" | "end" }; response: { gainDb: number; changeRevision: Int64String } };
  getVoiceLibrary: { request: Record<string, never>; response: { entries: VoiceLibraryEntry[]; parameters: VoiceParameterSpec[]; types?: VoiceTypeSpec[]; maxDefinitions?: number; editingBehavior?: "new_notes" } };
  getVoice: { request: { voiceId: string }; response: VoiceDefinition };
  saveVoice: { request: { voiceId: string; expectedRevision: Int64String }; response: VoiceDefinition };
  revertVoice: { request: { voiceId: string }; response: VoiceDefinition };
  setVoiceParameter: {
    request: { voiceId: string; parameterId: string; normalizedValue: number; gesture: "begin" | "change" | "end" };
    response: { normalizedValue: number; changeRevision: Int64String; dirty: boolean };
  };
  listRegions: { request: Record<string, never>; response: RegionView[] };
}

export interface HostRequest<M extends keyof HostMethods> {
  protocolVersion: 1;
  requestId: string;
  method: M;
  args: HostMethods[M]["request"];
}

export type HostReply<T> =
  | { requestId: string; ok: true; result: T }
  | { requestId: string; ok: false; error: { code: "unsupported" | "invalid_request" | "unavailable" | "stale_generation" | "internal"; message: string } };

export interface AtlasHost {
  request<M extends keyof HostMethods>(request: HostRequest<M>, signal?: AbortSignal): Promise<HostReply<HostMethods[M]["response"]>>;
}
