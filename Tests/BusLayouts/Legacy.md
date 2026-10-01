# Legacy adapter regression scope

`Legacy.cpp` is compiled with `VST3_API` and `CUSTOM_BUSTYPE_FUNC`, without
`PLUG_BUS_LAYOUTS`. It checks legacy mono/stereo type selection, custom bus
labels, a stereo main plus mono sidechain, two stereo outputs, and valid simple
stereo float/double passthrough. It does not claim multibus legacy audio routing,
other format adapters, or host behavior.
