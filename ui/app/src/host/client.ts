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

let requestCounter = 0;
const nextRequestId = () => `ui-${Date.now().toString(36)}-${(requestCounter += 1)}`;

export class HostClient {
  private capabilities: HostCapabilities | null = null;

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
        if (r.ok) return "result" in r ? { ok: true, value: r.result } : { ok: false, error: { code: "internal", message: "Reply has no result" } };
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
