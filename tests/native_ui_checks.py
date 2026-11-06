"""Read-only native presentation boundary; disposable data, no providers."""
import json
from pathlib import Path
import selectors
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1)).resolve()
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from animal_atlas.model import event, canonical
from animal_atlas.store import Store
from animal_atlas.sessions import Sessions


class NativeUiTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.path=Path(self.temp.name)/'library.sqlite3'
        self.store=Store(self.path)
        self.ingest('baseline')
        self.ingest('new')
        self.sessions=Sessions(self.store)
        self.saved=self.sessions.snapshot('Fixture historical hour',observed_from='2026-01-01T00:00:00Z',observed_to='2026-01-01T01:00:00Z')
        self.process=subprocess.Popen([str(BINARY),'ui-host','--db',str(self.path)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        self.selector=selectors.DefaultSelector(); self.selector.register(self.process.stdout,selectors.EVENT_READ)
        self.counter=0

    def tearDown(self):
        self.process.stdin.close()
        self.process.wait(timeout=5)
        self.selector.close(); self.process.stdout.close(); self.process.stderr.close()
        self.temp.cleanup()

    def ingest(self,identity,latitude=-41):
        value=event('fixture',identity,'2026-01-01T00:10:00Z','acoustic_species_detection','station_detection',latitude,174,
                    station_id='fixture:station:one',species_id='fixture:species:tui',common_name='Fixture Tūī 🐦')
        self.store.ingest('fixture',[(value,{'private_raw':'must not cross bridge'})],True)

    def send_raw(self,payload):
        self.process.stdin.write(payload+'\n'); self.process.stdin.flush()
        self.assertTrue(self.selector.select(timeout=5),'native host reply timed out')
        line=self.process.stdout.readline()
        self.assertTrue(line,'native host exited')
        return json.loads(line)

    def request(self,method,args=None,ok=True):
        self.counter+=1; rid='test-'+str(self.counter)
        response=self.send_raw(json.dumps(dict(protocolVersion=1,requestId=rid,method=method,args=args or {})))
        self.assertEqual(response['requestId'],rid)
        self.assertEqual(response['ok'],ok,response)
        return response['result'] if ok else response['error']

    def test_native_capabilities_are_truthful(self):
        caps=self.request('getCapabilities')
        self.assertEqual(caps['implementation'],'native'); self.assertFalse(caps['audioAvailable'])
        for method in ('setTransport','queryHistory','startRender','setVoiceParameter','setCollection'):
            self.assertNotIn(method,caps['supportedMethods'])
            self.assertEqual(self.request(method,ok=False)['code'],'unsupported')

    def test_baseline_new_and_correction_cursors(self):
        baseline=self.request('getChanges',dict(storeId='',afterSequence='0',limit=200))
        self.assertEqual(len(baseline['events']),2)
        self.assertTrue(all(not e['liveEligible'] and e['change']=='baseline' for e in baseline['events']))
        self.ingest('later'); self.ingest('new',latitude=-42)
        result=self.request('getChanges',dict(storeId=baseline['storeId'],afterSequence=baseline['nextSequence'],limit=200))
        self.assertEqual([e['change'] for e in result['events']],['new','corrected'])
        self.assertEqual([e['liveEligible'] for e in result['events']],[True,False])
        empty=self.request('getChanges',dict(storeId=result['storeId'],afterSequence=result['nextSequence'],limit=200))
        self.assertEqual(empty['events'],[])
        reset=self.request('getChanges',dict(storeId='different-library',afterSequence=result['nextSequence'],limit=200))
        self.assertTrue(all(not e['liveEligible'] for e in reset['events']))

    def test_verified_preparation_bounds_material_and_catalog(self):
        listed=self.request('listSessions',dict(limit=20))
        self.assertEqual(listed[0]['id'],self.saved['id'])
        self.assertEqual(listed[0]['metadataIntegrity'],'sha256-envelope-v1-verified')
        prepared=self.request('prepareReplay',dict(sessionId=self.saved['id'],basis='observed',speedNumerator='60',speedDenominator='1'))
        self.assertEqual(prepared['durationUs'],'60000000')
        self.assertEqual(prepared['session']['observedWindowPolicy'],'preserve_requested')
        material=dict(kind='session',sessionId=self.saved['id'])
        page=self.request('getMaterialEvents',dict(material=material,offset=0,limit=1))
        self.assertEqual(page['total'],2); self.assertEqual(len(page['events']),1)
        self.assertEqual(page['events'][0]['individualId'],None)
        self.assertNotIn('private_raw',json.dumps(page)); self.assertNotIn('raw_json',json.dumps(page))
        catalog=self.request('getCatalog',dict(material=material))
        self.assertEqual(catalog['species'][0]['commonName'],'Fixture Tūī 🐦')
        self.assertEqual(catalog['species'][0]['group'],'unclassified')
        self.assertEqual(catalog['stations'][0]['latitude'],-41)

    def test_individual_fix_station_metadata_does_not_create_receiver(self):
        value=event('fixture','gps-site','2026-01-01T00:20:00Z','gps_fix','individual_fix',40,-70,
                    station_id='fixture:site',individual_id='fixture:tag',provenance_mode='demo')
        self.store.ingest('fixture',[(value,{})],True)
        catalog=self.request('getCatalog')
        self.assertNotIn('fixture:site',[station['id'] for station in catalog['stations']])
        changes=self.request('getChanges',dict(storeId='',afterSequence='0',limit=200))
        fix=next(item['event'] for item in changes['events'] if item['event']['eventKind']=='gps_fix')
        self.assertEqual(fix['individualId'],'fixture:tag')
        self.assertEqual(fix['position']['latitude'],40)

    def test_corrupt_piece_never_returns_material(self):
        with self.store.connect() as db: db.execute("UPDATE sessions SET name='Changed'")
        error=self.request('prepareReplay',dict(sessionId=self.saved['id'],basis='observed',speedNumerator='1',speedDenominator='1'),ok=False)
        self.assertEqual(error['code'],'unavailable')
        self.request('getMaterialEvents',dict(material=dict(kind='session',sessionId=self.saved['id']),offset=0,limit=5),ok=False)

    def test_large_sequences_stay_strings(self):
        with self.store.connect() as db: db.execute("UPDATE sqlite_sequence SET seq=? WHERE name='revisions'",(9007199254740993,))
        self.ingest('large')
        page=self.request('getChanges',dict(storeId='',afterSequence='0',limit=200))
        self.assertEqual(page['nextSequence'],'9007199254740994')
        self.assertEqual(page['events'][-1]['event']['revisionSequence'],'9007199254740994')

    def test_rejects_malformed_duplicate_unknown_and_oversized_requests(self):
        requests=['{}','not json','[]',' '*16385,
            '{"protocolVersion":1,"protocolVersion":1,"requestId":"dup","method":"getCapabilities","args":{}}']
        for payload in requests:
            with self.subTest(payload=payload[:70]): self.assertFalse(self.send_raw(payload)['ok'])
        for method,args in [('getCapabilities',dict(extra=1)),('getChanges',dict(storeId='',afterSequence='0 OR 1',limit=20)),
            ('getChanges',dict(storeId='',afterSequence='0',limit=True)),('listSessions',dict(limit=101)),
            ('prepareReplay',dict(sessionId=self.saved['id'],basis='invalid',speedNumerator='1',speedDenominator='1')),
            ('getCatalog',dict(path='/private/not-allowed'))]:
            with self.subTest(method=method,args=args): self.assertEqual(self.request(method,args,ok=False)['code'],'invalid_request')
        self.request('getCapabilities')

    def test_read_only_database_bytes(self):
        before=self.path.read_bytes()
        self.request('getCatalog'); self.request('listSessions',dict(limit=10))
        self.request('getChanges',dict(storeId='',afterSequence='0',limit=100))
        self.assertEqual(self.path.read_bytes(),before)
        self.assertEqual(self.store.high_water(),2)


if __name__=='__main__': unittest.main()
