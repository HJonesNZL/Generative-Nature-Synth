import type { TimeBasis } from "../host/contracts";
export function playbackContext(mode: string, sessionId: string | undefined, basis: TimeBasis, numerator: string, denominator: string, revision: number): string {
  return JSON.stringify([mode, sessionId, basis, numerator, denominator, revision]);
}
