"""Create an explicitly synthetic UI test library at a caller-chosen NEW path."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from animal_atlas.model import event
from animal_atlas.store import Store
from animal_atlas.sessions import Sessions

path=Path(sys.argv[1])
if path.exists(): raise SystemExit('Refusing to replace an existing library')
store=Store(path)
values=[]
for index in range(2005):
    values.append((event('fixture',str(index),'2026-01-01T00:10:00Z',
        'acoustic_species_detection','station_detection',-38.14-(index%3)*.1,176.25,
        species_id='fixture:species:tui',common_name='Fixture Tūī',scientific_name='Synthetic UI fixture',
        station_id='fixture:station:'+str(index%3),provenance_mode='demo'),{}))
store.ingest('fixture',values,True)
sessions=Sessions(store)
large=sessions.snapshot('Fixture • 2,005 records',observed_from='2026-01-01T00:00:00Z',observed_to='2026-01-01T01:00:00Z')
empty=sessions.snapshot('Fixture • empty historical hour',sources=[],observed_from='2026-01-02T00:00:00Z',observed_to='2026-01-02T01:00:00Z')
print('Synthetic fixture created:',path)
