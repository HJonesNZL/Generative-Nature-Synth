#!/usr/bin/env python3
"""Disposable offline contract probes. Pass the independently compiled C++ binary."""
import csv
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()
HERE = Path(__file__).resolve().parent


def csv_bytes(rows):
    text = io.StringIO(newline='')
    csv.writer(text).writerows(rows)
    return text.getvalue().encode()


class PreflightTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.folder = Path(self.temporary.name)
        with (HERE/'fixtures/synthetic-events.csv').open(newline='') as source:
            self.events = list(csv.reader(source))
        with (HERE/'fixtures/synthetic-package.csv').open(newline='') as source:
            self.package = list(csv.reader(source))

    def tearDown(self):
        self.temporary.cleanup()

    def run_case(self, events=None, package=None, raw=None, ok=True):
        source = self.folder/'events.csv'
        metadata = self.folder/'package.csv'
        source.write_bytes(raw if raw is not None else csv_bytes(events if events is not None else self.events))
        metadata.write_bytes(csv_bytes(package if package is not None else self.package))
        before = hashlib.sha256(source.read_bytes()+metadata.read_bytes()).digest()
        result = subprocess.run([str(BINARY),str(source),str(metadata)],capture_output=True,text=True,timeout=5)
        self.assertEqual(hashlib.sha256(source.read_bytes()+metadata.read_bytes()).digest(),before)
        if not ok:
            self.assertNotEqual(result.returncode,0)
            self.assertEqual(result.stdout,'')
            return result.stderr.strip()
        self.assertEqual(result.returncode,0,result.stderr)
        output = json.loads(result.stdout)
        self.assertFalse(output['import_allowed'])
        self.assertIn('deployment-reference-join-not-implemented',output['blockers'])
        self.assertIn('per-study-rights-review-required',output['blockers'])
        return output

    def changed(self, key, value):
        events = [list(self.events[0]),list(self.events[1])]
        events[1][events[0].index(key)] = value
        return events

    def test_quoted_utf8_multiline_trailing_empty_exact_declarations(self):
        output = self.run_case()
        self.assertEqual(output['technical_candidate_rows'],[1,2])
        self.assertEqual(output['exclusions'],[])
        self.assertEqual(output['declarations_unverified']['citation'],self.package[1][3])
        self.assertEqual(output['declarations_unverified']['study-name'],'SYNTHETIC Tūī, study')
        # Nonterminated final record still retains its trailing empty cell.
        self.assertEqual(self.run_case(raw=csv_bytes(self.events).rstrip(b'\r\n'))['technical_candidate_rows'],[1,2])

    def test_missing_individual_tag_event_and_study(self):
        for key in ('individual-local-identifier','tag-local-identifier','event-id','study-name'):
            with self.subTest(key=key):
                output = self.run_case(events=self.changed(key,''))
                self.assertEqual(output['technical_candidate_rows'],[])
                self.assertEqual(len(output['exclusions']),1)

    def test_coarse_offset_impossible_and_excess_precision_timestamps(self):
        for value in ('2020-02-29','2020-02-29 12:34:56','2020-02-29T12:34:56.007Z',
                      '2020-02-29 12:34:56.007+13:00','2021-02-29 12:34:56.007',
                      '2020-02-29 24:34:56.007','2020-02-29 12:60:56.007',
                      '2020-02-29 12:34:60.007','2020-02-29 12:34:56.007001'):
            with self.subTest(value=value):
                output = self.run_case(events=self.changed('timestamp',value))
                self.assertEqual(output['technical_candidate_rows'],[])
                self.assertIn('timestamp_not_exact_utc_millisecond_profile',output['exclusions'][0]['reasons'])

    def test_invalid_coordinates_and_boundaries(self):
        for key,value in [('location-long','180.00001'),('location-lat','-90.01'),
                          ('location-lat','nan'),('location-long','inf'),('location-long',''),
                          ('location-lat',' 40'),('location-lat','40junk')]:
            with self.subTest(key=key,value=value):
                self.assertIn('invalid_coordinates',self.run_case(events=self.changed(key,value))['exclusions'][0]['reasons'])
        for key,value in [('location-long','-180'),('location-lat','90')]:
            self.assertEqual(self.run_case(events=self.changed(key,value))['technical_candidate_rows'],[1])

    def test_visible_outlier_and_unknown_flags_are_excluded(self):
        for key,value,reason in [('visible','FALSE','invisible_or_unknown_visibility'),
                                 ('visible','','invisible_or_unknown_visibility'),
                                 ('manually-marked-outlier','TRUE','outlier_or_unknown_flag'),
                                 ('manually-marked-outlier','','outlier_or_unknown_flag')]:
            with self.subTest(key=key,value=value):
                output = self.run_case(events=self.changed(key,value))
                self.assertEqual(output['technical_candidate_rows'],[])
                self.assertIn(reason,output['exclusions'][0]['reasons'])
                self.assertNotIn('174.000001',json.dumps(output))

    def test_private_generalized_missing_rights_and_units_declarations(self):
        for key,value,reason in [('download-access','private','private_or_unverified_download'),
                                 ('privacy-review','generalized','privacy_not_reviewed_or_generalized'),
                                 ('privacy-review','','privacy_not_reviewed_or_generalized'),
                                 ('coordinate-units','radians','unverified_coordinate_units'),
                                 ('license-uri','','missing_package_provenance'),
                                 ('package-doi','','missing_package_provenance')]:
            with self.subTest(key=key):
                package = [list(row) for row in self.package]
                package[1][package[0].index(key)] = value
                output = self.run_case(package=package)
                self.assertEqual(output['technical_candidate_rows'],[])
                self.assertIn(reason,output['exclusions'][0]['reasons'])

    def test_duplicate_identity_and_unsupported_sensors(self):
        events = [self.events[0],self.events[1],self.events[1]]
        output = self.run_case(events=events)
        self.assertEqual(output['technical_candidate_rows'],[])
        self.assertEqual(len(output['exclusions']),2)
        self.assertIn('duplicate_event_id',output['exclusions'][0]['reasons'])
        self.assertIn('unsupported_sensor_semantics',self.run_case(events=self.changed('sensor-type','acoustic-telemetry'))['exclusions'][0]['reasons'])

    def test_malformed_quotes_width_headers_and_utf8(self):
        for raw in (b'a,b\n"unfinished',b'a,b\n"one"junk,two\n',b'a,b\none\n',
                    b'a,a\none,two\n',b',b\none,two\n',b'a,b\n\xff,x\n',b'a,b\nx,\x00\n',
                    b'a,b\nx,y\r',b''):
            with self.subTest(raw=raw):
                self.run_case(raw=raw,ok=False)

    def test_capacity_rejections(self):
        self.assertEqual(self.run_case(raw=b'a\n'+b'x'*4097,ok=False),'cell_capacity')
        self.assertEqual(self.run_case(raw=b'x'*(4*1024*1024+1),ok=False),'file_capacity')
        self.assertEqual(self.run_case(raw=b','.join(str(i).encode() for i in range(65)),ok=False),'column_capacity')
        self.assertEqual(self.run_case(raw=b'a\n'+b'x\n'*10001,ok=False),'row_capacity')


if __name__=='__main__':
    unittest.main()
