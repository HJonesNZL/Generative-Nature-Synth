# Live collection sessions, 0.10.0

Collect starts a fresh live map, soundscape input and species ranking. The session has a UTC observation cutoff and a library revision cursor. Listening has its own start cursor within the collection. Qualifying first-poll data can sound when listening is armed in time. Changing sources while collecting preserves the session. Stopping and starting Collect creates a new session.

The [packaged measurements](release.json) cover an older library record, a current detection, a duplicate and correction, a fresh delivery observed before Collect and 500 current detections across eight species. The installed standalone used the real Collect control and a virtual stereo device. Its eight rendered counts sum to 501 and the audio callback confirmed 501 note starts. Stored reports remained available for historical use. The signed app and freshly extracted release ZIP matched file for file.

Browser regressions used 300 species with two retained map records at 1440×900 and 1280×800. The species list scrolled independently, counts changed order as new data arrived, the outer window stayed at scroll offset zero, and all four edges of the listening menu stayed inside the window. Separate checks covered stop/restart races, correction withdrawals, source failure, returning from History, and choosing a queued observation. Native tests added 6,912 revisions and verified that the next single detection required examining one additional revision.

Testing used disposable data and a virtual output. The playback visualization proposals are in the [concept gallery](../../design/live-session-concepts/README.md).
