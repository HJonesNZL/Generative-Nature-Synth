#!/bin/zsh
# Local UI with optional native saved-piece playback. Does not start, stop or reconfigure collection.
set -eu
atlas_root="${0:A:h}"
cd "$atlas_root/ui/app"
atlas_node="$(command -v node || true)"
if [[ -z "$atlas_node" ]]; then
  atlas_node="$HOME/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node"
fi
if [[ ! -x "$atlas_node" || ! -f node_modules/vite/bin/vite.js ]]; then
  print 'Node and pinned UI dependencies are required. See ui/app/README.md.'
  exit 1
fi
if [[ -z "${ATLAS_NATIVE_BINARY:-}" ]]; then
  if [[ -x "${ATLAS_AUDIO_HOST_BINARY:-$atlas_root/build/native-playback/atlas-audio-host}" ]]; then
    export ATLAS_NATIVE_BINARY="${ATLAS_AUDIO_HOST_BINARY:-$atlas_root/build/native-playback/atlas-audio-host}"
  else
    export ATLAS_NATIVE_BINARY="$atlas_root/build/native/atlas-native"
  fi
fi
export ATLAS_LIBRARY="${ATLAS_LIBRARY:-$atlas_root/.state/atlas.sqlite3}"
if [[ ! -x "$ATLAS_NATIVE_BINARY" || ! -f "$ATLAS_LIBRARY" ]]; then
  print 'Native binary or library is missing. Build the native target or set ATLAS_LIBRARY.'
  print 'See ui/app/README.md. No database will be created by this launcher.'
  exit 1
fi
print 'ANIMAL ATLAS — native library UI; capabilities determine audio availability'
print 'Saved-piece audio starts disabled. Select a piece, enable Listening, then Play.'
print 'Open http://127.0.0.1:5173/ for your library.'
print 'Open http://127.0.0.1:5173/demo.html for the illustrative design demo (no sound).'
print 'Leave this window open. Control-C stops only this UI preview.'
exec "$atlas_node" node_modules/vite/bin/vite.js
