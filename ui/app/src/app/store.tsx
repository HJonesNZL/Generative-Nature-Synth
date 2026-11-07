/**
 * Shared view state for both screens. This is UI selection and temporary
 * edit context only; canonical session/voice/region state belongs to the host.
 * The store lives above the screen switch so changing screens never resets
 * the selection, the live cursor, the piece or the transport.
 */
import { createContext, useContext, useMemo, useReducer, type Dispatch, type ReactNode } from "react";
import type { CoverageInterval, SessionView, TimeBasis, SoundscapeView, HostMethods } from "../host/contracts";
import type { HistoryFreezeResult, RenderJob } from "../host/provisional";
import { playbackContext } from "./playbackContext";
import type { SessionBrowserSnapshot } from "../screens/atlas/sessionBrowserState";
import { DISPLAY_TIMEZONE } from "./time";

export type Screen = "atlas" | "studio";
export type ViewMode = "live" | "replay" | "history";

export interface HistoryQueryDraft {
  speciesIds: string[];
  regionId: string;
  date: string;   // YYYY-MM-DD in `timezone`
  from: string;   // HH:MM
  to: string;     // HH:MM
  timezone: string;
}

export interface HistoryResult {
  selectionId: string;
  status: "ready" | "partial" | "unavailable" | "unknown";
  eventCount: number | null;
  coverage: CoverageInterval[];
  requestedFromUtc: string;
  requestedToUtc: string;
}

export interface TransportView {
  generation: string;
  state: "playing" | "paused" | "stopped";
  positionUs: string;
  durationUs: string | null;
  preparationId: string | null;
  error?: string | null;
  pollError?: string | null;
  preparing?: boolean;
  preparedContext?: string;
}

export interface AppState {
  sessionBrowser: SessionBrowserSnapshot | null;
  soundscape: { active: SoundscapeView | null; operation: "save" | "recall" | "composition" | "spatial" | null; epoch: number };
  screen: Screen;
  mode: ViewMode;
  timeBasis: TimeBasis;
  highlightSpeciesId: string | null;
  selectedEventId: string | null;
  materialReload: number;
  render: { job: RenderJob | null; pending: boolean };
  selectedRegionId: string;
  selectedVoiceId: string | null;
  voiceDrafts: Record<string, { dirty: boolean; changeRevision: string }>;
  mapView: "globe" | "map2d";
  reducedMotion: boolean;
  history: { draft: HistoryQueryDraft; result: HistoryResult | null; frozen: HistoryFreezeResult | null };
  replay: { session: SessionView | null; speedNumerator: string; speedDenominator: string; preparationRevision: number };
  transport: TransportView;
  nzLoupePan: { x: number; y: number };
  globeOrientation: { longitude: number; latitude: number; distance: number };
}

export type Action =
  | { type: "sessionBrowser"; browser: SessionBrowserSnapshot | null }
  | { type: "soundscapeOperation"; operation: "save" | "recall" | "composition" | "spatial" | null }
  | { type: "soundscapeRecalled"; scene: SoundscapeView; preparation: HostMethods["prepareReplay"]["response"] }
  | { type: "screen"; screen: Screen }
  | { type: "mode"; mode: ViewMode }
  | { type: "timeBasis"; basis: TimeBasis }
  | { type: "highlightSpecies"; speciesId: string | null }
  | { type: "selectEvent"; eventId: string | null }
  | { type: "retryMaterial" }
  | { type: "renderPending"; pending: boolean }
  | { type: "renderStarted"; job: RenderJob }
  | { type: "renderJob"; job: RenderJob }
  | { type: "selectRegion"; regionId: string }
  | { type: "selectVoice"; voiceId: string | null }
  | { type: "voiceDirty"; voiceId: string; dirty: boolean; changeRevision: string; authoritative: boolean }
  | { type: "mapView"; view: "globe" | "map2d" }
  | { type: "reducedMotion"; value: boolean }
  | { type: "historyDraft"; draft: Partial<HistoryQueryDraft> }
  | { type: "historyResult"; result: HistoryResult | null }
  | { type: "historyFrozen"; frozen: HistoryFreezeResult | null }
  | { type: "replaySession"; session: SessionView | null }
  | { type: "replaySpeed"; speedNumerator: string }
  | { type: "recipePrepared"; preparation: HostMethods["prepareReplay"]["response"] }
  | { type: "transport"; transport: Partial<TransportView> }
  | { type: "transportAck"; preparationId: string | null; generation: string; transport: Partial<TransportView> }
  | { type: "nzLoupePan"; pan: AppState["nzLoupePan"] }
  | { type: "globeOrientation"; orientation: AppState["globeOrientation"] };

function prefersReducedMotion(): boolean {
  return typeof window !== "undefined" && window.matchMedia?.("(prefers-reduced-motion: reduce)").matches === true;
}

function todayInZone(timeZone: string): string {
  const parts = new Intl.DateTimeFormat("en-CA", { timeZone, year: "numeric", month: "2-digit", day: "2-digit" }).format(new Date());
  return parts; // en-CA yields YYYY-MM-DD
}

export function initialState(demo = false): AppState {
  return {
    sessionBrowser: null,
    soundscape: { active: null, operation: null, epoch: 0 },
    screen: "atlas",
    mode: "live",
    timeBasis: "arrival",
    highlightSpeciesId: demo ? "tui" : null,
    selectedEventId: null,
    materialReload: 0,
    render: { job: null, pending: false },
    selectedRegionId: demo ? "region-central-forest" : "",
    selectedVoiceId: demo ? "voice-tui" : null,
    voiceDrafts: {},
    mapView: "globe",
    reducedMotion: prefersReducedMotion(),
    history: {
      draft: { speciesIds: demo ? ["tui", "korimako"] : [], regionId: demo ? "region-central-forest" : "", date: todayInZone(DISPLAY_TIMEZONE), from: "05:00", to: "08:00", timezone: DISPLAY_TIMEZONE },
      result: null,
      frozen: null,
    },
    replay: { session: null, speedNumerator: "60", speedDenominator: "1", preparationRevision: 0 },
    transport: { generation: "0", state: "stopped", positionUs: "0", durationUs: null, preparationId: null },
    nzLoupePan: { x: 0, y: 0 },
    globeOrientation: { longitude: 172, latitude: -34, distance: 4.1 },
  };
}

function reducer(state: AppState, action: Action): AppState {
  switch (action.type) {
    case "sessionBrowser": return { ...state, sessionBrowser: action.browser };
    case "soundscapeOperation": return { ...state, soundscape: { ...state.soundscape, operation: action.operation } };
    case "soundscapeRecalled": {
      const scene = action.scene, preparation = action.preparation;
      if (!preparation.session || !scene.basis || !scene.speedNumerator || !scene.speedDenominator) return state;
      const revision = state.replay.preparationRevision + 1;
      return { ...state, mode: "replay", timeBasis: scene.basis, selectedEventId: null, selectedVoiceId: null, voiceDrafts: {},
        history: { ...state.history, result: null, frozen: null }, materialReload: state.materialReload + 1,
        soundscape: { active: scene, operation: null, epoch: state.soundscape.epoch + 1 },
        replay: { session: preparation.session, speedNumerator: scene.speedNumerator, speedDenominator: scene.speedDenominator, preparationRevision: revision },
        transport: { preparationId: preparation.preparationId, generation: preparation.generation, durationUs: preparation.durationUs, positionUs: "0", state: "stopped", error: null, preparing: false,
          preparedContext: playbackContext("replay", preparation.session.id, scene.basis, scene.speedNumerator, scene.speedDenominator, revision) } };
    }
    case "screen": return { ...state, screen: action.screen };
    case "mode":
      // Changing material mode clears event selection (ids belong to different material) but keeps species/region/voice.
      return state.mode === action.mode ? state : { ...state, mode: action.mode, selectedEventId: null, timeBasis: action.mode === "live" ? "arrival" : "observed", transport: { ...state.transport, preparationId: null, state: "stopped", positionUs: "0", durationUs: null } };
    case "timeBasis": return { ...state, timeBasis: action.basis, transport: { ...state.transport, preparationId: null, state: "stopped", positionUs: "0", durationUs: null } };
    case "highlightSpecies": return { ...state, highlightSpeciesId: action.speciesId };
    case "renderPending": return { ...state, render: { ...state.render, pending: action.pending } };
    case "renderStarted": return { ...state, render: { ...state.render, job: action.job } };
    case "renderJob": {
      const previous = state.render.job;
      if (!previous || previous.jobId !== action.job.jobId) return state;
      const terminal = ["complete", "failed", "cancelled"].includes(previous.state);
      if (terminal && action.job.state !== previous.state) return state;
      return { ...state, render: { ...state.render, job: action.job } };
    }
    case "retryMaterial": return { ...state, materialReload: state.materialReload + 1 };
    case "selectEvent": return { ...state, selectedEventId: action.eventId };
    case "selectRegion": return { ...state, selectedRegionId: action.regionId };
    case "selectVoice": return { ...state, selectedVoiceId: action.voiceId };
    case "voiceDirty": {
      const previous = state.voiceDrafts[action.voiceId];
      if (previous && (!action.authoritative && BigInt(action.changeRevision) < BigInt(previous.changeRevision) ||
          previous.dirty === action.dirty && previous.changeRevision === action.changeRevision)) return state;
      if (!previous && Object.keys(state.voiceDrafts).length >= 513) return state;
      return { ...state, voiceDrafts: { ...state.voiceDrafts, [action.voiceId]: { dirty: action.dirty, changeRevision: action.changeRevision } } };
    }
    case "mapView": return { ...state, mapView: action.view };
    case "reducedMotion": return { ...state, reducedMotion: action.value };
    case "historyDraft": return { ...state, history: { ...state.history, draft: { ...state.history.draft, ...action.draft }, result: null, frozen: null } };
    case "historyResult": return { ...state, history: { ...state.history, result: action.result, frozen: null } };
    case "historyFrozen": return { ...state, history: { ...state.history, frozen: action.frozen } };
    case "replaySession": return { ...state, replay: { ...state.replay, session: action.session, preparationRevision: state.replay.preparationRevision + 1 }, transport: { ...state.transport, preparationId: null, durationUs: null, state: "stopped", positionUs: "0" } };
    case "replaySpeed": return { ...state, replay: { ...state.replay, speedNumerator: action.speedNumerator, speedDenominator: "1" }, transport: { ...state.transport, preparationId: null, state: "stopped", positionUs: "0", durationUs: null } };
    case "recipePrepared": {
      const preparation = action.preparation;
      if (state.mode !== "replay" || !preparation.session || state.replay.session?.id !== preparation.session.id) return state;
      return { ...state, transport: { ...state.transport, preparationId: preparation.preparationId, generation: preparation.generation,
        durationUs: preparation.durationUs, positionUs: "0", state: "stopped", error: null, preparing: false } };
    }
    case "transportAck":
      if (state.transport.preparationId !== action.preparationId || state.transport.generation !== action.generation) return state;
      return { ...state, transport: { ...state.transport, ...action.transport } };
    case "transport": return { ...state, transport: { ...state.transport, ...action.transport } };
    case "nzLoupePan": return { ...state, nzLoupePan: action.pan };
    case "globeOrientation": return { ...state, globeOrientation: action.orientation };
    default: return state;
  }
}

const StateContext = createContext<AppState | null>(null);
const DispatchContext = createContext<Dispatch<Action> | null>(null);

export function AppStateProvider({ children, demo = false }: { children: ReactNode; demo?: boolean }) {
  const [state, dispatch] = useReducer(reducer, undefined, () => initialState(demo));
  const stableDispatch = useMemo(() => dispatch, []);
  return (
    <DispatchContext.Provider value={stableDispatch}>
      <StateContext.Provider value={state}>{children}</StateContext.Provider>
    </DispatchContext.Provider>
  );
}

export function useAppState(): AppState {
  const s = useContext(StateContext);
  if (!s) throw new Error("useAppState outside provider");
  return s;
}
export function useDispatch(): Dispatch<Action> {
  const d = useContext(DispatchContext);
  if (!d) throw new Error("useDispatch outside provider");
  return d;
}
