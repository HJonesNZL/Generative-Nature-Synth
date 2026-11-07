import { useRef, type CSSProperties, type KeyboardEvent } from "react";
import type { LiveActivityVoice } from "../../host/contracts";
import { useLiveActivity, useLiveActivityNow } from "../../app/LiveActivityContext";
import { useLiveCollection } from "../../app/LiveCollectionContext";
import { activityAccuracy, activityColour, activityIsSounding, activityLocation, activitySpeciesName, activityVoiceName } from "../../app/liveActivity";
import { useDispatch } from "../../app/store";
import { ribbonInterval, ribbonLanes } from "./ribbonLayout";
import type { MaterialView } from "./useMaterialView";
import "./TimeRibbon.css";
export function TimeRibbon({view}: {view: MaterialView}) {
  const activity = useLiveActivity();
  const nowUs = useLiveActivityNow();
  const {session} = useLiveCollection();
  const dispatch = useDispatch();
  const noteButtons = useRef(new Map<string, HTMLButtonElement>());
  const voices = activity.snapshot?.voices ?? [];
  const sounding = voices.filter(voice => activityIsSounding(voice, nowUs));
  const lanes = ribbonLanes(voices, nowUs);
  const selected = voices.find(voice => voice.token === activity.selectedToken);
  const selectedLane = selected ? lanes.find(lane => lane.voices.some(voice => voice.token === selected.token)) : null;
  const select = (voice: LiveActivityVoice) => {
    activity.selectToken(voice.token);
    if (view.allEvents.some(event => event.id === voice.eventId)) dispatch({type: "selectEvent", eventId: voice.eventId});
  };
  const accuracyDetail = (voice: LiveActivityVoice) => voice.position?.uncertaintyMetres == null ? ` · ${activityAccuracy(voice)}` : "";
  const description = (voice: LiveActivityVoice) => `${activitySpeciesName(voice)} · ${activityVoiceName(voice)} · ${activityLocation(voice)}${accuracyDetail(voice)}${voice.stationId ? ` · ${voice.stationId}` : ""}`;
  const stateLabel = (voice: LiveActivityVoice) => activityIsSounding(voice, nowUs) ? "Sounding" : voice.endUs === null ? "Onset confirmed" : `Ended ${Math.max(0, (nowUs - Number(voice.endUs)) / 1_000_000).toFixed(1)} seconds ago`;
  const navigate = (event: KeyboardEvent<HTMLButtonElement>, lane: LiveActivityVoice[], voice: LiveActivityVoice) => {
    const at = lane.findIndex(note => note.token === voice.token);
    const next = event.key === "Home" ? 0 : event.key === "End" ? lane.length - 1 : event.key === "ArrowLeft" ? Math.max(0, at - 1) : event.key === "ArrowRight" ? Math.min(lane.length - 1, at + 1) : null;
    if (next === null) return;
    event.preventDefault(); const note = lane[next]!; select(note); noteButtons.current.get(note.token)?.focus();
  };
  return <section className="time-ribbon" aria-label="Confirmed playback, last 30 seconds">
    <div className="time-ribbon__head">
      <div><strong>Last 30 seconds</strong><span className="small muted">Actual notes · elapsed time</span></div>
      <div className="time-ribbon__now"><strong>Now{activity.snapshot ? ` · ${sounding.length}` : ""}</strong>
        <div className="time-ribbon__voices" role="list" aria-label="Now sounding">
          {sounding.length ? sounding.map(voice => <div role="listitem" key={voice.token}><button type="button" className={`time-ribbon__voice${voice.token === activity.selectedToken ? " is-selected" : ""}`} onClick={() => select(voice)} aria-label={description(voice)} aria-pressed={voice.token === activity.selectedToken} title={description(voice)} style={{"--note-color": activityColour(voice)} as CSSProperties}>
            <span><b>{activitySpeciesName(voice)}</b> · {activityVoiceName(voice)}</span><span className="small">{activityLocation(voice)}</span>
          </button></div>) : <span className="small muted">{!session ? "Start Collect to see playback." : !activity.supported ? "This host does not report actual playback." : activity.loading ? "Reading playback…" : "No notes sounding"}</span>}
        </div>
      </div>
    </div>
    {activity.error ? <p className="small is-problem" role="alert">Playback activity: {activity.error}</p> : null}
    {activity.snapshot?.dropped ? <p className="small is-caution" role="status">Playback history limit · {activity.snapshot.dropped} updates omitted</p> : null}
    <div className="time-ribbon__history" role="region" aria-label="Playback history, last 30 seconds" tabIndex={0}>
      <div className="time-ribbon__rows">
        {lanes.length ? lanes.map(lane => <div className="time-ribbon__lane" key={lane.key}>
          <button type="button" className="time-ribbon__origin" title={`${lane.name} · ${lane.location} · ${activityVoiceName(lane.voices[0]!)}`} onClick={() => select(lane.voices.at(-1)!)}>
            <b>{lane.name}</b><span>{lane.voices.length} {lane.voices.length === 1 ? "note" : "notes"} · {lane.location} · {activityVoiceName(lane.voices[0]!)}</span>
          </button>
          <div className="time-ribbon__track" role="group" aria-label={`${lane.name} notes; use left and right arrows to browse`}>
            {lane.voices.map((voice, i) => {
              const interval = ribbonInterval(voice, nowUs);
              return <button type="button" key={voice.token} data-activity-token={voice.token} data-event-id={voice.eventId} data-species-name={activitySpeciesName(voice)} data-voice-name={activityVoiceName(voice)} data-location-label={activityLocation(voice)} ref={node => { if (node) noteButtons.current.set(voice.token, node); else noteButtons.current.delete(voice.token); }}
                className={`time-ribbon__note${interval.active ? " is-sounding" : ""}${voice.token === activity.selectedToken ? " is-selected" : ""}`}
                tabIndex={voice.token === activity.selectedToken || !lane.voices.some(note => note.token === activity.selectedToken) && i === lane.voices.length - 1 ? 0 : -1}
                style={{left: `${interval.start}%`, width: `${interval.width}%`, "--note-color": activityColour(voice)} as CSSProperties}
                aria-label={`${description(voice)} · ${stateLabel(voice)}`} aria-pressed={voice.token === activity.selectedToken}
                title={`${description(voice)} · ${stateLabel(voice)}`} onClick={() => select(voice)} onKeyDown={event => navigate(event, lane.voices, voice)}><span aria-hidden="true" /></button>;
            })}
          </div>
        </div>) : <p className="time-ribbon__empty small muted">{activity.loading ? "Reading confirmed playback…" : "Notes appear only when the native audio engine confirms their onset."}</p>}
      </div>
    </div>
    <div className="time-ribbon__axis" aria-hidden="true"><span>30s ago</span><span>20s</span><span>10s</span><span>Now</span></div>
    {selected ? <div className="time-ribbon__selection small" aria-label="Selected playback note"><span><b>{activitySpeciesName(selected)}</b> · {activityVoiceName(selected)} · {activityLocation(selected)} · {stateLabel(selected)}{accuracyDetail(selected)}{selected.stationId ? ` · ${selected.stationId}` : ""}</span>{selectedLane && selectedLane.voices.length > 1 ? <select aria-label="Choose playback note" value={selected.token} onChange={event => { const note = selectedLane.voices.find(voice => voice.token === event.target.value); if (note) select(note); }}>{selectedLane.voices.map((voice, index) => <option key={voice.token} value={voice.token}>Note {index + 1} of {selectedLane.voices.length} · {Math.max(0, (nowUs - Number(voice.onsetUs)) / 1_000_000).toFixed(1)}s ago</option>)}</select> : null}<button type="button" className="btn btn--quiet btn--sm" aria-label="Clear selected playback note" onClick={() => activity.selectToken(null)}>×</button></div> : null}
  </section>;
}
