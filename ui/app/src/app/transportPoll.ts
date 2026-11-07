import type { TransportState } from "../host/contracts";
import type { HostResult } from "../host/client";
import type { TransportView } from "./store";

/** Poll health recovers independently of preparation/action failures. */
export function transportPollUpdate(expected: TransportView, result: HostResult<TransportState>): Partial<TransportView> | null {
  if (!result.ok) return expected.pollError === result.error.message ? null : { pollError: result.error.message };
  const recovered = expected.pollError ? { pollError: null } : null;
  if (!expected.preparationId) return recovered;
  const value = result.value;
  if (value.preparationId !== expected.preparationId) return {
    preparationId: null, state: "stopped", pollError: null,
    error: "Playback preparation changed. Select the saved piece again.",
  };
  if (BigInt(value.generation) < BigInt(expected.generation)) return recovered;
  return { ...value, pollError: null };
}
