/** UI-thread adapter: polls bounded host state and serializes explicit setting patches.
 * Failed writes stay visible; never synthesize animal events or enable audio implicitly. */
import { createContext, useContext, useEffect, useRef, useState, type ReactNode } from "react";
import type { CompositionSettings, LiveSettings, LiveListeningState } from "../host/contracts";
import { useHost, useHostQuery, valueOf } from "./HostContext";
interface Editor { ready: boolean; compositionDraft: CompositionSettings | null; editComposition: (patch: Partial<CompositionSettings>) => void; state: LiveListeningState | null; busy: boolean; error: string | null; refresh: () => void; change: (patch: Partial<LiveSettings>) => Promise<boolean> }
const Context = createContext<Editor | null>(null);
export function LiveSettingsProvider({children}: {children: ReactNode}) {
  const {client, connection} = useHost();
  const [query, refresh] = useHostQuery("getLiveState", {}, [], {refreshMs: 1000});
  const [busy, setBusy] = useState(false), [error, setError] = useState<string | null>(null);
  const pending = useRef(false);
  const [compositionDraft, setCompositionDraft] = useState<CompositionSettings | null>(null);
  const acceptedComposition = useRef<string | null>(null);
  const settings = valueOf(query)?.composition;
  useEffect(() => {
    if (!settings) return;
    const next = JSON.stringify(settings);
    if (next !== acceptedComposition.current) { acceptedComposition.current = next; setCompositionDraft(settings); }
  }, [settings]);
  const editComposition = (patch: Partial<CompositionSettings>) => { if (!pending.current) setCompositionDraft(previous => previous ? {...previous, ...patch} : null); };
  const change = async (patch: Partial<LiveSettings>) => {
    if (pending.current || connection !== "connected" || !client.supports("setLiveSettings")) return false;
    pending.current = true; setBusy(true); setError(null);
    try {
      const reply = await client.call("setLiveSettings", patch);
      if (!reply.ok) { setError(reply.error.message); return false; }
      refresh(); return true;
    } finally { pending.current = false; setBusy(false); }
  };
  return <Context.Provider value={{ready: query.status === "ready", compositionDraft, editComposition, state: connection === "connected" ? valueOf(query) : null, busy, error: error ?? (query.status === "error" ? query.error.message : null), refresh, change}}>{children}</Context.Provider>;
}
export function useLiveSettings() { const value = useContext(Context); if (!value) throw Error("LiveSettingsProvider missing"); return value; }
