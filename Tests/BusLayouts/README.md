# Bus layout tests

These are focused framework and adapter tests for the opt-in `PLUG_BUS_LAYOUTS()` model. They use synthetic policies and buffers. They are not DAW qualification or a shipping plug-in test suite.

## What is covered

- `Common.cpp`: policy validity, capacities, numeric `PLUG_CHANNEL_IO` shapes, reserved offsets, block attachment, null/silent channels, bypass routes, and commit/no-op behavior.
- `CLAPMapping.cpp` and `CLAPAdapter.cpp`: surround and ambisonic mappings, stable and unlisted configuration IDs, finite topology selection, malformed requests, float/double process buffers, constants, silence, and zero frames.
- `VST3.cpp`: semantic arrangement mapping and fixed topology processing.
- `AU.cpp`: AUv2 layout properties, tags, configuration lifecycle, factory ABI, and selected render paths.
- `AAXMapping.cpp`, `AAXRegistration.cpp`, and `AAXAdapter.cpp`: installed-SDK stem mapping, registration validation, and Native/AudioSuite SDK controller lifecycle and static render callbacks. `AAXTestHost.h` supplies test-owned versioned controller, transport and automation interfaces.
- `VST2.cpp` and `Legacy.cpp`: fixed flat arrangement behavior and default-off compatibility, including legacy VST3 custom bus hooks.

The suite does not establish commercial-host loading, scanner acceptance, signing, installation, realtime scheduling, or listening behavior. Real-host AAX AudioSuite rendering evidence is recorded separately in the qualification project; Native host routing remains unverified. Broad legacy qualification is incomplete beyond the listed fixtures and Bitter coverage.

## Configure and run

The test project expects local SDK roots. The existing test build is typically configured at `/private/tmp/iplug-bus-layout-tests`.

```sh
cmake --build /private/tmp/iplug-bus-layout-tests -j4
ctest --test-dir /private/tmp/iplug-bus-layout-tests --output-on-failure
```

For a fresh local configuration, provide the framework root and available SDK roots, for example:

```sh
cmake -S Tests/BusLayouts -B /private/tmp/iplug-bus-layout-tests \
  -DCMAKE_BUILD_TYPE=Release -DBUS_LAYOUT_SANITIZERS=ON \
  -DVST3_SDK_ROOT=/path/to/VST3_SDK \
  -DCLAP_SDK_ROOT=/path/to/clap \
  -DCLAP_HELPERS_ROOT=/path/to/clap-helpers \
  -DAAX_SDK_ROOT=/path/to/AAX_SDK \
  -DVST2_SDK_ROOT=/path/to/VST2_SDK
```

Set `-DBUS_LAYOUT_HOST_PROBES=ON` to build the five standalone probe bundles. This option does not install or sign bundles. AAX probe targets use the real installed SDK init/export implementation, but still require an actual host for controller and render qualification.

`Dependencies/IPlug/download-clap-layout-sdks.sh` is an isolated pinned downloader for local CLAP probe dependencies. It is non-destructive and does not change vendored SDK gitlinks. Do not use it as a shipping dependency update mechanism.

## Interpretation

Treat a passing test as evidence for only its named fixture and executable path. Current host, validator, and sanitizer reruns may be in progress; record their command, environment, and result with the qualification work rather than promoting pending counts into this README.

Named mapping tables `VST3Named.inc` and `AUNamed.inc` cover every implemented named constant from the installed headers. AAX mapping tests cover all 27 speaker stems and HOA orders 1-7. CLAP tests call the actual extension vtables for named speaker and ambisonic metadata, including undersized maps and unlisted configurations. AAX adapter targets invoke the SDK Initialize/Uninitialize lifecycle, ResetFieldData and StaticRenderAudio using a versioned test controller. They exercise all 27 speaker stems and HOA orders 1-7, mono sidechain, two auxiliary outputs, reserved-capacity holes, null channels, bypass/fades/latency, parameter changes, instruments, multiple instances, rejected controller/layout transactions, zero frames, and prepared-block bounds. The transparent HOA-7 regression covers Native block sizes through the SDK maximum of 1024 samples; a default-off stereo fixture checks the legacy path. This is synthetic controller/render evidence, separate from DAW qualification.

VST3/CLAP/AU direct fixtures use distinct nonzero data on every channel, at least three input buses where supported, reserved-capacity holes, precision conversion, null/silent cases, rejected transactions, no-op renegotiation, and grow/shrink/reselect cycles. AU additionally renders multiple output elements in reverse order and verifies cache invalidation at the live/offline boundary. Transparent exactly representable samples compare exactly; this does not promise universal float/double conversion identity.
