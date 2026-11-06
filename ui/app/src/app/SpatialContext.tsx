import { createContext, useContext, useEffect, useRef, useState, type ReactNode } from "react";
import type { SpatialState } from "../host/contracts";
import { spatialDraft, spatialFromDraft, type SpatialDraft } from "../host/spatialSettings";
import { recipeStillCurrent } from "./recipeIdentity";
import { useHost, useHostQuery, valueOf } from "./HostContext";
import { useAppState, useDispatch } from "./store";

interface SpatialEditorState {
  draft: SpatialDraft | null; accepted: SpatialState | null; ready: boolean; busy: boolean; dirty: boolean; message: string | null;
  edit: (change: (draft: SpatialDraft) => SpatialDraft) => void;
  apply: () => Promise<void>; refresh: () => void;
}
const Context = createContext<SpatialEditorState | null>(null);
export function SpatialProvider({ children }: { children: ReactNode }) {
  const { client, connection } = useHost();
  const state = useAppState(), dispatch = useDispatch();
  const [query, refresh] = useHostQuery("getSpatial", {}, [state.transport.preparationId, state.soundscape.epoch]);
  const [draft, setDraft] = useState<SpatialDraft | null>(null), [accepted, setAccepted] = useState<SpatialState | null>(null);
  const [message, setMessage] = useState<string | null>(null), [applying, setApplying] = useState(false);
  const refs = useRef({ draft, accepted, epoch: state.soundscape.epoch, loadedId: null as string | null, inFlight: false });
  const current = useRef(state); current.current = state;
  const value = valueOf(query);
  useEffect(() => {
    if (query.status !== "ready" || !value?.preparation || value.preparation.preparationId !== current.current.transport.preparationId) return;
    const previous = refs.current.accepted;
    const preserve = previous && refs.current.epoch === current.current.soundscape.epoch &&
      previous.preparation?.session?.id === value.preparation.session?.id && JSON.stringify(previous.settings) === JSON.stringify(value.settings);
    refs.current.accepted = value; refs.current.epoch = current.current.soundscape.epoch; refs.current.loadedId = value.preparation.preparationId;
    setAccepted(value);
    if (!preserve) { refs.current.draft = spatialDraft(value.settings); setDraft(refs.current.draft); setMessage(null); }
  }, [query, value]);
  const flush = useRef<() => Promise<boolean>>(async () => true);
  flush.current = async () => {
    const previous = refs.current.accepted, draft = refs.current.draft;
    if (!previous || !draft) return true;
    let settings;
    try { settings = spatialFromDraft(draft); } catch (error) { setMessage((error as Error).message); return false; }
    if (JSON.stringify(settings) === JSON.stringify(previous.settings)) return true;
    if (refs.current.inFlight) return false;
    refs.current.inFlight = true; setApplying(true); setMessage(null);
    try {
      const fresh = await client.call("getSpatial", {});
      if (!fresh.ok || !fresh.value.preparation || !recipeStillCurrent(fresh.value, previous)) {
        setMessage(fresh.ok ? "The source piece or spatial recipe changed. Reload its settings before applying." : fresh.error.message); return false;
      }
      const preparation = fresh.value.preparation;
      const result = await client.call("setSpatial", { settings, preparationId: preparation.preparationId, generation: preparation.generation }, { timeoutMs: 30000 });
      if (!result.ok || !result.value.preparation) { setMessage(result.ok ? "Spatial edit did not return a preparation." : result.error.message); return false; }
      refs.current.accepted = result.value; refs.current.draft = spatialDraft(result.value.settings); refs.current.loadedId = result.value.preparation.preparationId;
      setAccepted(result.value); setDraft(refs.current.draft);
      dispatch({ type: "recipePrepared", preparation: result.value.preparation });
      setMessage("Spatial settings applied. Listening is off; enable it and press Play to hear this candidate."); return true;
    } finally { refs.current.inFlight = false; setApplying(false); }
  };
  useEffect(() => client.edits.registerFinalizer(() => flush.current(), 1), [client]);
  const apply = async () => {
    if (!client.edits.begin()) return;
    dispatch({ type: "soundscapeOperation", operation: "spatial" });
    try { if (await client.edits.drain(true, false)) await flush.current(); }
    finally { client.edits.end(); dispatch({ type: "soundscapeOperation", operation: null }); }
  };
  const edit = (change: (draft: SpatialDraft) => SpatialDraft) => {
    if (!refs.current.draft || client.edits.locked) return;
    refs.current.draft = change(refs.current.draft); setDraft(refs.current.draft); setMessage(null);
  };
  const ready = !!draft && query.status === "ready" && refs.current.loadedId === state.transport.preparationId;
  const busy = state.soundscape.operation !== null || connection !== "connected" || applying;
  const dirty = !!draft && !!accepted && JSON.stringify(draft) !== JSON.stringify(spatialDraft(accepted.settings));
  return <Context.Provider value={{ draft, accepted, ready, busy, dirty, message: query.status === "error" ? query.error.message : message, edit, apply, refresh }}>{children}</Context.Provider>;
}
export function useSpatialEditor() {
  const value = useContext(Context); if (!value) throw Error("SpatialProvider missing"); return value;
}
