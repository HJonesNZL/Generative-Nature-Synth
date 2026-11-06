/**
 * Screen 2 — Sound Studio (first pass in the H2 language; NOT yet reviewed
 * as a canvas design). Same continuous ground, same Grain panel as Atlas.
 * Shares selection, material, transport and region with the Atlas.
 */
import { useCallback, useMemo, useState, type ReactNode } from "react";
import { useCatalog } from "../../app/catalog";
import { useHost, useHostQuery, valueOf } from "../../app/HostContext";
import { useAppState, useDispatch } from "../../app/store";
import { GrainPanel } from "../shared/GrainPanel";
import { useMaterialView } from "../shared/useMaterialView";
import { RegionEffectsPanel } from "./RegionEffectsPanel";
import { VoiceEditor } from "./VoiceEditor";
import { VoiceLibrary } from "./VoiceLibrary";

export function SoundStudioScreen() {
  const view = useMaterialView();
  const catalog = useCatalog();
  const state = useAppState();
  const dispatch = useDispatch();
  const catalogSpecies = catalog.data?.species.map((species) => species.id).sort().join("\n") ?? "";
  const [libQ] = useHostQuery("getVoiceLibrary", {}, [state.mode, state.transport.preparationId, state.replay.session?.id, state.history.frozen?.id, state.soundscape.epoch, catalogSpecies]);
  const lib = valueOf(libQ);
  const dirty = useMemo(() => new Set(Object.entries(state.voiceDrafts).filter(([, voice]) => voice.dirty).map(([id]) => id)), [state.voiceDrafts]);
  const onDirtyChange = useCallback((voiceId: string, isDirty: boolean, changeRevision: string, authoritative = false) => {
    dispatch({ type: "voiceDirty", voiceId, dirty: isDirty, changeRevision, authoritative });
  }, [dispatch]);
  const voiceName = useMemo(() => {
    const id = state.selectedVoiceId;
    if (!id || !lib) return null;
    for (const g of lib.entries) {
      if (g.groupVoiceId === id) return `${g.groupName} default`;
      for (const s of g.species) {
        if (s.voiceId === id) return `${s.commonName} voice`;
        for (const i of s.individuals) if (i.voiceId === id) return `${i.label} override`;
      }
    }
    return id;
  }, [lib, state.selectedVoiceId]);
  const libStatus = libQ.status === "ready" ? "ready" : libQ.status === "unsupported" ? "unsupported" : libQ.status === "error" ? "error" : "loading";

  return (
    <main className="screen studio" id="main" aria-label="Sound Studio">
      <ContextStrip actions={<StudioActions selectedEventId={state.selectedEventId} />} />
      <VoiceLibrary entries={lib?.entries ?? []} dirtyIds={dirty} status={libStatus} />
      <VoiceEditor key={`${state.soundscape.epoch}:${state.selectedVoiceId ?? "no-voice"}`} parameters={lib?.parameters ?? []} types={lib?.types} editingBehavior={lib?.editingBehavior} catalog={catalog} onDirtyChange={onDirtyChange} />
      <RegionEffectsPanel voiceName={voiceName} />
      <GrainPanel view={view} catalog={catalog} />
    </main>
  );
}

function ContextStrip({ actions }: { actions: ReactNode }) {
  const { caps } = useHost();
  const state = useAppState();
  const catalog = useCatalog();
  const text = caps?.implementation === "native" && !caps.audioAvailable ? "Data preview — audio output is not available from this host." : state.mode === "live"
    ? "Editing while listening live — changes apply to new arrivals."
    : state.mode === "replay"
      ? state.replay.session ? `Editing for ${state.replay.session.id.startsWith("scene:") ? "soundscape" : "saved piece"} “${state.replay.session.name}”${state.replay.session.origin === "imported_bundle" ? " (imported)" : ""}.` : "Replay — no piece open."
      : state.history.frozen ? `Editing for frozen history “${state.history.frozen.name}”.` : state.history.result ? "History selection not yet frozen." : "History — no selection yet.";
  return (
    <div className="context-strip" role="status">
      <span className="badge badge--context">{state.mode.toUpperCase()}</span>
      <span className="small">{text}</span>
      {state.highlightSpeciesId ? <span className="small muted">Highlighted: {catalog.speciesName(state.highlightSpeciesId)}</span> : null}
      <div className="context-strip__actions">{actions}</div>
    </div>
  );
}

function StudioActions({ selectedEventId }: { selectedEventId: string | null }) {
  const { client } = useHost();
  const state = useAppState();
  const [message, setMessage] = useState<string | null>(null);
  const audition = async () => {
    if (!selectedEventId || !state.selectedVoiceId) return;
    const result = await client.call("auditionEvent", { eventId: selectedEventId, voiceId: state.selectedVoiceId });
    setMessage(result.ok ? result.value.accepted ? "Auditioning selected event." : "Audio is off or unavailable." : result.error.message);
  };
  return <div className="studio-actions">
    <button type="button" className="btn btn--outline btn--sm" disabled={!selectedEventId || !state.selectedVoiceId || !client.supports("auditionEvent")} onClick={() => void audition()}>Audition selected event</button>
    {message ? <span className="small" role="status">{message}</span> : null}
  </div>;
}
