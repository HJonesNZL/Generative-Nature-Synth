/** A serial begin/change/end sender. Flush closes an open gesture and
 * resolves only after the host has acknowledged every outstanding request.
 * A failed request preserves a closing end; callers must not persist on failure.
 * The queue owns no host state and never retries a mutation. */
import type { HostResult } from "../host/client";
import type { Gesture } from "../components/Knob";

export type GestureReply = { changeRevision?: string; normalizedValue?: number };
export type GestureSender = (value: number, gesture: Gesture) => Promise<HostResult<GestureReply>>;
export class GestureQueue {
  private queue: Array<{ value: number; gesture: Gesture }> = [];
  private running = false;
  private open = false;
  private latest = 0;
  private acknowledged = 0;
  private failure: string | null = null;
  private revision = 0n;
  private waiters: Array<(ok: boolean) => void> = [];
  constructor(private readonly send: GestureSender, private readonly changed: (pending: boolean, error: string | null) => void) {}
  get pending() { return this.running || this.queue.length > 0 || this.open; }
  isOwnEcho(revision: string) { return /^\d+$/.test(revision) && BigInt(revision) <= this.revision; }
  push(gesture: Gesture, value: number) {
    if (gesture !== "begin" && !this.open) return;
    if (gesture === "begin") { this.open = true; this.failure = null; if (!this.running && !this.queue.length) this.acknowledged = value; }
    else if (gesture === "end") this.open = false;
    if (gesture === "end" && this.failure) value = this.acknowledged;
    this.latest = value;
    const last = this.queue.at(-1);
    if (gesture === "change" && last?.gesture === "change") last.value = value;
    else this.queue.push({ value, gesture });
    this.changed(true, this.failure);
    void this.pump();
  }
  resetError() {
    if (this.pending) return;
    this.failure = null;
    this.revision = 0n;
    this.changed(false, null);
  }
  flush(): Promise<boolean> {
    if (this.open) this.push("end", this.latest);
    if (!this.pending) return Promise.resolve(this.failure === null);
    return new Promise((resolve) => this.waiters.push(resolve));
  }
  private async pump() {
    if (this.running) return;
    this.running = true;
    while (this.queue.length) {
      const next = this.queue.shift()!;
      let result: HostResult<GestureReply>;
      try { result = await this.send(next.value, next.gesture); }
      catch (error) { result = { ok: false, error: { code: "internal", message: error instanceof Error ? error.message : "Parameter request failed" } }; }
      if (result.ok) {
        this.acknowledged = result.value.normalizedValue ?? next.value;
        const revision = result.value.changeRevision;
        if (revision && /^\d+$/.test(revision) && BigInt(revision) > this.revision) this.revision = BigInt(revision);
      } else {
        this.failure = result.error.message;
        this.latest = this.acknowledged;
        // Retire this failed gesture once. Later queued edits are discarded;
        // their end events must never be sent without the matching begin.
        const closing = next.gesture === "end" ? undefined : this.queue.find((item) => item.gesture === "end");
        this.queue = closing ? [{ ...closing, value: this.acknowledged }] : [];
        if (closing || next.gesture === "end") this.open = false;
      }
      this.changed(true, this.failure);
    }
    this.running = false;
    this.changed(this.open, this.failure);
    if (!this.open) for (const resolve of this.waiters.splice(0)) resolve(this.failure === null);
  }
}
