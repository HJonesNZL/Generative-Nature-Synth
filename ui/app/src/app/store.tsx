/**
 * Shared view state for both screens. This is UI selection and temporary
 * edit context only; canonical session/voice/region state belongs to the host.
 * The store lives above the screen switch so changing screens never resets
 * the selection, the live cursor, the piece or the transport.
 */
import { createContext, useContext, useMemo, useReducer, type Dispatch, type ReactNode } from "react";
import type { CoverageInterval, SessionView, TimeBasis } from "../host/contracts";
import type { HistoryFreezeResult } from "../host/provisional";
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
}

export interface AppState {
  screen: Screen;
  mode: ViewMode;
  timeBasis: TimeBasis;
  highlightSpeciesId: string | null;
  selectedEventId: string | null;
  selectedRegionId: string;
  selectedVoiceId: string | null;
  mapView: "globe" | "map2d";
  reducedMotion: boolean;
  history: { draft: HistoryQueryDraft; result: HistoryResult | null; frozen: HistoryFreezeResult | null };
  replay: { session: SessionView | null; speedNumerator: string };
  transport: TransportView;
  globeOrientation: { longitude: number; latitude: number; distance: number };
}

export type Action =
  | { type: "screen"; screen: Screen }
  | { type: "mode"; mode: ViewMode }
  | { type: "timeBasis"; basis: TimeBasis }
  | { type: "highlightSpecies"; speciesId: string | null }
  | { type: "selectEvent"; eventId: string | null }
  | { type: "selectRegion"; regionId: string }
  | { type: "selectVoice"; voiceId: string | null }
  | { type: "mapView"; view: "globe" | "map2d" }
  | { type: "reducedMotion"; value: boolean }
  | { type: "historyDraft"; draft: Partial<HistoryQueryDraft> }
  | { type: "historyResult"; result: HistoryResult | null }
  | { type: "historyFrozen"; frozen: HistoryFreezeResult | null }
  | { type: "replaySession"; session: SessionView | null }
  | { type: "replaySpeed"; speedNumerator: string }
  | { type: "transport"; transport: Partial<TransportView> }
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
    screen: "atlas",
    mode: "live",
    timeBasis: "arrival",
    highlightSpeciesId: demo ? "tui" : null,
    selectedEventId: null,
    selectedRegionId: demo ? "region-central-forest" : "",
    selectedVoiceId: demo ? "voice-tui" : null,
    mapView: "globe",
    reducedMotion: prefersReducedMotion(),
    history: {
      draft: { speciesIds: demo ? ["tui", "korimako"] : [], regionId: demo ? "region-central-forest" : "", date: todayInZone(DISPLAY_TIMEZONE), from: "05:00", to: "08:00", timezone: DISPLAY_TIMEZONE },
      result: null,
      frozen: null,
    },
    replay: { session: null, speedNumerator: "60" },
    transport: { generation: "0", state: "stopped", positionUs: "0", durationUs: null, preparationId: null },
    globeOrientation: { longitude: 172, latitude: -34, distance: 4.1 },
  };
}

function reducer(state: AppState, action: Action): AppState {
  switch (action.type) {
    case "screen": return { ...state, screen: action.screen };
    case "mode":
      // Changing material mode clears event selection (ids belong to different material) but keeps species/region/voice.
      return state.mode === action.mode ? state : { ...state, mode: action.mode, selectedEventId: null, timeBasis: action.mode === "live" ? "arrival" : "observed" };
    case "timeBasis": return { ...state, timeBasis: action.basis };
    case "highlightSpecies": return { ...state, highlightSpeciesId: action.speciesId };
    case "selectEvent": return { ...state, selectedEventId: action.eventId };
    case "selectRegion": return { ...state, selectedRegionId: action.regionId };
    case "selectVoice": return { ...state, selectedVoiceId: action.voiceId };
    case "mapView": return { ...state, mapView: action.view };
    case "reducedMotion": return { ...state, reducedMotion: action.value };
    case "historyDraft": return { ...state, history: { ...state.history, draft: { ...state.history.draft, ...action.draft }, result: null, frozen: null } };
    case "historyResult": return { ...state, history: { ...state.history, result: action.result, frozen: null } };
    case "historyFrozen": return { ...state, history: { ...state.history, frozen: action.frozen } };
    case "replaySession": return { ...state, replay: { ...state.replay, session: action.session }, transport: { ...state.transport, preparationId: null, durationUs: null, state: "stopped", positionUs: "0" } };
    case "replaySpeed": return { ...state, replay: { ...state.replay, speedNumerator: action.speedNumerator } };
    case "transport": return { ...state, transport: { ...state.transport, ...action.transport } };
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
