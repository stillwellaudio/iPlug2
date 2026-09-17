# CLAP adapter regression checks

Configure and run the fixture tests:

```sh
cmake -S Tests/CLAPAdapter -B /tmp/clap-adapter-tests -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/clap-adapter-tests -j 8
ctest --test-dir /tmp/clap-adapter-tests --output-on-failure
```

The preset tests cover bundled factory discovery, stable program-index keys,
receiver cancellation, vacant slots, recall of continuous and discrete parameters,
parameter rescans, loaded/error notifications, and rejection without state changes.
The single provider uses the plug-in ID: REAPER 7.79 skips providers with a
different ID before calling create(). Discovery constructs a temporary plug-in to read its existing bank, with no editor,
audio activation, or idle timer. Provider initialization caches only names and keys.

For clap-helpers' strict host checks, build with `-DCMAKE_CXX_FLAGS=-D_DEBUG`
and run `iplug_clap_adapter_tests --presets-only`. Other fixture tests deliberately
make invalid host calls which the strict helper terminates on.

## Built plug-in probe

```sh
/tmp/clap-adapter-tests/iplug_clap_factory_preset_probe /path/to/plugin-binary 85
```

On macOS, pass the executable inside the CLAP bundle, such as
`Olga.clap/Contents/MacOS/Olga`. On Linux and Windows, pass the `.clap` binary.
The second argument is the expected number of initialized factory presets.

This discovers the bank through the exported CLAP entry point, loads every key,
checks host notifications, and recalls the bank in reverse order to compare every
parameter exactly. It also rejects an invalid key without changing parameter values.
It does not establish editor, audio, signing, or release qualification.
