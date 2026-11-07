"""Development-only preparation; resulting SQLite library plays in native standalone."""
import argparse
import gc
from contextlib import closing
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import sys
import sqlite3
import shutil

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from animal_atlas.model import event, canonical, parse_time
from animal_atlas.store import Store
from animal_atlas.sessions import Sessions

HERE = Path(__file__).resolve().parent
DATASET = 'atn_174822_spotted-seal_trajectory_20180414-20181103'
SOURCE = 'atn-seal-history'
DEPLOYMENT = '5ad19120efec72080c8921a9'
URL = 'https://atn.ioos.us/erddap/tabledap/' + DATASET + '.html'

def normalize(metadata, table):
    attributes = {(r[1], r[2]): r[4] for r in metadata['table']['rows'] if r[0] == 'attribute'}
    assert attributes['NC_GLOBAL', 'animal_id'] == 'PL2018_1009'
    assert attributes['NC_GLOBAL', 'deployment_id'] == DEPLOYMENT
    assert attributes['time', 'units'] == 'seconds since 1970-01-01T00:00:00Z'
    assert table['columnUnits'][table['columnNames'].index('time')] == 'UTC'
    license_text = attributes['NC_GLOBAL', 'license']
    assert 'used and redistributed for free' in license_text
    attribution = ('U.S. Animal Telemetry Network (ATN); Josh London, NOAA Alaska Fisheries Science Center; ' +
        attributes['NC_GLOBAL', 'acknowledgement'])
    records, omitted = [], Counter()
    for values in table['rows']:
        assert len(table['columnNames']) == len(values)
        raw = dict(zip(table['columnNames'], values))
        assert raw['animal'] == 'PL2018_1009' and raw['trajectory'] == DEPLOYMENT
        assert raw['ptt'] == 174822 and raw['deploy_id'] == 'PL2018_1009_17U2917'
        parse_time(raw['time'])
        if raw['qartod_rollup_flag'] != 1:
            omitted['non_pass_qartod'] += 1
            continue
        if raw['type'] != 'Argos':
            omitted['non_argos_location'] += 1
            continue
        raw = {k: None if isinstance(v, float) and not math.isfinite(v) else v for k,v in raw.items()}
        radius = raw['error_radius']
        flags = ['historical_archive', 'argos_satellite_location', 'provider_instrument_argos_or_modeled',
            'no_interpolation_or_continuous_movement', 'argos_location_class:' + str(raw['location_class'])]
        flags += ['qartod_' + key + ':' + str(raw['qartod_' + key + '_flag'])
                  for key in ['time', 'speed', 'location', 'rollup']]
        record = event(SOURCE, DEPLOYMENT + ':' + raw['time'], raw['time'],
            'satellite_fix', 'individual_fix', raw['latitude'], raw['longitude'],
            individual_id='atn:' + DATASET + ':animal:' + raw['animal'],
            deployment_id='atn:deployment:' + DEPLOYMENT, provider_subject_id='atn:animal:' + raw['animal'],
            individual_display_name='Spotted seal PL2018_1009 (2018 archive)',
            scientific_name='Phoca largha', common_name='Spotted seal', species_id='worms:255021',
            uncertainty_m=radius if isinstance(radius, (int,float)) and radius >= 0 else None,
            provenance_mode='imported', permissions_status='permitted_with_attribution',
            attribution=attribution, source_url=URL, quality_flags=flags)
        records.append((record, raw))
    records.sort(key=lambda pair: pair[0]['observed_at_utc'])
    assert len({r[0]['event_id'] for r in records}) == len(records)
    return records, dict(omitted), license_text

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    metadata = json.loads((HERE/'source/metadata.json').read_text())
    table = json.loads((HERE/'source/locations.json').read_text())['table']
    records, omitted, license_text = normalize(metadata, table)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    library = output/'ATN spotted seal 2018.sqlite3'
    store = Store(library)
    store.ingest(SOURCE, records, True, dict(context='historical_archive', omitted=omitted,
        source_url=URL, permissions_status='permitted_with_attribution', enabled=False))
    session = Sessions(store).snapshot('Spotted seal PL2018_1009 · 2018 historical satellite fixes',
        sources=[SOURCE], observed_from=records[0][0]['observed_at_utc'],
        observed_to='2018-11-04T00:00:00Z')
    excerpt = Sessions(store).snapshot('Spotted seal · final five fixes · 2–3 Nov 2018',
        sources=[SOURCE], observed_from='2018-11-02T00:00:00Z', observed_to='2018-11-04T00:00:00Z')
    # Store uses short-lived SQLite contexts; release their reference cycles
    # before finalizing this newly created, exclusively owned archive.
    gc.collect()
    with closing(sqlite3.connect(library)) as db:
        db.execute('PRAGMA wal_checkpoint(TRUNCATE)')
        db.execute('PRAGMA journal_mode=DELETE')
    sidecar = Path(str(library) + '-shm')
    if sidecar.exists() and not Path(str(library) + '-wal').exists():
        sidecar.unlink()
    shutil.copyfile(HERE/'README-library.md', output/'README.md')
    shutil.copyfile(HERE/'source/metadata.json', output/'source-metadata.json')
    shutil.copyfile(HERE/'source/locations.json', output/'source-locations.json')
    manifest = dict(files_sha256={name:hashlib.sha256((output/name).read_bytes()).hexdigest() for name in [library.name, 'README.md', 'source-metadata.json', 'source-locations.json']}, excerpt_session_id=excerpt['id'], dataset=DATASET, library=library.name, session_id=session['id'],
        raw_records=len(table['rows']), accepted_records=len(records), omitted=omitted,
        observation_start=records[0][0]['observed_at_utc'], observation_end=records[-1][0]['observed_at_utc'],
        source_url=URL, license=license_text,
        raw_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (HERE/'source').iterdir()},
        context='Historical identified animal fixes, not live. No interpolation. Native replay requires no Python.')
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(canonical(manifest))

if __name__ == '__main__': main()
