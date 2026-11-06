#!/usr/bin/env python3
"""Deterministic synthetic development comparisons; never accesses owner state."""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
import math
from pathlib import Path
import random
import subprocess
import sys
import tempfile
from unittest.mock import patch
import wave
ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT/'tests')]
from animal_atlas.model import event, stamp
from animal_atlas.store import Store
from animal_atlas.sessions import Sessions
from audio_oracle import read_wave
RATE = 48000
SEED = 20261107


def timestamp(us):
    return stamp(datetime(2026, 1, 1, tzinfo=timezone.utc)+timedelta(microseconds=us))


def pcm(path, values):
    raw = bytearray()
    for value in values:
        if not math.isfinite(value) or abs(value)>1:
            raise ValueError('Invalid PCM')
        raw.extend(round(value*8388607).to_bytes(3, 'little', signed=True)*2)
    with wave.open(str(path), 'wb') as output:
        output.setparams((2, 3, RATE, 0, 'NONE', 'not compressed'))
        output.writeframes(raw)
    return [int.from_bytes(raw[i:i+3], 'little', signed=True)/8388607 for i in range(0,len(raw),6)]


def sine(age):
    a, d, r = 384, 31200, 960
    if age<0 or age>=a+d+r:
        return 0.0
    attack = (1-math.cos(math.pi*age/a))/2 if age<a else 1
    decay = math.exp(-math.log(1000)*max(0,age-a)/d)
    release = (1+math.cos(math.pi*(age-a-d)/r))/2 if age>=a+d else 1
    return .05*attack*decay*release*math.sin(2*math.pi*(440*2**((60-69)/12))*age/RATE)


def rms(values):
    return math.sqrt(math.fsum(v*v for v in values)/len(values))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    rng = random.Random(SEED)
    manifest = dict(seed=SEED, role='synthetic development; not held-out wildlife',
        listening='not_run', hardware='not_run', sample_rate=RATE,
        matching='whole-excerpt RMS including gaps; fixed attenuation to quieter original',
        settings=dict(root_midi=60, attack_seconds=.008, decay_seconds=.650, gain_db=0, speed=1), excerpts=[])
    with tempfile.TemporaryDirectory(prefix='atlas-listening-') as temporary:
        for index, (name, offsets, duration) in enumerate([
            ('sparse', [100001,1100000,2200000],3200000),
            ('dense', [100001]*4+[110001]*4,1200000)]):
            store = Store(Path(temporary)/(name+'.sqlite3'))
            records = [(event('listening-fixture',str(i),timestamp(us),
                'acoustic_species_detection','station_detection',
                attribution='Synthetic development fixture; not wildlife',provenance_mode='demo'),{}) for i,us in enumerate(offsets)]
            with patch('animal_atlas.store.stamp',side_effect=lambda value=None: stamp(value) if value is not None else timestamp(4000000)):
                store.ingest('listening-fixture',records,True)
            with patch('animal_atlas.sessions.uuid.uuid4',return_value=f'00000000-0000-4000-8000-{index:012d}'), patch('animal_atlas.sessions.stamp',side_effect=lambda value=None: stamp(value) if value is not None else timestamp(5000000)):
                saved = Sessions(store).snapshot('Synthetic listening '+name,observed_from=timestamp(0),observed_to=timestamp(duration))
            if saved['event_count']!=len(offsets):
                raise ValueError('Fixture selection mismatch')
            original = output/(name+'-candidate-original.wav')
            command = [str(binary),'render','--db',store.path,'--session',saved['id'],'--speed','1','--output',str(original)]
            result = json.loads(subprocess.run(command,check=True,capture_output=True,text=True).stdout)
            rate, stereo, recipe, _ = read_wave(original)
            if rate!=RATE or result['faults'] or result['stolen']:
                raise ValueError('Unexpected render state')
            candidate = stereo[::2]
            onsets = [(us*RATE+999999)//1000000 for us in offsets]
            baseline_path = output/(name+'-sine-original.wav')
            baseline = pcm(baseline_path,[sum(sine(frame-onset) for onset in onsets) for frame in range(len(candidate))])
            values = [candidate,baseline]
            levels = list(map(rms,values))
            target = min(levels)
            order = [0,1]
            rng.shuffle(order)
            pairs = {}
            for label,kind in zip(('A','B'),order):
                gain = target/levels[kind]
                path = output/(name+'-'+label+'.wav')
                matched = pcm(path,[v*gain for v in values[kind]])
                if abs(rms(matched)-target)>1e-7:
                    raise ValueError('RMS match exceeds PCM tolerance')
                pairs[label] = dict(file=path.name,identity=['candidate','neutral_sine'][kind],gain=gain,
                    original_rms=levels[kind],matched_rms=rms(matched),peak=max(map(abs,matched)))
            manifest['excerpts'].append(dict(name=name,offsets_us=offsets,duration_us=duration,
                recipe=recipe,render_summary=result,comparisons=pairs,originals=[original.name,baseline_path.name]))
    manifest['files_sha256'] = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(output.glob('*.wav'))}
    (output/'manifest-reveal.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(output)


if __name__=='__main__':
    main()
