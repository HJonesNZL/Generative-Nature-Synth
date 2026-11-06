#!/bin/zsh
# Local launcher: no installation, administrator access or account needed.
set -u
cd "${0:A:h}" || exit 1
export PYTHONDONTWRITEBYTECODE=1

while true; do
  print '\nANIMAL ATLAS — local collection and saved sessions'
  print '1  Record for one hour'
  print '2  Record for two hours'
  print '3  Collect until stopped'
  print '4  Show saved sessions'
  print '5  Save current bird activity as a replay session'
  print '6  Preview latest saved session at 60x (event timing, no sound yet)'
  print '7  Export latest saved session to the local Sessions folder'
  print '8  Show collector status'
  print '9  Quit'
  read 'choice?Choose a number: ' || break
  case "$choice" in
    1) python3 -B -m animal_atlas serve --config config/local.json --record-seconds 3600 --duration 3600 --session-name 'One-hour animal recording' ;;
    2) python3 -B -m animal_atlas serve --config config/local.json --record-seconds 7200 --duration 7200 --session-name 'Two-hour animal recording' ;;
    3) print 'Collecting. Press Control-C to stop and return here.'
       python3 -B -m animal_atlas serve --config config/local.json ;;
    4) python3 -B -m animal_atlas sessions ;;
    5) python3 -B -m animal_atlas snapshot --name 'Saved NZ bird activity' --source birdweather ;;
    6) python3 -B -m animal_atlas replay latest --mode observed --speed 60 --limit 5 ;;
    7) mkdir -p -m 700 Sessions
       filename="Sessions/Animal Atlas $(date +%Y%m%d-%H%M%S).atlas.json"
       python3 -B -m animal_atlas session-export latest "$filename" ;;
    8) python3 -B -m animal_atlas status ;;
    9) break ;;
    *) print 'Choose a number from 1 to 9.' ;;
  esac
done
