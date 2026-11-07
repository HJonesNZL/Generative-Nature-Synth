"""Actual abrupt-exit recovery checks; all SQLite files are disposable copies."""
import hashlib
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    binary, archive = map(lambda item: Path(item).resolve(), sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="atlas-import-crash-") as directory:
        root = Path(directory)
        source = root / "attached.sqlite3"
        shutil.copyfile(archive, source)
        source_digest = digest(source)

        def run(*arguments, succeeds=True):
            result = subprocess.run([str(binary), *map(str, arguments)],
                                    capture_output=True, text=True, timeout=60)
            require((result.returncode == 0) == succeeds,
                    f"Unexpected importer result: {result.stdout}\n{result.stderr}")
            return result

        def create_destination(name):
            destination = root / (name + ".sqlite3")
            run("--initialize", destination)
            return destination, Path(str(destination) + ".atlas-import-stage.sqlite3")

        def die(stage, statements):
            # Tiny cache forces dirty pages into the real rollback journal. An
            # immediate os._exit deliberately skips SQLite rollback/destructors.
            program = """import os, sqlite3, sys
connection = sqlite3.connect(sys.argv[1])
connection.execute('PRAGMA journal_mode=DELETE')
connection.execute('PRAGMA cache_size=1')
connection.execute('PRAGMA cache_spill=ON')
connection.execute('BEGIN IMMEDIATE')
""" + statements + "\nos._exit(0)\n"
            subprocess.run([sys.executable, "-c", program, str(stage)],
                           check=True, timeout=30)

        for scenario in ("owned-hot-journal", "uncommitted-create", "empty-open"):
            destination, stage = create_destination(scenario)
            if scenario == "owned-hot-journal":
                run("--stage-only", source, destination)
                die(stage, "connection.execute(\"UPDATE import_rows SET event_json=event_json||'uncommitted'\")")
            elif scenario == "uncommitted-create":
                die(stage, "connection.execute('CREATE TABLE import_identity(marker TEXT)')\n"
                           "connection.execute('CREATE TABLE import_rows(ordinal INTEGER)')")
            else:
                die(stage, "pass")
            journal = Path(str(stage) + "-journal")
            require(stage.exists() and journal.exists(), "Crash fixture did not leave a staging slot and journal")
            if scenario == "owned-hot-journal":
                require(journal.read_bytes()[:8] != b"\0" * 8,
                        "Crash fixture did not produce a hot journal")
            run("--resume", source, destination)
            with sqlite3.connect(destination) as connection:
                require(connection.execute("SELECT event_count FROM sessions").fetchall() == [(224,)],
                        "Recovered publication lost or duplicated events")
                require(connection.execute("SELECT count(*) FROM revisions").fetchone() == (0,),
                        "Crash recovery entered live intake")
            require(not stage.exists() and not journal.exists(), "Recovered stage was not retired")

        # An unrelated populated file, even with its own hot journal, must never
        # be recovered or replaced in the reserved slot before ownership proof.
        destination, stage = create_destination("unowned-hot-journal")
        with sqlite3.connect(stage) as connection:
            connection.execute("CREATE TABLE owner_data(value TEXT)")
            connection.executemany("INSERT INTO owner_data VALUES(?)", [("x" * 4096,)] * 128)
        die(stage, "connection.execute(\"UPDATE owner_data SET value=value||'uncommitted'\")")
        journal = Path(str(stage) + "-journal")
        untouched = (digest(stage), digest(journal))
        run("--resume", source, destination, succeeds=False)
        require((digest(stage), digest(journal)) == untouched,
                "Importer modified an unrelated database or recovered its journal")

        destination, stage = create_destination("journal-symlink")
        run("--stage-only", source, destination)
        journal = Path(str(stage) + "-journal")
        target = root / "unrelated-target"
        target.write_bytes(b"preserve")
        try:
            journal.symlink_to(target)
        except (OSError, NotImplementedError):
            # Symlink support requires additional privileges on some Windows
            # installations; abrupt-exit and ownership checks still run there.
            pass
        else:
            original_stage = digest(stage)
            run("--resume", source, destination, succeeds=False)
            require(target.read_bytes() == b"preserve" and digest(stage) == original_stage,
                    "Importer followed the checkpoint journal symlink")
        require(digest(source) == source_digest, "Crash recovery modified the attached source")
    print("piece import crash recovery passed")


if __name__ == "__main__":
    main()
