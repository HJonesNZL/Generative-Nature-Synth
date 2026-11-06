#!/usr/bin/env python3
"""Create a reviewable proposed patch, qualify only a disposable package copy."""
import difflib
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
originals = {name: (ROOT/'animal_atlas'/name).read_text() for name in ['sources.py', 'collector.py', 'store.py']}
changed = dict(originals)
changed['sources.py'] = changed['sources.py'].replace(
    '            if result.get("errors"):',
    '            if stop.is_set():\n                raise FetchError("cancelled")\n            if result.get("errors"):')
changed['sources.py'] = changed['sources.py'].replace(
    '        bootstrap = self.transport.json(',
    '        if stop.is_set():\n            raise FetchError("cancelled")\n        bootstrap = self.transport.json(')
changed['sources.py'] = changed['sources.py'].replace(
    '        cfg = self.transport.json(config_url)',
    '        if stop.is_set():\n            raise FetchError("cancelled")\n        cfg = self.transport.json(config_url)\n        if stop.is_set():\n            raise FetchError("cancelled")')
changed['sources.py'] = changed['sources.py'].replace(
    '        response = self.transport.json(url)',
    '        response = self.transport.json(url)\n        if stop.is_set():\n            raise FetchError("cancelled")')
changed['collector.py'] = changed['collector.py'].replace(
    '            result = self.store.ingest(',
    '            if self.stop.is_set():\n                raise FetchError("cancelled")\n            result = self.store.ingest(')
changed['store.py'] = changed['store.py'].replace(
    '            status.update(metadata or {})',
    '            usable_complete = bool(complete) and not (metadata or {}).get("invalid_records", 0)\n            status.update(metadata or {})')
changed['store.py'] = changed['store.py'].replace(
    'baseline_complete=status.get("baseline_complete", False) or bool(complete)',
    'baseline_complete=status.get("baseline_complete", False) or usable_complete')
changed['store.py'] = changed['store.py'].replace('            if complete:\n', '            if usable_complete:\n')
patch = ''.join(''.join(difflib.unified_diff(originals[name].splitlines(True), changed[name].splitlines(True),
    fromfile='a/animal_atlas/'+name, tofile='b/animal_atlas/'+name)) for name in originals)
if not patch:
    raise RuntimeError('candidate patch unexpectedly empty')
(HERE/'proposed-reference-fix.patch').write_text(patch)

def run(package_root):
    env = dict(os.environ, PYTHONPATH=str(package_root), PYTHONDONTWRITEBYTECODE='1')
    result = subprocess.run([sys.executable, '-B', str(HERE/'acceptance_checks.py')],
        cwd=HERE, env=env, text=True, capture_output=True, timeout=30)
    return dict(exit_code=result.returncode, stdout=result.stdout, stderr=result.stderr)

before = run(ROOT)
with tempfile.TemporaryDirectory(prefix='atlas-reliability-candidate-') as directory:
    temporary = Path(directory)
    shutil.copytree(ROOT/'animal_atlas', temporary/'animal_atlas', ignore=shutil.ignore_patterns('__pycache__'))
    for name, text in changed.items():
        (temporary/'animal_atlas'/name).write_text(text)
    after = run(temporary)
report = dict(role='proposed reference-only patch; not applied to production or proof of native reliability',
    source_sha256={f'animal_atlas/{name}':hashlib.sha256(text.encode()).hexdigest() for name,text in originals.items()},
    patch_sha256=hashlib.sha256(patch.encode()).hexdigest(), before=before, after=after,
    expected_original_failures=4, expected_candidate_passes=4, network='forbidden', owner_access=False)
(HERE/'candidate-results.json').write_text(json.dumps(report, indent=2)+'\n')
if before['exit_code'] != 1 or 'FAILED (failures=4)' not in before['stderr'] or after['exit_code'] != 0:
    print(json.dumps(report, indent=2))
    raise SystemExit('candidate qualification mismatch; inspect preserved results')
print('Original:4 acceptance failures reproduced. Temporary patched reference:4/4 pass. Production unchanged.')
