# ANIMAL ATLAS — full Codex design and implementation handoff

Copy everything below into a dedicated Codex session. This document is self-contained; no earlier chat is required.

---

You are my product designer, audio software engineer and wildlife-data integration engineer. Help me develop an organic generative synthesizer driven by real animal tracking and detection events from around the world, with a strong New Zealand component.

Read this complete brief before making architectural decisions. Inspect the available repository and development environment. Produce a concrete, technically grounded implementation plan and then implement the first working vertical slice where the environment allows it. Do not stop at a conceptual mockup. Work autonomously on reversible implementation choices; ask only for decisions that materially block progress. Do not contact organisations, buy services or publish a product without my explicit instruction.

“ANIMAL ATLAS” is a working title, not a settled product name. Everything proposed here about interface, synthesis and technology is a starting design direction unless specifically identified as a user requirement. Improve these proposals where justified and document the reason.

## 1. Product intent and boundaries

Create a musical instrument in which real animal activity becomes a changing musical composition. A world map is the primary visual surface. When a new location fix, receiver detection, bird call detection or other supported animal event arrives, something visible and audible can happen: a point appears, a ripple expands, an individual journey extends, and a musical gesture is triggered.

I am interested in great white sharks, whales, dolphins, birds, turtles and other species. New Zealand's Great White App is an important starting source, but one slow shark feed cannot support the entire instrument. Find and integrate a diverse source mix, prioritising accessible data with frequent genuinely fresh events.

I am happy to let a composition accumulate over several hours and then listen back, speed it up, edit the event selection or render it as a piece. The system does not need to pretend every tagged animal reports constantly.

The eventual goal is a synthesizer VST plug-in. A standalone application and background collector are acceptable, and likely preferable for the first version. Architect for shared data and synthesis engines so standalone work is reusable in a later plug-in.

The exact sound-design language, note mappings and effects are not settled. Build a flexible mapping architecture and a small satisfying reference instrument; do not treat speculative sound ideas in this document as permanently approved artistic decisions.

### Actual user requirements

- Real animal activity drives the instrument.
- Broad worldwide source coverage, including NZ, with enough events to make an hours-long session worthwhile.
- A substantial geographic map with visible incoming events and movement where genuinely measured.
- Collect, record, replay and accelerate animal-event timelines.
- Explore suitable APIs and other permitted machine-access routes rather than assuming every public map is directly reusable.
- A self-contained route to a working product, eventually usable as a VST instrument.

### Working defaults, open to improvement

- macOS first, because my music workflow uses Macs and Ableton; retain a cross-platform core.
- JUCE/C++ is the initial candidate for standalone audio and VST3. Verify the current supported version, toolchain and licensing before committing. Do not invent a version number.
- A local collector plus SQLite event store; avoid a mandatory hosted backend or account for the first prototype.
- A polished dark atlas aesthetic with restrained organic motion and a clear instrument panel.
- No affiliation with any tracking organisation is implied.

## 2. Reasoning behind the design

The value is not simply random notes with an animal-themed skin. Every incoming gesture should have an inspectable connection to a real record: species, individual or station, place, observation time and source. The listener should be able to understand what happened without reading engineering details.

Individual GPS tracks supply identity, continuity, migration and emotional attachment. Acoustic monitoring supplies much denser activity, often with species diversity. These complement each other, but they are different measurements and must retain their identities throughout the system.

A stationary microphone hearing a bird is not tracking that bird's journey. A listening buoy hearing a whale does not locate that whale precisely. An acoustic receiver detecting a shark gives proximity to the receiver, not an exact shark coordinate. A public sighting is not telemetry. Display and sonify these honestly.

Satellite telemetry often arrives in batches, and some publication is delayed. Keep both observation time and discovery time. Live-arrival music, chronological replay and accelerated replay can consequently sound very different. This is a creative feature as well as a data-integrity requirement.

The collector should be independent of the audio processing callback and ideally independent of the DAW process. This permits long collection sessions, shared access by multiple plug-in instances and stable audio during network failures. A device or DAW being offline must not make saved compositions unusable.

High event volume alone is not artistic diversity. Thousands of detections from one station or species should not erase rare shark and turtle events. Offer transparent filtering, quotas and weighting while preserving original raw events.

## 3. Research baseline and evidence status

The findings below come from research in a previous session dated 6 November 2026. Treat them as leads and dated observations, not present-day guarantees. Reopen official documentation and test endpoints in this session before claiming a connector works. Do not invent missing API endpoints, credentials, rate limits, licences or public access.

### BirdWeather: previously tested, dense activity

The previous research session reported successful unauthenticated queries against BirdWeather's documented GraphQL API. Reported available fields included detection IDs, timestamps, coordinates, species and confidence information. Schema details and terms must be rechecked.

Reported snapshot around 4:04–4:06 pm NZDT, 6 November 2026:

| Query | Reported result |
| --- | ---: |
| Global detections within a one-hour window | 37,467 |
| Species in that global window | 507 |
| Stations in that global window | 2,694 |
| Detections in a mainland NZ bounding-box hour | 2,536 |
| Species in that NZ window | 22 |
| Stations in that NZ window | 41 |
| A separate 100-record global sample | Covered 19 seconds and 51 species |

These counts were reported by the prior session and are not a throughput SLA. They describe detections, not individual birds. A calling bird can generate repeated records. Coordinates generally identify a station/detection location. The original NZ bounding box and exact query are not included here; recreate and document your own query before comparing numbers.

### NZ great whites: direct public-map inspection

A direct inspection in the earlier session found 28 public tag records representing 27 named individuals, including two records for Ra's separate tagging deployments. Fourteen records were explicitly configured as deactivated; the other fourteen used automatic status, which does not mean fourteen currently transmitting animals.

Across the thirty-day analysis window ending 6 November 2026 at 00:00 UTC, the downloaded public tracks contained 67 distinct (timestamp, shark) records from five sharks on 19 NZ calendar dates. Mean gap between successive distinct location timestamps was about nine hours; the longest internal gap was about 76.5 hours. The last available fix was Waihi on 3 November at 4:36 pm NZDT, with fourteen Waihi fixes that day. The observation window ended at 1 pm NZDT on 6 November, rather than exactly at the time the answer was sent. These are location-event times, not measured publication times.

An August 2026 report stated the project had tagged 32 sharks overall. Project totals and publicly visible records are different counts. Do not present these dated counts as current numbers.

The actual public-map integration discovered in that session was:

- Map: https://map.sustainableoceansociety.co.nz/
- Bootstrap: https://map.sustainableoceansociety.co.nz/public/bootstrap.json
- Configuration fallback: https://map.sustainableoceansociety.co.nz/public/configs/config.json
- Configuration supplied `server`, `public_name`, `subject_group`, per-subject names, descriptions and tag status overrides.
- Then-current public subject request: `https://ermap-server.pamdas.org/nzsharkman/api/v1.0/subjects?subject_group=5cd1e429-439c-47dd-adbc-e7746a5fe543`
- Then-current track request pattern: `https://ermap-server.pamdas.org/nzsharkman/api/v1.0/subject/{subject_id}/tracks`
- Subject response used `data.data`; last fix appeared under `last_position.properties.DateTime`.
- Track response used `data.features`; line coordinates were under `geometry.coordinates`, paired with `properties.coordinateProperties.times`.

These were accessible endpoints used by the public client, not a documented contractual API. Discover current configuration rather than permanently hardcoding these values. Check permitted reuse and access expectations before shipping this connector. Public technical accessibility does not itself grant distribution rights. Do not reuse private EarthRanger credentials or probe private installations.

## 4. Source catalogue and integration priorities

Retain every candidate below in a source registry. Each must have a verified status rather than a vague claim of support. Different projects may share platforms and records; avoid counting them as independent sources without checking.

### Priority sources

| Source | Contribution | Starting URL and integration posture |
| --- | --- | --- |
| BirdWeather | Dense worldwide acoustic bird detections, including NZ | https://app.birdweather.com/ and https://app.birdweather.com/api/index.html — previously tested GraphQL route; revalidate schema, pagination, request limits and reuse terms. |
| Movebank | Individual movement across species and countries | https://www.movebank.org/ and https://www.movebank.org/cms/movebank-content/movebank-api — documented interfaces; study-specific permissions, embargoes and freshness. Find concrete accessible active studies, not just a generic integration. |
| NZ Great White App / Sustainable Oceans Society | Individual great whites around NZ and the wider Pacific | https://www.sustainableoceansociety.co.nz/ and https://map.sustainableoceansociety.co.nz/ — public-client route previously inspected; no supported integration agreement established. |
| NSW SharkSmart | Tagged shark detections at coastal receivers | https://www.sharksmart.nsw.gov.au/shark-activity — provider describes immediate alerts near listening stations; machine-feed route not yet established. |
| WA SharkSmart | Tagged detections and sightings | https://www.sharksmart.com.au/ — near-real-time public information; distinguish event types and verify automated access. |
| Robots4Whales / WHOI | Whale species detections from acoustic monitoring platforms | https://robots4whales.whoi.edu/ — near-real-time products; platform-specific review and reporting windows. Establish a current machine-readable route and real latency. |
| OCEARCH | Individual satellite-tagged marine animals | https://www.ocearch.org/research/track-sharks-with-us/ — public tracker confirmed in earlier research; third-party API and reuse rights not established. |
| Sharktivity / Atlantic White Shark Conservancy | Acoustic white-shark detections and sightings, especially Cape Cod | https://www.atlanticwhiteshark.org/sharktivity-app/ — seasonal receiver coverage; historical logbook is not automatically the live feed. |
| Shark Research Foundation | Satellite-tagged sharks | https://sharkresearch.org/shark-tracking/ — Wildlife Computers map embed observed previously; determine permitted provider/project route. |
| Dorsal | Shark reports and official alerts | https://www.dorsalwatch.com/faq/ — earlier research found an API collaboration route, not public self-service documentation. May duplicate government feeds. |

Movebank reportedly connects many manufacturers; this does not grant universal public telemetry access. Earlier documentation described approximately six-hour Argos imports, with other feeds behaving differently. Verify current documentation and each selected study's actual latency. Download permission and map-view permission may differ.

### Additional NZ candidates: live public data not established

| Project | What to investigate | Starting URL |
| --- | --- | --- |
| DOC / Fisheries NZ albatross tracker | Active individual birds, most recent event, update interval, underlying data and permitted export | https://docnewzealand.shinyapps.io/albatrosstracker/ |
| Muriwai tākapu / gannet GPS project | Research reported 15 birds tagged in October 2025, sampling up to every two minutes and uploading via mobile coverage. Tail-mounted tags were expected to be shed in 2026; do not assume still active. | https://www.doc.govt.nz/news/media-releases/2026-media-releases/muriwai-gannets-reveal-flight-paths-for-the-first-time/ |
| Black petrel fledgling project | Research reported 47 fledglings tagged in 2026. Need current reporting individuals, telemetry interval and access route. | https://acap.aq/latest-news/what-happens-to-black-petrels-in-their-first-year-at-sea-a-tracking-study-aims-to-find-out |
| Team Turtle: DOC, Auckland Zoo, Kelly Tarlton's and iwi | Earlier report described 11 rehabilitated turtles tracked daily for 6–12 months since 2025. Verify live fixes, current cohort and public/research access. | https://www.doc.govt.nz/news/media-releases/2026-media-releases/satellite-tagged-turtles-reveal-epic-journeys/ |
| Whale Watch Kaikōura | Verify whether the map contains boat positions, sightings or tagged animals, and whether data are actually current. | https://whalewatch.co.nz/our-nature/latest-sightings/live-map/ |

### Further international candidates

| Source | Contribution and unresolved question | Starting URL |
| --- | --- | --- |
| SEATURTLE.ORG | Multiple turtle and other tracking projects; cadence and permissions vary, some require written permission. | https://www.seaturtle.org/tracking/ |
| Oceanic Society leatherbacks | Previously advertised daily tracking; current animals, machine feed and rights need checking. | https://www.oceanicsociety.org/ — search site for leatherback live tracking. |
| Sea Turtle Conservancy | Active-project catalogue; inspect each selected deployment and its last fixes. | https://conserveturtles.org/tracking-status/active/ |
| Loggerhead Marinelife Center | Rehabilitation releases and individual journeys; verify current cadence and extraction. | https://marinelife.org/rehabilitation/rehabilitation-tracking/ |
| STRETCH | Pacific loggerheads; map publication may be delayed. | https://www.loggerheadstretch.org/ |
| Monterey Bay White Sharks / TOPP | Shark detections and tracks; newer project preferable to assuming legacy TOPP maps work. | https://www.montereybaywhitesharks.org/ |
| Pacific Whale Foundation | Whale/dolphin sightings, rather than guaranteed individual telemetry. | https://pacificwhale.org/ — search for Whale & Dolphin Tracker. |
| Save the Elephants / WildTracks / EarthRanger partners | Terrestrial individual tracks; partnership and authorised access, not an open worldwide feed. | https://savetheelephants.org/our-work/science/movement/tracking/ |
| iNaturalist | Broad wildlife observations; upload time can differ greatly from encounter time, coordinates can be obscured. | https://api.inaturalist.org/ |
| eBird | Recent bird observations with documented API; not individual GPS tracks or guaranteed immediate uploads. | https://science.ebird.org/en/use-ebird-data/download-ebird-data-products |

### Archive or restricted candidates

- Motus: https://motus.org/ — earlier research reported processing backlogs for major ingestion pathways, while internet-connected CTT SensorStations were less affected. Recheck current status and access before selecting a live pathway.
- Polar Bears International: https://polarbearsinternational.org/ — weekly receipt and intentional publication delays reported previously; likely archive/replay material.
- BirdLife Seabird Tracking Database: https://www.seabirdtracking.org/ — extensive archive; current-year study does not imply live delivery.
- Ocean Tracking Network: https://oceantrackingnetwork.org/ and OBIS: https://obis.org/ — useful research discovery and archival records; public release may be delayed.

Continue source discovery where useful, but prioritise proving several independent streams over collecting a large list of untested logos. No source above is promised as shippable merely by appearing in this document.

## 5. Source validation and registry

For every candidate record: official organisation, URL, geography, taxa, measurement type, individual/station identity, public visibility, access method, authentication, latest successful test time, oldest/newest accessible observation, documented reporting cadence, measured observation-to-publication lag if known, request constraints, pagination/cursors, archive availability, licence/attribution, sensitive-location policy and integration status.

Use explicit statuses: research lead, documented route, endpoint tested, live freshness confirmed, permitted integration confirmed, production-ready, archive-only, blocked or unavailable. Technical and permission status should be separate fields.

A check must distinguish sampling interval, device upload interval, platform processing delay, public publication delay and collector poll interval. If a quantity is unknown, say unknown. Polling a stale endpoint more often does not create more live data.

For the first prototype prove BirdWeather plus at least one accessible individual-track source where possible. Keep connectors blocked by access replaceable without preventing the rest of the application from working. Do not silently substitute fabricated data.

## 6. Complete functional scope

### A. Data acquisition

- Adapter interface for polling, paginated fetches and streams when genuinely supported.
- Per-source configuration and credentials stored outside presets and logs.
- Conservative provider-specific polling, backoff with jitter, timeout, Retry-After handling, cancellation and connection recovery.
- Incremental watermarks, overlapping fetch windows, stable deduplication and reconciliation of late or corrected records.
- First connection imports historical records without triggering the entire archive as new live events by default. Offer an explicit historical catch-up action.
- Separate animal identity from tag/deployment identity; one animal may have multiple tags and a tag may be reused.
- Separate source original records from normalized interpretations; schema-version both.
- Quality and timestamp validation, coordinate range validation, UTC normalization, suspicious future-date handling and missing-field handling.
- Preserve uncertainty and provider redaction. Never infer hidden endangered-animal locations.
- Handle marine tracks crossing the antimeridian correctly.
- Support imported CSV/JSON/GeoJSON or GPX where useful, labelled as imported/replay material.
- Persist before dispatching to audio where feasible; use idempotent recovery after interruption.

### B. Common event model

At minimum include `event_id`, `source_id`, `provider_event_id`, `provider_record_revision`, `species_id`, `scientific_name`, `common_name`, `individual_id`, `deployment_id`, `station_id`, `event_kind`, `location_kind`, latitude/longitude, uncertainty or detection radius, `observed_at_utc`, provider publication timestamp if supplied, `first_seen_at_utc`, `ingested_at_utc`, confidence plus its provider-specific meaning, raw-record reference, provenance/attribution, permissions metadata and quality flags.

Use nullable fields rather than invented values. Identity must be namespaced. Coordinate order must be explicit. Supported event kinds should include GPS/satellite fix, acoustic receiver detection, acoustic species detection and human observation. An animal detection may have no known individual. Preserve positional uncertainty rather than displaying false precision.

Derived movement values include distance, bearing, displacement and estimated speed only between compatible fixes from the same individual/deployment with adequate timing and quality. Label estimates. Do not derive a bird's speed between unrelated stationary microphones or connect different animals into one path. Do not interpolate across huge gaps as measured motion.

### C. World map and exploration

- Global view, NZ focus, region zoom, geographic search and saved regions.
- Scalable clustering/aggregation at global scale, with selectable real events beneath it.
- Distinct visual treatment for individual fixes, stationary detections, uncertain areas and sightings.
- New-event ripple, species/group colour, inspectable marker and optional individual trail.
- Accurate path wrapping around ±180°, with no line drawn across the entire map by mistake.
- Species, source, individual, measurement type and freshness filters.
- Favourites and an optional follow-animal camera with gentle movement and an off switch.
- Event inspector: identity, timestamp, source, accuracy, confidence and musical action.
- Timeline scrubber coordinated with map and sound; clear LIVE, REPLAY, IMPORTED and DEMO badges.
- Latest-observation age separate from last successful fetch and connection status.
- Source activity list, compact event log and selected-region activity.
- Reduced motion, readable labels and colour-blind-safe alternatives.
- Historical tracks never animate as fresh animal arrivals unless explicitly replayed.

### D. Musical mapping engine

Create editable mappings from event attributes to triggers, pitch, timbre, dynamics, space and effects. Keep mapping independent of source adapters and renderers.

Candidate inputs: species/group, individual, location, observation time, local solar time if derived correctly, confidence, measured movement, interval since last event, source and region. Candidate outputs: note pitch, chord/scale degree, voice/layer, velocity, envelope, duration, pan, filter, modulation and effect sends.

Provide bounded mapping ranges, curves, smoothing, deterministic randomness, seed control, transposition, key/scale restriction, optional chord context and musical density controls. Preserve raw input alongside transformed output for inspection. Species identity must remain stable across restarts, rather than depending on database insertion order.

Provide immediate triggering, optional host-grid quantization and optional phrase scheduling. Make quantization a deliberate timing transformation and retain original timing. Resolve simultaneous events deterministically. Support note-off scheduling, voice limits and safe voice stealing.

Do not automatically map longitude to equal-tempered pitch and call the musical design complete. Offer this as one preset experiment alongside more carefully bounded mapping approaches. Do not normalize species confidence scores as if every provider uses the same calibration.

### E. Reference synth and effects

Build a small original DSP instrument sufficient to hear the concept: a few contrasting voices such as soft tonal pluck, sustained pad, resonant/noise texture and short bell-like event. These are audition defaults, not the final sound palette.

Provide per-layer envelope, tuning, gain, pan, filter and sends; controlled polyphony; click-free parameter smoothing; stereo output; master trim, meter and sensible headroom. A modest delay and reverb may supply continuity between events. Avoid a huge effects suite before data and replay are reliable.

Expose simple performance macros, provisionally Density, Motion, Texture and Space, with transparent underlying assignments. Keep richer designer-level parameters available. Optional MIDI output should be an extension where host/format support permits, not a prerequisite for internal synthesis.

Never fetch network data, allocate unbounded memory, parse JSON, access SQLite or block on locks on the audio callback. Implement bounded event transfer and define overflow behaviour.

### F. Density, diversity and silence

- Master event-rate limit and separate per-source/species/station/individual limits.
- Duplicate suppression and optional repeated-call grouping with visible counts.
- Quotas/weights to balance high-volume birds with rare individual-track events.
- Confidence and quality thresholds with clear provider-specific semantics.
- Burst handling choices: direct, capped, grouped or scheduled within a chosen musical window. Preserve original timestamps and show the transformation.
- Strict event-driven mode that permits silence.
- Optional sustain/resonance mode allowing existing voices to decay or evolve between arrivals.
- Optional derived generative continuation, clearly labelled, with its own controls and provenance; no fake animal events inserted to fill gaps.
- Pause listening, mute a source, solo a species and immediately stop all notes/tails safely.

### G. Collection, sessions and replay

Allow collect-only mode without running audio, and simultaneous collect-and-play. Define useful recording sessions of 1, 2 or 4 hours plus custom duration. This is app functionality, not a request to schedule an external automation now.

Provide three distinct modes:

1. Live arrivals: dispatch genuinely newly discovered eligible events when received.
2. Observation timeline: replay by original observation time, including late-uploaded records at their historical position.
3. Accelerated replay: apply an adjustable factor to the selected recorded timeline, e.g. four hours into four minutes at 60×.

Also retain an arrival-timeline replay option to reproduce how a live session actually unfolded. Expose observed-time versus arrival-time explicitly.

Include start/stop, range selection, scrub, loop, zoom, bookmarks and deterministic replay seed. Speed changes must not change audio pitch unless explicitly requested. Define note-duration scaling, overlapping voices, effect-tail handling, seek/reset semantics and maximum event density at extreme speeds.

A saved session snapshots eligible events, event ordering, mappings, transformations, routing, synth parameters, software/schema versions and seeds. Revisiting a piece must not depend on today's feed state. Event/mapping determinism is required; bit-identical audio is only claimed if tested for the specific platform and renderer.

Export audio WAV, normalized events as JSON/CSV, session bundle and optional generated MIDI. Include metadata/attribution and prevent exporting credentials or restricted raw payloads. Define event-time range separately from collection-time range.

### H. Source health and activity analytics

Measure unique events/hour, unique animals/stations/species, event-active days, inter-event median/mean/percentiles/max, longest ongoing quiet gap, batches and publication lag where measurable. Distinguish locations recorded from records newly published. Do not report publication lag as zero when the provider does not supply it.

Measure both pre-filter and post-filter musical activity. Calculate aggregate across-source inter-event statistics from merged timestamps, rather than averaging per-animal averages. Deduplicate exact duplicate records; simultaneous legitimate events may share timestamps. Preserve timezone context for calendar-day counts.

Include source connection health, last successful fetch, latest observation, ingestion lag if known, schema errors and last error. Healthy connection plus stale observations is a valid state. New events may be available even if an individual remains quiet.

### I. Plug-in integration

Use a shared engine for standalone and VST3. Add Audio Unit only if practical and useful after the initial slice. Validate host support and packaging requirements.

The plug-in should attach to an existing collector or run a managed worker outside audio processing. Define discovery, versioned IPC, reconnect, message size limits, per-instance subscriptions and multiple-instance behaviour. Choose a local socket or authenticated loopback transport after inspecting environment constraints. Avoid multiple DAW instances hammering providers independently.

Support host tempo and transport, sample-rate/block-size changes, automation, state restoration and offline rendering from a fixed recorded session. Define stopped-transport behaviour and whether live events queue, discard or continue under an explicit free-running mode. Offline bounce must not request future live data or wait on the network.

Preset/session separation: a preset stores an instrument configuration; a session also stores a fixed event timeline. DAW state stores stable references and enough snapshot data to recover when the collector is unavailable, within reasonable size limits. Avoid absolute machine-specific paths as the sole recovery route.

## 7. Visual design brief

Aim for an evocative scientific atlas that is also playable. A large map occupies most of the screen. Keep the main musical controls visible in a compact bottom or side panel. Put source health and filtering in a collapsible drawer and session transport/timeline in a stable location. Design for a resizable plug-in window and standalone full-screen use.

Use a quiet dark ocean/land base, restrained typography and a limited colour system. Ripples should communicate an actual event and decay clearly. Trails show histories, never fabricated measured travel. Optional ambient motion is visually distinct from telemetry. A station can pulse repeatedly without its marker moving. Avoid dense decorative controls and gratuitous camera motion.

Provide useful first-use states: connecting, no credentials, no sources selected, fresh events incoming, source quiet, offline cached session, empty replay and demo. Demo material must be conspicuously labelled. An empty map should explain the relevant state rather than looking broken.

Keep implementation jargon out of the normal performance UI. Put endpoint diagnostics and raw records in an advanced inspector. Attribution and freshness should still be easy to find.

Map implementation must respect tile/style licensing and attribution. Do not ship someone else's map access token found in a public bundle. Evaluate a modest 2D map before committing to a globe or heavy embedded web engine. Explain CPU/GPU tradeoffs inside a DAW. Unity is not a requirement.

## 8. Architecture and implementation constraints

Choose a defensible stack after environment inspection, with a short decision record. A reasonable decomposition is:

- Source adapters and scheduled acquisition.
- Raw data/provenance store and normalized event store.
- Source registry, identity reconciliation, validation and deduplication.
- Session recorder and deterministic timeline scheduler.
- Mapping engine producing bounded musical control events.
- Real-time DSP engine.
- Map/instrument UI and diagnostics.
- Versioned local transport between collector and instrument where needed.

Prefer a local database such as SQLite for the first version, with indexes for source/id/time and sessions. Use migrations and interruption-safe writes. Bound raw-response retention, queues and caches; expose disk use and retention preferences. Keep indexes and recordings separate from downloaded map tiles.

Specify schema versions, adapter capabilities, cursor persistence, startup recovery, transaction boundaries, correction handling and export format. Use monotonic clocks for scheduling intervals and UTC for record timestamps; handle system clock changes. State the delivery semantics and deduplication strategy rather than claiming impossible universal exactly-once behaviour.

Security basics: TLS validation, credentials outside repository/session exports, redacted logging and loopback IPC access protection. Preserve provider obfuscation and publication restrictions. Technical access and permission to distribute/sonify must be tracked separately. Do not block independent implementation while a particular source awaits permission.

## 9. Staged build and delivery gates

### Stage 0 — inspect and prove access

Inspect repository, AGENTS.md, build tools and target platform. Revalidate priority source documentation. Make small read-only requests, save representative permitted fixtures and record exact query/time/window. Confirm at least one genuinely fresh source, then pursue one individual-track connector. Produce a source/access matrix with clear unknowns. Choose architecture and initial map approach.

Do not spend the whole project gathering candidates. If a provider blocks access, record the blocker and keep moving with sources that work.

### Stage 1 — first audible vertical slice

Deliver a runnable standalone application: a real connector, normalized persistence, map showing actual events, event inspector, one simple synth voice, bounded event dispatch and collect/play controls. Separate explicitly labelled demo data from live data. The build should be useful even before additional species are integrated.

### Stage 2 — individual journeys and source mix

Add one permitted individual-track source, deduplication across restarts, late-arrival handling, uncertainty display, filtering and source health. Add other high-value connectors according to measured contribution. Preserve unsupported connector leads in the registry without pretending they work.

### Stage 3 — recording and deterministic composition

Complete session snapshots, observed/arrival replay, speed control, seek/loop, density transforms and audio/data exports. Add reference voice diversity, macros and restrained effects after recording integrity is established.

### Stage 4 — VST integration

Share the proven engine with VST3, manage collector connection, host state/tempo/transport, automation and offline bounce. Validate in Ableton when available. Document manual host checks if this environment cannot run a DAW; do not claim host testing from a standalone test.

### Stage 5 — measured source evaluation and polish

Provide a collector that can run for 48–72 hours, plus reporting on volume, quiet gaps, latency and diversity. Run the soak test if the environment permits; otherwise deliver the exact command and clearly mark it unrun. Do not claim a multi-day measurement based on a short sample. Complete visual/performance polish and packaging after the core works.

## 10. Meaningful acceptance tests

- Repeated polling, reconnect and process restart do not retrigger already ingested records.
- First-run historical import does not explode into a live note burst.
- A late batch produces intentionally different arrival and observation replays, both reproducible.
- A station's repeated detections do not create a fictitious individual path or speed.
- Retagged animals remain one individual with distinct deployment histories.
- Map handles antimeridian crossings, missing coordinates and uncertainty.
- Provider timeout, HTTP 429, malformed record and empty successful response do not stall audio.
- High-volume bursts are bounded, keep audio stable and show any dropped/grouped musical actions.
- No network/database work runs on the audio thread; no stuck notes on stop, seek, source mute or disconnect.
- Replaying a saved session with the same seed/settings reproduces musical events in the same order at different block sizes within the stated timing tolerance.
- Accelerating a four-hour timeline to four minutes preserves event ordering and has documented duration/quantization policies.
- Offline rendering uses frozen data and finishes without live network availability.
- Secrets never appear in exports, presets or ordinary logs.
- Distinguish automated checks, direct live API tests, visual inspection and actual DAW tests in validation results.

## 11. Required outputs from this Codex session

1. Concise restatement of the product and any consequential assumptions.
2. Prioritised full feature specification: essential first slice, next release and later experiments.
3. Source registry with links, measured evidence, access/permission status and unresolved requirements.
4. Architecture decision, normalized schema and collector/instrument boundary.
5. Visual layout and interaction specification, including empty/quiet/offline states.
6. Staged build plan with concrete completion gates.
7. Working implementation through the earliest achievable useful stage, with build/run instructions and relevant tests.
8. Example recorded session or fixture, with truthful live/archive/demo provenance.
9. Remaining blockers and specific next actions, without overstating integration readiness.

Build toward an instrument where I can open a world map, select animal sources, collect a few hours of real activity, hear events forming a composition, and replay or accelerate that composition with confidence about where every event came from.
