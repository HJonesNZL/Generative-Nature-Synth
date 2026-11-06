/**
 * Centre of Sound Studio: one saved VOICE DEFINITION (not a sounding synth
 * voice). Parameter edits go to the host as begin/change/end gestures and are
 * shown as unsaved until Save. Undo/redo covers this definition's unsaved
 * edits in this view only; Save or Revert clears it.
 */
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import type { MappingRow, VoiceDefinition, VoiceParameterSpec, VoiceType } from "../../host/provisional";
import { useHost, useHostQuery, valueOf } from "../../app/HostContext";
import { useAppState } from "../../app/store";
import type { Catalog } from "../../app/catalog";
import { formatParameter, fromNormalized, parseParameter } from "../../app/units";
import { useCoalescedGesture } from "../../app/useCoalescedGesture";
import { Icon } from "../../components/Icon";
import { Knob, type Gesture } from "../../components/Knob";
import { EmptyState, Notice } from "../../components/Notice";
import { SegmentedControl } from "../../components/SegmentedControl";
import { Toggle } from "../../components/Toggle";

interface UndoEntry { parameterId: string; before: number; after: number }

export function VoiceEditor({ parameters, catalog, onDirtyChange }: { parameters: VoiceParameterSpec[]; catalog: Catalog; onDirtyChange: (voiceId: string, dirty: boolean) => void }) {
  const { client, onNotification } = useHost();
  const state = useAppState();
  const voiceId = state.selectedVoiceId;
  const [voice, setVoice] = useState<VoiceDefinition | null>(null);
  const [parent, setParent] = useState<VoiceDefinition | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [undo, setUndo] = useState<UndoEntry[]>([]);
  const [redo, setRedo] = useState<UndoEntry[]>([]);
  const [busy, setBusy] = useState<"save" | "revert" | null>(null);
  const [mappingsQ, refreshMappings] = useHostQuery("getMappings", voiceId ? { voiceId } : null, [voiceId]);
  const mappings = valueOf(mappingsQ) ?? [];
  const [audioQ] = useHostQuery("getAudioState", {}, [], { refreshMs: 2000 });
  const audio = valueOf(audioQ);
  const gestureStart = useRef<Record<string, number>>({});

  const requests = useRef<AbortController | null>(null);
  useEffect(() => {
    const controller = new AbortController();
    requests.current = controller;
    setVoice(null);
    setParent(null);
    setError(null);
    setUndo([]);
    setRedo([]);
    if (voiceId) void (async () => {
      const r = await client.call("getVoice", { voiceId }, { signal: controller.signal });
      if (controller.signal.aborted) return;
      if (!r.ok) { setError(r.error.message); return; }
      setVoice(r.value);
      if (r.value.inheritsFromId) {
        const parentReply = await client.call("getVoice", { voiceId: r.value.inheritsFromId }, { signal: controller.signal });
        if (!controller.signal.aborted) setParent(parentReply.ok ? parentReply.value : null);
      }
    })();
    return () => controller.abort();
  }, [client, voiceId]);
  useEffect(() => { if (voice) onDirtyChange(voice.id, voice.dirty); }, [voice, onDirtyChange]);

  const sendParam = useCallback((parameterId: string) => (value: number, gesture: Gesture) => client.call("setVoiceParameter", { voiceId: voiceId!, parameterId, normalizedValue: value, gesture }), [client, voiceId]);
  const senders = useRef(new Map<string, ReturnType<typeof useCoalescedGesture>>());

  // Host automation / restore updates the display only — never re-sent.
  useEffect(() => onNotification((n) => {
    if (n.type !== "voiceParameterChanged" || n.voiceId !== voiceId) return;
    const own = senders.current.get(n.parameterId)?.isOwnEcho(n.changeRevision);
    if (own || n.origin === "ui") return;
    setVoice((v) => (v ? { ...v, values: { ...v.values, [n.parameterId]: n.normalizedValue } } : v));
    setNotice(`${parameters.find((p) => p.id === n.parameterId)?.label ?? n.parameterId} changed by host ${n.origin}.`);
  }), [onNotification, voiceId, parameters]);

  const applyLocal = (parameterId: string, value: number) => setVoice((v) => (v ? { ...v, values: { ...v.values, [parameterId]: value }, dirty: true, overriddenParameterIds: v.inheritsFromId && !v.overriddenParameterIds.includes(parameterId) ? [...v.overriddenParameterIds, parameterId] : v.overriddenParameterIds } : v));

  const replay = (entry: UndoEntry, value: number, s: ReturnType<typeof useCoalescedGesture> | undefined) => {
    if (!s) return;
    s.push("begin", value === entry.before ? entry.after : entry.before);
    s.push("change", value);
    s.push("end", value);
    applyLocal(entry.parameterId, value);
  };

  const save = async () => {
    if (!voice) return;
    const signal = requests.current?.signal;
    setBusy("save");
    const r = await client.call("saveVoice", { voiceId: voice.id, expectedRevision: voice.savedRevision }, { signal });
    if (signal?.aborted) return;
    setBusy(null);
    if (!r.ok) { setError(r.error.code === "stale_generation" ? "Saved elsewhere since you opened it. Revert to load the saved version, then reapply." : r.error.message); return; }
    setVoice(r.value); setUndo([]); setRedo([]); setError(null);
  };
  const revert = async () => {
    if (!voice) return;
    const signal = requests.current?.signal;
    setBusy("revert");
    const r = await client.call("revertVoice", { voiceId: voice.id }, { signal });
    if (signal?.aborted) return;
    setBusy(null);
    if (!r.ok) { setError(r.error.message); return; }
    setVoice(r.value); setUndo([]); setRedo([]); setError(null);
  };
  const setType = async (type: VoiceType) => {
    if (!voice) return;
    const signal = requests.current?.signal;
    const r = await client.call("setVoiceType", { voiceId: voice.id, type }, { signal });
    if (signal?.aborted) return;
    if (r.ok) setVoice(r.value); else setError(r.error.message);
  };
  const toggleMapping = async (row: MappingRow, enabled: boolean) => {
    if (!voice) return;
    const signal = requests.current?.signal;
    const r = await client.call("setMappingEnabled", { voiceId: voice.id, mappingId: row.id, enabled }, { signal });
    if (signal?.aborted) return;
    if (!r.ok) { setError(r.error.message); return; }
    refreshMappings();
    setVoice((v) => (v ? { ...v, dirty: true } : v));
  };

  const owner = useMemo(() => {
    if (!voice) return "";
    if (voice.scope === "group") return `Group default for ${voice.targetId === "forest_bird" ? "forest birds" : voice.targetId === "shark" ? "sharks" : voice.targetId}. Species without their own voice use it.`;
    if (voice.scope === "species") return `${catalog.speciesName(voice.targetId)} species voice · overrides ${parent?.name ?? "group default"} for ${voice.overriddenParameterIds.length} parameter(s).`;
    return `Known individual ${voice.targetId} · overrides ${parent?.name ?? "species/group voice"} for ${voice.overriddenParameterIds.length} parameter(s).`;
  }, [voice, parent, catalog]);

  if (!voiceId) {
    return (
      <section className="studio__center">
        <EmptyState title="No voice selected">Choose a species or group in the library. Species shown as “no voice” need a voice assigned before they can sound.</EmptyState>
      </section>
    );
  }
  if (!voice) {
    return <section className="studio__center">{error ? <Notice tone="problem" title="Voice could not be loaded">{error}</Notice> : <p className="small muted">Loading voice…</p>}</section>;
  }

  const canEdit = client.supports("setVoiceParameter");
  const env = (id: string) => { const spec = parameters.find((p) => p.id === id); return spec ? fromNormalized(spec, voice.values[id] ?? 0) : 0; };

  return (
    <section className="studio__center" aria-labelledby="voice-title">
      <div className="row-between">
        <span className="label">SELECTED VOICE · DEFINITION</span>
        <span className="label">{voice.dirty ? <strong className="warn">Unsaved changes</strong> : `Saved rev ${voice.savedRevision}`}</span>
      </div>
      <div className="row-between">
        <h2 id="voice-title" className="h2 accent">{voice.name}</h2>
        <div className="toolbar" role="toolbar" aria-label="Voice edits">
          <button type="button" className="icon-btn icon-btn--sm" aria-label="Undo" disabled={!undo.length} onClick={() => { const e = undo[undo.length - 1]!; replay(e, e.before, senders.current.get(e.parameterId)); setUndo(undo.slice(0, -1)); setRedo([...redo, e]); }}><Icon name="undo" size={13} /></button>
          <button type="button" className="icon-btn icon-btn--sm" aria-label="Redo" disabled={!redo.length} onClick={() => { const e = redo[redo.length - 1]!; replay(e, e.after, senders.current.get(e.parameterId)); setRedo(redo.slice(0, -1)); setUndo([...undo, e]); }}><Icon name="redo" size={13} /></button>
          <button type="button" className="btn btn--quiet btn--sm" disabled={!voice.dirty || busy !== null || !client.supports("revertVoice")} onClick={() => void revert()}>{busy === "revert" ? "Reverting…" : "Revert"}</button>
          <button type="button" className="btn btn--primary btn--sm" disabled={!voice.dirty || busy !== null || !client.supports("saveVoice")} onClick={() => void save()}>{busy === "save" ? "Saving…" : "Save voice"}</button>
        </div>
      </div>
      <p className="small muted">{owner}</p>
      {!canEdit ? <Notice tone="caution" title="Editing not offered">This host does not accept voice parameter changes yet.</Notice> : null}
      {error ? <Notice tone="problem" title="Voice change not applied">{error}</Notice> : null}
      {notice ? <p className="small" role="status">{notice}</p> : null}

      <div className="row-between">
        <SegmentedControl<VoiceType> label="Voice type" value={voice.type} onChange={(t) => void setType(t)} options={[{ value: "pluck", label: "Pluck" }, { value: "air", label: "Air" }, { value: "resonance", label: "Resonance" }, { value: "bell", label: "Bell" }]} disabledValues={client.supports("setVoiceType") ? [] : ["pluck", "air", "resonance", "bell"]} />
        <span className="small muted" title="Currently sounding synth voices across all definitions">
          {audio ? `${audio.activeSynthVoices} of ${audio.maxSynthVoices} synth voices sounding${audio.meterIsSimulated ? " (simulated)" : ""}` : ""}
        </span>
      </div>

      <Envelope attack={env("attack")} decay={env("decay")} sustain={env("sustain")} release={env("release")} />

      <div className="knob-grid" role="group" aria-label="Voice parameters (proposed ranges)">
        {parameters.map((spec) => (
          <ParameterKnob
            key={`${voice.id}-${spec.id}`}
            spec={spec}
            value={voice.values[spec.id] ?? 0}
            inherited={parent?.values[spec.id]}
            overridden={voice.overriddenParameterIds.includes(spec.id)}
            disabled={!canEdit}
            register={(s) => senders.current.set(spec.id, s)}
            send={sendParam(spec.id)}
            onLocal={(v) => applyLocal(spec.id, v)}
            onGesture={(g, v) => {
              if (g === "begin") gestureStart.current[spec.id] = voice.values[spec.id] ?? 0;
              if (g === "end") {
                const before = gestureStart.current[spec.id] ?? v;
                if (before !== v) { setUndo((u) => [...u.slice(-49), { parameterId: spec.id, before, after: v }]); setRedo([]); }
              }
            }}
          />
        ))}
      </div>
      <p className="small muted">Ranges and curves are proposals for DSP review. • marks a value that overrides the inherited one; Delete resets to the inherited value.</p>

      <div className="mapping">
        <span className="label">CAUSAL MAPPING · EVENT → MUSIC (PROPOSED)</span>
        {mappingsQ.status === "unsupported" ? <Notice tone="caution" title="Mappings unavailable">This host does not expose mappings yet.</Notice> : null}
        <table className="mapping__table">
          <thead><tr><th scope="col">Input</th><th scope="col">Transform</th><th scope="col">Output</th><th scope="col"><span className="sr-only">Enabled</span></th></tr></thead>
          <tbody>
            {mappings.map((row) => (
              <tr key={row.id} className={row.enabled ? "" : "is-off"}>
                <td>{row.input}</td>
                <td className="muted">{row.transform}</td>
                <td>{row.output}</td>
                <td><Toggle label={`${row.input} to ${row.output}`} checked={row.enabled} disabled={!client.supports("setMappingEnabled")} onChange={(v) => void toggleMapping(row, v)} /></td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </section>
  );
}

function ParameterKnob({ spec, value, inherited, overridden, disabled, register, send, onLocal, onGesture }: {
  spec: VoiceParameterSpec;
  value: number;
  inherited: number | undefined;
  overridden: boolean;
  disabled: boolean;
  register: (s: ReturnType<typeof useCoalescedGesture>) => void;
  send: (value: number, gesture: Gesture) => ReturnType<ReturnType<typeof useHost>["client"]["call"]>;
  onLocal: (value: number) => void;
  onGesture: (gesture: Gesture, value: number) => void;
}) {
  const sender = useCoalescedGesture(send as never);
  useEffect(() => { register(sender); }, [register, sender]);
  const defaultValue = inherited ?? (spec.curve === "log" ? Math.log(spec.defaultValue / spec.min) / Math.log(spec.max / spec.min) : (spec.defaultValue - spec.min) / (spec.max - spec.min));
  return (
    <div className="knob-cell">
      <Knob
        label={spec.label}
        value={value}
        defaultValue={defaultValue}
        format={(n) => formatParameter(spec, n)}
        parse={(t) => parseParameter(spec, t)}
        disabled={disabled}
        overridden={overridden}
        pending={sender.pending}
        onGesture={(g, v) => { onGesture(g, v); if (g !== "begin") onLocal(v); sender.push(g, v); }}
      />
      {sender.error ? <span className="small field-error" role="alert">{sender.error}</span> : null}
    </div>
  );
}

function Envelope({ attack, decay, sustain, release }: { attack: number; decay: number; sustain: number; release: number }) {
  const total = attack + decay + 1 + release;
  const x = (t: number) => (t / total) * 580 + 10;
  const y = (level: number) => 110 - level * 96;
  const pts = [[x(0), y(0)], [x(attack), y(1)], [x(attack + decay), y(sustain)], [x(attack + decay + 1), y(sustain)], [x(total), y(0)]];
  const d = pts.map(([px, py], i) => `${i ? "L" : "M"}${px!.toFixed(1)} ${py!.toFixed(1)}`).join("");
  return (
    <figure className="envelope">
      <svg viewBox="0 0 600 124" role="img" aria-label={`Envelope: attack ${attack.toFixed(2)} s, decay ${decay.toFixed(2)} s, sustain ${sustain.toFixed(2)}, release ${release.toFixed(2)} s`}>
        <path d="M10 110H590" className="envelope__axis" />
        <path d={`${d}L${x(total)} 110L10 110Z`} className="envelope__fill" />
        <path d={d} className="envelope__line" />
        {pts.slice(1, 4).map(([px, py], i) => <circle key={i} cx={px} cy={py} r={4} className="envelope__point" />)}
      </svg>
      <figcaption className="label">ENVELOPE (A · D · S · R) — SUSTAIN SEGMENT DRAWN AT 1 S</figcaption>
    </figure>
  );
}
