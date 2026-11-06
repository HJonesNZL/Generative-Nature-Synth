"""Explicit, bounded real-output smoke. Uses only disposable synthetic demo data.

The default mode is deliberately excluded from routine unittest/CTest: it enables
the current JUCE default output briefly at the runtime's fixed -18 dB setting.
--prepare-only exercises the actual host lifecycle without opening a device.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import selectors
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from animal_atlas.model import event
from animal_atlas.store import Store
from animal_atlas.sessions import Sessions


class ProtocolFailure(RuntimeError):
    pass


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot_binary(source):
    payload = source.read_bytes()
    identity = hashlib.sha256(payload).hexdigest()
    destination = ROOT / 'build' / ('native-playback-smoke-' + identity[:16]) / source.name
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        if digest(destination) != identity:
            raise RuntimeError('existing smoke artifact has conflicting content')
    else:
        with destination.open('xb') as output:
            output.write(payload)
        shutil.copymode(source, destination)
    if digest(source) != identity:
        raise RuntimeError('source artifact changed during snapshot; smoke not started')
    return destination, identity


class Host:
    def __init__(self, binary, database, evidence):
        self.evidence = evidence
        self.start = time.monotonic()
        self.sequence = 0
        self.process = subprocess.Popen([str(binary), 'ui-host', '--db', str(database)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            env={**os.environ, "ATLAS_VOICE_LIBRARY": str(database.parent / "voices.sqlite3")})
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)

    def request(self, method, args=None, timeout=3):
        self.sequence += 1
        request = dict(protocolVersion=1, requestId='device-smoke-' + str(self.sequence),
                       method=method, args=args or {})
        record = dict(elapsedSeconds=round(time.monotonic() - self.start, 6), request=request)
        self.evidence['protocol'].append(record)
        try:
            self.process.stdin.write(json.dumps(request) + '\n')
            self.process.stdin.flush()
            if not self.selector.select(timeout=timeout):
                raise ProtocolFailure('native device reply timed out: ' + method)
            line = self.process.stdout.readline()
            if not line:
                raise ProtocolFailure('native device host exited: ' + method)
            reply = json.loads(line)
            record['reply'] = reply
            if reply.get('requestId') != request['requestId']:
                raise ProtocolFailure('native device response identity mismatch')
            return reply
        except Exception as failure:
            record['failure'] = str(failure)
            raise

    def success(self, method, args=None):
        reply = self.request(method, args)
        if not reply.get('ok'):
            raise ProtocolFailure(method + ': ' + json.dumps(reply.get('error')))
        return reply['result']

    def transport(self, action, identity=None, **extra):
        value = identity or self.success('getTransport')
        return self.success('setTransport', dict(action=action,
            preparationId=value['preparationId'], generation=value['generation'], **extra))

    def wait_for(self, predicate, description, timeout=2):
        deadline = time.monotonic() + timeout
        while True:
            value = self.success('getTransport')  # Renews only an existing lease.
            if predicate(value):
                return value
            if time.monotonic() >= deadline:
                raise ProtocolFailure('device did not reach ' + description)
            time.sleep(.05)

    def close(self):
        cleanup = self.evidence['cleanup']
        if self.process.poll() is None:
            for method, args in (('setTransport', dict(action='panic')),
                                 ('setAudioEnabled', dict(enabled=False))):
                try:
                    reply = self.request(method, args, timeout=1)
                    cleanup[method] = bool(reply.get('ok'))
                except Exception as failure:
                    cleanup[method] = str(failure)
            try:
                self.process.stdin.close()
                cleanup['stdinEofSent'] = True
            except (BrokenPipeError, OSError) as failure:
                cleanup['stdinEofError'] = str(failure)
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            cleanup['forcedKill'] = True
            self.process.kill()
            self.process.wait(timeout=3)
        cleanup['exitCode'] = self.process.returncode
        cleanup['stderr'] = self.process.stderr.read(16384)
        self.selector.close()
        self.process.stdout.close()
        self.process.stderr.close()


def require(value, message):
    if not value:
        raise ProtocolFailure(message)


def run(source, prepare_only=False):
    evidence = dict(schema='animal-atlas-native-device-smoke-v1',
        startedAtUtc=datetime.now(timezone.utc).isoformat(), platform=platform.platform(),
        status='failed', protocol=[], checks={}, cleanup=dict(forcedKill=False),
        limits=dict(syntheticOnly=True, ownerLibraryAccess=False, providerRequests=False,
            notesSeconds=[0, .3, .6], requestedDurationSeconds=3, masterGainDb=-18, prepareOnly=prepare_only,
            deviceSelection='current default output only; no fallback/retry',
            physicalSpeakerOrHeardAudioVerified=False, microphoneUsed=False,
            latencyQualified=False, cpuQualified=False, dawQualified=False,
            sustainedDeviceQualified=False))
    started = time.monotonic()
    binary, identity = snapshot_binary(source)
    version = subprocess.run([str(binary), 'version'], capture_output=True, text=True, timeout=5, check=True)
    evidence['artifact'] = dict(source=str(source), immutableCopy=str(binary), sha256=identity,
                                version=json.loads(version.stdout), versionStderr=version.stderr)
    host = None
    with tempfile.TemporaryDirectory(prefix='atlas-real-device-smoke-') as temporary:
        database = Path(temporary) / 'synthetic device smoke.sqlite3'
        store = Store(database)
        store.ingest('device-smoke', [(event('device-smoke', str(i),
            '2026-01-01T00:00:00.%06dZ' % offset, 'acoustic_species_detection',
            'station_detection', -41, 174, station_id='device-smoke:station',
            species_id='device-smoke:species', provenance_mode='demo'), {})
            for i, offset in enumerate((0, 300000, 600000))], True)
        saved = Sessions(store).snapshot('Disposable synthetic device smoke',
            observed_from='2026-01-01T00:00:00Z', observed_to='2026-01-01T00:00:03Z')
        with store.connect() as connection:
            connection.execute('PRAGMA wal_checkpoint(TRUNCATE)')
        before = digest(database)
        evidence['fixture'] = dict(eventCount=3, sessionId=saved['id'], provenance='demo',
                                   databaseFileName=database.name, databasePathContainsSpaces=' ' in str(database),
                                   databaseSha256Before=before, temporaryDatabaseDeletedOnExit=True)
        try:
            host = Host(binary, database, evidence)
            caps = host.success('getCapabilities')
            require(caps['audioAvailable'], 'device artifact did not expose native playback')
            initial = host.success('getAudioState')
            require(initial['status'] == 'disabled', 'output was not disabled at startup')
            prepared = host.success('prepareReplay', dict(sessionId=saved['id'], basis='observed',
                speedNumerator='1', speedDenominator='1'))
            require(prepared['session']['metadataIntegrity'] == 'sha256-envelope-v1-verified',
                    'synthetic saved session was not verified')
            require(prepared['eventCount'] == 3, 'synthetic event count changed')
            require(int(prepared['durationUs']) == 3000000, 'requested synthetic window changed')
            if prepare_only:
                current = host.success('getTransport')
                require(current['state'] == 'stopped' and current['positionUs'] == '0', 'prepare auto-played')
                require(host.success('setAudioEnabled', dict(enabled=False))['status'] == 'disabled', 'disable failed')
                evidence['checks']['realHostPrepareAndQuiescentLifecycle'] = True
                evidence['status'] = 'passed_host_lifecycle_smoke'
                return evidence
            enabled = host.request('setAudioEnabled', dict(enabled=True), timeout=5)
            if not enabled.get('ok'):
                failure = enabled.get('error', {})
                if failure.get('code') == 'unavailable' and failure.get('message') in (
                        'device_unavailable', 'unsupported_device', 'device_changed',
                        'The default audio output could not be opened. Check the device and enable Listening again.',
                        'The default output does not support the prepared stereo sample rate or buffer size.',
                        'Audio output changed. Playback stopped; enable Listening to reconnect.'):
                    evidence['status'] = 'blocked_hardware'
                    evidence['blockedReason'] = failure
                    return evidence
                raise ProtocolFailure('enable failed: ' + json.dumps(failure))
            require(enabled['result']['status'] == 'running', 'device did not start')
            require(enabled['result']['masterGainDb'] == -18, 'unexpected gain; no play attempted')
            current = host.success('getTransport')
            require(current['positionUs'] == '0' and current['state'] == 'stopped', 'enable auto-played')
            evidence['checks']['defaultDisabledAndEnableSilent'] = True
            played = host.transport('play', current)
            require(played['state'] == 'playing', 'play was not applied')
            advancing = host.wait_for(lambda value: int(value['positionUs']) >= 800000, 'advancing playback')
            require(advancing['generation'] == played['generation'], 'generation changed while playing')
            evidence['checks']['callbackPositionAdvanced'] = True
            host.transport('pause', advancing)
            paused = host.wait_for(lambda value: value['state'] == 'paused', 'paused state')
            time.sleep(.08)
            frozen = host.success('getTransport')
            require(frozen['positionUs'] == paused['positionUs'], 'paused playhead advanced')
            evidence['checks']['pauseSettledAndFroze'] = True
            sought = host.transport('seek', frozen, positionUs='300000')
            require(sought['positionUs'] == '300000' and sought['state'] == 'paused', 'paused seek failed')
            require(int(sought['generation']) > int(paused['generation']), 'seek generation did not advance')
            evidence['checks']['seekWhilePaused'] = True
            resumed = host.transport('play', sought)
            host.wait_for(lambda value: int(value['positionUs']) >= 450000, 'resumed playback')
            evidence['checks']['resumeAdvanced'] = resumed['state'] == 'playing'
            stopped = host.success('setTransport', dict(action='stop'))
            settled = host.wait_for(lambda value: value['state'] == 'stopped', 'stop completion')
            require(settled['positionUs'] == '0', 'stop failed to reset playhead')
            require(int(stopped['generation']) > int(sought['generation']), 'stop generation did not advance')
            require(host.success('setAudioEnabled', dict(enabled=False))['status'] == 'disabled', 'disable failed')
            evidence['checks']['stopResetAndOutputDisabled'] = True
            evidence['status'] = 'passed_device_callback_smoke'
        except Exception as failure:
            evidence['failure'] = dict(type=type(failure).__name__, message=str(failure))
        finally:
            if host:
                host.close()
            evidence['fixture']['databaseSha256After'] = digest(database)
            evidence['checks']['temporaryDatabaseUnchanged'] = digest(database) == before
            if not evidence['checks']['temporaryDatabaseUnchanged']:
                evidence['status'] = 'failed'
            if host and (evidence['cleanup']['exitCode'] != 0 or evidence['cleanup']['forcedKill']):
                evidence['status'] = 'failed'
            evidence['elapsedSeconds'] = round(time.monotonic() - started, 6)
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/native-playback/atlas-audio-host')
    parser.add_argument('--prepare-only', action='store_true', help='exercise actual host lifecycle without opening audio hardware')
    parser.add_argument('--evidence', type=Path, default=ROOT / 'evidence/native-playback/device-smoke.json')
    args = parser.parse_args()
    result = run(args.binary.resolve(), args.prepare_only)
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(status=result['status'], elapsedSeconds=result.get('elapsedSeconds'),
                         artifactSha256=result['artifact']['sha256'], evidence=str(args.evidence))))
    return 1 if result['status'] == 'failed' else 0


if __name__ == '__main__':
    raise SystemExit(main())
