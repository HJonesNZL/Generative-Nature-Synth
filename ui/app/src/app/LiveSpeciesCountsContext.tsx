import { createContext, useContext, useEffect, useState, type ReactNode } from "react";
import { useHost } from "./HostContext";
import { useLiveCollection } from "./LiveCollectionContext";
import { collectSpeciesPages, type LiveSpeciesCount } from "./liveSpeciesRanking";
interface SpeciesRanking {
  rows: LiveSpeciesCount[] | null;
  loading: boolean;
  error: string | null;
  supported: boolean;
  sessionId: string | null;
}
const Context = createContext<SpeciesRanking | null>(null);
export function LiveSpeciesCountsProvider({ children }: { children: ReactNode }) {
  const { client, caps, connection } = useHost();
  const { session, invalidateView } = useLiveCollection();
  const sessionId = session?.id ?? null;
  const supported = !!caps && client.supports("getLiveSpeciesCounts");
  const [result, setResult] = useState<{ sessionId: string; rows: LiveSpeciesCount[] } | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  useEffect(() => {
    setResult(null); setError(null); setLoading(false);
    if (!sessionId || !supported || connection !== "connected") return;
    let cancelled = false;
    let timer = 0;
    const controller = new AbortController();
    const pull = async () => {
      if (document.hidden) { timer = window.setTimeout(() => void pull(), 1000); return; }
      setLoading(true);
      try {
        const rows = await collectSpeciesPages(sessionId, async (cursor) => {
          const reply = await client.call("getLiveSpeciesCounts", { sessionId, limit: 250, ...(cursor === undefined ? {} : { cursor }) }, { signal: controller.signal });
          if (!reply.ok) {
            if (!cancelled && reply.error.code === "internal") invalidateView();
            throw Error(reply.error.message);
          }
          if (!cancelled && (!reply.value.sessionActive || reply.value.sessionId !== sessionId)) { setResult(null); invalidateView(); }
          return reply.value;
        }, () => cancelled);
        if (cancelled) return;
        if (rows !== null) { setResult({ sessionId, rows }); setError(null); }
        else setResult(null);
      } catch (reason) {
        if (!cancelled) { setResult(null); setError(reason instanceof Error ? reason.message : "Species counts unavailable"); }
      }
      if (!cancelled) { setLoading(false); timer = window.setTimeout(() => void pull(), 1000); }
    };
    void pull();
    return () => { cancelled = true; controller.abort(); window.clearTimeout(timer); };
  }, [client, supported, connection, sessionId, invalidateView]);
  const rows = result?.sessionId === sessionId ? result.rows : null;
  return <Context.Provider value={{ rows, loading, error, supported, sessionId }}>{children}</Context.Provider>;
}
export function useLiveSpeciesCounts() {
  const value = useContext(Context);
  if (!value) throw Error("LiveSpeciesCountsProvider missing");
  return value;
}
