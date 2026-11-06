/**
 * PROPOSED host boundary for React/JUCE integration, version 1.
 * These are presentation DTOs, not an existing RPC implementation.
 * A labelled mock adapter can implement this shape; production must check capabilities.
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

export interface HostMethods {
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
  setTransport: {
    request: { action: "play" | "pause" | "stop" | "seek" | "panic"; preparationId?: string; positionUs?: Int64String };
    response: { generation: Int64String; state: "playing" | "paused" | "stopped" };
  };
  setVoiceParameter: {
    request: { voiceId: string; parameterId: string; normalizedValue: number; gesture: "begin" | "change" | "end" };
    response: { normalizedValue: number; changeRevision: Int64String };
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
