import { useCallback, useEffect, useRef, useState } from "react";
import { Popover } from "../components/Popover";
import type { AudioDeviceCatalog, AudioDeviceConfiguration, AudioState, HostMethods } from "../host/contracts";
import type { HostResult } from "../host/client";
import { useHost } from "./HostContext";

type DeviceRequest = HostMethods["setAudioDevice"]["request"];

/** The mounted popover owns a bounded catalog and local draft. Only Apply mutates the host. */
export function AudioDeviceSettings({ audio, busy, apply }: {
  audio: AudioState | null; busy: boolean;
  apply: (request: DeviceRequest) => Promise<HostResult<AudioDeviceCatalog>>;
}) {
  const { client } = useHost();
  if (!client.supports("getAudioDevices") || !client.supports("setAudioDevice")) return null;
  return <Popover label="Audio settings" trigger={<span className="btn btn--outline btn--sm">Audio settings</span>}>
    {() => <DevicePreferences audio={audio} busy={busy} apply={apply} />}
  </Popover>;
}

function DevicePreferences({ audio, busy, apply }: {
  audio: AudioState | null; busy: boolean;
  apply: (request: DeviceRequest) => Promise<HostResult<AudioDeviceCatalog>>;
}) {
  const { client, connection } = useHost();
  const [refreshing, setRefreshing] = useState(true);
  const [queryError, setQueryError] = useState<string | null>(null);
  const controller = useRef<AbortController | null>(null);
  const [catalog, setCatalog] = useState<AudioDeviceCatalog | null>(null);
  const [configuration, setConfiguration] = useState<AudioDeviceConfiguration>({ mode: "systemDefault", deviceId: null, bufferFrames: 0 });
  const [message, setMessage] = useState<string | null>(null);
  const [pending, setPending] = useState(false);
  const refresh = useCallback(async (signal?: AbortSignal) => {
    setRefreshing(true); setQueryError(null); setMessage(null);
    const result = await client.call("getAudioDevices", {}, { signal });
    if (signal?.aborted) return;
    if (result.ok) { setCatalog(result.value); setConfiguration(result.value.configuration); }
    else setQueryError(result.error.message);
    setRefreshing(false);
  }, [client]);
  useEffect(() => {
    const pendingRead = new AbortController();
    controller.current = pendingRead;
    void refresh(pendingRead.signal);
    return () => pendingRead.abort();
  }, [refresh]);
  const device = catalog?.devices.find(item => configuration.mode === "explicitDevice" ? item.id === configuration.deviceId : item.isDefault);
  const selectedMissing = configuration.mode === "explicitDevice" && !device;
  const bufferMissing = configuration.bufferFrames !== 0 && !device?.bufferFrames.includes(configuration.bufferFrames);
  const disabled = busy || pending || refreshing || connection !== "connected";
  const current = audio?.status === "running" ? catalog?.active : null;
  const applyDraft = async () => {
    if (!catalog || disabled || !catalog.available || !catalog.devices.length || selectedMissing || bufferMissing) return;
    setPending(true); setMessage(null);
    const result = await apply({ controlRevision: catalog.controlRevision, preparationId: catalog.preparationId, generation: catalog.generation, configuration });
    setPending(false);
    if (result.ok) {
      setCatalog(result.value); setConfiguration(result.value.configuration);
      setMessage("Output applied. Listening is off. Enable Listening, then press Play when ready.");
    } else setMessage(`${result.error.message}${result.error.code === "stale_generation" ? " Refresh devices, then apply again." : ""}`);
  };
  return <div className="menu audio-preferences">
    <p className="menu__title">Audio output</p>
    <p className="menu__note" role="status">{current ? `${current.name} · ${current.sampleRate.toLocaleString()} Hz · ${current.bufferFrames} frames · ${current.outputChannels} outputs` : `Output ${audio?.status === "device_lost" ? "disconnected" : "off"}${audio?.deviceName ? ` · ${audio.deviceName}` : ""}`}</p>
    <label>Output device
      <select aria-label="Output device" disabled={disabled || !catalog} value={configuration.mode === "systemDefault" ? "default" : `device:${configuration.deviceId}`} onChange={event => {
        const value = event.target.value;
        setConfiguration({ mode: value === "default" ? "systemDefault" : "explicitDevice", deviceId: value === "default" ? null : value.slice(7), bufferFrames: 0 });
        setMessage(null);
      }}>
        <option value="default">System default</option>
        {selectedMissing ? <option value={`device:${configuration.deviceId}`}>Selected device unavailable</option> : null}
        {catalog?.devices.map(item => <option key={item.id} value={`device:${item.id}`}>{item.name} ({item.type}){item.isDefault ? " · default" : ""}{item.outputChannels < 2 ? " · stereo unavailable" : ""}</option>)}
      </select>
    </label>
    <label>Buffer size
      <select aria-label="Buffer size" disabled={disabled || !catalog} value={configuration.bufferFrames} onChange={event => { setConfiguration(previous => ({ ...previous, bufferFrames: Number(event.target.value) })); setMessage(null); }}>
        <option value={0}>Device default{device?.defaultBufferFrames ? ` (${device.defaultBufferFrames} frames)` : ""}</option>
        {configuration.bufferFrames !== 0 && !device?.bufferFrames.includes(configuration.bufferFrames) ? <option value={configuration.bufferFrames}>{configuration.bufferFrames} frames (unavailable)</option> : null}
        {device?.bufferFrames.map(frames => <option key={frames} value={frames}>{frames} frames</option>)}
      </select>
    </label>
    <p className="menu__note">Applying stops playback and releases sounding notes. Listening stays off. After a device disconnects, refresh, apply an available output, then enable Listening and press Play.</p>
    {catalog && !catalog.devices.length ? <p className="menu__note">No output devices found. Connect an output and refresh.</p> : null}
    {catalog && !catalog.available ? <p className="menu__error" role="alert">Audio device controls are unavailable in this host.</p> : null}
    {queryError ? <p className="menu__error" role="alert">{queryError}</p> : null}
    {catalog?.error ? <p className="menu__error" role="alert">{catalog.error}</p> : null}
    {audio?.error && audio.error !== catalog?.error ? <p className="menu__error" role="alert">{audio.error}</p> : null}
    {message ? <p className="menu__note" role="status">{message}</p> : null}
    <div className="menu__row">
      <button type="button" className="btn btn--outline btn--sm" disabled={disabled} onClick={() => void refresh(controller.current?.signal)}>{refreshing ? "Refreshing…" : "Refresh devices"}</button>
      <button type="button" className="btn btn--primary btn--sm" disabled={disabled || !catalog?.available || !catalog.devices.length || selectedMissing || bufferMissing || !!queryError} onClick={() => void applyDraft()}>{pending ? "Applying…" : "Apply output"}</button>
    </div>
  </div>;
}
