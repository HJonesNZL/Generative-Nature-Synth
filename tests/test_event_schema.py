import json
import unittest
from pathlib import Path
from animal_atlas.model import validate_event, event

class EventSchemaTests(unittest.TestCase):
    def test_shared_semantic_corpus(self):
        corpus=json.loads((Path(__file__).parent/'fixtures/event_semantics_v1.json').read_text())
        for case in corpus['cases']:
            value=dict(corpus['base'], **case['changes'])
            for key in case['remove']: value.pop(key)
            with self.subTest(case=case['name']):
                if case['accepted']:
                    validate_event(value, frozen=True)
                else:
                    with self.assertRaises(ValueError): validate_event(value, frozen=True)

    def test_ingress_rejects_optional_field_misuse(self):
        for fields in ({'quality_flags':'string'}, {'confidence':.8}, {'individual_id':'fixture:invented'},
                       {'provider_scores':{'unknown':float('inf')}}):
            with self.subTest(fields=fields), self.assertRaises(ValueError):
                event('fixture','one','2026-01-01T00:00:00Z','acoustic_species_detection','station_detection',**fields)
