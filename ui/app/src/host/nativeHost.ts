import type { HostReply } from "./contracts";
import type { ExtendedHost, ExtendedRequest } from "./client";
import type { AllMethods, HostNotification, MethodName } from "./provisional";

declare global {
  interface Window { atlasHost?: ExtendedHost }
}

/** Production receives an injected native transport. HTTP exists only in development. */
export class NativeAtlasHost implements ExtendedHost {
  async request<M extends MethodName>(request: ExtendedRequest<M>, signal?: AbortSignal): Promise<HostReply<AllMethods[M]["response"]>> {
    if (window.atlasHost) return window.atlasHost.request(request, signal);
    if (!import.meta.env.DEV) return { requestId: request.requestId, ok: false,
      error: { code: "unavailable", message: "Native host is not connected. This build does not substitute demo data." } };
    const response = await fetch("/__atlas/request", {
      method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(request),
      signal, credentials: "omit", redirect: "error",
    });
    if (!response.ok) return { requestId: request.requestId, ok: false,
      error: { code: "unavailable", message: "Native data host unavailable. Start the preview with a configured library." } };
    return await response.json() as HostReply<AllMethods[M]["response"]>;
  }
  subscribe(listener: (notification: HostNotification) => void): () => void {
    return window.atlasHost?.subscribe?.(listener) ?? (() => undefined);
  }
}
