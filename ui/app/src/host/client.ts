/**
 * The single injected host boundary used by every component.
 *
 * - Wraps an `AtlasHost` (contracts.ts) or an extended host that also answers
 *   the provisional methods in provisional.ts.
 * - Checks `getCapabilities().supportedMethods` before sending anything, so a
 *   host that lacks a method yields `{ code: "unsupported" }` locally.
 * - Adds request ids, timeouts and AbortSignal support. It never retries
 *   mutations on its own; callers decide.
 */
import type { AtlasHost, HostCapabilities, HostReply } from "./contracts";
import type { AllMethods, HostNotification, MethodName } from "./provisional";

export interface ExtendedRequest<M extends MethodName> {
  protocolVersion: 1;
  requestId: string;
  method: M;
  args: AllMethods[M]["request"];
}

/** A host that may also implement provisional methods and push notifications. */
export interface ExtendedHost {
  request<M extends MethodName>(request: ExtendedRequest<M>, signal?: AbortSignal): Promise<HostReply<AllMethods[M]["response"]>>;
  subscribe?(listener: (notification: HostNotification) => void): () => void;
}

export type HostErrorCode = "unsupported" | "invalid_request" | "unavailable" | "stale_generation" | "internal" | "timeout" | "aborted";
export type HostResult<T> = { ok: true; value: T } | { ok: false; error: { code: HostErrorCode; message: string } };

const unsignedDecimal = (value: unknown): value is string => typeof value === "string" && /^(0|[1-9][0-9]{0,18})$/.test(value) && BigInt(value) <= 9223372036854775807n;
const identity = (value: unknown): value is string => typeof value === "string" && value.length > 0 && value.length <= 256;
const voiceIdentity = (value: unknown): value is string => typeof value === "string" && value.length > 0 && value.length <= 264;
function validVoiceDefinition(value: unknown): boolean {
  if (!value || typeof value !== "object") return false;
  const voice = value as Record<string, unknown>;
  return voiceIdentity(voice.id) && typeof voice.name === "string" && voice.name.length <= 512 &&
    ["group", "species", "individual"].includes(voice.scope as string) && identity(voice.targetId) &&
    (voice.inheritsFromId === null || voiceIdentity(voice.inheritsFromId)) &&
    ["struck", "pluck", "air", "resonance", "bell"].includes(voice.type as string) &&
    unsignedDecimal(voice.savedRevision) && unsignedDecimal(voice.changeRevision) && typeof voice.dirty === "boolean" &&
    !!voice.values && typeof voice.values === "object" && !Array.isArray(voice.values) &&
    Object.entries(voice.values).length <= 32 && Object.entries(voice.values).every(([key, value]) => identity(key) && typeof value === "number" && Number.isFinite(value) && value >= 0 && value <= 1) &&
    Array.isArray(voice.overriddenParameterIds) && voice.overriddenParameterIds.length <= 32 && voice.overriddenParameterIds.every(identity);
}
function validVoiceReply(method: string, value: unknown, args: unknown): boolean {
  if (!["getVoiceLibrary", "getVoice", "setVoiceParameter", "saveVoice", "revertVoice", "setVoiceType"].includes(method)) return true;
  if (!value || typeof value !== "object") return false;
  const reply = value as Record<string, unknown>;
  if (["getVoice", "saveVoice", "revertVoice", "setVoiceType"].includes(method)) return validVoiceDefinition(value) && reply.id === (args as { voiceId: string }).voiceId;
  if (method === "setVoiceParameter") return typeof reply.normalizedValue === "number" && Number.isFinite(reply.normalizedValue) &&
    reply.normalizedValue >= 0 && reply.normalizedValue <= 1 && unsignedDecimal(reply.changeRevision) && typeof reply.dirty === "boolean";
  if (!Array.isArray(reply.parameters) || reply.parameters.length > 32 || !Array.isArray(reply.entries) || reply.entries.length > 513) return false;
  const parameterIds = new Set<string>();
  if (!reply.parameters.every((parameter: Record<string, unknown>) => {
    if (!parameter || !identity(parameter.id) || parameterIds.has(parameter.id)) return false;
    parameterIds.add(parameter.id);
    return typeof parameter.label === "string" && ["st", "s", "ratio", "dB", "pan", "%"].includes(parameter.unit as string) &&
      [parameter.min, parameter.max, parameter.defaultValue].every((number) => typeof number === "number" && Number.isFinite(number)) &&
      (parameter.min as number) < (parameter.max as number) && (parameter.defaultValue as number) >= (parameter.min as number) && (parameter.defaultValue as number) <= (parameter.max as number) &&
      ["linear", "log"].includes(parameter.curve as string) && (parameter.curve !== "log" || (parameter.min as number) > 0) &&
      typeof parameter.proposal === "boolean" && (parameter.step === undefined || typeof parameter.step === "number" && Number.isFinite(parameter.step) && parameter.step > 0);
  })) return false;
  let speciesCount = 0;
  if (!reply.entries.every((entry: Record<string, unknown>) => {
    if (!entry || !identity(entry.groupId) || typeof entry.groupName !== "string" || !(entry.groupVoiceId === null || voiceIdentity(entry.groupVoiceId)) || !Array.isArray(entry.species)) return false;
    speciesCount += entry.species.length;
    return speciesCount <= 512 && entry.species.every((species: Record<string, unknown>) => species && identity(species.speciesId) &&
      typeof species.commonName === "string" && typeof species.scientificName === "string" && (species.voiceId === null || voiceIdentity(species.voiceId)) &&
      Array.isArray(species.individuals) && species.individuals.length <= 512 && species.individuals.every((individual: Record<string, unknown>) =>
        individual && identity(individual.individualId) && typeof individual.label === "string" && (individual.voiceId === null || voiceIdentity(individual.voiceId))));
  })) return false;
  return (reply.types === undefined || Array.isArray(reply.types) && reply.types.length <= 5 && reply.types.every((type: Record<string, unknown>) =>
    type && ["struck", "pluck", "air", "resonance", "bell"].includes(type.id as string) && typeof type.label === "string" && typeof type.proposal === "boolean")) &&
    (reply.maxDefinitions === undefined || Number.isInteger(reply.maxDefinitions) && (reply.maxDefinitions as number) > 0 && (reply.maxDefinitions as number) <= 513) &&
    (reply.editingBehavior === undefined || reply.editingBehavior === "new_notes");
}
function validPlaybackReply(method: string, value: unknown): boolean {
  if (!["getTransport", "setTransport", "prepareReplay", "getAudioState", "setAudioEnabled"].includes(method)) return true;
  if (!value || typeof value !== "object") return false;
  const v = value as Record<string, unknown>;
  if (method === "getTransport" || method === "setTransport") return (v.preparationId === null || identity(v.preparationId)) && unsignedDecimal(v.generation) &&
    ["playing", "paused", "stopped"].includes(v.state as string) && unsignedDecimal(v.positionUs) && unsignedDecimal(v.durationUs) && BigInt(v.positionUs) <= BigInt(v.durationUs);
  if (method === "prepareReplay") return identity(v.preparationId) && unsignedDecimal(v.generation) && unsignedDecimal(v.durationUs) && Number.isInteger(v.eventCount) && (v.eventCount as number) >= 0 && typeof v.peakEventsPerSecond === "number" && Number.isFinite(v.peakEventsPerSecond);
  return ["running", "disabled", "unavailable", "device_lost"].includes(v.status as string) && (v.deviceName === null || typeof v.deviceName === "string") &&
    typeof v.masterGainDb === "number" && Number.isFinite(v.masterGainDb) && (v.peakDbfs === null || (typeof v.peakDbfs === "number" && Number.isFinite(v.peakDbfs))) &&
    typeof v.meterIsSimulated === "boolean" && typeof v.clipping === "boolean" && (v.activeSynthVoices === null || (Number.isInteger(v.activeSynthVoices) && (v.activeSynthVoices as number) >= 0)) && Number.isInteger(v.maxSynthVoices) && (v.maxSynthVoices as number) > 0 &&
    (v.error === undefined || v.error === null || typeof v.error === "string");
}

let requestCounter = 0;
const nextRequestId = () => `ui-${Date.now().toString(36)}-${(requestCounter += 1)}`;

export class HostClient {
  private capabilities: HostCapabilities | null = null;
  private playbackMutation = 0;
  get playbackRevision() { return this.playbackMutation; }

  constructor(private readonly host: ExtendedHost | AtlasHost) {}

  get implementation(): HostCapabilities["implementation"] | "unknown" {
    return this.capabilities?.implementation ?? "unknown";
  }
  get caps(): HostCapabilities | null {
    return this.capabilities;
  }

  /** Fetch capabilities first; nothing else is offered until this succeeds. */
  async connect(signal?: AbortSignal): Promise<HostResult<HostCapabilities>> {
    const result = await this.send("getCapabilities", {} as Record<string, never>, { signal, skipCapabilityCheck: true });
    this.capabilities = null;
    if (result.ok) {
      const value = result.value;
      if (!value || value.protocolVersion !== 1 || !["native", "mock"].includes(value.implementation) ||
          !Array.isArray(value.supportedMethods) || value.supportedMethods.length > 100 ||
          !value.supportedMethods.every((method) => typeof method === "string") || typeof value.audioAvailable !== "boolean" ||
          !Number.isInteger(value.maxEventBatch) || value.maxEventBatch < 1 || value.maxEventBatch > 500 ||
          !Number.isInteger(value.maxVisibleMarkers) || value.maxVisibleMarkers < 1 || value.maxVisibleMarkers > 2000) {
        return { ok: false, error: { code: "unsupported", message: "Invalid or unsupported native capabilities" } };
      }
      this.capabilities = value;
    }
    return result;
  }

  supports(method: MethodName): boolean {
    return (this.capabilities?.supportedMethods as string[] | undefined)?.includes(method) ?? false;
  }

  subscribe(listener: (notification: HostNotification) => void): () => void {
    const host = this.host as ExtendedHost;
    return host.subscribe ? host.subscribe(listener) : () => undefined;
  }

  call<M extends MethodName>(method: M, args: AllMethods[M]["request"], options: { signal?: AbortSignal; timeoutMs?: number } = {}) {
    if (["prepareReplay", "setTransport", "setAudioEnabled"].includes(method)) this.playbackMutation += 1;
    return this.send(method, args, options);
  }

  private async send<M extends MethodName>(
    method: M,
    args: AllMethods[M]["request"],
    options: { signal?: AbortSignal; timeoutMs?: number; skipCapabilityCheck?: boolean },
  ): Promise<HostResult<AllMethods[M]["response"]>> {
    if (!options.skipCapabilityCheck && !this.supports(method)) {
      return { ok: false, error: { code: "unsupported", message: `${method} is not offered by this host` } };
    }
    if (options.signal?.aborted) return { ok: false, error: { code: "aborted", message: "Aborted before sending" } };
    const requestId = nextRequestId();
    const timeoutMs = options.timeoutMs ?? 8000;
    const controller = new AbortController();
    const forwardAbort = () => controller.abort();
    options.signal?.addEventListener("abort", forwardAbort, { once: true });
    let timedOut = false;
    let onAbort: () => void = () => undefined;
    const interrupted = new Promise<HostResult<never>>((resolve) => {
      onAbort = () => resolve({ ok: false, error: {
        code: timedOut ? "timeout" : "aborted",
        message: timedOut ? `${method} did not answer within ${timeoutMs} ms` : "Request cancelled",
      } });
      controller.signal.addEventListener("abort", onAbort, { once: true });
      if (controller.signal.aborted) onAbort();
    });
    const timer = window.setTimeout(() => { timedOut = true; controller.abort(); }, timeoutMs);
    try {
      const request = { protocolVersion: 1 as const, requestId, method, args };
      const reply = (this.host as ExtendedHost).request(request, controller.signal).then((r): HostResult<AllMethods[M]["response"]> => {
        if (!r || typeof r !== "object" || typeof r.ok !== "boolean" || r.requestId !== requestId) return { ok: false, error: { code: "internal", message: "Reply did not match request id" } };
        if (r.ok) return "result" in r && validPlaybackReply(method, r.result) && validVoiceReply(method, r.result, args) ? { ok: true, value: r.result } : { ok: false, error: { code: "internal", message: "Malformed host result" } };
        if (!r.error || typeof r.error.message !== "string" || !["unsupported", "invalid_request", "unavailable", "stale_generation", "internal"].includes(r.error.code))
          return { ok: false, error: { code: "internal", message: "Malformed host error" } };
        return { ok: false, error: r.error };
      });
      const result = await Promise.race([reply, interrupted]);
      if (options.signal?.aborted) return { ok: false, error: { code: "aborted", message: "Superseded by a newer request" } };
      return result;
    } catch (error) {
      return { ok: false, error: { code: "internal", message: error instanceof Error ? error.message : String(error) } };
    } finally {
      controller.signal.removeEventListener("abort", onAbort);
      window.clearTimeout(timer);
      options.signal?.removeEventListener("abort", forwardAbort);
    }
  }
}
