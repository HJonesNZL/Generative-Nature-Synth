#!/usr/bin/env python3
"""Launch the actual bundled WKWebView and validate its native bridge receipt.

All writable files live in a temporary state directory. This never enables audio,
attaches the owner's library, or alters external collectors. The explicit --live
option contacts public providers using only a disposable managed library. A supplied scene is
fully verified and imported as an independent copy by the app.
"""
import argparse
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
from audio_oracle import read_wave


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--scene', type=Path)
    parser.add_argument('--live', action='store_true', help='Opt in to actual public-provider HTTP in disposable state')
    parser.add_argument('--seconds', type=int, default=3)
    parser.add_argument('--evidence', type=Path)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 600:
        parser.error('--seconds must be 1..600')
    with tempfile.TemporaryDirectory(prefix='atlas-standalone-smoke-') as temporary:
        folder = Path(temporary)
        receipt = folder / 'receipt.json'
        exported = folder / 'exported.wav'
        scene_export = folder / 'exported.atlas-scene'
        backup = folder / 'backup.atlas-project'
        restored = folder / 'restored-state'
        command = [str(args.binary.resolve()), '--state-dir', str(folder / 'state'),
                   '--smoke-test', str(receipt), '--smoke-seconds', str(args.seconds)]
        if args.live:
            command += ['--smoke-live', '1']
        if args.scene:
            command += ['--smoke-scene', str(args.scene.resolve()), '--smoke-export', str(exported),
                        '--smoke-scene-export', str(scene_export), '--smoke-backup', str(backup),
                        '--smoke-restore', str(restored)]
        # Inherited owner paths cannot influence this shell; explicit state is
        # authoritative. Remove old development bridge options for clarity.
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith('ATLAS_')}
        result = subprocess.run(command, env=environment, capture_output=True, text=True,
                                timeout=args.seconds + (150 if args.live else 90), check=False)
        if result.returncode:
            raise AssertionError(f'App exited {result.returncode}: {result.stderr[-3000:]}')
        evidence = json.loads(receipt.read_text())
        if args.evidence:
            args.evidence.parent.mkdir(parents=True, exist_ok=True)
            args.evidence.write_text(json.dumps(evidence, indent=2) + '\n')
        assert evidence['ok'], evidence
        assert evidence['shutdownComplete'], evidence
        assert evidence['audioEnabled'] is False, evidence
        assert evidence['nativeRequests'] >= 5, evidence
        assert evidence['webview']['rounds'] >= args.seconds * 5, evidence
        assert evidence['webview']['capabilities']['implementation'] == 'native', evidence
        assert evidence['webview']['capabilities']['libraryAvailable'] is args.live, evidence
        assert evidence['webview']['dom']['buttons'], evidence
        assert evidence['webview']['audio']['status'] != 'running', evidence
        assert evidence['webview']['deviceUI']['visible'], evidence
        assert evidence['webview']['audioDevices']['available'], evidence
        assert evidence['webview']['afterDiscoveryAudio']['status'] != 'running', evidence
        assert evidence['webview']['dom']['title'] == 'Animal Atlas', evidence
        if args.live:
            assert evidence['webview']['liveStarted']['collecting'], evidence
            assert evidence['webview']['liveUI']['speciesRows'] > 0, evidence
            assert evidence['webview']['liveIncoming']['pings'] > 0, evidence
            assert evidence['webview']['liveStopped']['collecting'] is False, evidence
            assert set(evidence['webview']['liveMap']['sources']) == {'birdweather', 'nzsharks'}, evidence
        if args.scene:
            # The existing oracle validates RIFF extents, extensible PCM,
            # stereo/24-bit channel layout, fact length and embedded recipe.
            rate, pcm, recipe, raw = read_wave(exported)
            frames = len(raw) // 6
            assert rate in (44100, 48000, 96000)
            assert frames > 0 and len(pcm) == frames * 2
            assert recipe['channel_count'] == 2 and recipe['pcm_bits'] == 24
            assert re.fullmatch(r'[0-9a-f]{64}', recipe['engine_source_sha256'])
            evidence['exportedWav'] = {'channels': 2, 'pcmBits': 24,
                                      'sampleRate': rate, 'frames': frames,
                                      'bytes': exported.stat().st_size,
                                      'engineSourceSha256': recipe['engine_source_sha256'],
                                      'sceneHash': recipe['scene_hash']}
            assert evidence['webview']['render']['state'] == 'complete', evidence
            scenes = list((folder / 'state' / 'soundscapes').glob('*.atlas-scene'))
            assert len(scenes) == 1
            assert args.scene.resolve().read_bytes() == scenes[0].read_bytes()
            assert evidence['sceneExported'] and evidence['projectRestored'], evidence
            assert scene_export.read_bytes() == scenes[0].read_bytes()
            assert backup.is_file() and backup.stat().st_size > 0
            restored_scenes = list((restored / 'soundscapes').glob('*.atlas-scene'))
            assert len(restored_scenes) == 1
            assert restored_scenes[0].name != scenes[0].name
            assert restored_scenes[0].read_bytes() == scenes[0].read_bytes()
            assert (restored / 'voices.sqlite3').is_file()
            assert not (restored / 'settings.json').exists()
            assert not (restored / 'exports').exists() or not list((restored / 'exports').iterdir())
            evidence['projectFiles'] = {'backupBytes': backup.stat().st_size,
                                        'sceneBytes': scene_export.stat().st_size,
                                        'restoredSceneCount': len(restored_scenes),
                                        'freshSceneIdentity': True}
        if args.evidence:
            args.evidence.parent.mkdir(parents=True, exist_ok=True)
            args.evidence.write_text(json.dumps(evidence, indent=2) + '\n')
        print(json.dumps({'ok': True, 'requests': evidence['nativeRequests'],
                          'rounds': evidence['webview']['rounds'],
                          'elapsedSeconds': evidence['elapsedSeconds'],
                          'exportedWav': evidence.get('exportedWav')}))


if __name__ == '__main__':
    main()
