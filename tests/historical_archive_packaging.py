"""Offline disposable verification of the bundled identified-animal archive."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sqlite3
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('package_standalone',ROOT/'scripts/package_standalone.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
source = ROOT/'deliverables/ATN-spotted-seal-2018'
with tempfile.TemporaryDirectory(prefix='atlas-history-package-') as folder:
    root=Path(folder); resources=root/'Resources'; resources.mkdir()
    result=module.package_historical_library(source,resources)
    library=resources/result['relativeLibraryPath']
    assert library.is_file() and not library.stat().st_mode & 0o222
    assert result['records']==224 and result['sessionEventCounts']==[5,224]
    for name,digest in result['sha256'].items():
        assert hashlib.sha256((library.parent/name).read_bytes()).hexdigest()==digest
    assert 'acknowledge' in json.loads((library.parent/'manifest.json').read_text())['license']
    with sqlite3.connect(library.as_uri()+'?mode=ro&immutable=1',uri=True) as db:
        assert db.execute('SELECT sum(live_eligible) FROM revisions').fetchone()[0]==0
        assert db.execute("SELECT count(*) FROM revisions WHERE json_extract(event_json,'$.attribution') LIKE '%Animal Telemetry Network%'").fetchone()[0]==224
    assert not list(library.parent.glob('*-wal')) and not list(library.parent.glob('*-shm'))
    # A changed upstream metadata file cannot silently replace packaged provenance.
    bad=root/'bad'; shutil.copytree(source,bad)
    (bad/'source-metadata.json').write_text('{}')
    try:
        module.package_historical_library(bad,resources)
    except ValueError:
        pass
    else:
        raise RuntimeError('corrupt historical provenance accepted')
    assert hashlib.sha256(library.read_bytes()).hexdigest()==result['sha256'][library.name]
print('historical archive packaging: immutable library, identity/context/session counts, attribution, hashes and corruption rejection passed')
