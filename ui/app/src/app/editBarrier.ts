/** Capture waits for acknowledged edits, including queues retiring on screen changes.
 * Lock stops new UI gestures synchronously; queued host mutations are never aborted. */
export class EditBarrier {
  private queues = new Set<() => Promise<boolean>>();
  private finalizers = new Map<() => Promise<boolean>, number>();
  registerFinalizer(flush: () => Promise<boolean>, order = 0): () => void {
    this.finalizers.set(flush, order);
    return () => { this.finalizers.delete(flush); };
  }
  private mutations = new Set<Promise<{ ok: boolean }>>();
  locked = false;
  register(flush: () => Promise<boolean>): () => void {
    this.queues.add(flush);
    return () => { void flush().finally(() => this.queues.delete(flush)); };
  }
  track<T extends { ok: boolean }>(promise: Promise<T>): Promise<T> {
    this.mutations.add(promise);
    void promise.then(() => this.mutations.delete(promise), () => this.mutations.delete(promise));
    return promise;
  }
  begin(): boolean {
    if (this.locked) return false;
    this.locked = true;
    return true;
  }
  async drain(requireSuccess = true, finalize = true): Promise<boolean> {
    const replies = await Promise.all([...this.queues].map(flush => flush()));
    let succeeded = replies.every(Boolean);
    while (this.mutations.size) {
      const results = await Promise.all([...this.mutations]);
      succeeded = succeeded && results.every(result => result.ok);
    }
    // Complete voice gestures before a full recipe re-preparation.
    if (finalize && (succeeded || !requireSuccess)) {
      for (const [flush] of [...this.finalizers].sort((a, b) => a[1] - b[1])) {
        if (!await flush()) { succeeded = false; if (requireSuccess) break; }
      }
    }
    return !requireSuccess || succeeded;
  }
  end() { this.locked = false; }
}
