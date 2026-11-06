"""Full native scene protocol on disposable data and fake output only."""
import json
from pathlib import Path
import sqlite3
import struct
import subprocess
import time
import unittest
from native_voice_checks import Client, NativeVoiceTests, BINARY

class SoundscapeTests(NativeVoiceTests):
    # Reuse fixture construction, not the previous milestone's test methods.
    def prepare(self, numerator="3", denominator="2"):
        return self.client.request('prepareReplay', dict(sessionId=self.saved['id'], basis='observed', speedNumerator=numerator, speedDenominator=denominator))

    def edit(self, value):
        for phase in ('begin', 'end'):
            self.client.request('setVoiceParameter',dict(voiceId='species:studio:bird',parameterId='tuning',normalizedValue=value,gesture=phase))

    def render(self, scene):
        job=self.client.request('startSoundscapeRender',dict(soundscapeId=scene['id']))
        deadline=time.monotonic()+10
        while job['state']=='rendering' and time.monotonic()<deadline:
            time.sleep(.02)
            job=self.client.request('getRenderJob',dict(jobId=job['jobId']))
        self.assertEqual(job['state'],'complete',job)
        output=Path(self.temp.name)/'exports'/job['filename']
        self.assertEqual(output.name,job['artifactId']+'.wav')
        data=output.read_bytes()
        self.assertEqual(data[:4],b'RIFF')
        return data

    def test_scene_complete_recall_immutable_render_and_no_source(self):
        self.prepare()
        self.edit(1)
        before=self.client.request('getVoice',dict(voiceId='species:studio:bird'))
        scene=self.client.request('saveSoundscape',dict(name='Captured scene'))
        self.assertTrue(scene['valid'] and scene['contentVerified'])
        self.assertEqual((scene['speedNumerator'],scene['speedDenominator']),('3','2'))
        self.edit(0)
        first=self.render(scene)
        recalled=self.client.request('recallSoundscape',dict(soundscapeId=scene['id']))
        self.assertEqual(recalled['preparation']['session']['id'],'scene:'+scene['id'])
        self.assertEqual(self.client.request('getAudioState')['status'],'disabled')
        voice=self.client.request('getVoice',dict(voiceId='species:studio:bird'))
        self.assertEqual(voice['values'],before['values'])
        self.assertEqual(voice['savedRevision'],'0')
        self.assertTrue(voice['dirty'])
        self.assertEqual(self.render(scene),first)
        self.edit(.5)
        self.client.request('prepareReplay',dict(sessionId='scene:'+scene['id'],basis='arrival',speedNumerator='2',speedDenominator='1'))
        self.assertEqual(self.client.request('getVoice',dict(voiceId='species:studio:bird'))['values']['tuning'],.5)
        clone=self.client.request('saveSoundscape',dict(name='Alternate time basis'))
        self.assertEqual(clone['basis'],'arrival')
        self.assertEqual(clone['sourceSessionId'],scene['sourceSessionId'])
        # Close all readers before remove; retained immutable scene is sufficient.
        self.client.close(); self.clients.remove(self.client)
        hidden=self.path.with_suffix('.hidden'); self.path.rename(hidden)
        try:
            self.client=self.new_client()
            self.client.request('recallSoundscape',dict(soundscapeId=scene['id']))
            self.assertEqual(self.render(scene),first)
            cli_output=Path(self.temp.name)/'self-contained-cli.wav'
            scene_path=Path(self.temp.name)/'soundscapes'/(scene['id']+'.atlas-scene')
            command=[str(BINARY.with_name('atlas-native')),'scene-render','--scene',str(scene_path),'--output',str(cli_output)]
            subprocess.run(command,check=True,capture_output=True,text=True,timeout=10)
            def pcm(wav):
                offset=12
                while offset+8<=len(wav):
                    size=struct.unpack_from('<I',wav,offset+4)[0]
                    if wav[offset:offset+4]==b'data': return wav[offset+8:offset+8+size]
                    offset+=8+size+(size%2)
                raise AssertionError('WAV data chunk missing')
            self.assertEqual(pcm(cli_output.read_bytes()),pcm(first))
            before_cli=cli_output.read_bytes()
            refused=subprocess.run(command,capture_output=True,text=True,timeout=10)
            self.assertNotEqual(refused.returncode,0)
            self.assertEqual(cli_output.read_bytes(),before_cli)
            page=self.client.request('getMaterialEvents',dict(material=dict(kind='session',sessionId='scene:'+scene['id']),offset=0,limit=500))
            self.assertEqual(page['total'],scene['eventCount'])
            self.assertTrue(self.client.request('getCatalog',dict(material=dict(kind='session',sessionId='scene:'+scene['id'])))['species'])
            self.client.request('listSessions',dict(limit=10),error='unavailable')
            self.client.close();self.clients.remove(self.client)
        finally:
            hidden.rename(self.path)

    def test_invalid_ids_and_corrupt_scene_do_not_replace_drafts(self):
        self.prepare();self.edit(1)
        scene=self.client.request('saveSoundscape',dict(name='Integrity test'))
        for invalid in ('../voices.sqlite3','A'*32,'0'*31,'0'*33):
            self.client.request('recallSoundscape',dict(soundscapeId=invalid),error='invalid_request')
        path=Path(self.temp.name)/'soundscapes'/(scene['id']+'.atlas-scene')
        path.write_bytes(b'not sqlite')
        before=self.client.request('getVoice',dict(voiceId='species:studio:bird'))
        self.client.request('recallSoundscape',dict(soundscapeId=scene['id']),error='unavailable')
        self.assertEqual(self.client.request('getVoice',dict(voiceId='species:studio:bird')),before)
        listing=self.client.request('listSoundscapes',dict(limit=100))
        self.assertFalse(listing['items'][0]['valid'])
        self.assertTrue(listing['items'][0]['error'])
        (path.parent/'unsupported-name.atlas-scene').write_bytes(b'garbage')
        listing=self.client.request('listSoundscapes',dict(limit=100))
        self.assertEqual(listing['unmanagedFiles'],1)
        self.assertTrue(listing['limited'])
        self.assertEqual(len(listing['items']),1)

    def test_exact_species_candidates_are_defaults_and_explicit_draft_edits(self):
        from animal_atlas.model import event
        self.store.ingest('studio',[(event('studio','ruru','2026-01-01T00:00:02Z','acoustic_species_detection','station_detection',-41,174,station_id='studio:station',species_id='birdweather:species:2298',provenance_mode='demo'),{})],True)
        self.library_bytes=self.path.read_bytes()
        self.client.request('getVoiceLibrary')
        identity='species:birdweather:species:2298'
        initial=self.client.request('getVoice',dict(voiceId=identity))
        self.assertEqual(initial['type'],'whistle')
        self.assertFalse(initial['dirty'])
        self.assertEqual(initial['savedRevision'],'0')
        presets=self.client.request('listVoicePresets',dict(voiceId=identity))
        self.assertFalse(presets['listeningApproved'])
        self.assertEqual(len(presets['items']),1)
        preset=presets['items'][0]
        self.assertEqual(preset['speciesId'],'birdweather:species:2298')
        self.assertEqual(preset['evidenceStatus'],'research_candidate')
        self.client.request('applyVoicePreset',dict(voiceId='species:studio:bird',presetId=preset['id']),error='invalid_request')
        for phase in ('begin','end'):
            self.client.request('setVoiceParameter',dict(voiceId=identity,parameterId='brightness',normalizedValue=1,gesture=phase))
        applied=self.client.request('applyVoicePreset',dict(voiceId=identity,presetId=preset['id']))
        self.assertEqual(applied['values'],initial['values'])
        self.assertEqual(applied['savedRevision'],'0')
        with sqlite3.connect(self.voices) as db:
            self.assertEqual(db.execute('SELECT count(*) FROM voice_presets').fetchone()[0],0)

    def test_composition_exact_controls_stale_and_scene_recall(self):
        prepared=self.prepare()
        initial=self.client.request('getComposition')
        self.assertFalse(initial['settings']['enabled'])
        settings=dict(enabled=True,scale='naturalMinor',tonicPitchClass=7,tempoBpm=112,density=2,variation=.37,seed=4294967295,quantize=True)
        applied=self.client.request('setComposition',dict(settings=settings,preparationId=prepared['preparationId'],generation=prepared['generation']))
        self.assertEqual(applied['settings'],settings)
        self.assertEqual(self.client.request('getAudioState')['status'],'disabled')
        self.client.request('setComposition',dict(settings=settings,preparationId=prepared['preparationId'],generation=prepared['generation']),error='stale_generation')
        scene=self.client.request('saveSoundscape',dict(name='Composed history'))
        wav=self.render(scene)
        changed=dict(settings,tempoBpm=60,seed=3)
        prep=applied['preparation']
        self.client.request('setComposition',dict(settings=changed,preparationId=prep['preparationId'],generation=prep['generation']))
        self.assertEqual(self.render(scene),wav)
        self.client.request('recallSoundscape',dict(soundscapeId=scene['id']))
        self.assertEqual(self.client.request('getComposition')['settings'],settings)
        self.assertEqual(self.render(scene),wav)
        self.client.request('prepareReplay',dict(sessionId='scene:'+scene['id'],basis='observed',speedNumerator='2',speedDenominator='1'))
        self.assertEqual(self.client.request('getComposition')['settings'],settings)

    def test_capture_rejects_active_gesture_and_unprepared(self):
        self.client.request('saveSoundscape',dict(name='Unavailable'),error='invalid_request')
        self.prepare()
        self.client.request('setVoiceParameter',dict(voiceId='species:studio:bird',parameterId='tuning',normalizedValue=1,gesture='begin'))
        self.client.request('saveSoundscape',dict(name='Gesture incomplete'),error='invalid_request')
        self.client.request('setVoiceParameter',dict(voiceId='species:studio:bird',parameterId='tuning',normalizedValue=1,gesture='end'))
        self.client.request('saveSoundscape',dict(name='Gesture complete'))

# Do not duplicate inherited old-milestone tests in this separate target.
for name in list(NativeVoiceTests.__dict__):
    if name.startswith('test_'):
        setattr(SoundscapeTests,name,None)

del NativeVoiceTests

if __name__=='__main__':
    unittest.main()
