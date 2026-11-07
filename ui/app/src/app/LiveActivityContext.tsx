/** Single bounded activity poll and clock shared by both projections and ribbon.
 * Only native callback-confirmed tokens enter this cache; collection arrivals,
 * note admission and the UI clock never generate playback activity. */
import { createContext, useContext, useEffect, useMemo, useRef, useState, useSyncExternalStore, type ReactNode } from "react";
import type { LiveActivitySnapshot } from "../host/contracts";
import { useHost } from "./HostContext";
import { useLiveCollection } from "./LiveCollectionContext";
import { useAppState } from "./store";
import { LiveActivityCache, LiveActivityClock } from "./liveActivity";
interface Activity {
  snapshot: LiveActivitySnapshot | null;
  sampleNowUs: () => number;
  clock: LiveActivityClock;
  supported: boolean;
  loading: boolean;
  error: string | null;
  selectedToken: string | null;
  selectToken: (token: string | null) => void;
  panelTab: "playback" | "species" | null;
  setPanelTab: (tab: "playback" | "species") => void;
}
const Context = createContext<Activity | null>(null);
export function LiveActivityProvider({children}: {children: ReactNode}) {
  const {client, caps, connection} = useHost();
  const {session, invalidateView} = useLiveCollection();
  const {mode, reducedMotion} = useAppState();
  const supported = !!caps && client.supports("getLiveActivity");
  const enabled = mode === "live" && connection === "connected" && supported && session !== null;
  const epoch = useMemo(() => ({}), [enabled, session?.id, caps]);
  const clock = useMemo(() => new LiveActivityClock(), []);
  const [result, setResult] = useState<{epoch: object; snapshot: LiveActivitySnapshot} | null>(null);
  const [status, setStatus] = useState<{epoch: object; loading: boolean; error: string | null}>({epoch, loading: false, error: null});
  const [selectedToken, selectToken] = useState<string | null>(null);
  const [panelTab, setPanelTab] = useState<"playback" | "species" | null>(null);
  const fingerprint = useRef("");
  useEffect(() => {
    clock.reset(); fingerprint.current = ""; setResult(null);
    setStatus({epoch, loading: enabled, error: null});
    if (!enabled || !session) return;
    const cache = new LiveActivityCache();
    const controller = new AbortController();
    let cancelled = false, inFlight = false, timer = 0;
    const pull = async () => {
      if (cancelled || inFlight || document.hidden) return;
      inFlight = true;
      try {
        let pages = 0;
        do {
          const reply = await client.call("getLiveActivity", {sessionId: session.id, afterSequence: cache.nextSequence, limit: 250}, {signal: controller.signal});
          if (cancelled) return;
          if (!reply.ok) throw Error(reply.error.message);
          const page = reply.value;
          if (!page.sessionActive || page.sessionId !== session.id) { setResult(null); invalidateView(); return; }
          if (page.reset) fingerprint.current = "";
          cache.apply(page); clock.anchor(page.clockUs);
          if (!page.hasMore) {
            // Journal revision and local expiry define data changes; clock ticks
            // are independent so reduced motion never becomes a 10 Hz render.
            const snapshot = cache.snapshot(page);
            const key = `${page.nextSequence}:${page.dropped}:${snapshot.voices.length}`;
            if (key !== fingerprint.current) { fingerprint.current = key; clock.tick(); setResult({epoch, snapshot}); }
            setStatus(previous => previous.epoch === epoch && !previous.loading && !previous.error ? previous : {epoch, loading: false, error: null});
            break;
          }
          if (++pages >= 64) { timer = window.setTimeout(() => void pull(), 0); return; }
        } while (!cancelled);
      } catch (reason) {
        if (!cancelled) { fingerprint.current = ""; setResult(null); setStatus({epoch, loading: false, error: reason instanceof Error ? reason.message : "Playback activity unavailable"}); }
      } finally { inFlight = false; }
      if (!cancelled) timer = window.setTimeout(() => void pull(), 100);
    };
    const visibility = () => { window.clearTimeout(timer); if (!document.hidden) void pull(); };
    document.addEventListener("visibilitychange", visibility); void pull();
    return () => { cancelled = true; controller.abort(); window.clearTimeout(timer); document.removeEventListener("visibilitychange", visibility); };
  }, [client, enabled, epoch, session?.id, invalidateView, clock]);
  useEffect(() => {
    if (!enabled) return;
    let timer = 0;
    const cadence = () => { window.clearInterval(timer); if (!document.hidden) timer = window.setInterval(clock.tick, reducedMotion ? 1000 : 100); };
    document.addEventListener("visibilitychange", cadence); cadence();
    return () => { window.clearInterval(timer); document.removeEventListener("visibilitychange", cadence); };
  }, [clock, enabled, reducedMotion]);
  const snapshot = enabled && result?.epoch === epoch ? result.snapshot : null;
  const value = useMemo(() => ({snapshot, sampleNowUs: clock.sampleNowUs, clock, supported,
    loading: enabled && status.epoch === epoch && status.loading,
    error: enabled && status.epoch === epoch ? status.error : null,
    selectedToken: snapshot?.voices.some(voice => voice.token === selectedToken) ? selectedToken : null,
    selectToken, panelTab, setPanelTab}), [snapshot, clock, supported, enabled, status, epoch, selectedToken, panelTab]);
  return <Context.Provider value={value}>{children}</Context.Provider>;
}
export function useLiveActivity() { const value = useContext(Context); if (!value) throw Error("LiveActivityProvider missing"); return value; }
export function useLiveActivityNow() { const {clock} = useLiveActivity(); return useSyncExternalStore(clock.subscribe, clock.getSnapshot, clock.getSnapshot); }
