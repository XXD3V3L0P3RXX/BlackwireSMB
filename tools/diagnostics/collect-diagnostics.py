#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Collect a small read-only Blackwire summary, excluding credentials and tokens."""
import argparse
from datetime import datetime, timezone
import ipaddress
import json
from pathlib import Path
import urllib.request


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def select(source, fields):
    result = {}
    if not isinstance(source, dict):
        return result
    for key in fields:
        value = source.get(key)
        if value is None or type(value) in (bool, int, float):
            result[key] = value
        elif isinstance(value, str):
            result[key] = value[:1024]
    return result


def summarize(health, state):
    games = state.get('games', [])
    if not isinstance(games, list) or len(games) > 4096:
        raise ValueError('Unexpected games response')
    return {
        'schema': 'blackwire-diagnostic-summary/1',
        'capturedUtc': datetime.now(timezone.utc).isoformat(),
        'health': select(health, ('ok', 'buildId', 'readerSha256')),
        'setup': select(state.get('setup'), ('supported', 'editable', 'busy', 'configured', 'hasSavedCredentials',
                                            'revision', 'operation', 'phase', 'message', 'settingsFile', 'storageStatus')),
        'library': select(state.get('library'), ('phase', 'items', 'entries', 'unsearched', 'stale', 'requests', 'contentBytes')),
        'games': [select(game, ('name', 'titleId', 'status', 'containerFormat', 'formatValidated',
                              'startupReady', 'dashboardLaunchReady', 'preparationReason')) for game in games],
        'notice': 'Review game names and diagnostic messages before sharing. Settings, credentials, tokens and source bindings are excluded.'
    }


def fetch(address, route):
    request = urllib.request.Request('http://' + address + ':5918' + route,
                                     headers={'Accept': 'application/json'}, method='GET')
    with urllib.request.build_opener(NoRedirect()).open(request, timeout=20) as response:
        if response.status != 200:
            raise ValueError('Unexpected HTTP status')
        raw = response.read(8 * 1024 * 1024 + 1)
    if len(raw) > 8 * 1024 * 1024:
        raise ValueError('Response exceeds diagnostic limit')
    value = json.loads(raw)
    if not isinstance(value, dict):
        raise ValueError('Expected JSON object')
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ps5', required=True, help='PS5 IPv4 address on your LAN')
    parser.add_argument('--output', type=Path, default=Path('blackwire-diagnostic-summary.json'))
    args = parser.parse_args()
    address = str(ipaddress.IPv4Address(args.ps5))
    if args.output.exists():
        parser.error('Output already exists; choose a new filename')
    result = summarize(fetch(address, '/health'), fetch(address, '/api/state'))
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2, allow_nan=False)
        stream.write('\n')
    print('Saved diagnostic summary to ' + str(args.output))


if __name__ == '__main__':
    main()
