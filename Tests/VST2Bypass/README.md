# VST2 host bypass checks

Configure and run against the VST2 SDK (by default `../VST2_SDK`, next to
iPlug2 as in cmake-common):

```sh
cmake -S Tests/VST2Bypass -B /tmp/vst2-bypass-tests -DCMAKE_BUILD_TYPE=Release \
  -DVST2_SDK_ROOT=/path/to/VST2_SDK
cmake --build /tmp/vst2-bypass-tests -j 8
ctest --test-dir /tmp/vst2-bypass-tests --output-on-failure
```

The test loads a fixture plug-in through `VSTPluginMain`, as a host does, and
checks `canDo("bypass")`, `effSetBypass` and the `OnHostBypassChanged()`
notification for a plug-in that returns true from
`HandlesHostBypassInternally()` and for one that does not. macOS and Linux only.
It does not establish behavior in any particular host.
