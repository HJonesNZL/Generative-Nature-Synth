# Stored event v1 boundary

M1 freezes normalized events in canonical JSON, at most 16 KiB per event and 50,000 per piece. Validation runs on collection/storage workers, never an audio callback. Unknown fields and duplicate object keys fail closed. JSON numbers must be finite; booleans are not numbers. Text is valid UTF-8 without NUL, nonempty when present; optional unknown text may be absent or null. IDs allow 512 UTF-8 bytes, source IDs 128, other text 2048. Provider identity is preserved verbatim; event identity must equal `source_id + ':' + provider_event_id`. Optional ecological IDs require a nonempty namespace and components. This does not prove identity reconciliation.

Required fields: integer schema_version=1; event_id; source_id; provider_event_id; observed_at_utc; event_kind; location_kind; latitude; longitude; coordinate_order; provenance_mode. Frozen material additionally requires first_seen_at_utc and ingested_at_utc. Optional fields are enumerated in model.py and independently in the native validator; all are exercised by the common fixture corpus.

| Event kind | Location kind |
| --- | --- |
| gps_fix / satellite_fix | individual_fix |
| acoustic_receiver_detection | receiver_detection |
| acoustic_species_detection | station_detection |
| human_observation | reported_location |

These are schema categories, not claims that adapters already support every category. Station species detections cannot carry individual or deployment IDs. Both coordinates must be numeric and in geographic range, or both null. Coordinate order is explicitly latitude,longitude. Uncertainty is nullable and nonnegative. Confidence is nullable, finite and requires a provider-specific meaning when present; no universal 0–1 calibration is imposed. Provider scores are at most 32 named finite numeric/null values; nested objects/arrays are unsupported. Quality flags are at most 64 nonempty strings, each at most 128 bytes.

Timestamps use YYYY-MM-DDTHH:MM:SS, optional 1–6 fractional digits, and Z or an explicit ±HH:MM offset. Dates and offsets must be valid; converted UTC must remain within years 1–9999. Historical frozen validation has no wall-clock freshness requirement. Adapter ingress retains its future-time sanity check. Provenance is live_source, imported or demo; replaying an archive does not rewrite source provenance.

Python replay/export and native replay verify metadata and frozen events within one SQLite read snapshot. Both reject stored noncanonical event bytes that violate the writer contract. The shared event_semantics_v1.json fixture includes independently labelled valid/invalid cases; native checks are not based on Python's verdict. Exact epoch checks cover 1900, 2040 and the supported year endpoints to prevent 32-bit intermediate overflow.
