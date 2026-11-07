"""Bundle the existing offline UI and SQLite into a locally signed macOS app.

No downloads, installer, developer account or paid signing service. Generated
Resources/ui is replaced on rebuild; user state must never live in the bundle.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import sqlite3
import subprocess
import sys


def command(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT).strip()


def dependencies(path):
    return [line.strip().split(' (', 1)[0] for line in command('otool', '-L', str(path)).splitlines()[1:]]


def system_dependency(path):
    return path.startswith(('/System/Library/', '/usr/lib/'))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def package_historical_library(source, resources):
    """Verified public development artifact only; never reads user libraries."""
    names = ('ATN spotted seal 2018.sqlite3', 'README.md', 'manifest.json',
             'source-metadata.json', 'source-locations.json')
    for name in names:
        path = source / name
        if path.is_symlink() or not path.is_file():
            raise ValueError('Missing regular historical archive resource: ' + name)
    manifest = json.loads((source / 'manifest.json').read_text())
    if manifest.get('dataset') != 'atn_174822_spotted-seal_trajectory_20180414-20181103':
        raise ValueError('Unexpected historical archive dataset')
    for original, bundled in (('metadata.json', 'source-metadata.json'),
                              ('locations.json', 'source-locations.json')):
        if digest(source / bundled) != manifest['raw_sha256'][original]:
            raise ValueError('Historical archive source integrity mismatch')
    for name in (names[0], 'README.md', 'source-metadata.json', 'source-locations.json'):
        if digest(source / name) != manifest['files_sha256'][name]:
            raise ValueError('Historical archive deliverable integrity mismatch')
    library = source / names[0]
    with sqlite3.connect(library.as_uri() + '?mode=ro&immutable=1', uri=True) as db:
        count, live = db.execute('SELECT count(*),sum(live_eligible) FROM revisions').fetchone()
        sessions = sorted(row[0] for row in db.execute('SELECT event_count FROM sessions'))
        imported = db.execute("SELECT count(*) FROM revisions WHERE json_extract(event_json,'$.provenance_mode')='imported' AND json_extract(event_json,'$.individual_id') IS NOT NULL AND json_extract(event_json,'$.deployment_id') IS NOT NULL").fetchone()[0]
        if count != 224 or live != 0 or imported != 224 or sessions != [5, 224]:
            raise ValueError('Historical archive identity/session/context verification failed')
    target = resources / 'historical-spotted-seal'
    if target.is_symlink():
        raise ValueError('Historical resource directory must not be a symlink')
    if target.exists():
        shutil.rmtree(target)
    target.mkdir()
    hashes = {}
    for name in names:
        shutil.copyfile(source / name, target / name)
        (target / name).chmod(0o444)
        hashes[name] = digest(target / name)
    return dict(relativeLibraryPath='historical-spotted-seal/' + names[0],
                context='historical imported satellite fixes; never live',
                records=count, sessionEventCounts=sessions, sha256=hashes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--ui', required=True, type=Path)
    parser.add_argument('--sqlite', required=True, type=Path)
    parser.add_argument('--juce', required=True, type=Path)
    parser.add_argument('--copy-to', type=Path, help='Optional NEW app path for an installed-copy check')
    parser.add_argument('--zip', type=Path, help='Optional NEW ZIP path')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('Packaging currently requires macOS')
    if args.app.is_symlink() or not args.app.name.endswith('.app'):
        parser.error('App must be a real .app bundle')
    app = args.app.resolve(strict=True)
    contents = app / 'Contents'
    info = plistlib.loads((contents / 'Info.plist').read_bytes())
    executable_name = info['CFBundleExecutable']
    if Path(executable_name).name != executable_name:
        parser.error('Invalid bundle executable name')
    executable = contents / 'MacOS' / executable_name
    if not executable.is_file() or executable.is_symlink():
        parser.error('Missing native bundle executable')
    ui = args.ui.resolve(strict=True)
    files = []
    total = 0
    for path in sorted(ui.rglob('*')):
        if path.is_symlink():
            parser.error('UI assets must not contain symlinks')
        if path.is_file():
            total += path.stat().st_size
            files.append(path)
    if not (ui / 'index.html').is_file() or len(files) > 512 or total > 64 * 1024 * 1024:
        parser.error('Offline UI is missing or exceeds its resource budget')
    for name in ('Resources', 'Frameworks'):
        path = contents / name
        if path.is_symlink():
            parser.error('Bundle resource directories must not be symlinks')
        path.mkdir(exist_ok=True)
    target_ui = contents / 'Resources' / 'ui'
    if target_ui.is_symlink():
        parser.error('Generated UI directory must not be a symlink')
    if target_ui.exists():
        shutil.rmtree(target_ui)
    shutil.copytree(ui, target_ui)
    historical = package_historical_library(
        Path(__file__).resolve().parents[1] / 'deliverables' / 'ATN-spotted-seal-2018',
        contents / 'Resources')
    sqlite = args.sqlite.resolve(strict=True)
    bundled_sqlite = contents / 'Frameworks' / 'libsqlite3.dylib'
    if bundled_sqlite.is_symlink():
        parser.error('Bundled SQLite must not be a symlink')
    if bundled_sqlite.exists():
        bundled_sqlite.unlink()  # Replace the generated copy, including read-only Homebrew modes.
    shutil.copy2(sqlite, bundled_sqlite)
    bundled_sqlite.chmod(bundled_sqlite.stat().st_mode | 0o200)
    command('install_name_tool', '-id', '@rpath/libsqlite3.dylib', str(bundled_sqlite))
    linked = [path for path in dependencies(executable) if Path(path).name.startswith('libsqlite3')]
    if len(linked) != 1:
        parser.error('Expected exactly one SQLite runtime dependency')
    command('install_name_tool', '-change', linked[0], '@executable_path/../Frameworks/libsqlite3.dylib', str(executable))
    for path in dependencies(executable):
        if not system_dependency(path) and path != '@executable_path/../Frameworks/libsqlite3.dylib':
            parser.error('Unbundled native dependency: ' + path)
    for path in dependencies(bundled_sqlite)[1:]:
        if not system_dependency(path):
            parser.error('SQLite has an unbundled dependency: ' + path)
    notices = contents / 'Resources' / 'notices'
    if notices.is_symlink():
        parser.error('Notices directory must not be a symlink')
    notices.mkdir(exist_ok=True)
    juce = args.juce.resolve(strict=True)
    upstream_notices = {
        'JUCE-LICENSE.md': 'LICENSE.md',
        'jpeg.txt': 'modules/juce_graphics/image_formats/jpglib/README',
        'png.txt': 'modules/juce_graphics/image_formats/pnglib/LICENSE',
        'zlib.txt': 'modules/juce_core/zip/zlib/README',
        'harfbuzz.txt': 'modules/juce_graphics/fonts/harfbuzz/COPYING',
        'sheenbidi.txt': 'modules/juce_graphics/unicode/sheenbidi/LICENSE',
        'lunasvg.txt': 'modules/juce_graphics/drawables/lunasvg/LICENSE',
        'plutovg.txt': 'modules/juce_graphics/drawables/lunasvg/plutovg/LICENSE',
    }
    for target, source in upstream_notices.items():
        shutil.copy2(juce / source, notices / target)
    (notices / 'sqlite.txt').write_text('SQLite deliverable code is public domain.\nhttps://sqlite.org/copyright.html\n')
    (notices / 'LOCAL-BUILD.txt').write_text(
        'Animal Atlas local beta build. No paid runtime dependency or service.\n'
        'JUCE 9 modules offer AGPLv3 or commercial licensing; see JUCE-LICENSE.md.\n'
        'This build does not assign a new licence to project code or authorise public distribution.\n'
        'The local ad-hoc signature is not Developer ID signing or notarisation.\n'
        'Frontend dependencies and asset origins are in Resources/ui/licenses.\n')
    command('codesign', '--force', '--sign', '-', '--timestamp=none', str(bundled_sqlite))
    # The manifest is created before signing so it is included in the seal.
    manifest = {
        'recordedAtUtc': datetime.now(timezone.utc).isoformat(),
        'architecture': command('lipo', '-archs', str(executable)),
        'executableSha256BeforeOuterSignature': digest(executable),
        'sqliteSha256': digest(bundled_sqlite),
        'ui': {str(path.relative_to(ui)): digest(path) for path in files},
        'historicalSpottedSeal': historical,
        'externalNativeDependencies': dependencies(executable),
        'signing': 'local ad-hoc; not notarised',
        'runtimeRequiresNode': False,
        'runtimeRequiresRepository': False,
    }
    (contents / 'Resources' / 'package-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    command('codesign', '--force', '--sign', '-', '--timestamp=none', str(app))
    command('codesign', '--verify', '--deep', '--strict', str(app))
    if args.copy_to:
        target = args.copy_to.absolute()
        if target.exists() or target.is_symlink():
            parser.error('Installed-copy destination already exists')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(app, target)
        command('codesign', '--verify', '--deep', '--strict', str(target))
    if args.zip:
        target = args.zip.absolute()
        if target.exists() or target.is_symlink():
            parser.error('ZIP destination already exists')
        target.parent.mkdir(parents=True, exist_ok=True)
        command('ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(app), str(target))
    print(json.dumps({'app': str(app), 'architecture': manifest['architecture'], 'assets': len(files), 'assetBytes': total, 'verified': True}))


if __name__ == '__main__':
    main()
