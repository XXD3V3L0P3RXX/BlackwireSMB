# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('collector', ROOT / 'tools/diagnostics/collect-diagnostics.py')
collector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(collector)


class DiagnosticSummary(unittest.TestCase):
    def test_credentials_and_unknown_fields_never_copied(self):
        secret = 'SENSITIVE-TEST-MARKER'
        health = {'ok': True, 'buildId': 'diagnostic-test', 'password': secret}
        state = {'settings': {'password': secret, 'server': secret}, 'setup': {'token': secret},
                 'library': {'phase': 'ready', 'extra': secret},
                 'games': [{'titleId': 'TEST00001', 'name': 'Example game', 'sourceBinding': secret,
                            'source': secret, 'credentials': secret, 'status': 'unsupported_format',
                            'containerFormat': 'unknown', 'formatValidated': False}]}
        result = collector.summarize(health, state)
        self.assertNotIn(secret, json.dumps(result))
        self.assertEqual(result['games'][0]['status'], 'unsupported_format')
        self.assertFalse(result['games'][0]['formatValidated'])

    def test_nested_value_in_public_field_is_excluded(self):
        result = collector.summarize({}, {'games': [{'name': {'password': 'SENSITIVE-TEST-MARKER'}}]})
        self.assertNotIn('SENSITIVE-TEST-MARKER', json.dumps(result))
        self.assertNotIn('name', result['games'][0])

    def test_bounds_and_invalid_response(self):
        self.assertEqual(len(collector.summarize({}, {'games': [{'name': 'a' * 4096}]})['games'][0]['name']), 1024)
        with self.assertRaises(ValueError):
            collector.summarize({}, {'games': {}})
        with self.assertRaises(ValueError):
            collector.summarize({}, {'games': [{}] * 4097})


if __name__ == '__main__':
    unittest.main()
