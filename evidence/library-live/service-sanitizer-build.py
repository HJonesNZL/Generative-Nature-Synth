import subprocess, sys
from pathlib import Path
root=Path('/Users/henryjones/Documents/ChatGPT/Generative Nature Synth')
mode=sys.argv[1]
args=['c++','-std=c++20','-O1','-g','-DNDEBUG','-mmacosx-version-min=15.0','-Wall','-Wextra','-Wpedantic','-Wconversion','-Werror','-DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1','-DJUCE_USE_CURL=0','-DJUCE_STANDALONE_APPLICATION=1','-I','native/include','-I','native/src','-I','/Users/henryjones/Documents/Audio/Project-Ceramic-Kiln/third_party/JUCE/modules']
if mode=='asan': args+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
if mode=='tsan': args+=['-fsanitize=thread']
args+=['tests/collection_service_tests.cpp','native/src/juce_collection.cpp','native/src/collection.cpp','native/src/core.cpp','native/src/sha256.cpp','native/src/event_validation.cpp']
objects=root/'build/standalone/CMakeFiles/atlas-standalone-support-tests.dir/Users/henryjones/Documents/Audio/Project-Ceramic-Kiln/third_party/JUCE/modules/juce_core'
args += [str(objects/file) for file in ['juce_core.mm.o','juce_core_CompilationTime.cpp.o','juce_core_zlib.c.o']]
output='/private/tmp/atlas-collection-service-'+mode
args+=['/opt/homebrew/opt/sqlite/lib/libsqlite3.dylib','-framework','Cocoa','-framework','Foundation','-framework','IOKit','-framework','Security','-o',output]
subprocess.run(args,cwd=root,check=True)
subprocess.run([output],cwd=root,check=True)
