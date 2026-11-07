#!/usr/bin/env python3
"""Explicit packaged WKWebView import/reload/replay/save/export regression.

Uses only bundled verified animal history and disposable application state.
Never enables listening, collection, or provider requests.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import tempfile
from audio_oracle import read_wave


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--library', type=Path)
    parser.add_argument('--evidence', type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    library = (args.library or binary.parent.parent / 'Resources' / 'historical' /
               'ATN-spotted-seal-2018' / 'ATN spotted seal 2018.sqlite3').resolve()
    if not library.is_file():
        candidates = list((binary.parent.parent / 'Resources').rglob('ATN spotted seal 2018.sqlite3'))
        if len(candidates) != 1:
            raise AssertionError('Expected one packaged verified historical library')
        library = candidates[0]
    original_hash = hashlib.sha256(library.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory(prefix='atlas-piece-workflow-') as temporary:
        folder = Path(temporary)
        receipt, exported, state = folder/'receipt.json', folder/'piece.wav', folder/'state'
        environment = {key: value for key, value in os.environ.items() if not key.startswith('ATLAS_')}
        whole_receipt = folder/'whole-receipt.json'
        whole_process = subprocess.run([str(binary), '--state-dir', str(folder/'whole-state'), '--library', str(library),
                                        '--smoke-test', str(whole_receipt), '--smoke-pieces', 'whole', '--smoke-seconds', '1'],
                                       env=environment, capture_output=True, text=True, timeout=90, check=False)
        assert whole_process.returncode == 0 and whole_receipt.is_file(), whole_process.stderr
        whole_evidence = json.loads(whole_receipt.read_text())
        assert whole_evidence['ok'] and whole_evidence['shutdownComplete'], whole_evidence
        whole = whole_evidence['webview']['wholeDeployment']
        assert whole['id'] == 'd774207b-342c-49bd-a328-5649f9de84ce' and whole['eventCount'] == 224, whole
        assert whole['highlighted'] and whole['basis'] == 'observed' and whole['fitLabel'].startswith('Fit full interval'), whole
        assert whole['preparation']['preparationId'] and 0 < int(whole['preparation']['durationUs']) <= 1800000000, whole
        process = subprocess.run([str(binary), '--state-dir', str(state), '--library', str(library),
                                  '--smoke-test', str(receipt), '--smoke-pieces', '1',
                                  '--smoke-export', str(exported), '--smoke-seconds', '2'],
                                 env=environment, capture_output=True, text=True, timeout=100, check=False)
        if process.returncode or not receipt.is_file():
            raise AssertionError(f'Packaged app failed ({process.returncode}): {process.stderr[-3000:]}')
        evidence = json.loads(receipt.read_text())
        if args.evidence:
            args.evidence.parent.mkdir(parents=True, exist_ok=True)
            args.evidence.write_text(json.dumps(evidence, indent=2)+'\n')
        assert evidence['ok'] and evidence['shutdownComplete'], evidence
        assert evidence['audioEnabled'] is False, evidence
        flow = evidence['webview']['pieceWorkflow']
        assert flow['managed'] and not flow['collecting'], flow
        assert flow['imported']['status'] == 'completed', flow
        assert flow['imported']['piece']['eventCount'] == 5, flow
        assert flow['transport']['preparationId'] and flow['transport']['state'] != 'playing', flow
        assert flow['saved']['valid'] and flow['saved']['eventCount'] == 5, flow
        databases = list(state.rglob('*.sqlite3'))
        copies = []
        for database in databases:
            with sqlite3.connect(database.resolve().as_uri()+'?mode=ro&immutable=1', uri=True) as connection:
                tables = {row[0] for row in connection.execute("SELECT name FROM sqlite_master WHERE type='table'")}
                if {'sessions', 'session_events', 'metadata'} <= tables:
                    marker = connection.execute("SELECT value FROM metadata WHERE key='native_collection_owner'").fetchone()
                    if marker:
                        copies.append((database, connection.execute('SELECT count(*) FROM revisions').fetchone()[0],
                                       connection.execute('SELECT count(*) FROM events').fetchone()[0],
                                       connection.execute('SELECT event_count,envelope_version FROM sessions WHERE id=?',
                                                          (flow['imported']['piece']['id'],)).fetchone()))
        assert len(copies) == 1 and copies[0][1:] == (0, 0, (5, 1)), copies
        rate, pcm, recipe, raw = read_wave(exported)
        assert rate in (44100, 48000, 96000) and len(raw) > 0 and len(pcm) > 0
        assert any(sample != 0 for sample in pcm), 'Exported PCM is entirely silent'
        assert recipe['channel_count'] == 2 and recipe['pcm_bits'] == 24
        assert hashlib.sha256(library.read_bytes()).hexdigest() == original_hash
        evidence['wholeDeployment'] = whole
        evidence['checks'] = {'wholeDeploymentObservedPrepared': True, 'exportedPcmNonzero': True, 'sourceUnchanged': True, 'managedLiveRecords': 0, 'sealedImportedEvents': 5,
                              'reactPreparedAfterManagedReload': True, 'savedSoundscapeEvents': 5,
                              'exportedWavBytes': exported.stat().st_size, 'physicalAudioEnabled': False}
        if args.evidence:
            args.evidence.write_text(json.dumps(evidence, indent=2)+'\n')
        print(json.dumps({'ok': True, **evidence['checks']}))


if __name__ == '__main__':
    main()
