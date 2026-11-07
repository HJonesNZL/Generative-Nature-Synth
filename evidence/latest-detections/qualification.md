# Latest detections standalone 0.4.1

7 November 2026, Apple Silicon. The new app and ZIP are local build products under `Releases/Latest Detections/`. ZIP SHA-256 is `76aba06132561fe0193c83c13d616d3d9cc0964aeb7ecb73c271e9015401b1f8`. The app passed strict signature verification and the ZIP passed its integrity check.

## Collection

Read-only observation of the previous release showed 19 NZ bird polls and 1,775 inserted bird records through 04:21:38 UTC. Polls repeatedly reached the previous 400-record limit. The 04:17:49 UTC shark refresh completed with 28 duplicates, zero new positions and zero invalid records. The library passed SQLite quick_check.

A single isolated poll of BirdWeather using the rebuilt native collection service received 679 records in three pages in 2.3 seconds. All records were valid, the final page was exhausted and there was no provider error. Fixture tests also exercise up to four full 250-record pages, partial coverage above that limit, deduplication and rollback.

## Map and standalone

- All 30 native CTests and the dedicated sanitizer collector test passed. TypeScript and all 59 UI unit tests passed. Both production and demo bundle checks passed.
- The browser regression in `ui/app/tests/latest-detections-browser.mjs` used synthetic events. It covered observation ordering and ties, recent rings on flat map/globe/loupe, species highlight and event selection, explicit locate, and preserving the user's view as another report arrived.
- Atlas and Studio were checked at 1280×800 and 1440×900. The globe canvas measured 688×455 and 788×555 respectively. The right column scrolled independently and the activity strip remained visible. [Fixture screenshot](atlas-live-fixture.png)
- The packaged app completed the real WebKit offline smoke in disposable state with 97 native requests, 30 polling rounds, scene recall, project backup/restore and stereo 24-bit WAV export at 48 kHz. Provider polling and the new map interactions were qualified separately. Physical playback was outside this change.
