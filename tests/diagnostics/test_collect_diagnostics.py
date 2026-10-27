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
    def test_setup_status_is_included_without_its_authority_or_credentials(self):
        result = collector.summarize({}, {'setup': {'supported': True, 'editable': False, 'busy': False,
            'phase': 'idle', 'message': 'Connection settings are unavailable.', 'settingsFile': 'share.conf',
            'storageStatus': 'unavailable', 'token': 'SENSITIVE-TEST-MARKER', 'password': 'SENSITIVE-TEST-MARKER'}})
        self.assertEqual(result['setup']['storageStatus'], 'unavailable')
        self.assertFalse(result['setup']['editable'])
        self.assertEqual(result['setup']['settingsFile'], 'share.conf')
        self.assertNotIn('SENSITIVE-TEST-MARKER', json.dumps(result))

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

    def test_catalog_admission_reasons_and_authoritative_counts_are_included(self):
        counts = {'containerCandidates': 52, 'containersValidated': 27, 'containersRejected': 25}
        valid = {'containerErrorCode': 0, 'containerErrorName': 'none', 'containerStage': 'validated',
                 'namespaceFilesObserved': 12, 'namespacePathBytesObserved': 256}
        rejected = {'containerErrorCode': -5, 'containerErrorName': 'limit', 'containerStage': 'namespace',
                    'namespaceFilesObserved': 4097, 'namespacePathBytesObserved': 100000}
        result = collector.summarize({}, {'library': counts, 'games': [valid, rejected]})
        # Forward the reported totals, rather than inferring them from this
        # bounded sample of visible items or conflating rejected and listed.
        self.assertEqual({key: result['library'].get(key) for key in counts}, counts)
        for actual, expected in zip(result['games'], (valid, rejected)):
            self.assertEqual({key: actual.get(key) for key in expected}, expected)

    def test_absent_or_unknown_admission_values_do_not_become_zero(self):
        counts = ('containerCandidates', 'containersValidated', 'containersRejected')
        fields = ('containerErrorCode', 'containerErrorName', 'containerStage',
                  'namespaceFilesObserved', 'namespacePathBytesObserved')
        absent_parent = collector.summarize({}, {'games': [{}]})
        for key in counts:
            self.assertNotIn(key, absent_parent['library'])
        missing = collector.summarize({}, {'library': {}, 'games': [{}]})
        unknown = collector.summarize({}, {'library': dict.fromkeys(counts),
                                           'games': [dict.fromkeys(fields)]})
        for result in (missing, unknown):
            for key in counts:
                self.assertIn(key, result['library'])
                self.assertIsNone(result['library'][key])
            for key in fields:
                self.assertIn(key, result['games'][0])
                self.assertIsNone(result['games'][0][key])
        zeros = dict.fromkeys(counts, 0)
        unobserved = {'containerErrorCode': 0, 'containerErrorName': 'unknown',
                      'containerStage': 'not_checked', 'namespaceFilesObserved': 0,
                      'namespacePathBytesObserved': 0}
        result = collector.summarize({}, {'library': zeros, 'games': [unobserved]})
        self.assertEqual({key: result['library'][key] for key in counts}, zeros)
        self.assertEqual({key: result['games'][0][key] for key in fields}, unobserved)

    def test_admission_fields_keep_bounds_and_exclude_nested_private_neighbors(self):
        secret = 'SENSITIVE-TEST-MARKER'
        result = collector.summarize({}, {
            'settings': {'password': secret}, 'rawApiState': secret,
            'library': {'containerCandidates': {'password': secret}, 'rawResponse': secret},
            'games': [{'containerErrorCode': {'token': secret}, 'containerErrorName': 'x' * 4096,
                       'containerStage': {'source': secret}, 'namespaceFilesObserved': [secret],
                       'namespacePathBytesObserved': {'path': secret}, 'source': secret,
                       'sourceBinding': secret, 'password': secret}]})
        self.assertNotIn(secret, json.dumps(result))
        self.assertNotIn('containerCandidates', result['library'])
        self.assertEqual(result['games'][0]['containerErrorName'], 'x' * 1024)
        for key in ('containerErrorCode', 'containerStage', 'namespaceFilesObserved', 'namespacePathBytesObserved'):
            self.assertNotIn(key, result['games'][0])

    def test_bounds_and_invalid_response(self):
        self.assertEqual(len(collector.summarize({}, {'games': [{'name': 'a' * 4096}]})['games'][0]['name']), 1024)
        with self.assertRaises(ValueError):
            collector.summarize({}, {'games': {}})
        with self.assertRaises(ValueError):
            collector.summarize({}, {'games': [{}] * 4097})


if __name__ == '__main__':
    unittest.main()
