#!/bin/zsh
set -euo pipefail
atlas_root="${0:A:h}"
cd "$atlas_root"
atlas_binary="${ATLAS_BINARY:-$atlas_root/build/native/atlas-native}"
atlas_library="${ATLAS_LIBRARY:-$atlas_root/.state/atlas.sqlite3}"
atlas_speed="${ATLAS_SPEED:-60}"
if [[ ! -x "$atlas_binary" || ! -f "$atlas_library" ]]; then
  print 'Native renderer or library missing. Build atlas-native and save a session first.'
  exit 1
fi
print 'ATLAS — experimental saved-session offline audition'
print "Library (read-only): $atlas_library"
print "Observed time; speed $atlas_speed; 48 kHz; root MIDI 60; attack 8 ms; decay 650 ms; gain 0 dB."
print 'This renders the latest saved session, including gaps. It does not collect or control live audio.'
mkdir -p build/auditions
atlas_directory="$(mktemp -d "$atlas_root/build/auditions/audition.XXXXXXXX")"
atlas_output="$atlas_directory/session.wav"
"$atlas_binary" render --db "$atlas_library" --session latest --mode observed --speed "$atlas_speed" \
  --sample-rate 48000 --root-midi 60 --attack-ms 8 --decay-ms 650 --gain-db 0 \
  --output "$atlas_output" | tee "$atlas_directory/render-evidence.json"
print "Saved WAV: $atlas_output"
print 'Evidence above records verified source hash, build identity, admissions, peak and faults.'
print 'Play this WAV with afplay? Type y then Return; anything else exits.'
read -r atlas_answer
if [[ "$atlas_answer" == y || "$atlas_answer" == Y ]]; then
  print 'Playing; Control-C stops this player only. The WAV remains saved.'
  afplay "$atlas_output" &
  atlas_player=$!
  trap 'kill "$atlas_player" 2>/dev/null || true; exit 130' INT TERM
  wait "$atlas_player"
  trap - INT TERM
fi
