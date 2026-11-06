"""Offline fixture contract validator, not a provider adapter or production parser."""
import copy
import datetime as dt
import json
from pathlib import Path
import re
import unittest

FIELDS = set("key datasetKey occurrenceID publishingOrgKey datasetName license rightsHolder references taxonKey classKey kingdom class scientificName basisOfRecord occurrenceStatus countryCode eventDate year month day decimalLatitude decimalLongitude coordinateUncertaintyInMeters geodeticDatum modified lastCrawled lastParsed lastInterpreted issues dataGeneralizations informationWithheld".split())


def representation(value):
    if value is None:
        return "missing"
    if not isinstance(value, str):
        raise ValueError("non-text eventDate")
    if re.fullmatch(r"\d{4}", value):
        dt.date(int(value), 1, 1)
        return "year"
    if re.fullmatch(r"\d{4}-\d{2}", value):
        dt.date.fromisoformat(value + "-01")
        return "month"
    if re.fullmatch(r"\d{4}-\d{2}-\d{2}", value):
        dt.date.fromisoformat(value)
        return "day"
    if "/" in value:
        start, end = value.split("/")
        if dt.date.fromisoformat(start) > dt.date.fromisoformat(end):
            raise ValueError("reversed interval")
        return "interval"
    parsed = dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
    if "T" not in value or parsed.tzinfo is None:
        raise ValueError("unsupported date syntax or missing timezone")
    return "timestamp_unconfirmed"


def validate(record, candidate):
    # This validates the bounded synthetic cases; it does not implement generic JSON ingress.
    if type(record.get("key")) is not int or record["key"] <= 0:
        raise ValueError("positive integer key required")
    if not isinstance(record.get("datasetKey"), str) or not record["datasetKey"]:
        raise ValueError("dataset identity required")
    if set(record) - FIELDS:
        raise ValueError("fixture field needs an explicit preservation rule")
    if candidate["source_fields"] != record:
        raise ValueError("source fields lost or changed")
    if candidate["provider_event_key"] != str(record["key"]):
        raise ValueError("wrong source key")
    if candidate["canonical_source_url"] != f"https://www.gbif.org/occurrence/{record['key']}":
        raise ValueError("wrong canonical URL")
    if candidate["date_representation"] != representation(record.get("eventDate")):
        raise ValueError("date representation changed")
    latitude, longitude = record.get("decimalLatitude"), record.get("decimalLongitude")
    if (latitude is None) != (longitude is None):
        raise ValueError("incomplete coordinates")
    if latitude is not None:
        if type(latitude) not in (int, float) or type(longitude) not in (int, float) or not -90 <= latitude <= 90 or not -180 <= longitude <= 180:
            raise ValueError("invalid coordinates")
    uncertainty = record.get("coordinateUncertaintyInMeters")
    if uncertainty is not None and (type(uncertainty) not in (int, float) or not 0 <= uncertainty < float("inf")):
        raise ValueError("invalid uncertainty")
    licence = record.get("license")
    gate = {"http://creativecommons.org/licenses/by/4.0/legalcode": "dataset_review_required",
            "http://creativecommons.org/publicdomain/zero/1.0/legalcode": "dataset_review_required",
            "http://creativecommons.org/licenses/by-nc/4.0/legalcode": "excluded_nc"}.get(licence, "unresolved")
    if candidate["reuse_gate"] != gate:
        raise ValueError("licence gate mismatch")
    sensitivity = "flagged" if record.get("dataGeneralizations") or record.get("informationWithheld") else "unassessed"
    if candidate["sensitivity_gate"] != sensitivity:
        raise ValueError("sensitivity not preserved")
    for field in ("observed_at_utc", "first_seen_at_utc", "ingested_at_utc", "individual_id", "deployment_id", "station_id"):
        if candidate[field] is not None:
            raise ValueError("invented precision, identity, or ingestion metadata")
    if candidate["integration_status"] != "blocked_event_v1_schema" or candidate["display_permission"] != "none" or candidate["provenance_mode"] != "demo" or candidate["synthetic_fixture"] is not True:
        raise ValueError("fixture activation or provenance changed")


class ContractTests(unittest.TestCase):
    def setUp(self):
        fixture = json.loads((Path(__file__).parent / "fixtures/synthetic-mapping-cases.json").read_text())
        self.assertTrue(fixture["synthetic_only"] and fixture["not_live_verified"])
        self.cases = fixture["cases"]

    def test_all_preservation_cases(self):
        self.assertEqual(len(self.cases), 6)
        for case in self.cases:
            with self.subTest(case=case["name"]):
                validate(case["response_record"], case["expected_candidate"])

    def test_coarse_date_and_update_are_not_observation_instants(self):
        for case in self.cases:
            self.assertIsNone(case["expected_candidate"]["observed_at_utc"])
        self.assertEqual(self.cases[2]["expected_candidate"]["date_representation"], "year")
        self.assertEqual(self.cases[3]["expected_candidate"]["date_representation"], "interval")

    def test_bad_boundary_values(self):
        for field, value in (("key", True), ("datasetKey", None), ("decimalLatitude", 91), ("decimalLongitude", None), ("coordinateUncertaintyInMeters", -1), ("coordinateUncertaintyInMeters", float("nan")), ("eventDate", "2020-02-30"), ("eventDate", "2020-12-31/2020-01-01"), ("eventDate", "2020-01-01T00:00:00")):
            case = copy.deepcopy(self.cases[0]); case["response_record"][field] = value
            case["expected_candidate"]["source_fields"] = copy.deepcopy(case["response_record"])
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                validate(case["response_record"], case["expected_candidate"])

    def test_loss_or_fabrication_rejected(self):
        for field, value in (("observed_at_utc", "2020-06-15T00:00:00Z"), ("station_id", "invented:station"), ("individual_id", "observer:123"), ("display_permission", "allowed"), ("reuse_gate", "approved")):
            case = copy.deepcopy(self.cases[0]); case["expected_candidate"][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate(case["response_record"], case["expected_candidate"])
        case = copy.deepcopy(self.cases[2]); del case["expected_candidate"]["source_fields"]["informationWithheld"]
        with self.assertRaises(ValueError):
            validate(case["response_record"], case["expected_candidate"])


if __name__ == "__main__":
    unittest.main()
