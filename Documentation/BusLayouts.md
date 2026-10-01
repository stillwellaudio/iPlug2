# Opt-in bus layouts

This feature is opt-in. Existing plug-ins remain on the `PLUG_CHANNEL_IO` path unless their `config.h` defines `PLUG_BUS_LAYOUTS()` to return a stable `const iplug::BusLayoutPolicy*`.

```cpp
// config.h: numeric shape appears once, despite three semantic alternatives.
#define PLUG_CHANNEL_IO "4-4"
#include "IPlugBusLayout.h"

static const iplug::BusLayoutPolicy* MyLayouts()
{
  using namespace iplug;
  static const BusLayoutPolicy policy = [] {
    BusLayoutPolicy p;
    p.capacities[0] = {4}; p.capacities[1] = {4}; p.defaultID = 100;
    const BusLayout layouts[] = {
      BusLayout::Speakers({SpeakerPosition::L, SpeakerPosition::R,
                          SpeakerPosition::Ls, SpeakerPosition::Rs}),
      BusLayout::Speakers({SpeakerPosition::L, SpeakerPosition::R,
                          SpeakerPosition::C, SpeakerPosition::Cs}),
      BusLayout::Ambisonic(1, AmbisonicOrdering::ACN,
                          AmbisonicNormalization::SN3D)
    };
    const char* names[] = {"Quad", "LCRS", "First-order ACN/SN3D"};
    // Assign permanent IDs; retain them when inserting/reordering entries.
    const uint32_t ids[] = {100, 200, 300};
    const uint32_t nativeIDs[] = {0x42714e31, 0, 0x42614e31};
    const uint32_t suiteIDs[] = {0x42714131, 0, 0x42614131};
    for (unsigned i = 0; i < 3; ++i) {
      BusLayoutConfiguration c; c.id = ids[i]; c.name = names[i];
      c.aaxNativeID = nativeIDs[i]; c.aaxAudioSuiteID = suiteIDs[i];
      LayoutBus input; input.id = 10; input.role = BusRole::Main;
      input.name = "Main input"; input.layout = layouts[i];
      LayoutBus output = input; output.id = 20; output.name = "Main output";
      output.bypassInputBus = 0;
      c.buses[0] = {input}; c.buses[1] = {output};
      p.configurations.push_back(c);
    }
    return p;
  }();
  return &policy;
}
#define PLUG_BUS_LAYOUTS() MyLayouts()
```

These example AAX IDs belong to an illustrative product; assign your own permanent product IDs before shipping. AAX skips configurations with both AAX IDs zero. Every registered configuration requires a Native ID; an AudioSuite build also requires its AudioSuite ID. The example LCRS order is L,R,C,Cs; AAX delivers L,C,R,Cs, so that entry is deliberately unregistered for AAX. Add an explicitly declared AAX-order alternative if that product needs it; do not silently reorder the delivered samples. Product/component IDs outside this policy are unchanged. Configuration names are display text and can be localized without changing identity. The policy and everything it references must remain immutable and alive for the plug-in lifetime. Construction and all vector/string work happen outside render.


The macro is read by `MakeConfig()` in `IPlug_include_in_plug_src.h`. A null policy is the default and preserves the existing adapter behavior. Existing `PLUG_CHANNEL_IO`, `CUSTOM_BUSTYPE_FUNC`, and format-specific custom bus hooks continue to control the default-off path; the policy controls the opt-in path.

## Policy and processing contract

A policy contains finite `BusLayoutConfiguration` entries, per-direction/per-bus maximum capacities, and one declared `defaultID`. Each bus has a stable ID, role (`Main`, `Auxiliary`, or `Sidechain`), semantic `BusLayout`, active state, and optional bypass source. A configuration must also match one numeric `PLUG_CHANNEL_IO` shape. This keeps old format registration and the opt-in model consistent.

`BusLayout` distinguishes speaker identities, discrete or unspecified channels, native-unknown identities, and ambisonics. Ambisonic identity includes ordering, normalization, dimension, and component identity. Do not infer a speaker arrangement or HOA convention solely from channel count.

Capacities reserve a fixed flat buffer range for every declared bus. `GetBusBufferOffset()` and `GetBusBufferCapacity()` are immutable after construction. An adapter calls `BeginBusLayoutBlock(nFrames)` once, then attaches every present bus with `AttachBusBuffers()`. Missing, inactive, null, and silent input channels use preallocated zero scratch. The render path must not resize, allocate, lock, log, or do I/O.

An explicit bypass route requires a non-sidechain source with equal width. The full declared policy or its bounded `accept` predicate determines semantic compatibility, including intentionally anonymous endpoints. There is no implicit channel remapping.

`CanApplyBusLayout()` checks capacity, numeric shape, and policy acceptance. An adapter must prepare and validate its native objects first, then call `CommitBusLayout()` once. A failed negotiation must leave the active layout unchanged. `OnBusLayoutChanged()` runs after a successful publication, outside render, and must not touch IGraphics. `GetActiveBusLayoutConfiguration()` and `GetActiveBusLayout()` return transient references: use them on the lifecycle thread, or on the audio thread while activated. GUI code receives a copied snapshot through existing synchronized messaging.

The host owns topology and lifecycle ordering. Adapter metadata and configuration calls are not a general cross-thread serialization mechanism.

## Adapter capability matrix

| Adapter | Opt-in behavior | Topology and lifecycle | Installed-SDK limits and gaps |
|---|---|---|---|
| CLAP | Audio ports, audio-port configs, configurable audio ports, surround, and ambisonic/config-info where representable | Finite configurations may select topology while inactive. No activation extension is used. | CLAP SDK plus clap-helpers are external pinned probe dependencies; no SDK gitlink changes. Unknown or unrepresentable native layouts are not advertised. |
| VST3 | Named semantic arrangements and fixed native audio bus set | Arrangement changes are accepted only while inactive. Dynamic bus-count changes require the host restart protocol and are not implemented. | Current local validators and pluginval evidence are tracked separately from this document. |
| AUv2 | Channel-layout, stream-format, tag, element-count, connection, and render handling for declared layouts | Width or topology changes require uninitialize. After initialize, a same-width layout property edit is accepted only after whole-candidate `CanApplyBusLayout()` and `CommitBusLayout()`. Compatible connections and render callbacks may be replaced under the existing AU lifecycle serialization; no new locks are added. | Stream-format width-only changes publish an anonymous/unknown layout tag. An explicit unset reports info size zero/no error and value `kAudioUnitErr_PropertyNotInUse`. `auval` is local evidence, not Logic qualification. |
| AAX | Explicit per-configuration Native and AudioSuite IDs and installed-SDK stem mapping | Registration is static; instance selection uses the host stem pair. | All 27 installed speaker stem mappings and ACN/SN3D 3D HOA orders 1 through 7 are represented. Native and AudioSuite adapters have SDK controller/render fixtures. Real-host AudioSuite stereo, Quad, HOA-3 and HOA-7 render checks pass; Native host routing and state restore remain unverified. |
| VST2 | One flat input and one flat output semantic bus, where the VST2 arrangement API can represent it | Whole arrangement negotiation is inactive-only and fixed-width. | No HOA or arbitrary semantic-count inference. |

AUv3, APP, Web, and other adapters ignore the policy. Other-platform qualification remains open.

## Verification boundary

The focused tests exercise common-policy validation, mappings, adapter negotiation, malformed blocks, and selected real adapter calls. They do not prove DAW loading, scanner behavior, signing, installation, realtime scheduling in a commercial host, or listening behavior.

Use the current build directory and roots supplied at configuration time:

```sh
cmake --build /private/tmp/iplug-bus-layout-tests -j4
ctest --test-dir /private/tmp/iplug-bus-layout-tests --output-on-failure
```

`BUS_LAYOUT_HOST_PROBES=ON` builds reproducible VST3, AUv2, CLAP, AAX Native, and AAX AudioSuite probe bundles. It does not install, sign, or publish them. The AAX probes compile the real SDK initialization and ACF export sources. Separate AAXAdapter fixtures use the SDK versioned wrappers with a test controller and invoke the actual static render entry point. Opt-in AAX instances prepare the SDK maximum ProcessProc block size (1024 samples) during construction; larger blocks are cleared without resizing on render.

For external contracts, consult the primary format documentation: [CLAP](https://cleveraudio.org/), [VST3](https://steinbergmedia.github.io/vst3_doc/), and [Apple Audio Unit Services](https://developer.apple.com/documentation/audiotoolbox/audio-unit-services). The installed AAX SDK is the authoritative contract for the AAX integration.

## Delivered buffers and author responsibilities

The initial descriptor becomes valid at adapter initialization/publication, after the derived object exists. Initial publication, identity/order/native alias changes, and activation changes invoke `OnBusLayoutChanged()`; identical negotiations do not. A descriptor reference expires at the next successful commit. Reading it concurrently with negotiation is unsupported. The host must serialize its lifecycle calls; this change adds no locks to render. For a separate VST3 controller, send a copied descriptor or compact labels using the existing processor/controller messages. Never access processor memory directly or call IGraphics from the notification.

`ProcessBlock()` receives fixed reserved flat ranges. A bus starts at `GetBusBufferOffset(direction, bus)`, and only `GetActiveBusChannelCount(direction, bus)` entries represent active channels. Sum-of-per-bus capacity can exceed any one configuration's simultaneous width. Do not walk a compact aggregate and assume there are no holes. Queries and indexed channel identities require no render-time allocation. Input scratch is zeroed; disconnected output scratch absorbs writes. The adapter preserves native delivery order and exposes that same order in `layout.channels`.

For an equal-layout transparent main bus, an author can copy the active channels using these offsets, or let the framework's declared bypass route do so. A bypass route performs an ordered sample copy with existing latency handling. It does not convert channel meaning, normalization, or spatial position. If an author deliberately declares mismatched semantics, the author owns its DSP transformation; framework bypass still copies raw native order. Never route a sidechain as an implicit main bypass source.

A bounded pure `accept` predicate is optional. It runs after capacity, numeric-shape, and structural checks; validate the entire input/output relationship. It cannot authorize buffer growth or bypass the format's representability checks. Listed IDs are recovered by semantic matching; an accepted unlisted configuration reports `kInvalidLayoutID`. Host activation is separate from the declared semantic capability.

Do not serialize the active host topology into plug-in presets. The probe saves its parameter only, and save/reopen restores that parameter while the host renegotiates layout.

## Exact representability in the checked-out adapters

| Layer | Speaker/discrete representation | Ambisonic representation | Boundaries |
|---|---|---|---|
| Common model | Ordered channel vector; native label and finite coordinate supplements | Complete ACN 2D/3D; established FuMa 3D orders 0-3; explicit normalization | Factory bounded to order 255. Tests include discrete 128 and 3D order 10 (121 components). Per-bus capacities and checked INT32 aggregate bounds apply. Partial/mixed orders are rejected. |
| VST3 | Installed speaker bits and named arrangements, custom exact speaker combinations; unknown native bits retained when explicitly declared | Installed HOA encodings 1-7, complete 3D ACN/SN3D | Ordinary masks have 64 bits, but HOA encoding is interpreted explicitly. No discrete or zeroth-order ambisonic advertisement. Native bus count stays fixed; restart-based count changes are deferred. |
| CLAP | 20 SDK surround positions; mono/stereo; arbitrary bounded discrete counts with null port type | Complete ACN 3D SN3D/N3D/MaxN; FuMa 3D through order 3; ACN 2D SN2D/N2D | Unsupported supplements/coordinates/native identities are rejected. Finite inactive topology selection supported. Audio-port activation extension is not implemented. |
| AUv2 | SDK named tags, bitmap and ordered explicit descriptions, coordinates, native unknown labels, discrete/unknown count tags | Complete 3D ACN/SN3D or N3D count-encoded HOA tags; first-order FuMa/MaxN tag | Adapter bound 128 channels per bus, allowing complete 3D order 10. Noninterleaved canonical float delivery; existing framework precision conversion remains separate. Tags carry component count, not order. |
| AAX Native/AS | All 27 installed ordinary/immersive stems through 9.1.6 | Installed complete 3D ACN/SN3D HOA stems 1-7 | At most 64 channels per stem, one main input, optional mono sidechain, main output and at most 32 auxiliary outputs. Static registration; no arbitrary generic input ports, discrete stems, or zeroth-order identity. |
| VST2 | Installed speaker constants/user-defined arrangement and discrete undefined speakers | No truthful HOA identity API in this implementation | One input and one output, fixed widths at most 8 (local inline arrangement storage). No multibus semantic topology. |

FuMa component permutation follows BBC WHP 261 equation 2, with ACN identities retained independently of delivered order. Ordering and normalization are separate; no adapter changes samples to translate either. Native aliases stay observable even where two named tags have the same speaker meaning. AU setup may temporarily contain incompatible scope properties; only a fully validated candidate becomes the processing configuration at initialize. Width-only AU setup remains anonymous instead of inventing a surround arrangement.

## Default-off and build limits

A missing/null provider retains existing numeric parser/wildcards, IDs, custom hooks, bus order, and processing branches. Invalid opt-in policies fail closed. The opt-in path takes precedence over custom layout hooks; those hooks retain their legacy use when default-off. Other adapters ignore the policy and retain their legacy path; this is not semantic parity for AUv3/APP/Web.

The AU factory property setter thunk now uses the SDK's by-value data-size ABI. The existing pointer-taking helper remains source-compatible. This repair is also necessary for correct default-off factory dispatch and has explicit factory-call fixture coverage.

Native adapter fixtures currently build on macOS with the local SDKs; the portable common model uses C++14. Windows/Linux and all released shipping build variants have not been qualified. Existing SDK deprecation/packing warnings remain. ASan/UBSan is exercised; concurrent unsynchronized metadata access is outside the contract and TSan qualification is pending. Host and validator evidence is recorded separately in the consumer repository's `docs/bus-layouts/README.md`.
