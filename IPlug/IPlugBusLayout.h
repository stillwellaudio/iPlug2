/* Opt-in bus semantics. No SDK types, masks, rendering, or serialization. */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace iplug {

constexpr uint32_t kInvalidLayoutID = UINT32_MAX;
enum class LayoutKind { Unspecified, Discrete, Speakers, Ambisonic, NativeUnknown };
enum class SpeakerPosition {
  Unknown, Mono, L, R, C, LFE, Ls, Rs, Lc, Rc, Cs, Sl, Sr,
  Tc, Tfl, Tfc, Tfr, Trl, Trc, Trr, LFE2, Tsl, Tsr,
  Bfl, Bfc, Bfr, Pl, Pr, Bsl, Bsr, Brl, Brc, Brr, Lcs, Rcs, Lw, Rw
};
enum class AmbisonicOrdering { Unknown, ACN, FuMa };
enum class AmbisonicNormalization { Unknown, SN3D, N3D, MaxN, SN2D, N2D };
enum class LayoutAPI { None, VST3, CLAP, AU, AAX, VST2 };
enum class BusRole { Main, Auxiliary, Sidechain };

struct ChannelIdentity
{
  SpeakerPosition speaker = SpeakerPosition::Unknown;
  // 3D: ACN identity independent of delivered ordering. 2D: circular index
  // 0, sin(phi), cos(phi), sin(2phi), cos(2phi), ... . Discrete: channel index.
  uint32_t component = 0;
  // Native speaker/label when the common vocabulary cannot describe it.
  LayoutAPI nativeAPI = LayoutAPI::None;
  uint64_t native = 0;
  uint32_t coordinateFlags = 0;
  float coordinates[3] = {0.f, 0.f, 0.f};

  bool operator==(const ChannelIdentity& rhs) const
  {
    return speaker == rhs.speaker && component == rhs.component &&
      nativeAPI == rhs.nativeAPI && native == rhs.native &&
      coordinateFlags == rhs.coordinateFlags &&
      std::equal(coordinates, coordinates + 3, rhs.coordinates);
  }
};

struct BusLayout
{
  LayoutKind kind = LayoutKind::Unspecified;
  std::vector<ChannelIdentity> channels;
  AmbisonicOrdering ordering = AmbisonicOrdering::Unknown;
  AmbisonicNormalization normalization = AmbisonicNormalization::Unknown;
  // 0: dimensionality/order unknown; completeOrder -1: unknown/partial.
  unsigned dimensions = 0;
  int completeOrder = -1;
  LayoutAPI nativeAPI = LayoutAPI::None;
  uint64_t native = 0;
  std::string name;

  uint32_t NChannels() const { return static_cast<uint32_t>(channels.size()); }
  bool SameMeaning(const BusLayout& rhs) const
  {
    return kind == rhs.kind && channels == rhs.channels && ordering == rhs.ordering &&
      normalization == rhs.normalization && dimensions == rhs.dimensions &&
      completeOrder == rhs.completeOrder &&
      (kind != LayoutKind::NativeUnknown ||
       (nativeAPI == rhs.nativeAPI && native == rhs.native));
  }
  // Names never determine identity. Native aliases remain observable in active state.
  bool operator==(const BusLayout& rhs) const
  { return SameMeaning(rhs) && nativeAPI == rhs.nativeAPI && native == rhs.native; }

  bool IsValid(uint32_t capacity) const
  {
    if (channels.empty() || channels.size() > capacity) return false;
    for (const auto& ch : channels)
      for (float x : ch.coordinates) if (!std::isfinite(x)) return false;
    if (kind != LayoutKind::Ambisonic)
    {
      if (ordering != AmbisonicOrdering::Unknown ||
          normalization != AmbisonicNormalization::Unknown || dimensions || completeOrder != -1)
        return false;
      if (kind == LayoutKind::NativeUnknown && nativeAPI == LayoutAPI::None) return false;
      if (kind == LayoutKind::Speakers)
        for (const auto& ch : channels)
          if (ch.speaker == SpeakerPosition::Unknown && ch.nativeAPI == LayoutAPI::None && !ch.coordinateFlags)
            return false;
      return true;
    }
    if (ordering == AmbisonicOrdering::Unknown || normalization == AmbisonicNormalization::Unknown)
      return false;
    if (dimensions != 2 && dimensions != 3) return false;
    if ((dimensions == 2) != (normalization == AmbisonicNormalization::SN2D || normalization == AmbisonicNormalization::N2D))
      return false;
    if (completeOrder < 0) return false; // Partial/mixed orders require a separate convention.
    const uint64_t order = static_cast<unsigned>(completeOrder);
    const uint64_t count = dimensions == 3 ? (order + 1) * (order + 1) : order * 2 + 1;
    if (count != channels.size()) return false;
    if (ordering == AmbisonicOrdering::FuMa && (dimensions != 3 || order > 3))
      return false; // FuMa has established complete 3D ordering through third order.
    for (size_t i = 0; i < channels.size(); ++i)
    {
      uint32_t expected = static_cast<uint32_t>(i);
      if (ordering == AmbisonicOrdering::FuMa)
      {
        // WXYZ RSTUV KLMNOPQ; ACN=n*(n+1)+m. BBC WHP261, eq. 2.
        const uint32_t fuma[] = {0,3,1,2,6,7,5,8,4,12,13,11,14,10,15,9};
        expected = fuma[i];
      }
      if (channels[i].component != expected || channels[i].speaker != SpeakerPosition::Unknown ||
          channels[i].nativeAPI != LayoutAPI::None || channels[i].coordinateFlags) return false;
    }
    return true;
  }

  static BusLayout Discrete(uint32_t count, bool unspecified = false)
  {
    BusLayout result;
    result.kind = unspecified ? LayoutKind::Unspecified : LayoutKind::Discrete;
    result.channels.resize(count);
    for (uint32_t i = 0; i < count; ++i) result.channels[i].component = i;
    return result;
  }
  static BusLayout Speakers(std::initializer_list<SpeakerPosition> positions)
  {
    BusLayout result;
    result.kind = LayoutKind::Speakers;
    for (auto p : positions) { ChannelIdentity ch; ch.speaker = p; result.channels.push_back(ch); }
    return result;
  }
  static BusLayout Ambisonic(unsigned order, AmbisonicOrdering ordering,
                            AmbisonicNormalization normalization, unsigned dimensions = 3)
  {
    BusLayout result;
    result.kind = LayoutKind::Ambisonic;
    result.ordering = ordering; result.normalization = normalization;
    result.dimensions = dimensions;
    // Factory is bounded; author capacities can be larger using explicit descriptors.
    if (order > 255 || (dimensions != 2 && dimensions != 3)) return result;
    result.completeOrder = static_cast<int>(order);
    const uint32_t count = dimensions == 3 ? (order + 1) * (order + 1) : 2 * order + 1;
    result.channels.resize(count);
    for (uint32_t i = 0; i < count; ++i) result.channels[i].component = i;
    if (ordering == AmbisonicOrdering::FuMa && order<=3 && dimensions==3)
    { const uint32_t fuma[] = {0,3,1,2,6,7,5,8,4,12,13,11,14,10,15,9};
      for (uint32_t i=0; i<count; ++i) result.channels[i].component=fuma[i]; }
    return result;
  }
};

struct LayoutBus
{
  uint32_t id = kInvalidLayoutID;
  BusRole role = BusRole::Auxiliary;
  BusLayout layout;
  std::string name;
  bool active = true;
  // Output bypass source, -1 for silence. Never automatically flatten sidechains.
  int bypassInputBus = -1;

  bool SameMeaning(const LayoutBus& rhs) const
  { return id == rhs.id && role == rhs.role && bypassInputBus == rhs.bypassInputBus && layout.SameMeaning(rhs.layout); }
  bool operator==(const LayoutBus& rhs) const
  { return SameMeaning(rhs) && layout == rhs.layout && active == rhs.active; }
};

struct BusLayoutConfiguration
{
  uint32_t id = kInvalidLayoutID;
  std::string name;
  std::vector<LayoutBus> buses[2]; // 0 input, 1 output, matching ERoute.
  // Explicit IDs for semantic AAX registrations. Zero means not registered for that API.
  uint32_t aaxNativeID = 0;
  uint32_t aaxAudioSuiteID = 0;

  bool SameMeaning(const BusLayoutConfiguration& rhs) const
  {
    for (unsigned d=0; d<2; ++d)
    {
      if (buses[d].size() != rhs.buses[d].size()) return false;
      for (size_t b=0; b<buses[d].size(); ++b)
        if (!buses[d][b].SameMeaning(rhs.buses[d][b])) return false;
    }
    return true;
  }
  bool operator==(const BusLayoutConfiguration& rhs) const
  { return id == rhs.id && buses[0] == rhs.buses[0] && buses[1] == rhs.buses[1]; }
};

struct BusLayoutPolicy
{
  std::vector<uint32_t> capacities[2];
  std::vector<BusLayoutConfiguration> configurations;
  uint32_t defaultID = kInvalidLayoutID;
  // Called only during negotiation, after bounds/shape validation. Must be bounded,
  // side-effect-free, and safe to call repeatedly. Does not override capacity checks.
  bool (*accept)(const BusLayoutConfiguration&) = nullptr;

  const BusLayoutConfiguration* Find(uint32_t id) const
  { for (const auto& c : configurations) if (c.id == id) return &c; return nullptr; }
  bool WithinCapacity(const BusLayoutConfiguration& c) const
  {
    for (unsigned d=0; d<2; ++d)
    {
      if (c.buses[d].size() > capacities[d].size()) return false;
      for (size_t b=0; b<c.buses[d].size(); ++b)
      {
        const auto& bus = c.buses[d][b];
        if (bus.id == kInvalidLayoutID || !bus.layout.IsValid(capacities[d][b])) return false;
        for (size_t a=0; a<b; ++a) if (bus.id == c.buses[d][a].id) return false;
        if (bus.role == BusRole::Main && b != 0) return false;
        if (d == 1 && bus.bypassInputBus >= 0)
        {
          if (static_cast<size_t>(bus.bypassInputBus) >= c.buses[0].size()) return false;
          const auto& source = c.buses[0][bus.bypassInputBus];
          // An explicit bypass route copies native channel order without a
          // conversion. Complete policy acceptance decides the semantic
          // relationship, including an intentionally anonymous endpoint.
          if (source.role == BusRole::Sidechain || source.layout.NChannels()!=bus.layout.NChannels()) return false;
        }
      }
    }
    return !c.buses[0].empty() || !c.buses[1].empty();
  }
  bool IsValid() const
  {
    if (!Find(defaultID)) return false;
    for (unsigned d=0; d<2; ++d)
    {
      uint64_t total = 0;
      for (auto n : capacities[d])
      { if (!n) return false; total += n; if (total > INT32_MAX) return false; }
    }
    for (size_t i=0; i<configurations.size(); ++i)
    {
      const auto& c = configurations[i];
      if (c.id == kInvalidLayoutID || !WithinCapacity(c)) return false;
      for (size_t j=0; j<i; ++j) if (c.id == configurations[j].id) return false;
    }
    return true;
  }
  bool Accepts(const BusLayoutConfiguration& c) const
  {
    if (!WithinCapacity(c)) return false;
    for (const auto& declared : configurations) if (declared.SameMeaning(c)) return true;
    return accept && accept(c);
  }
  uint32_t ListedID(const BusLayoutConfiguration& c) const
  { for (const auto& declared : configurations) if (declared.SameMeaning(c)) return declared.id; return kInvalidLayoutID; }
};

} // namespace iplug
