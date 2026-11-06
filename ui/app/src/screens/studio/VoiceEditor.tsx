/** Sound Studio edits one host-owned definition. Only acknowledged replies
 * change its dirty/revision state. Parameter queues drain before persistence
 * and undo; replies are scoped to the mounted voice. UI history is bounded to
 * 50 edits and is cleared after successful Save/Revert. No browser preset copy. */
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import type { MappingRow, VoiceDefinition, VoiceParameterSpec, VoiceType, VoiceTypeSpec } from "../../host/provisional";
import { useHost, useHostQuery, valueOf } from "../../app/HostContext";
import { useAppState } from "../../app/store";
import type { Catalog } from "../../app/catalog";
import { formatParameter, fromNormalized, parseParameter, toNormalized } from "../../app/units";
import { useCoalescedGesture } from "../../app/useCoalescedGesture";
import type { HostResult } from "../../host/client";
import { Icon } from "../../components/Icon";
import { Knob, type Gesture } from "../../components/Knob";
import { EmptyState, Notice } from "../../components/Notice";
import { SegmentedControl } from "../../components/SegmentedControl";
import { Toggle } from "../../components/Toggle";

interface UndoEntry { parameterId: string; before: number; after: number }
type ParameterReply = { normalizedValue: number; changeRevision: string; dirty: boolean };
type ParameterSender = ReturnType<typeof useCoalescedGesture>;
const demoTypes: VoiceTypeSpec[] = [
  { id: "pluck", label: "Pluck", proposal: true }, { id: "air", label: "Air", proposal: true },
  { id: "resonance", label: "Resonance", proposal: true }, { id: "bell", label: "Bell", proposal: true },
];

export function VoiceEditor({ parameters, types, editingBehavior, catalog, onDirtyChange }: {
  parameters: VoiceParameterSpec[]; types?: VoiceTypeSpec[]; editingBehavior?: "new_notes";
  catalog: Catalog; onDirtyChange: (voiceId: string, dirty: boolean, changeRevision: string, authoritative?: boolean) => void;
}) {
  const { client, onNotification } = useHost();
  const voiceId = useAppState().selectedVoiceId;
  const [voice, setVoice] = useState<VoiceDefinition | null>(null);
  const current = useRef<VoiceDefinition | null>(null);
  const alive = useRef(true);
  const [parent, setParent] = useState<VoiceDefinition | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [presetId, setPresetId] = useState("");
  const [presetsQ] = useHostQuery("listVoicePresets", voiceId ? { voiceId } : null, [voiceId]);
  const presets = valueOf(presetsQ)?.items ?? [];
  const selectedPreset = presets.find(preset => preset.id === presetId);
  const [undo, setUndo] = useState<UndoEntry[]>([]);
  const [redo, setRedo] = useState<UndoEntry[]>([]);
  const [busy, setBusy] = useState<string | null>(null);
  const busyRef = useRef(false);
  const [pendingParameters, setPendingParameters] = useState<Set<string>>(new Set());
  const [failedParameters, setFailedParameters] = useState<Set<string>>(new Set());
  const senders = useRef(new Map<string, ParameterSender>());
  const undoRef = useRef<UndoEntry[]>([]);
  const redoRef = useRef<UndoEntry[]>([]);
  const replaying = useRef(false);
  const gestureHistory = useRef(new Map<string, { before: number; failed: boolean; replay: boolean }>());
  const activity = useRef(0);
  const inFlightParameters = useRef(0);
  const readSequence = useRef(0);
  const definitionEpoch = useRef(0);
  const recoveryRequired = useRef(false);
  const [needsRecovery, setNeedsRecovery] = useState(false);
  const parameterRevisions = useRef(new Map<string, bigint>());
  const history = useCallback((nextUndo: UndoEntry[], nextRedo: UndoEntry[]) => {
    undoRef.current = nextUndo; redoRef.current = nextRedo;
    setUndo(nextUndo); setRedo(nextRedo);
  }, []);
  const [mappingsQ, refreshMappings] = useHostQuery("getMappings", voiceId ? { voiceId } : null, [voiceId]);
  const mappings = valueOf(mappingsQ) ?? [];
  const [audioQ] = useHostQuery("getAudioState", {}, [], { refreshMs: 2000 });
  const audio = valueOf(audioQ);

  const accept = useCallback((next: VoiceDefinition, expectedActivity?: number, expectedRead?: number) => {
    if (!alive.current || next.id !== voiceId) return;
    if (inFlightParameters.current > 0 || expectedActivity !== undefined && expectedActivity !== activity.current ||
        expectedRead !== undefined && expectedRead !== readSequence.current) return;
    readSequence.current += 1;
    definitionEpoch.current += 1;
    parameterRevisions.current.clear();
    onDirtyChange(next.id, next.dirty, next.changeRevision, true);
    for (const id of Object.keys(next.values)) parameterRevisions.current.set(id, BigInt(next.changeRevision));
    current.current = next;
    setVoice(next);
  }, [voiceId, onDirtyChange]);
  useEffect(() => {
    alive.current = true;
    const controller = new AbortController();
    current.current = null;
    setVoice(null); setParent(null); setError(null); history([], []);
    if (voiceId) void (async () => {
      const expectedActivity = activity.current;
      const expectedRead = ++readSequence.current;
      const reply = await client.call("getVoice", { voiceId }, { signal: controller.signal });
      if (controller.signal.aborted || !alive.current) return;
      if (!reply.ok) { setError(reply.error.message); return; }
      accept(reply.value, expectedActivity, expectedRead);
      if (reply.value.inheritsFromId) {
        const result = await client.call("getVoice", { voiceId: reply.value.inheritsFromId }, { signal: controller.signal });
        if (!controller.signal.aborted && alive.current) setParent(result.ok ? result.value : null);
      }
    })();
    return () => {
      alive.current = false;
      controller.abort();
      // Let old-voice mutations finish and close their gestures. Cancelling an
      // audible mutation would retire the bridge worker and lose its drafts.
      for (const sender of senders.current.values()) void sender.flush();
    };
  }, [client, voiceId, accept, history]);
  useEffect(() => { if (voice) onDirtyChange(voice.id, voice.dirty, voice.changeRevision); }, [voice, onDirtyChange]);

  const sendParam = useCallback((parameterId: string) => async (value: number, gesture: Gesture) => {
    const suppressHistory = replaying.current;
    const epoch = definitionEpoch.current;
    activity.current += 1;
    inFlightParameters.current += 1;
    const reply = await client.call("setVoiceParameter", { voiceId: voiceId!, parameterId, normalizedValue: value, gesture });
    activity.current += 1;
    inFlightParameters.current -= 1;
    if (epoch !== definitionEpoch.current) return reply;
    if (reply.ok) onDirtyChange(voiceId!, reply.value.dirty, reply.value.changeRevision);
    else if (alive.current) { recoveryRequired.current = true; setNeedsRecovery(true); }
    const previous = current.current;
    if (!alive.current || previous?.id !== voiceId) return reply;
    if (gesture === "begin") {
      if (reply.ok) gestureHistory.current.set(parameterId, { before: reply.value.normalizedValue, failed: false, replay: suppressHistory });
      else gestureHistory.current.delete(parameterId);
    }
    const edit = gestureHistory.current.get(parameterId);
    if (!reply.ok && edit) edit.failed = true;
    if (reply.ok) {
      const revision = BigInt(reply.value.changeRevision);
      const newest = revision >= BigInt(previous.changeRevision);
      const parameterNewest = revision >= (parameterRevisions.current.get(parameterId) ?? 0n);
      if (parameterNewest) parameterRevisions.current.set(parameterId, revision);
      const next = { ...previous,
        values: parameterNewest ? { ...previous.values, [parameterId]: reply.value.normalizedValue } : previous.values,
        dirty: newest ? reply.value.dirty : previous.dirty,
        changeRevision: newest ? reply.value.changeRevision : previous.changeRevision,
        overriddenParameterIds: previous.inheritsFromId && !previous.overriddenParameterIds.includes(parameterId)
          ? [...previous.overriddenParameterIds, parameterId] : previous.overriddenParameterIds };
      current.current = next; setVoice(next);
      if (gesture === "end" && edit && !edit.failed && !edit.replay && edit.before !== reply.value.normalizedValue) {
        history([...undoRef.current.slice(-49), { parameterId, before: edit.before, after: reply.value.normalizedValue }], []);
      }
    }
    if (gesture === "end") gestureHistory.current.delete(parameterId);
    return reply;
  }, [client, voiceId, onDirtyChange, history]);

  useEffect(() => onNotification((notification) => {
    if (notification.type !== "voiceParameterChanged" || notification.voiceId !== voiceId || !alive.current) return;
    if (senders.current.get(notification.parameterId)?.isOwnEcho(notification.changeRevision) || notification.origin === "ui") return;
    // A full authoritative refresh includes dirty and saved revision metadata.
    const expectedActivity = activity.current;
    const expectedRead = ++readSequence.current;
    void client.call("getVoice", { voiceId: notification.voiceId }).then((reply) => {
      if (reply.ok) accept(reply.value, expectedActivity, expectedRead);
    });
    setNotice(`${parameters.find((p) => p.id === notification.parameterId)?.label ?? notification.parameterId} changed by host ${notification.origin}.`);
  }), [onNotification, voiceId, parameters, client, accept]);

  const flush = async (reportFailure = true) => {
    const replies = await Promise.all([...senders.current.values()].map((sender) => sender.flush()));
    if (!alive.current) return false;
    if (replies.some((ok) => !ok)) {
      if (reportFailure) setError("A parameter change failed. Its last acknowledged value is shown. Choose Revert to reload the saved definition before saving.");
      return false;
    }
    return true;
  };
  const persist = async (action: "save" | "revert") => {
    if (!current.current || busyRef.current) return;
    busyRef.current = true; setBusy(action);
    try {
      const flushed = await flush(action === "save");
      if (!alive.current || !flushed && action === "save") return;
      const snapshot = current.current;
      if (!snapshot) return;
      const reply = action === "save"
        ? await client.call("saveVoice", { voiceId: snapshot.id, expectedRevision: snapshot.savedRevision })
        : await client.call("revertVoice", { voiceId: snapshot.id });
      if (reply.ok) onDirtyChange(reply.value.id, reply.value.dirty, reply.value.changeRevision);
      if (!alive.current || current.current?.id !== snapshot.id) return;
      if (!reply.ok) {
        if (action === "revert") {
          const expectedActivity = activity.current;
          const expectedRead = ++readSequence.current;
          const refreshed = await client.call("getVoice", { voiceId: snapshot.id });
          if (refreshed.ok) accept(refreshed.value, expectedActivity, expectedRead);
        }
        if (!alive.current) return;
        if (["unavailable", "timeout", "internal"].includes(reply.error.code)) { recoveryRequired.current = true; setNeedsRecovery(true); }
        setError(reply.error.code === "stale_generation" ? "Saved elsewhere since you opened it. Revert to load the saved version, then reapply." : reply.error.message);
        return;
      }
      accept(reply.value); history([], []); setError(null);
      if (action === "revert") {
        for (const sender of senders.current.values()) sender.resetError();
        recoveryRequired.current = false; setNeedsRecovery(false);
      }
    } finally { busyRef.current = false; if (alive.current) setBusy(null); }
  };
  const replay = async (direction: "undo" | "redo") => {
    if (busyRef.current || recoveryRequired.current) return;
    busyRef.current = true; setBusy(direction);
    try {
      if (!await flush()) return;
      const entry = (direction === "undo" ? undoRef.current : redoRef.current).at(-1);
      if (!entry) return;
      const sender = senders.current.get(entry.parameterId);
      if (!sender) return;
      const value = direction === "undo" ? entry.before : entry.after;
      replaying.current = true;
      sender.push("begin", current.current?.values[entry.parameterId] ?? value);
      sender.push("change", value); sender.push("end", value);
      if (!await sender.flush() || !alive.current) return;
      if (direction === "undo") history(undoRef.current.slice(0, -1), [...redoRef.current, entry]);
      else history([...undoRef.current, entry], redoRef.current.slice(0, -1));
      setError(null);
    } finally { replaying.current = false; busyRef.current = false; if (alive.current) setBusy(null); }
  };
  const setType = async (type: VoiceType) => {
    const snapshot = current.current;
    if (!snapshot || busyRef.current || recoveryRequired.current || client.edits.locked) return;
    busyRef.current = true; setBusy("type");
    try {
      if (!await flush()) return;
      const reply = await client.call("setVoiceType", { voiceId: snapshot.id, type });
      if (!alive.current) return;
      if (reply.ok) accept(reply.value); else setError(reply.error.message);
    } finally { busyRef.current = false; if (alive.current) setBusy(null); }
  };
  const applyPreset = async () => {
    const snapshot = current.current;
    if (!snapshot || !presetId || busyRef.current || recoveryRequired.current || client.edits.locked) return;
    busyRef.current = true; setBusy("preset");
    try {
      if (!await flush()) return;
      const reply = await client.call("applyVoicePreset", { voiceId: snapshot.id, presetId });
      if (!alive.current) return;
      if (!reply.ok) { setError(reply.error.message); return; }
      accept(reply.value); history([], []); setError(null);
      setNotice("Experimental preset applied to this voice draft. Save voice or save a soundscape to keep it.");
    } finally { busyRef.current = false; if (alive.current) setBusy(null); }
  };
  const toggleMapping = async (row: MappingRow, enabled: boolean) => {
    const snapshot = current.current;
    if (!snapshot) return;
    const reply = await client.call("setMappingEnabled", { voiceId: snapshot.id, mappingId: row.id, enabled });
    if (!alive.current) return;
    if (!reply.ok) { setError(reply.error.message); return; }
    refreshMappings();
    const expectedActivity = activity.current;
    const expectedRead = ++readSequence.current;
    const updated = await client.call("getVoice", { voiceId: snapshot.id });
    if (updated.ok) accept(updated.value, expectedActivity, expectedRead);
  };

  const owner = useMemo(() => {
    if (!voice) return "";
    if (voice.scope === "group") return client.implementation === "native" || voice.targetId === "all_animals"
      ? "Default for animals without a species voice."
      : `Group default · ${voice.targetId === "forest_bird" ? "forest birds" : voice.targetId === "shark" ? "sharks" : voice.targetId}`;
    if (!voice.inheritsFromId) return `${catalog.speciesName(voice.targetId)} · independent voice`;
    return `${voice.scope === "species" ? catalog.speciesName(voice.targetId) : `Individual ${voice.targetId}`} · overrides ${parent?.name ?? "parent voice"} · ${voice.overriddenParameterIds.length} parameter overrides.`;
  }, [voice, parent, catalog, client]);

  if (!voiceId) return <section className="studio__center"><EmptyState title="No voice selected">Choose an animal voice or the default in the library.</EmptyState></section>;
  if (!voice) return <section className="studio__center">{error ? <Notice tone="problem" title="Voice could not be loaded">{error}</Notice> : <p className="small muted">Loading voice…</p>}</section>;
  const supportsEditing = client.supports("setVoiceParameter");
  const canEdit = supportsEditing && !needsRecovery;
  const env = (id: string) => { const spec = parameters.find((p) => p.id === id); return spec ? fromNormalized(spec, voice.values[id] ?? 0) : 0; };
  const offeredTypes = types ?? (client.implementation === "mock" ? demoTypes : [{ id: voice.type, label: voice.type, proposal: false }]);
  const hasProposal = parameters.some((spec) => spec.proposal);
  const changedOrSending = voice.dirty || pendingParameters.size > 0 || failedParameters.size > 0 || needsRecovery;
  return (
    <section className="studio__center" aria-labelledby="voice-title">
      <div className="row-between">
        <span className="label">SELECTED VOICE · DEFINITION</span>
        <span className="label">{voice.dirty ? <strong className="warn">Unsaved changes</strong> : `Saved rev ${voice.savedRevision}`}{pendingParameters.size ? " · Sending…" : ""}</span>
      </div>
      <div className="row-between">
        <h2 id="voice-title" className="h2 accent">{voice.name}</h2>
        <div className="toolbar" role="toolbar" aria-label="Voice edits">
          <button type="button" className="icon-btn icon-btn--sm" aria-label="Undo" disabled={!undo.length || busy !== null || needsRecovery} onClick={() => void replay("undo")}><Icon name="undo" size={13} /></button>
          <button type="button" className="icon-btn icon-btn--sm" aria-label="Redo" disabled={!redo.length || busy !== null || needsRecovery} onClick={() => void replay("redo")}><Icon name="redo" size={13} /></button>
          <button type="button" className="btn btn--quiet btn--sm" disabled={!changedOrSending || busy !== null || !client.supports("revertVoice")} onClick={() => void persist("revert")}>{busy === "revert" ? "Reverting…" : "Revert"}</button>
          <button type="button" className="btn btn--primary btn--sm" disabled={!changedOrSending || needsRecovery || failedParameters.size > 0 || busy !== null || !client.supports("saveVoice")} onClick={() => void persist("save")}>{busy === "save" ? "Saving…" : "Save voice"}</button>
        </div>
      </div>
      <p className="small muted">{owner}</p>
      {needsRecovery ? <Notice tone="caution" title="Reload saved voice to continue">Choose Revert to reload the saved definition before editing again. Unsaved drafts may have been lost if the native worker restarted.</Notice> : null}
      {!supportsEditing ? <Notice tone="caution" title="Editing not offered">This host does not accept voice parameter changes yet.</Notice> : null}
      {error ? <Notice tone="problem" title="Voice change not applied">{error}</Notice> : null}
      {notice ? <p className="small" role="status">{notice}</p> : null}
      {client.supports("listVoicePresets") ? <div className="voice-presets stack-sm">
        <div className="toolbar">
          <label className="small">Preset · experimental <select aria-label="Experimental voice preset" value={presetId} disabled={busy !== null || needsRecovery} onChange={event => setPresetId(event.target.value)}>
            <option value="">Choose preset…</option>
            {presets.map(preset => <option key={preset.id} value={preset.id}>{preset.name}</option>)}
          </select></label>
          <button className="btn btn--outline btn--sm" aria-label={busy === "preset" ? "Applying…" : "Apply to voice draft"} disabled={!selectedPreset || busy !== null || needsRecovery || !client.supports("applyVoicePreset")} onClick={() => void applyPreset()}>{busy === "preset" ? "Applying…" : "Apply"}</button>
        </div>
        <details className="panel-details small muted"><summary>Preset details · listening approval pending</summary>
          {selectedPreset ? <p>{selectedPreset.evidenceStatus === "ecological_metaphor" ? "Nonvocal ecological metaphor" : selectedPreset.evidenceStatus === "unresolved_taxon" ? "Unresolved taxon — generic candidate" : "Research candidate"}. {selectedPreset.description}</p> : <p>Researched starting points awaiting listening approval. Apply changes the draft without starting playback.</p>}
        </details>
        {presetsQ.status === "error" ? <span role="alert" className="small">{presetsQ.error.message}</span> : null}
      </div> : null}
      <div className="row-between">
        <SegmentedControl<VoiceType> label="Voice type" value={voice.type} onChange={(type) => void setType(type)} options={offeredTypes.map((type) => ({ value: type.id, label: `${type.label}${type.proposal ? " (proposed)" : ""}` }))} disabledValues={client.supports("setVoiceType") && busy === null && !needsRecovery ? [] : offeredTypes.map((type) => type.id)} />
        <span className="small muted" title="Currently sounding synth voices across all definitions">
          {audio ? audio.activeSynthVoices == null ? `Activity unavailable · ${audio.maxSynthVoices} voices` : `${audio.activeSynthVoices} / ${audio.maxSynthVoices} sounding${audio.meterIsSimulated ? " (simulated)" : ""}` : ""}
        </span>
      </div>
      <Envelope attack={env("attack")} decay={env("decay")} sustain={env("sustain")} release={env("release")} adOnly={!parameters.some((spec) => spec.id === "sustain" || spec.id === "release")} />
      <div className="knob-grid" role="group" aria-label={`Voice parameters${hasProposal ? " (proposed ranges)" : ""}`}>
        {parameters.map((spec) => <ParameterKnob key={`${voice.id}-${spec.id}`} spec={spec} value={voice.values[spec.id] ?? 0}
          inherited={parent?.values[spec.id]} overridden={voice.overriddenParameterIds.includes(spec.id)} disabled={!canEdit || busy !== null}
          register={(sender) => senders.current.set(spec.id, sender)} send={sendParam(spec.id)}
          onPending={(pending, failure) => {
            setPendingParameters((ids) => {
              if (ids.has(spec.id) === pending) return ids;
              const next = new Set(ids); if (pending) next.add(spec.id); else next.delete(spec.id); return next;
            });
            setFailedParameters((ids) => {
              if (ids.has(spec.id) === !!failure) return ids;
              const next = new Set(ids); if (failure) next.add(spec.id); else next.delete(spec.id); return next;
            });
          }}
 />)}
      </div>
      <details className="panel-details small muted"><summary>Voice help{hasProposal ? " · proposed ranges" : ""}</summary>
        <p>{parent ? "• marks an inherited override; Delete resets to the inherited value." : "Delete resets a parameter to its default."}</p>
        {editingBehavior === "new_notes" || client.implementation === "native" ? <p>Edits apply to new notes; sounding notes keep their settings and tails. Save voice keeps this definition for the next launch. Species voices remain independent of the default.</p> : null}
        <p>{hasProposal ? "Marked ranges and curves are proposals for DSP review." : "Ranges and curves come from the native host."}</p>
      </details>
      <div className="mapping">
        <span className="label">EVENT → MUSIC · PROPOSED</span>
        {mappingsQ.status === "unsupported" ? <Notice tone="caution" title="Mappings unavailable">This host does not expose editable mappings yet.</Notice> : <table className="mapping__table">
          <thead><tr><th scope="col">Input</th><th scope="col">Transform</th><th scope="col">Output</th><th scope="col"><span className="sr-only">Enabled</span></th></tr></thead>
          <tbody>{mappings.map((row) => <tr key={row.id} className={row.enabled ? "" : "is-off"}><td>{row.input}</td><td className="muted">{row.transform}</td><td>{row.output}</td><td><Toggle label={`${row.input} to ${row.output}`} checked={row.enabled} disabled={!client.supports("setMappingEnabled") || busy !== null} onChange={(enabled) => void toggleMapping(row, enabled)} /></td></tr>)}</tbody>
        </table>}
      </div>
    </section>
  );
}

function ParameterKnob({ spec, value, inherited, overridden, disabled, register, send, onPending }: {
  spec: VoiceParameterSpec; value: number; inherited: number | undefined; overridden: boolean; disabled: boolean;
  register: (sender: ParameterSender) => void; send: (value: number, gesture: Gesture) => Promise<HostResult<ParameterReply>>;
  onPending: (pending: boolean, error: string | null) => void;
}) {
  const sender = useCoalescedGesture(send);
  const [preview, setPreview] = useState<number | null>(null);
  useEffect(() => { register(sender); });
  useEffect(() => { onPending(sender.pending, sender.error); if (!sender.pending) setPreview(null); }, [sender.pending, sender.error, onPending]);
  return <div className="knob-cell"><Knob label={spec.label} value={sender.pending ? preview ?? value : value}
    defaultValue={inherited ?? toNormalized(spec, spec.defaultValue)}
    normalizedStep={spec.step && spec.curve === "linear" ? spec.step / (spec.max - spec.min) : undefined}
    format={(normalized) => formatParameter(spec, normalized)} parse={(text) => parseParameter(spec, text)}
    disabled={disabled} overridden={overridden} pending={sender.pending}
    onGesture={(gesture, normalized) => { if (gesture !== "begin") setPreview(normalized); sender.push(gesture, normalized); }} />
    {sender.error ? <span className="small field-error" role="alert">{sender.error}</span> : null}
  </div>;
}

function Envelope({ attack, decay, sustain, release, adOnly }: { attack: number; decay: number; sustain: number; release: number; adOnly: boolean }) {
  const total = Math.max(0.001, attack + decay + (adOnly ? 0 : 1 + release));
  const x = (time: number) => (time / total) * 580 + 10;
  const y = (level: number) => 110 - level * 96;
  const points = adOnly
    ? [[x(0), y(0)], [x(attack), y(1)], ...Array.from({ length: 60 }, (_, index) => {
      const fraction = (index + 1) / 60;
      return [x(attack + decay * fraction), y(Math.pow(0.001, fraction))];
    })]
    : [[x(0), y(0)], [x(attack), y(1)], [x(attack + decay), y(sustain)], [x(attack + decay + 1), y(sustain)], [x(total), y(0)]];
  const path = points.map(([px, py], index) => `${index ? "L" : "M"}${px!.toFixed(1)} ${py!.toFixed(1)}`).join("");
  return <figure className="envelope"><svg viewBox="0 0 600 124" role="img" aria-label={adOnly
    ? `Envelope: attack ${attack.toFixed(3)} s, decay T60 ${decay.toFixed(3)} s to −60 dB; no sustain or editable release; fixed 20 ms terminal fade`
    : `Envelope: attack ${attack.toFixed(2)} s, decay ${decay.toFixed(2)} s, sustain ${sustain.toFixed(2)}, release ${release.toFixed(2)} s`}>
    <path d="M10 110H590" className="envelope__axis" /><path d={`${path}L590 110L10 110Z`} className="envelope__fill" /><path d={path} className="envelope__line" />
    {(adOnly ? points.slice(1, 2) : points.slice(1, 4)).map(([px, py], index) => <circle key={index} cx={px} cy={py} r={4} className="envelope__point" />)}
  </svg><figcaption className="label">{adOnly ? "ENVELOPE · ATTACK + DECAY (T60)" : "ENVELOPE (A · D · S · R) — SUSTAIN SEGMENT DRAWN AT 1 S"}</figcaption></figure>;
}
