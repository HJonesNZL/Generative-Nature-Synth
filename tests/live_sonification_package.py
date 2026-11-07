#!/usr/bin/env python3
"""Opt-in packaged live smoke: disposable native Collector and virtual output only."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import selectors
import sqlite3
import subprocess
import tempfile
import time


def require(value, message):
    if not value:
        raise AssertionError(message)


class Fixture:
    def __init__(self, binary, state, environment):
        self.process = subprocess.Popen([str(binary), str(state)], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=environment)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)

    def reply(self, status):
        require(bool(self.selector.select(10)), 'Collector fixture reply timed out: ' + status)
        line = self.process.stdout.readline()
        require(bool(line), 'Collector fixture exited before ' + status)
        result = json.loads(line)
        require(result.get('status') == status, 'Unexpected Collector fixture reply: ' + line)
        return result

    def command(self, command):
        self.process.stdin.write(command + '\n')
        self.process.stdin.flush()
        return self.reply(command)

    def close(self):
        if self.process.poll() is None:
            try:
                self.process.stdin.write('quit\n')
                self.process.stdin.flush()
                self.process.stdin.close()
            except (BrokenPipeError, OSError):
                pass
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)
        self.selector.close()
        error = self.process.stderr.read()
        self.process.stdout.close()
        self.process.stderr.close()
        require(self.process.returncode == 0, 'Collector fixture failed: ' + error)


def wait_marker(path, app, timeout=90):
    deadline = time.monotonic() + timeout
    while not path.exists():
        require(app.poll() is None, 'Packaged app exited before synchronization marker ' + path.name)
        require(time.monotonic() < deadline, 'Timed out waiting for ' + path.name)
        time.sleep(.05)


def provenance(database):
    with sqlite3.connect(database.as_uri() + '?mode=ro', uri=True) as db:
        counts = dict(events=db.execute('SELECT count(*) FROM events').fetchone()[0],
                      revisions=db.execute('SELECT count(*) FROM revisions').fetchone()[0])
        require(counts == dict(events=503, revisions=504), 'Canonical counts differ: ' + repr(counts))
        rows = db.execute('SELECT change_kind, event_json, raw_json FROM revisions WHERE '
                          "json_extract(event_json,'$.provider_event_id')='fixture-current' ORDER BY seq").fetchall()
        require(len(rows) == 2, 'Expected one current insertion and its correction')
        first, corrected = json.loads(rows[0][1]), json.loads(rows[1][1])
        for value in (first, corrected):
            require(value['source_id'] == 'birdweather', 'Source provenance lost')
            require(value['provenance_mode'] == 'live_source', 'Live source context lost')
            require(value['permissions_status'] == 'unresolved', 'Reuse uncertainty lost')
            require(value['event_kind'] == 'acoustic_species_detection' and
                    value['location_kind'] == 'station_detection', 'Station semantics lost')
            require(value['station_id'] == 'birdweather:station:fixture-live-station', 'Station identity lost')
            require(value['species_id'] == 'birdweather:species:424242', 'Species identity lost')
            require(value['individual_id'] is None and value['deployment_id'] is None,
                    'Station detection fabricated individual/deployment')
            require(value['latitude'] == -41 and value['longitude'] == 174 and
                    value['uncertainty_m'] is None, 'Coordinates or uncertainty changed')
            require('station_location_not_individual_position' in value['quality_flags'] and
                    'machine_classification_unverified' in value['quality_flags'], 'Sensitive quality provenance lost')
            require(value['common_name'] == 'Fixture live bird' and
                    value['attribution'] == 'BirdWeather and contributing station' and
                    value['source_url'] == 'https://app.birdweather.com/', 'Provider provenance lost')
            observed = datetime.fromisoformat(value['observed_at_utc'].replace('Z', '+00:00'))
            require(abs((datetime.now(timezone.utc) - observed).total_seconds()) < 180,
                    'Current fixture observation time became stale')
        require(first['observed_at_utc'] == corrected['observed_at_utc'] and
                first['first_seen_at_utc'] == corrected['first_seen_at_utc'], 'Correction changed frozen identity/time')
        require(first['confidence'] == .87 and corrected['confidence'] == .93,
                'Provider correction was not preserved')
        require(json.loads(rows[0][2])['confidence'] == .87 and
                json.loads(rows[1][2])['confidence'] == .93, 'Raw provider records were lost')
        dense = db.execute("SELECT count(*),count(DISTINCT json_extract(event_json,'$.species_id')) "
                           "FROM revisions WHERE json_extract(event_json,'$.provider_event_id') "
                           "LIKE 'fixture-dense-%'").fetchone()
        require(dense == (500, 8), 'Dense fixture observations/species groups differ: ' + repr(dense))
        return {**counts, 'denseObservations': dense[0], 'denseSpeciesGroups': dense[1], 'source': first['source_id'], 'stationId': first['station_id'],
                'observedAtUtc': first['observed_at_utc'], 'provenancePreserved': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--fixture', required=True, type=Path)
    parser.add_argument('--device', default='ZoomAudioDevice')
    parser.add_argument('--evidence', type=Path)
    args = parser.parse_args()
    require(args.device == 'ZoomAudioDevice', 'Smoke is restricted to the named virtual ZoomAudioDevice')
    environment = {key: value for key, value in os.environ.items() if not key.startswith('ATLAS_')}
    with tempfile.TemporaryDirectory(prefix='atlas-live-sonification-') as temporary:
        folder = Path(temporary)
        state, receipt = folder / 'state', folder / 'receipt.json'
        fixture = Fixture(args.fixture.resolve(), state, environment)
        app = None
        try:
            baseline = fixture.reply('ready')
            command = [str(args.binary.resolve()), '--library', str(state / 'live.sqlite3'),
                       '--state-dir', str(state), '--smoke-test', str(receipt),
                       '--smoke-sonification', args.device, '--smoke-seconds', '1']
            with (folder / 'app.stdout').open('w+') as stdout, (folder / 'app.stderr').open('w+') as stderr:
                app = subprocess.Popen(command, env=environment, stdout=stdout, stderr=stderr)
                wait_marker(Path(str(receipt) + '.armed'), app)
                inserted = fixture.command('live')
                wait_marker(Path(str(receipt) + '.admitted'), app)
                duplicates = fixture.command('duplicates')
                wait_marker(Path(str(receipt) + '.dense-ready'), app)
                dense = fixture.command('dense')
                try:
                    result = app.wait(timeout=90)
                except subprocess.TimeoutExpired as failure:
                    raise AssertionError('Packaged sonification smoke did not finish') from failure
                stderr.seek(0)
                require(result == 0, f'Packaged app exited {result}: ' + stderr.read()[-3000:])
            value = json.loads(receipt.read_text())
            require(value.get('ok') and value.get('shutdownComplete'), 'App receipt or shutdown failed')
            sonification = value['webview']['sonification']
            require(sonification['device'] == args.device, 'Wrong output device in receipt')
            listening, off = sonification['listening'], sonification['off']
            require(listening['enabled'] is True and listening['received'] == 1 and
                    listening['admitted'] == 1, 'Current insertion was not admitted exactly once')
            require(listening.get('mode') == 'musical' and listening.get('density') == 4,
                    'React did not select musical mode/density')
            outcome = sonification.get('selectedOutcome', {})
            require(outcome.get('status') == 'sounded' and
                    outcome.get('speciesId') == 'birdweather:species:424242' and outcome.get('voiceCharacter') == 'struck',
                    'Selected React observation did not report its sounded species outcome')
            dense_state = sonification.get('dense', {})
            require(dense_state.get('received') == 501 and 2 <= dense_state.get('admitted', 0) <= 5 and
                    dense_state.get('grouped', 0) > 0 and dense_state.get('dropped', 0) > 0 and
                    dense_state['admitted'] + dense_state['grouped'] + dense_state['dropped'] == 501,
                    'Dense phrase did not complete with bounded, conserved musical accounting')
            require(sonification.get('outputPeakDbfs', -96) > -96,
                    'Actual output signal was not observed by React polling')
            require(off['enabled'] is False, 'Live sonification did not turn off')
            frames = sonification['callbackFrames']
            require(isinstance(frames, str) and frames.isdecimal() and int(frames) > 0,
                    'No actual output callbacks were observed')
            require(sonification['mapObserved'] is True and sonification['duplicatesQuiet'] is True,
                    'Map observation or duplicate/correction suppression failed')
            require(sonification.get('headerErrorFree') is True,
                    'Live listening header error regression was not checked')
            for method in ('getTransport', 'getAudioState'):
                require(sonification['polls'][method] >= 3, 'Missing React live polls: ' + method)
            require(sonification.get('denseCountersRendered') is True and sonification.get('muteMeterCleared') is True,
                    'Dense React counters or mute meter reset were not verified')
            require(sonification['polls'].get('getLiveOutcome', 0) >= 1,
                    'Missing actual React selected-outcome polling')
            require(int(sonification['denseCallbackFrames']) > int(frames), 'Dense output callbacks did not advance')
            evidence = dict(schema='animal-atlas-live-sonification-package-v2', status='passed',
                binary=str(args.binary.resolve()), fixture=str(args.fixture.resolve()),
                fixturePhases=[baseline, inserted, duplicates, dense], database=provenance(state / 'live.sqlite3'),
                sonification=sonification, shutdownComplete=True,
                limits=dict(disposableOnly=True, providerRequests=False, ownerStateAccess=False,
                            device=args.device, physicalSpeakers=False, heardAudioVerified=False))
            if args.evidence:
                args.evidence.parent.mkdir(parents=True, exist_ok=True)
                args.evidence.write_text(json.dumps(evidence, indent=2) + '\n')
            print(json.dumps(evidence, sort_keys=True))
        except Exception as error:
            if receipt.is_file():
                outcome = json.loads(receipt.read_text()).get('webview', {})
                raise RuntimeError(f"{error}: {outcome.get('error')} {str(outcome.get('failureDom', ''))[:2500]}") from error
            raise
        finally:
            if app is not None and app.poll() is None:
                app.terminate()
                try:
                    app.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    app.kill()
                    app.wait(timeout=5)
            fixture.close()


if __name__ == '__main__':
    main()
