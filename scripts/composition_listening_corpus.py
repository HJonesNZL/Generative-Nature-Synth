"""Generate explicitly synthetic sparse/dense compositions in a NEW folder.
Uses the native scene protocol and renderer. Never reads the owner library.
Run: python3 -B scripts/composition_listening_corpus.py --output Auditions/composition-v1
"""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import html
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from animal_atlas.model import event, stamp
from animal_atlas.store import Store
from animal_atlas.sessions import Sessions

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
# Reuse the bounded test-protocol client; it never starts physical audio.
sys.path.insert(0, str(ROOT / 'tests'))
sys.argv = [sys.argv[0], str(ROOT / 'build/native-playback/atlas-studio-fixture-host')]
from native_voice_checks import Client
species = [('2298', 'Ruru'), ('2561', 'Tūī'), ('1276', 'Korimako'),
           ('420', 'Tauhou'), ('4251', 'Kākā'), ('4456', 'Pīwakawaka')]
manifest = {'context': 'Synthetic demonstration events; original musical hypotheses, not recordings or listening-approved presets.', 'examples': []}
for name, density, spacing in [('sparse', 0, 1.75), ('dense', 2, .19)]:
    folder = output / name
    folder.mkdir()
    library = folder / 'synthetic.sqlite3'
    store = Store(library)
    records = []
    start = datetime(2026, 1, 1, tzinfo=timezone.utc)
    for index in range(int(24 / spacing)):
        identity, label = species[index % len(species)]
        record = event('composition-demo', str(index), stamp(start + timedelta(seconds=index * spacing)),
            'acoustic_species_detection', 'station_detection', -41, 174,
            species_id='birdweather:species:' + identity, common_name=label,
            station_id='composition-demo:station', provenance_mode='demo')
        records.append((record, {}))
    store.ingest('composition-demo', records, True)
    session = Sessions(store).snapshot('Synthetic ' + name + ' ensemble',
        observed_from='2026-01-01T00:00:00Z', observed_to='2026-01-01T00:00:26Z')
    client = Client(library, folder / 'voices.sqlite3', folder / 'starts.txt')
    try:
        prep = client.request('prepareReplay', dict(sessionId=session['id'], basis='observed', speedNumerator='1', speedDenominator='1'))
        settings = dict(enabled=True, tempoBpm=96, tonicPitchClass=2, scale='minorPentatonic', density=density, variation=.35, seed=812, quantize=True)
        applied = client.request('setComposition', dict(settings=settings, preparationId=prep['preparationId'], generation=prep['generation']))
        scene = client.request('saveSoundscape', dict(name='Synthetic ' + name + ' ensemble'))
    finally:
        client.close()
    scene_path = folder / 'soundscapes' / (scene['id'] + '.atlas-scene')
    wav = output / (name + '.wav')
    subprocess.run([str(ROOT / 'build/native-playback/atlas-native'), 'scene-render', '--scene', str(scene_path), '--output', str(wav)], check=True)
    manifest['examples'].append(dict(name=name, file=wav.name, sha256=hashlib.sha256(wav.read_bytes()).hexdigest(),
        sceneHash=scene['contentHash'], settings=settings, sourceEvents=len(records), generatedNotes=applied['generatedNotes'],
        suppressedSourceEvents=applied['suppressedSourceEvents'], suppressedPhraseNotes=applied['suppressed']))
(output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
items = ''.join('<h2>' + html.escape(item['name']) + '</h2><audio controls src="' + item['file'] + '"></audio>' for item in manifest['examples'])
(output / 'index.html').write_text('<!doctype html><meta charset="utf-8"><title>Composition candidates</title><h1>Composition candidates</h1><p>' + html.escape(manifest['context']) + '</p>' + items)
print(output / 'index.html')
