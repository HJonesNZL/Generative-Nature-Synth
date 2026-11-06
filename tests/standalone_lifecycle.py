#!/usr/bin/env python3
"""Actual app restart/state-lock checks, with isolated files and disabled output."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--evidence', required=True, type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    results = {}
    with tempfile.TemporaryDirectory(prefix='atlas-shell-lifecycle-') as temporary:
        root = Path(temporary)
        state = root / 'state'
        state.mkdir()
        settings = state / 'settings.json'
        original = json.dumps({'version': 1, 'library': str(root / 'absent.sqlite3'),
                               'output': {'mode': 'explicitDevice', 'deviceId': 'juce-0000000000000000',
                                          'bufferFrames': 256}})
        settings.write_text(original)

        def launch(name, seconds=1):
            receipt = root / (name + '.json')
            process = subprocess.Popen([str(binary), '--state-dir', str(state), '--smoke-test',
                                        str(receipt), '--smoke-seconds', str(seconds)],
                                       cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return process, receipt

        def collect(job, success=True):
            process, receipt = job
            try:
                code = process.wait(timeout=75)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                raise
            assert code == 0, code
            data = json.loads(receipt.read_text())
            assert data['ok'] is success, data
            assert data['shutdownComplete'] is True, data
            assert data['audioEnabled'] is False, data
            return data

        for name in ('missing-selection', 'restart'):
            data = collect(launch(name))
            assert settings.read_text() == original
            assert data['webview']['audioDevices']['configuration']['deviceId'] == 'juce-0000000000000000'
            assert 'unavailable' in data['nativeStatus'] and 'saved path retained' in data['nativeStatus'], data
            assert data['webview']['afterDiscoveryAudio']['callbackCount'] == '0'
            results[name] = data

        invalid = '{"version":1,"library":null,"output":null,"version":1}'
        settings.write_text(invalid)
        data = collect(launch('invalid-state'))
        assert settings.read_text() == invalid
        assert 'duplicate fields' in data['nativeStatus'] and 'retained' in data['nativeStatus'], data
        results['invalid-state'] = data
        settings.unlink()

        # The first app is held alive in actual WebKit. Wait for its VoiceStore
        # creation, then give its window a moment to initialise before contention.
        (state / 'voices.sqlite3').unlink()
        first = launch('lock-owner', seconds=6)
        try:
            deadline = time.monotonic() + 15
            while not (state / 'voices.sqlite3').exists():
                assert first[0].poll() is None and time.monotonic() < deadline
                time.sleep(.05)
            second = collect(launch('same-state-rejected'), success=False)
            assert 'already open' in second['nativeStatus'], second
            assert second['nativeRequests'] == 0, second
            results['same-state-rejected'] = second
            results['lock-owner'] = collect(first)
        finally:
            if first[0].poll() is None:
                first[0].kill()
                first[0].wait()
        results['after-lock-release'] = collect(launch('after-lock-release'))
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps({'passed': list(results), 'physicalOutputEnabled': False}))


if __name__ == '__main__':
    main()
