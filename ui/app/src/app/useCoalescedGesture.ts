/** React ownership for a serial parameter queue. Sender closures stay current;
 * the queue identity remains stable. Call flush before Save/Revert/Undo. */
import { useEffect, useRef, useState } from "react";
import { GestureQueue, type GestureSender } from "./gestureQueue";

export function useCoalescedGesture(send: GestureSender) {
  const sendRef = useRef(send);
  sendRef.current = send;
  const alive = useRef(true);
  const [status, setStatus] = useState<{ pending: boolean; error: string | null }>({ pending: false, error: null });
  const [queue] = useState(() => new GestureQueue((value, gesture) => sendRef.current(value, gesture), (pending, error) => {
    if (alive.current) setStatus({ pending, error });
  }));
  useEffect(() => {
    alive.current = true;
    return () => { alive.current = false; void queue.flush(); };
  }, [queue]);
  return { resetError: queue.resetError.bind(queue), push: queue.push.bind(queue), flush: queue.flush.bind(queue), pending: status.pending, error: status.error, isOwnEcho: queue.isOwnEcho.bind(queue) };
}
