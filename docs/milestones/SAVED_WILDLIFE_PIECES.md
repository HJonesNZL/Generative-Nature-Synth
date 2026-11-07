# Saved wildlife pieces

Select a UTC observation window in Project, optionally restrict it to a source or species, and freeze the matching collected events into a named piece. The start is inclusive and the end exclusive. Gaps remain gaps. Reopening the same piece after later collection must reproduce the captured revisions.

With an external library attached, select a saved piece to import. Only the shell supplies database paths. Validate the source, preserve its event bytes, original revision numbers and sealed metadata, then publish atomically to the managed library. The source remains read-only. Resume a matching interrupted import from bounded local staging. Repeating a successful operation must not create a duplicate.

One background job at a time reports progress and supports cancellation. Keep the live collector, audio callback and interface independent. A completed piece enters the existing Replay, Sound Studio, soundscape save and WAV export workflow. Verify boundaries, filters, cancellation, restart, tampering and the packaged WebKit bridge using disposable state.
