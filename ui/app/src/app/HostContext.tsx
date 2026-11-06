/**
 * Provides the single HostClient to the tree, tracks bridge connection state
 * and owns the two bounded event stores (live arrivals and replay/history
 * material). Components never import a host implementation directly.
 */
import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import { HostClient, type ExtendedHost, type HostResult } from "../host/client";
import type { AtlasHost, HostCapabilities } from "../host/contracts";
import type { AllMethods, HostNotification, MethodName } from "../host/provisional";
import type { MockAtlasHost } from "../host/mockHost";
import { EventStore } from "./eventStore";

export type ConnectionState = "connecting" | "connected" | "reconnecting" | "failed";

interface HostContextValue {
  client: HostClient;
  caps: HostCapabilities | null;
  connection: ConnectionState;
  connectionError: string | null;
  mock: MockAtlasHost | null;
  liveStore: EventStore;
  materialStore: EventStore;
  onNotification: (listener: (n: HostNotification) => void) => () => void;
  reconnect: () => void;
}

const Ctx = createContext<HostContextValue | null>(null);

export function HostProvider({ host, children }: { host: ExtendedHost | AtlasHost; children: ReactNode }) {
  const client = useMemo(() => new HostClient(host), [host]);
  const mock = (host as Partial<MockAtlasHost>).isMock ? (host as MockAtlasHost) : null;
  const [caps, setCaps] = useState<HostCapabilities | null>(null);
  const [connection, setConnection] = useState<ConnectionState>("connecting");
  const [connectionError, setConnectionError] = useState<string | null>(null);
  const liveStore = useMemo(() => new EventStore("live"), []);
  const materialStore = useMemo(() => new EventStore("material"), []);
  const listeners = useRef(new Set<(n: HostNotification) => void>());
  const [retry, setRetry] = useState(0);
  const reconnect = useCallback(() => setRetry((value) => value + 1), []);

  useEffect(() => {
    let cancelled = false;
    let inFlight = false;
    let attempts = 0;
    let timer = 0;
    let controller: AbortController | null = null;
    const connect = async () => {
      if (cancelled || inFlight) return;
      inFlight = true;
      controller = new AbortController();
      setCaps(null);
      setConnection(attempts ? "reconnecting" : "connecting");
      const result = await client.connect(controller.signal);
      inFlight = false;
      if (cancelled) return;
      if (result.ok) {
        attempts = 0;
        setCaps(result.value);
        setConnection("connected");
        setConnectionError(null);
      } else {
        attempts += 1;
        setConnectionError(result.error.message);
        setConnection(attempts >= 4 ? "failed" : "reconnecting");
        if (attempts < 4) timer = window.setTimeout(() => void connect(), Math.min(8000, 1000 * 2 ** attempts));
      }
    };
    void connect();
    const unsubscribe = client.subscribe((n) => {
      if (n.type === "connection") {
        window.clearTimeout(timer);
        if (n.state === "connected") void connect();
        else { setCaps(null); setConnection("reconnecting"); timer = window.setTimeout(() => void connect(), 1000); }
      }
      for (const listener of listeners.current) listener(n);
    });
    return () => { cancelled = true; controller?.abort(); window.clearTimeout(timer); unsubscribe(); };
  }, [client, retry]);

  useEffect(() => () => { liveStore.dispose(); materialStore.dispose(); }, [liveStore, materialStore]);

  const onNotification = useCallback((listener: (n: HostNotification) => void) => {
    listeners.current.add(listener);
    return () => { listeners.current.delete(listener); };
  }, []);

  const value = useMemo<HostContextValue>(
    () => ({ client, caps, connection, connectionError, mock, liveStore, materialStore, onNotification, reconnect }),
    [client, caps, connection, connectionError, mock, liveStore, materialStore, onNotification, reconnect],
  );
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}

export function useHost(): HostContextValue {
  const v = useContext(Ctx);
  if (!v) throw new Error("useHost outside HostProvider");
  return v;
}

export type QueryState<T> =
  | { status: "unsupported" }
  | { status: "loading"; previous: T | null }
  | { status: "ready"; value: T }
  | { status: "error"; error: { code: string; message: string }; previous: T | null };

/**
 * Read-only host query with stale-response protection: a newer request aborts
 * the older one, and only the latest reply is applied. `refreshKey` lets
 * callers re-run on notifications without re-creating args.
 */
export function useHostQuery<M extends MethodName>(
  method: M,
  args: AllMethods[M]["request"] | null,
  deps: unknown[],
  options: { refreshMs?: number } = {},
): [QueryState<AllMethods[M]["response"]>, () => void] {
  const { client, caps, connection } = useHost();
  const [state, setState] = useState<QueryState<AllMethods[M]["response"]>>({ status: "loading", previous: null });
  const [tick, setTick] = useState(0);
  const lastValue = useRef<AllMethods[M]["response"] | null>(null);
  const lastKey = useRef("");
  const queryKey = JSON.stringify([method, args]);
  const supported = caps ? client.supports(method) : true;

  useEffect(() => {
    if (lastKey.current !== queryKey) {
      lastKey.current = queryKey;
      lastValue.current = null;
      setState({ status: "loading", previous: null });
    }
    if (!caps || connection !== "connected" || args === null) return;
    if (!supported) { setState({ status: "unsupported" }); return; }
    const controller = new AbortController();
    setState((s) => (s.status === "ready" ? s : { status: "loading", previous: lastValue.current }));
    void client.call(method, args, { signal: controller.signal }).then((result: HostResult<AllMethods[M]["response"]>) => {
      if (controller.signal.aborted) return;
      if (result.ok) { lastValue.current = result.value; setState({ status: "ready", value: result.value }); }
      else if (result.error.code === "unsupported") setState({ status: "unsupported" });
      else setState({ status: "error", error: result.error, previous: lastValue.current });
    });
    return () => controller.abort();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [caps, connection, supported, tick, queryKey, ...deps]);

  useEffect(() => {
    if (!options.refreshMs) return;
    const id = window.setInterval(() => { if (!document.hidden) setTick((t) => t + 1); }, options.refreshMs);
    return () => window.clearInterval(id);
  }, [options.refreshMs]);

  const refresh = useCallback(() => setTick((t) => t + 1), []);
  return [state, refresh];
}

export function valueOf<T>(q: QueryState<T>): T | null {
  if (q.status === "ready") return q.value;
  if (q.status === "loading" || q.status === "error") return q.previous;
  return null;
}
