/**
 * Voice library: group default → species voice → known-individual override.
 * Each row says where its sound comes from, so inheritance is never implicit.
 */
import type { VoiceLibraryEntry } from "../../host/provisional";
import { useHost } from "../../app/HostContext";
import { useAppState, useDispatch } from "../../app/store";
import { Notice } from "../../components/Notice";

export function VoiceLibrary({ entries, dirtyIds, status }: { entries: VoiceLibraryEntry[]; dirtyIds: Set<string>; status: "loading" | "ready" | "unsupported" | "error" }) {
  const native = useHost().client.implementation === "native";
  const { selectedVoiceId, highlightSpeciesId } = useAppState();
  const dispatch = useDispatch();
  const select = (voiceId: string | null, speciesId?: string) => {
    dispatch({ type: "selectVoice", voiceId });
    if (speciesId) dispatch({ type: "highlightSpecies", speciesId });
  };
  return (
    <aside className="studio__left" aria-label="Voice library">
      <span className="label">VOICE LIBRARY</span>
      {status === "unsupported" ? <Notice tone="caution" title="Voice library unavailable">This host does not describe voices yet.</Notice> : null}
      {status === "loading" ? <p className="small muted">Loading voices…</p> : null}
      {entries.map((group) => (
        <section key={group.groupId} className="voice-group" aria-label={group.groupName}>
          <div className="voice-group__head">
            <span className="voice-group__name">{group.groupName}</span>
            {group.groupVoiceId ? (
              <button type="button" className={`pill pill--quiet${selectedVoiceId === group.groupVoiceId ? " is-current" : ""}`} aria-pressed={selectedVoiceId === group.groupVoiceId} onClick={() => select(group.groupVoiceId)}>
                {native ? "Fallback voice" : "Group default"}{dirtyIds.has(group.groupVoiceId) ? " •" : ""}
              </button>
            ) : <span className="small muted">no default</span>}
          </div>
          <ul role="list" className="voice-list">
            {group.species.map((s) => {
              const effective = s.voiceId ?? group.groupVoiceId;
              const on = (s.voiceId && selectedVoiceId === s.voiceId) || (!s.voiceId && highlightSpeciesId === s.speciesId && selectedVoiceId === group.groupVoiceId);
              return (
                <li key={s.speciesId}>
                  <button type="button" className={`voice-row${on ? " is-on" : ""}`} aria-pressed={!!on} onClick={() => select(effective, s.speciesId)}>
                    <span className="voice-row__name">{s.commonName}</span>
                    <span className={`voice-row__source mono${effective ? "" : " is-missing"}`}>
                      {s.voiceId ? `species voice${dirtyIds.has(s.voiceId) ? " • unsaved" : ""}` : effective ? native ? "uses fallback" : "inherits group" : "no voice"}
                    </span>
                  </button>
                  {s.individuals.length ? (
                    <ul role="list" className="voice-list voice-list--nested">
                      {s.individuals.map((ind) => {
                        const indEffective = ind.voiceId ?? effective;
                        const indOn = ind.voiceId ? selectedVoiceId === ind.voiceId : false;
                        return (
                          <li key={ind.individualId}>
                            <button type="button" className={`voice-row voice-row--individual${indOn ? " is-on" : ""}`} aria-pressed={indOn} onClick={() => select(indEffective, s.speciesId)}>
                              <span className="voice-row__name">{ind.label}</span>
                              <span className="voice-row__source mono">{ind.voiceId ? `override${dirtyIds.has(ind.voiceId) ? " • unsaved" : ""}` : "inherits"}</span>
                            </button>
                          </li>
                        );
                      })}
                    </ul>
                  ) : null}
                </li>
              );
            })}
          </ul>
        </section>
      ))}
      <button type="button" className="btn btn--outline btn--sm" disabled title="Assigning and creating voices needs host methods not in contract v1 (see INTEGRATION_GAPS.md)">+ Assign animal voice</button>
      <p className="small muted">{native ? "Species voices are independent. The fallback is used only for animals without a species definition." : "Individual overrides exist only where the source identifies a tagged animal. A microphone detection never identifies an individual."}</p>
    </aside>
  );
}
