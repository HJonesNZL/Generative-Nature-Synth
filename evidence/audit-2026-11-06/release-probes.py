"""Isolated audit probes: no owner state or external/loopback network use."""
import json
import sys
import tempfile
import urllib.request
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from animal_atlas.api import Handler
from animal_atlas.client import Client
from animal_atlas.sessions import Sessions
from animal_atlas.store import Store

results = {}
with tempfile.TemporaryDirectory(prefix='atlas-release-probe-', dir='/private/tmp') as folder:
    root = Path(folder)
    (root / 'connection.json').write_text(json.dumps({'port': 12345, 'store_id': 'fixture'}))
    (root / 'api.token').write_text('synthetic-audit-token-not-a-secret')
    client = Client(root)
    redirect = next(h for h in client.opener.handlers if isinstance(h, urllib.request.HTTPRedirectHandler))
    original = urllib.request.Request(client.base + '/v1/status', headers={'Authorization': 'Bearer ' + client.token})
    # Constructing a redirected request does not connect to this reserved example address.
    redirected = redirect.redirect_request(original, None, 302, 'Found', {}, 'http://192.0.2.1/audit')
    results['client_cross_authority_redirect'] = {
        'redirect_handler': type(redirect).__name__,
        'authorization_forwarded': redirected.get_header('Authorization') == original.get_header('Authorization'),
        'network_requests': 0,
    }
    # Invoke the current request authorization method without constructing a server/socket.
    replies = []
    fake = SimpleNamespace(server=SimpleNamespace(server_port=12345, token=''),
                           headers={'Host': '127.0.0.1:12345', 'Authorization': 'Bearer '},
                           path='/v1/status', reply=lambda *args: replies.append(args))
    results['empty_token_method_probe'] = {'accepted': Handler.authorized(fake), 'replies': replies}

    sessions = Sessions(Store(root / 'fixture.sqlite3'))
    first = sessions.snapshot('first fixture')
    for i in range(100):
        sessions.snapshot('later fixture ' + str(i))
    listed = sessions.list(100)
    results['session_library_capacity'] = {
        'created': 101, 'listed': len(listed),
        'first_present_in_list': first['id'] in {s['id'] for s in listed},
        'first_still_readable_by_id': sessions.get(first['id'])['name'] == 'first fixture',
    }

registry = json.loads((ROOT / 'docs/source-registry.json').read_text())['entries']
results['registry'] = {
    'entries': len(registry),
    'implemented': sum(bool(r.get('implemented')) for r in registry),
    'licence_null': sum(r.get('licence') is None for r in registry),
}
print(json.dumps(results, indent=2))
