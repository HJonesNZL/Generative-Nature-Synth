/** Host owns event consumption and playback; this control sends only explicit enable/disable. */
import { useRef, useState } from "react";
import { Toggle } from "../components/Toggle";
import { Popover } from "../components/Popover";
import { useDispatch } from "./store";
import { useLiveSettings } from "./LiveSettingsContext";
import { useLiveCollection } from "./LiveCollectionContext";
import { useHost } from "./HostContext";

export function LiveListeningControl() {
  const { client, caps, connection } = useHost();
  const live = useLiveSettings();
  const {session} = useLiveCollection();
  const needsCollection = caps?.implementation === "native" && client.supports("getLiveSources") && client.supports("getLiveChanges");
  const canStart = !needsCollection || session !== null;
  const state = live.state, refresh = live.refresh;
  const dispatch = useDispatch();
  const pending = useRef(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const enabled = connection === "connected" && live.ready && state?.enabled === true;
  const change = async (enabled: boolean) => {
    if (pending.current || enabled && !canStart) return;
    pending.current = true;
    setBusy(true); setError(null);
    const reply = await client.call("setLiveEnabled", { enabled });
    if (!reply.ok) setError(reply.error.message);
    refresh(); pending.current = false; setBusy(false);
  };
  const fault = error ?? live.error ?? (state?.status === "error" ? state.error || "Live listening stopped." : null);
  return <>
    <Toggle label={enabled ? "Live listening (on)" : "Live listening (off)"} checked={enabled}
      disabled={busy || connection !== "connected" || !state || (!enabled && (!caps?.audioAvailable || !live.ready || !canStart))}
      onChange={value => void change(value)} showState={false} />
    <Popover label="Live listening status" trigger={<span className="small muted">{connection !== "connected" || !live.ready ? "Live unknown" : enabled ? "Live on" : !canStart ? "Start Collect to listen" : "Live off"}</span>}>
      {close => <div className="menu"><p className="menu__title">Live listening · {enabled ? "on" : "off"}</p>
        {client.supports("setLiveSettings") ? <fieldset disabled={live.busy || !live.state?.mode} className="composition-panel__controls">
          <label>Mode <select aria-label="Live mode" value={live.state?.mode ?? "direct"} onChange={e => void live.change({mode: e.target.value as "direct" | "musical" | "smoothed"})}>{caps?.supportsSmoothedLive === true ? <option value="smoothed">Smoothed (recommended)</option> : null}<option value="direct">Direct</option><option value="musical">Musical</option></select></label>
          {state?.mode === "musical" ? <label>Notes per phrase <select aria-label="Live density" value={live.state?.density ?? 1} onChange={e => void live.change({density: Number(e.target.value)})}>{[1,2,3,4,5,6,7,8].map(n => <option key={n} value={n}>{n}</option>)}</select></label> : null}
          <button type="button" className="btn btn--outline btn--sm" onClick={() => {dispatch({type: "screen", screen: "studio"}); close();}}>{state?.mode === "smoothed" ? "Edit live voices & regions" : "Edit live voices, key & regions"}</button>
          <p className="menu__note">{state?.mode === "smoothed" ? "Each eligible detection plays one note as the burst spreads out." : state?.mode === "musical" ? "Groups detections into timed musical phrases." : "Plays eligible detections as they arrive."}</p>
          <p className="menu__note">Changes apply to upcoming observations.</p>
        </fieldset> : null}
        {live.error ? <p role="alert">{live.error}</p> : null}
        <p className="menu__note">Sounds newly arriving eligible detections through their species voices. Collection continues independently. Existing records and corrections do not sound again.</p>
        {state?.mode === "smoothed" ? <p className="menu__note">Pending counts notes already read and waiting to sound. Catching up means more arrivals remain to be read.</p> : null}
        <p className="menu__note">{state ? `${state.received} received · ${state.admitted} ${state.mode === "smoothed" ? "sounded" : "admitted"}${state.mode === "smoothed" && state.pending !== undefined ? ` · ${state.pending} pending` : ""}${state.catchingUp ? " · catching up with arrivals" : ""}${state.grouped !== undefined ? ` · ${state.grouped} grouped` : ""} · ${state.dropped} dropped · ${state.skippedRevisions} revisions skipped` : "Status unavailable"}</p>
      </div>}
    </Popover>
    {fault ? <span className="header-error" role="alert">{fault}</span> : (state?.dropped || state?.skippedRevisions) ? <span className="small" role="status">{state.mode === "smoothed" ? "Live listening" : "Live overload"} · {state.dropped} dropped · {state.skippedRevisions} revisions skipped</span> : null}
  </>;
}
