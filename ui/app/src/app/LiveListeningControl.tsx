/** Host owns event consumption and playback; this control sends only explicit enable/disable. */
import { useRef, useState } from "react";
import { Toggle } from "../components/Toggle";
import { Popover } from "../components/Popover";
import { useHost, useHostQuery, valueOf } from "./HostContext";

export function LiveListeningControl() {
  const { client, caps, connection } = useHost();
  const [query, refresh] = useHostQuery("getLiveState", {}, [], { refreshMs: 1000 });
  const state = valueOf(query);
  const pending = useRef(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const enabled = connection === "connected" && query.status === "ready" && state?.enabled === true;
  const change = async (enabled: boolean) => {
    if (pending.current) return;
    pending.current = true;
    setBusy(true); setError(null);
    const reply = await client.call("setLiveEnabled", { enabled });
    if (!reply.ok) setError(reply.error.message);
    refresh(); pending.current = false; setBusy(false);
  };
  const fault = error ?? (query.status === "error" ? query.error.message : null) ?? (state?.status === "error" ? state.error || "Live listening stopped." : null);
  return <>
    <Toggle label={enabled ? "Live listening (on)" : "Live listening (off)"} checked={enabled}
      disabled={busy || connection !== "connected" || !state || (!enabled && (!caps?.audioAvailable || query.status !== "ready"))}
      onChange={value => void change(value)} showState={false} />
    <Popover label="Live listening status" trigger={<span className="small muted">{connection !== "connected" || query.status !== "ready" ? "Live unknown" : enabled ? "Live on" : "Live off"}</span>}>
      {() => <div className="menu"><p className="menu__title">Live listening · {enabled ? "on" : "off"}</p>
        <p className="menu__note">Sounds newly arriving eligible detections through their species voices. Collection continues independently. Existing records and corrections do not sound again.</p>
        <p className="menu__note">{state ? `${state.received} received · ${state.admitted} admitted · ${state.dropped} dropped · ${state.skippedRevisions} revisions skipped` : "Status unavailable"}</p>
      </div>}
    </Popover>
    {fault ? <span className="header-error" role="alert">{fault}</span> : (state?.dropped || state?.skippedRevisions) ? <span className="small" role="status">Live overload · {state.dropped} dropped · {state.skippedRevisions} revisions skipped</span> : null}
  </>;
}
