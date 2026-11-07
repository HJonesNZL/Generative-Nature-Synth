import type { EventView } from "../host/contracts";
import { useHost, useHostQuery, valueOf } from "./HostContext";
/** One bounded lookup for the selected/recent observation, independent of collection. */
export function LiveObservationOutcome({event}: {event: EventView}) {
  const {client} = useHost();
  const [query] = useHostQuery("getLiveOutcome", {eventId: event.id}, [event.id], {refreshMs: 1000});
  if (!client.supports("getLiveOutcome")) return null;
  const result = query.status === "ready" ? valueOf(query)?.outcome : null;
  return <details className="panel-details small"><summary>Live musical outcome{result ? ` · ${result.status}` : ""}</summary>
    {result?.status === "queued" ? <p className="small muted">Waiting to sound. Queued is not confirmation of playback.</p> : result?.status === "skipped" ? <p className="small muted">Did not sound. Listening may have stopped, or the pending note was withdrawn or invalidated.</p> : null}
    <dl className="kv kv--tight"><dt>Species</dt><dd>{event.commonName || event.speciesId || "Not provided"}</dd>
      <dt>Species voice</dt><dd>{result?.voiceCharacter ?? "Not reported"}</dd>
      <dt>Status</dt><dd>{query.status === "error" ? query.error.message : result?.status ?? "No retained live outcome"}</dd>
      <dt>Region</dt><dd>{result?.region || "Unassigned"}</dd>
      {result?.representativeId ? <><dt>Phrase representative</dt><dd>{result.representativeId}</dd></> : null}
    </dl>
  </details>;
}
