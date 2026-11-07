# ATN spotted seal: actual historical playback library

Downloaded 7 November 2026 UTC from NOAA/IOOS ATN's public ERDDAP. Raw source JSON and full metadata are checked in under `source/`; `prepare_archive.py --output NEW_FOLDER` reproducibly prepares a self-contained new folder containing the SQLite library, both raw source files, the user README and a SHA-256 manifest using existing development-only Store/Sessions tooling. The standalone opens the resulting SQLite file and replays natively; Python and network access are unnecessary for playback.

263 actual source rows cover 14 April–3 November 2018. We accept 224 Argos rows with QARTOD rollup PASS. We exclude 39 SUSPECT/FAIL rows (including the one User location); the original source file retains them. All four QC flags and Argos location classes remain attached to accepted events. PASS rollup does not imply every component test was evaluated. Error radii remain in metres, often kilometres; no precise track or interpolation is invented.

Animal PL2018_1009 is a real provider animal ID, scoped here to the dataset. Deployment UUID 5ad19120efec72080c8921a9 is independent. PTT 174822 and tag serial 17U2917 are equipment identities, never substituted for the animal. Provider `deploy_id` is the friendly combined label PL2018_1009_17U2917, not the authoritative deployment UUID. The source's instrument metadata says `argos / modeled`; individual rows say `Argos`, and both facts remain visible in event flags/raw metadata. We do not claim raw unmodeled sensor positions.

ERDDAP provides explicit UTC date strings and `time.units=seconds since 1970-01-01T00:00:00Z`. A contradictory `time.long_name` mentions 1990: we use returned UTC strings, never apply that erroneous epoch hint. The deployment's rounded global end says 08:00 but its actual final fix is 08:01:10; preserve actual observation time. Publication time is unknown; import times remain separate. All events are `imported`, never live eligible.

The dataset explicitly allows free use and redistribution, requires acknowledgement of ATN or specified citation, disclaims accuracy/legal use and encourages author collaboration. Full licence is in source metadata and deliverable manifest. Every event attributes U.S. ATN, Josh London/NOAA Alaska Fisheries Science Center and the dataset acknowledgements. Preserve attribution when exporting/sharing.

Source: https://atn.ioos.us/erddap/info/atn_174822_spotted-seal_trajectory_20180414-20181103/index.html

User artifact: `deliverables/ATN-spotted-seal-2018/ATN spotted seal 2018.sqlite3`. Choose wildlife library, select this file, then choose a saved piece. The final-five-fixes excerpt is suitable for a short replay; the complete-deployment piece retains real long gaps. Increase observed replay speed as desired (3600× makes a day about 24 seconds). No animal sounds are included; synthesizer output is musical interpretation.
