/**
 * Coalesces a begin → change* → end gesture into at most one in-flight host
 * request. While a request is pending, only the latest "change" value is kept.
 * "begin" and "end" are always delivered, in order, so the host can group the
 * edit into one undo step / automation gesture.
 *
 * Returns the last acknowledged change revision so callers can ignore their
 * own echoes in host notifications (feedback-loop guard).
 */
import { useCallback, useRef, useState } from "react";
import type { HostResult } from "../host/client";
import type { Gesture } from "../components/Knob";

type Sender = (value: number, gesture: Gesture) => Promise<HostResult<{ changeRevision?: string }>>;

export function useCoalescedGesture(send: Sender) {
  const queue = useRef<Array<{ value: number; gesture: Gesture }>>([]);
  const inFlight = useRef(false);
  const lastRevision = useRef<bigint>(0n);
  const [pending, setPending] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const pump = useCallback(async () => {
    if (inFlight.current) return;
    const next = queue.current.shift();
    if (!next) { setPending(false); return; }
    inFlight.current = true;
    setPending(true);
    const result = await send(next.value, next.gesture);
    inFlight.current = false;
    if (result.ok) {
      setError(null);
      const rev = result.value.changeRevision;
      if (rev && /^\d+$/.test(rev) && BigInt(rev) > lastRevision.current) lastRevision.current = BigInt(rev);
    } else {
      setError(result.error.message);
      queue.current = queue.current.filter((q) => q.gesture === "end"); // keep the closing event so the host ends the gesture
    }
    void pump();
  }, [send]);

  const push = useCallback((gesture: Gesture, value: number) => {
    if (gesture === "change") {
      const last = queue.current[queue.current.length - 1];
      if (last && last.gesture === "change") last.value = value; // coalesce
      else queue.current.push({ value, gesture });
    } else {
      queue.current.push({ value, gesture });
    }
    void pump();
  }, [pump]);

  const isOwnEcho = useCallback((changeRevision: string) => /^\d+$/.test(changeRevision) && BigInt(changeRevision) <= lastRevision.current, []);

  return { push, pending, error, isOwnEcho };
}
