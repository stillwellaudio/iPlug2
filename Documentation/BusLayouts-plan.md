# Opt-in bus layouts implementation plan

Base: Stillwell `stillwellaudio/master`, 44d9eef19cfb1e4ec403aed37befbb311fc3e336.
Ownership: Bitter -> cmake-common -> iPlug2. All three are on
`feature/opt-in-bus-layouts`. Unrelated root WIP and vendor SDK pins are preserved.

## Architecture

A default-null policy supplied through Config and a static config.h provider
opts in. Numeric PLUG_CHANNEL_IO stays unchanged. Complete semantic catalogs
use stable IDs and can share a numeric shape. An optional bounded acceptance
predicate supports host-proposed combinations without enumerating masks.
Descriptors separate ordered channel identities, ambisonic conventions, native
identity, human names, bus roles, capacities, and active state.

New-path flat channels reserve the sum of declared per-bus capacities. Each bus
has an immutable reserved offset, independent of active widths and alternative
shapes. Legacy allocation/indexing is unchanged. Bypass maps declared output
buses to input buses and never treats a sidechain as a main output.

Negotiation validates complete candidates without mutation, then publishes a
coherent active state and a no-op-suppressed callback under the format lifecycle.
Metadata is not preset state. No layout construction, allocation, labels, or
notifications occur in rendering. Editors must use existing messages or copy a
snapshot under lifecycle synchronization; a distributed VST3 controller has no
implicit processor-memory access.

## Stages and checks

1. Record SDK/lifecycle matrix and reproduce legacy semantic/buffer limitations.
2. Header-only common descriptors/policy; real processor capacity/offset tests;
   128-channel discrete and 121-component complete 3D descriptors, conventions,
   semantic equality, transactional rejection and stable IDs.
3. VST3 mapping/negotiation plus every declared input/output bus and precision
   path. Round-trip named masks, HOA encodings, custom masks, active buses,
   strict rejections, grow/shrink and bit-identical passthrough.
4. CLAP, AUv2 and AAX mappings and real adapters, honoring pinned interfaces,
   static registration, sequential AU setup, native identities and notifications.
5. Headless transparent framework probe; sanitizers/validators and available
   real-host smoke/save-reopen tests; legacy examples remain default-off.
6. Document exact support and unverified gaps. Commit framework, parent gitlink,
   then top-level evidence; verify final recursive pointers and WIP preservation.

## Initial verified findings

- VST3 discards requested masks after matching widths; Mixbus/Ardour early success
  also precedes validation. Legacy input processing attaches only buses 0 and 1.
- Processor allocation uses maximum simultaneous total, while AU independently
  reserves bus maxima. Alternatives such as 8.2 versus 2.8 can overflow that model.
- AAX registers one component per width token and returns only one stem from the
  custom bus hook. Auxiliary stem indexing currently starts at bus 0.
- AU layout get returns invalid property value; set is unimplemented. Stream
  changes mutate counts before checking and use aggregate rather than topology.
- CLAP SDK/helper header availability and exact versions need local inventory;
  checked-in dependency folders contain placeholders rather than SDK headers.

SDK support is separate from exercised host support. Bitter production analysis
and lane UI remain unchanged. No SDK contents will be redistributed.
