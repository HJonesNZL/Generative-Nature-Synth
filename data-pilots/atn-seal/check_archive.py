"""Offline regression: source semantics, filtering and native replay compatibility."""
import copy
import importlib.util
import json
from pathlib import Path
import sqlite3
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
spec = importlib.util.spec_from_file_location('prepare_archive', HERE/'prepare_archive.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
metadata = json.loads((HERE/'source/metadata.json').read_text())
table = json.loads((HERE/'source/locations.json').read_text())['table']
records, omitted, _ = module.normalize(metadata, table)
assert len(records) == 224 and omitted == {'non_pass_qartod':39}
assert records[0][0]['observed_at_utc'] == '2018-04-14T04:48:22.000000Z'
assert records[-1][0]['observed_at_utc'] == '2018-11-03T08:01:10.000000Z'
assert all(r['provenance_mode'] == 'imported' and r['station_id'] is None for r,_ in records)
assert all(r['individual_id'] != r['deployment_id'] for r,_ in records)
assert records[0][0]['uncertainty_m'] == 3892
bad = copy.deepcopy(table)
bad['rows'][0][bad['columnNames'].index('animal')] = 'invented-animal'
try:
    module.normalize(metadata, bad)
except AssertionError:
    pass
else:
    raise RuntimeError('animal/deployment mismatch accepted')
with tempfile.TemporaryDirectory(prefix='atlas-atn-check-') as temp:
    output = Path(temp)/'archive'
    subprocess.run(['python3','-B',str(HERE/'prepare_archive.py'),'--output',str(output)], check=True, stdout=subprocess.DEVNULL)
    library = output/'ATN spotted seal 2018.sqlite3'
    expected={'ATN spotted seal 2018.sqlite3','README.md','manifest.json','source-metadata.json','source-locations.json'}
    assert {p.name for p in output.iterdir()} == expected
    import hashlib
    manifest=json.loads((output/'manifest.json').read_text())
    assert all(hashlib.sha256((output/name).read_bytes()).hexdigest()==value for name,value in manifest['files_sha256'].items())
    with sqlite3.connect(library) as db:
        assert db.execute('SELECT count(*),sum(live_eligible) FROM revisions').fetchone() == (224,0)
        assert sorted(row[0] for row in db.execute('SELECT event_count FROM sessions')) == [5,224]
    native = ROOT/'build/native/atlas-native'
    result = subprocess.run([str(native),'replay','--db',str(library),'--session','latest','--speed','3600','--limit','5'],check=True,capture_output=True,text=True)
    replay = json.loads(result.stdout)
    assert replay['event_count'] == 5 and len(replay['events']) == 5
    assert 'verified' in replay['integrity']
    assert all(x['event']['event_kind']=='satellite_fix' for x in replay['events'])
print('ATN: 263 real records, 224 accepted, 39 excluded; identity/UTC/QC/uncertainty, zero live arrivals, native 5-fix replay verified')
