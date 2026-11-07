/**
 * Persistent header shared by both screens: navigation, material context,
 * transport, collection, listening (audio enable), master level, panic.
 * Transport, collection and listening are deliberately separate controls.
 */
import { useEffect, useRef, useState, type ReactNode } from "react";
import { Icon } from "../components/Icon";
import { Popover } from "../components/Popover";
import { SegmentedControl } from "../components/SegmentedControl";
import { Toggle } from "../components/Toggle";
import type { CollectionState } from "../host/provisional";
import { useHost, useHostQuery, valueOf } from "./HostContext";
import { useAppState, useDispatch, type ViewMode } from "./store";
import { formatTime, formatUs, zoneAbbreviation } from "./time";
import { LiveListeningControl } from "./LiveListeningControl";
import { AudioDeviceSettings } from "./AudioDeviceSettings";
import { LiveSourcesControl } from "./LiveSources";
import { useLiveSettings } from "./LiveSettingsContext";
import type { HostMethods } from "../host/contracts";
import { useCoalescedGesture } from "./useCoalescedGesture";

export function Header({ demoMenu, projectTools }: { demoMenu?: ReactNode; projectTools?: ReactNode }) {
  const state = useAppState();
  const dispatch = useDispatch();
  const { client, caps, onNotification } = useHost();
  const livePlayback = useLiveSettings().state;
  const [audioQ, refreshAudio] = useHostQuery("getAudioState", {}, [state.soundscape.epoch], { refreshMs: livePlayback?.enabled || state.transport.state === "playing" ? 250 : 1000 });
  const nativeLiveSources = caps?.implementation === "native" && client.supports("getLiveSources");
  const [collectionQ, refreshCollection] = useHostQuery("getCollectionState", nativeLiveSources ? null : {}, [], { refreshMs: 1000 });
  const audio = valueOf(audioQ);
  const collection = valueOf(collectionQ);
  const [now, setNow] = useState(() => Date.now());
  const [transportError, setTransportError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [enabledPreparation, setEnabledPreparation] = useState<string | null>(null);
  const modePending = useRef(false);
  const [confirmStop, setConfirmStop] = useState(false);

  const current = useRef(state.transport);
  current.current = state.transport;
  useEffect(() => {
    const id = window.setInterval(() => setNow(Date.now()), 1000);
    return () => window.clearInterval(id);
  }, []);
  useEffect(() => onNotification((n) => {
    if (n.type === "audioStateChanged") refreshAudio();
    if (n.type === "collectionChanged") refreshCollection();
    if (n.type === "transportChanged" && !client.supports("getTransport")) dispatch({ type: "transportAck", preparationId: current.current.preparationId, generation: n.generation, transport: { state: n.state, positionUs: n.positionUs } });
  }), [onNotification, refreshAudio, refreshCollection, dispatch, client]);

  const live = state.mode === "live";
  const canTransport = !live && !!state.transport.preparationId && !state.transport.preparing && !busy && client.supports("setTransport");
  const transport = async (action: "play" | "pause" | "stop" | "panic" | "seek") => {
    setTransportError(null);
    setBusy(true);
    const expected = state.transport;
    // Seeking while sounding is a native two-step transition. End-of-piece
    // replay likewise resets before play; each accepted response supplies the fence.
    let identity = expected;
    if (action === "seek" && client.supports("getTransport")) {
      const status = await client.call("getTransport", {});
      if (!status.ok) { setBusy(false); setTransportError(status.error.message); return; }
      if (current.current.preparationId !== expected.preparationId || status.value.preparationId !== expected.preparationId) { setBusy(false); return; }
      identity = { ...expected, ...status.value };
    }
    if ((action === "seek" && identity.state === "playing") ||
        (action === "play" && identity.state === "stopped" && identity.durationUs && BigInt(identity.positionUs) >= BigInt(identity.durationUs))) {
      const reset = await client.call("setTransport", action === "seek" ? { action: "pause", preparationId: identity.preparationId!, generation: identity.generation } : { action: "stop" });
      if (!reset.ok) {
        // Natural completion may race the pause between the query and command.
        const status = action === "seek" && client.supports("getTransport") ? await client.call("getTransport", {}) : null;
        if (!status?.ok || status.value.preparationId !== expected.preparationId || status.value.state !== "stopped" || status.value.positionUs !== status.value.durationUs) {
          setBusy(false); setTransportError(reset.error.message); return;
        }
        identity = { ...expected, ...status.value };
      } else identity = { ...expected, ...reset.value };
      if (current.current.preparationId !== expected.preparationId) { setBusy(false); return; }
      if (action === "seek" && client.supports("getTransport")) {
        // Pause acceptance precedes the bounded native fade. Seek only after
        // the callback has published paused, with a finite wait and visible failure.
        for (let attempt = 0; identity.state === "playing" && attempt < 25; attempt += 1) {
          await new Promise(resolve => window.setTimeout(resolve, 20));
          if (current.current.preparationId !== expected.preparationId) { setBusy(false); return; }
          const status = await client.call("getTransport", {}, { timeoutMs: 1000 });
          if (!status.ok) { setBusy(false); setTransportError(status.error.message); return; }
          if (status.value.preparationId !== expected.preparationId) { setBusy(false); return; }
          identity = { ...expected, ...status.value };
        }
        if (identity.state !== "paused" && !(identity.state === "stopped" && identity.positionUs === identity.durationUs)) { setBusy(false); setTransportError("Playback has not paused yet. Try Return to start again."); return; }
      }
    }
    const request = action === "panic" || action === "stop" ? { action } : {
      action, preparationId: identity.preparationId!, generation: identity.generation,
      ...(action === "seek" ? { positionUs: "0" } : {}),
    };
    const result = await client.call("setTransport", request);
    setBusy(false);
    if (current.current.preparationId !== expected.preparationId) return;
    if (result.ok && BigInt(result.value.generation) >= BigInt(current.current.generation)) dispatch({ type: "transportAck", preparationId: current.current.preparationId, generation: current.current.generation, transport: result.value });
    else if (!result.ok) setTransportError(result.error.code === "stale_generation" ? "Playback was re-prepared elsewhere. Select the saved piece again." : result.error.message);
    refreshAudio();
  };
  const setAudio = async (enabled: boolean) => {
    setTransportError(null);
    setBusy(true);
    const expected = state.transport;
    const result = await client.call("setAudioEnabled", { enabled });
    if (result.ok && current.current.preparationId === expected.preparationId) setEnabledPreparation(enabled ? expected.preparationId : null);
    if (result.ok && client.supports("getTransport")) {
      // Device enable/disable may reset native transport generation. Synchronise
      // before another Play can carry the old generation into the host.
      const status = await client.call("getTransport", {});
      if (current.current.preparationId === expected.preparationId) {
        if (status.ok && status.value.preparationId === expected.preparationId && BigInt(status.value.generation) >= BigInt(current.current.generation)) {
          dispatch({ type: "transportAck", preparationId: current.current.preparationId, generation: current.current.generation, transport: status.value });
        } else if (!status.ok) setTransportError(status.error.message);
      }
    }
    setBusy(false);
    if (current.current.preparationId === expected.preparationId && !result.ok) setTransportError(result.error.message);
    refreshAudio();
  };

  const setDevice = async (request: HostMethods["setAudioDevice"]["request"]) => {
    setBusy(true); setTransportError(null); setEnabledPreparation(null);
    const expected = current.current;
    try {
      const result = await client.call("setAudioDevice", request, { timeoutMs: 30000 });
      // Opening a device can fail after native output has already been quieted.
      // Always read its resulting fence, and never reinstate an older preparation.
      if (client.supports("getTransport")) {
        const status = await client.call("getTransport", {});
        if (current.current.preparationId === expected.preparationId) {
          if (status.ok && status.value.preparationId === expected.preparationId && BigInt(status.value.generation) >= BigInt(current.current.generation)) {
            dispatch({ type: "transportAck", preparationId: current.current.preparationId, generation: current.current.generation, transport: status.value });
          } else if (!status.ok) setTransportError(status.error.message);
        }
      }
      return result;
    } finally { refreshAudio(); setBusy(false); }
  };

  const gain = useCoalescedGesture((value, gesture) => client.call("setMasterGain", { gainDb: value, gesture }));
  const [gainLocal, setGainLocal] = useState<number | null>(null);
  const gainShown = gainLocal ?? audio?.masterGainDb ?? -18;
  const dragging = useRef(false);
  useEffect(() => {
    setGainLocal(null); setTransportError(null); setEnabledPreparation(null);
    dragging.current = false; gain.resetError();
    // A scene replaces globals and old transient edit/error context together.
  }, [state.soundscape.epoch]);

  const audioOn = audio?.status === "running" && (caps?.implementation !== "native" || (!!state.transport.preparationId && enabledPreparation === state.transport.preparationId));
  const audioUsable = !!caps && (caps.implementation === "mock" || caps.audioAvailable) && (audio?.status === "running" || audio?.status === "disabled" || audio?.status === "device_lost");
  const headerError = transportError || state.transport.error || state.transport.pollError || audio?.error ||
    (audioQ.status === "error" ? audioQ.error.message : null) || gain.error;
  const collecting = collection?.state === "collecting";
  const elapsedMinutes = collection ? Number(BigInt(collection.elapsedUs) / 60_000_000n) : 0;

  return (
    <header className="app-header">
      <span className="wordmark">ANIMAL ATLAS</span>
      <nav aria-label="Screens" className="screen-nav">
        <button type="button" className={`pill${state.screen === "atlas" ? " is-current" : ""}`} aria-current={state.screen === "atlas" ? "page" : undefined} onClick={() => dispatch({ type: "screen", screen: "atlas" })}>
          <Icon name="globe" size={14} /> Atlas
        </button>
        <button type="button" className={`pill${state.screen === "studio" ? " is-current" : ""}`} aria-current={state.screen === "studio" ? "page" : undefined} onClick={() => dispatch({ type: "screen", screen: "studio" })}>
          <Icon name="studio" size={14} /> Sound Studio
        </button>
      </nav>
      {projectTools}
      {caps?.implementation === "mock" ? <span className="badge badge--demo" title="Mock host: deterministic demo data, no audio">DEMO</span> : null}
      <div className="header-spacer" />
      <SegmentedControl<ViewMode>
        label="Material"
        size="sm"
        value={state.mode}
        onChange={(mode) => {
          if (mode === state.mode || modePending.current) return;
          if (state.mode !== "live" || !client.supports("setLiveEnabled")) { dispatch({ type: "mode", mode }); return; }
          modePending.current = true;
          setBusy(true); setTransportError(null);
          void client.call("setLiveEnabled", { enabled: false }).then(reply => {
            modePending.current = false; setBusy(false);
            if (!reply.ok) { setTransportError(reply.error.message); return; }
            if (reply.value.enabled) { setTransportError(reply.value.error || "Live listening has not stopped yet."); return; }
            dispatch({ type: "mode", mode });
          });
        }}
        options={[
          { value: "live", label: "Live", hint: "Newly arriving records" },
          { value: "replay", label: "Replay", hint: "Play a saved piece" },
          { value: "history", label: "History", hint: "Select archived material" },
        ]}
        disabledValues={busy ? ["live", "replay", "history"] : client.supports("queryHistory") ? [] : ["history"]}
      />

      <div className="header-group" role="group" aria-label="Transport">
        <button type="button" className="icon-btn" aria-label="Return to start" disabled={!canTransport} onClick={() => void transport("seek")}>
          <Icon name="to-start" size={13} />
        </button>
        {state.transport.state === "playing" ? (
          <button type="button" className="icon-btn icon-btn--primary" aria-label="Pause playback" disabled={!canTransport} onClick={() => void transport("pause")}><Icon name="pause" size={13} /></button>
        ) : (
          <button type="button" className="icon-btn icon-btn--primary" aria-label={live ? "Play (not available for live arrivals)" : "Play"} disabled={!canTransport || (caps?.implementation === "native" && !audioOn)} onClick={() => void transport("play")}><Icon name="play" size={13} /></button>
        )}
        <button type="button" className="icon-btn" aria-label="Stop playback" disabled={!canTransport} onClick={() => void transport("stop")}><Icon name="stop" size={11} /></button>
        <button type="button" className="btn btn--outline btn--sm" onClick={() => void transport("panic")} disabled={!client.supports("setTransport")} title="Release every sounding note and effect tail now">
          <Icon name="panic" size={13} /> All off
        </button>
      </div>

      <div className="header-group" role="group" aria-label="Collection">
        {nativeLiveSources ? <LiveSourcesControl /> : <Popover
          label={collecting ? "Collection running — options" : "Start collection"}
          trigger={<span className={`collect-chip${collecting ? " is-on" : ""}`}><Icon name="collect" size={14} />{collecting ? `Collecting ${Math.floor(elapsedMinutes / 60)}:${String(elapsedMinutes % 60).padStart(2, "0")}` : "Collect"}</span>}
        >
          {(close) => <CollectionMenu collection={collection} onDone={() => { refreshCollection(); close(); }} confirmStop={confirmStop} setConfirmStop={setConfirmStop} />}
        </Popover>}
      </div>

      <div className="header-clock" aria-live="off">
        {live ? (
          <>
            <span className="mono">{formatTime(now)}</span>
            <span className="label">{zoneAbbreviation(now)} · arrival</span>
          </>
        ) : (
          <>
            <span className="mono">{formatUs(state.transport.positionUs)}</span>
            <span className="label">{state.transport.durationUs ? `of ${formatUs(state.transport.durationUs)}` : "nothing ready"}</span>
          </>
        )}
      </div>

      <div className="header-group master" role="group" aria-label="Output">
        {live && client.supports("getLiveState") && client.supports("setLiveEnabled") ? <LiveListeningControl /> : <Toggle
          label={audioOn ? "Listening (audio on)" : "Listening (audio off)"}
          checked={audioOn}
          disabled={!audioUsable || !client.supports("setAudioEnabled") || busy || (caps?.implementation === "native" && (!state.transport.preparationId || live))}
          onChange={(enabled) => void setAudio(enabled)}
          showState={false}
        />}
        <label className="sr-only" htmlFor="master-gain">Master level</label>
        <input
          id="master-gain"
          type="range"
          min={-60}
          max={0}
          step={0.5}
          value={gainShown}
          disabled={!client.supports("setMasterGain")}
          aria-valuetext={audio ? `${gainShown.toFixed(1)} dB` : "Unavailable"}
          onPointerDown={() => { dragging.current = true; gain.push("begin", gainShown); }}
          onChange={(e) => { const v = Number(e.target.value); setGainLocal(v); if (!dragging.current) gain.push("begin", gainShown); gain.push("change", v); if (!dragging.current) { gain.push("end", v); } }}
          onPointerUp={(e) => { dragging.current = false; gain.push("end", Number((e.target as HTMLInputElement).value)); window.setTimeout(() => setGainLocal(null), 300); }}
          className={`range range--master${client.supports("setMasterGain") ? "" : " is-unavailable"}`}
        />
        <span className="mono master__db">{audio ? `${gainShown.toFixed(1)} dB` : "— dB"}</span>
        <span className="meter" role="img" aria-label={audio?.peakDbfs == null ? "Output level unavailable" : `Output peak ${audio.peakDbfs} dBFS${audio.meterIsSimulated ? " (simulated)" : ""}`}>
          {Array.from({ length: 8 }, (_, i) => {
            const threshold = -48 + i * 6;
            const lit = audio?.peakDbfs != null && audio.peakDbfs >= threshold;
            return <span key={i} className={`meter__seg${lit ? " is-lit" : ""}${i >= 7 ? " is-hot" : ""}`} />;
          })}
          {audio?.meterIsSimulated ? <span className="meter__sim">SIM</span> : null}
        </span>
      </div>

      <AudioDeviceSettings audio={audio} busy={busy} apply={setDevice} />
      {demoMenu}
      {headerError ? <span className="header-error" role="alert">{headerError}</span> : null}
    </header>
  );
}

function CollectionMenu({ collection, onDone, confirmStop, setConfirmStop }: {
  collection: CollectionState | null;
  onDone: () => void;
  confirmStop: boolean;
  setConfirmStop: (v: boolean) => void;
}) {
  const { client } = useHost();
  const [minutes, setMinutes] = useState(240);
  const [listening, setListening] = useState(true);
  const [error, setError] = useState<string | null>(null);
  if (!client.supports("setCollection")) return <div className="menu"><p className="menu__note">This host does not offer collection control.</p></div>;
  if (collection?.state === "collecting") {
    return (
      <div className="menu">
        <p className="menu__title">Collection running</p>
        <p className="menu__note">{collection.recordCount} records · {collection.listening ? "collect and play" : "collect only"}. Stopping keeps every record; playback is unaffected.</p>
        {confirmStop ? (
          <div className="menu__row">
            <button type="button" className="btn btn--warn btn--sm" onClick={async () => { const r = await client.call("setCollection", { action: "stop" }); if (!r.ok) setError(r.error.message); else { setConfirmStop(false); onDone(); } }}>Stop now</button>
            <button type="button" className="btn btn--quiet btn--sm" onClick={() => setConfirmStop(false)}>Keep collecting</button>
          </div>
        ) : (
          <button type="button" className="btn btn--outline btn--sm" onClick={() => setConfirmStop(true)}>Stop collection…</button>
        )}
        {error ? <p className="menu__error" role="alert">{error}</p> : null}
      </div>
    );
  }
  return (
    <div className="menu">
      <p className="menu__title">Start collection</p>
      <SegmentedControl<string> label="Duration" size="sm" value={String(minutes)} onChange={(v) => setMinutes(Number(v))} options={[{ value: "60", label: "1 h" }, { value: "120", label: "2 h" }, { value: "240", label: "4 h" }]} />
      <label className="menu__check"><input type="checkbox" checked={listening} onChange={(e) => setListening(e.target.checked)} /> Play while collecting</label>
      <p className="menu__note">Collection runs in the host and continues if this window closes. It is independent of playback.</p>
      <button type="button" className="btn btn--primary btn--sm" onClick={async () => { const r = await client.call("setCollection", { action: "start", durationMinutes: minutes, listening }); if (!r.ok) setError(r.error.message); else onDone(); }}>Start</button>
      {error ? <p className="menu__error" role="alert">{error}</p> : null}
    </div>
  );
}
