/** One UI-thread collection boundary shared by the live feed and controls.
 * Starts/stops mask the previous view immediately; only confirmed native session
 * receipts admit display data. Failed writes remain visible in the controls. */
import { createContext, useCallback, useContext, useEffect, useRef, useState, type ReactNode } from "react";
import type { HostCapabilities, HostMethods } from "../host/contracts";
import { useHost } from "./HostContext";
type LiveSourceState = HostMethods["getLiveSources"]["response"];
type SourceId = LiveSourceState["sources"][number]["id"];
function useLiveSourceController() {
  const { client, caps, connection } = useHost();
  const confirmedCaps = useRef<HostCapabilities | null>(null);
  const [status, setStatus] = useState<LiveSourceState | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [pending, setPending] = useState(false);
  const [viewBlocked, setViewBlocked] = useState(false);
  const [refresh, setRefresh] = useState(0);
  const generation = useRef(0);
  const readController = useRef<AbortController | null>(null);
  const writing = useRef(false);
  const mounted = useRef(true);
  useEffect(() => { mounted.current = true; return () => { mounted.current = false; ++generation.current; readController.current?.abort(); }; }, []);
  useEffect(() => {
    if (!caps || connection !== "connected" || !client.supports("getLiveSources")) return;
    let stopped = false;
    const pull = async () => {
      if (writing.current || document.hidden) return;
      readController.current?.abort();
      const controller = new AbortController();
      readController.current = controller;
      const requestGeneration = ++generation.current;
      const reply = await client.call("getLiveSources", {}, { signal: controller.signal });
      if (stopped || controller.signal.aborted || requestGeneration !== generation.current) return;
      if (reply.ok) { confirmedCaps.current = caps; setViewBlocked(false); setStatus(reply.value); setError(null); }
      else setError(reply.error.message);
    };
    void pull();
    const timer = window.setInterval(() => void pull(), 5000);
    return () => { stopped = true; readController.current?.abort(); ++generation.current; window.clearInterval(timer); };
  }, [client, caps, connection, refresh]);

  const apply = useCallback(async (sourceIds: SourceId[]) => {
    writing.current = true;
    setViewBlocked(!status?.collecting || sourceIds.length === 0);
    readController.current?.abort();
    const requestGeneration = ++generation.current;
    setPending(true);
    const reply = await client.call("setLiveSources", { sourceIds }, { timeoutMs: 30000 });
    writing.current = false;
    if (mounted.current) {
      setPending(false);
      setViewBlocked(false);
      if (reply.ok && requestGeneration === generation.current) { confirmedCaps.current = caps; setViewBlocked(false); setStatus(reply.value); setError(null); }
    }
    return reply;
  }, [client, caps, status?.collecting]);
  const refreshStatus = useCallback(() => setRefresh(value => value + 1), []);
  const invalidateView = useCallback(() => { setViewBlocked(true); setRefresh(value => value + 1); }, []);
  const confirmed = connection === "connected" && caps !== null && confirmedCaps.current === caps;
  const session = confirmed && !viewBlocked && status?.collecting && status.libraryActive ? status.session ?? null : null;
  return { status: confirmed ? status : null, error, pending, apply, refresh: refreshStatus, invalidateView, connected: connection === "connected", session };
}

const Context = createContext<ReturnType<typeof useLiveSourceController> | null>(null);
export function LiveCollectionProvider({children}: {children: ReactNode}) {
  const collection = useLiveSourceController();
  return <Context.Provider value={collection}>{children}</Context.Provider>;
}
export function useLiveCollection() {
  const value = useContext(Context);
  if (!value) throw Error("LiveCollectionProvider missing");
  return value;
}
