#pragma once

#include "IPlugBusLayout.h"
#include "IPlugAAX_Parameters.h"
#include "AAX.h"
#include <set>

namespace iplug {
namespace aaxlayouts {

struct StemMapping { AAX_EStemFormat stem; std::vector<SpeakerPosition> speakers; };

inline const std::vector<StemMapping>& SpeakerStems()
{
  static const std::vector<StemMapping> stems = {
    {AAX_eStemFormat_Mono,{SpeakerPosition::Mono}},
    {AAX_eStemFormat_Stereo,{SpeakerPosition::L,SpeakerPosition::R}},
    {AAX_eStemFormat_LCR,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R}},
    {AAX_eStemFormat_LCRS,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Cs}},
    {AAX_eStemFormat_Quad,{SpeakerPosition::L,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Rs}},
    {AAX_eStemFormat_5_0,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Rs}},
    {AAX_eStemFormat_5_1,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::LFE}},
    {AAX_eStemFormat_6_0,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Cs,SpeakerPosition::Rs}},
    {AAX_eStemFormat_6_1,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Cs,SpeakerPosition::Rs,SpeakerPosition::LFE}},
    {AAX_eStemFormat_7_0_SDDS,{SpeakerPosition::L,SpeakerPosition::Lc,SpeakerPosition::C,SpeakerPosition::Rc,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Rs}},
    {AAX_eStemFormat_7_1_SDDS,{SpeakerPosition::L,SpeakerPosition::Lc,SpeakerPosition::C,SpeakerPosition::Rc,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::LFE}},
    {AAX_eStemFormat_7_0_DTS,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Sl,SpeakerPosition::Sr,SpeakerPosition::Ls,SpeakerPosition::Rs}},
    {AAX_eStemFormat_7_1_DTS,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Sl,SpeakerPosition::Sr,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::LFE}},
    {AAX_eStemFormat_7_0_2,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Sl,SpeakerPosition::Sr,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::Tsl,SpeakerPosition::Tsr}},
    {AAX_eStemFormat_7_1_2,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Sl,SpeakerPosition::Sr,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::LFE,SpeakerPosition::Tsl,SpeakerPosition::Tsr}},
    {AAX_eStemFormat_5_0_2,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::Tsl,SpeakerPosition::Tsr}},
    {AAX_eStemFormat_5_1_2,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::LFE,SpeakerPosition::Tsl,SpeakerPosition::Tsr}},
    {AAX_eStemFormat_5_0_4,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::Tfl,SpeakerPosition::Tfr,SpeakerPosition::Trl,SpeakerPosition::Trr}},
    {AAX_eStemFormat_5_1_4,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::LFE,SpeakerPosition::Tfl,SpeakerPosition::Tfr,SpeakerPosition::Trl,SpeakerPosition::Trr}},
    {AAX_eStemFormat_7_0_4,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Sl,SpeakerPosition::Sr,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::Tfl,SpeakerPosition::Tfr,SpeakerPosition::Trl,SpeakerPosition::Trr}},
    {AAX_eStemFormat_7_1_4,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Sl,SpeakerPosition::Sr,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::LFE,SpeakerPosition::Tfl,SpeakerPosition::Tfr,SpeakerPosition::Trl,SpeakerPosition::Trr}},
    {AAX_eStemFormat_7_0_6,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Sl,SpeakerPosition::Sr,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::Tfl,SpeakerPosition::Tfr,SpeakerPosition::Tsl,SpeakerPosition::Tsr,SpeakerPosition::Trl,SpeakerPosition::Trr}},
    {AAX_eStemFormat_7_1_6,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Sl,SpeakerPosition::Sr,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::LFE,SpeakerPosition::Tfl,SpeakerPosition::Tfr,SpeakerPosition::Tsl,SpeakerPosition::Tsr,SpeakerPosition::Trl,SpeakerPosition::Trr}},
    {AAX_eStemFormat_9_0_4,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Lw,SpeakerPosition::Rw,SpeakerPosition::Sl,SpeakerPosition::Sr,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::Tfl,SpeakerPosition::Tfr,SpeakerPosition::Trl,SpeakerPosition::Trr}},
    {AAX_eStemFormat_9_1_4,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Lw,SpeakerPosition::Rw,SpeakerPosition::Sl,SpeakerPosition::Sr,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::LFE,SpeakerPosition::Tfl,SpeakerPosition::Tfr,SpeakerPosition::Trl,SpeakerPosition::Trr}},
    {AAX_eStemFormat_9_0_6,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Lw,SpeakerPosition::Rw,SpeakerPosition::Sl,SpeakerPosition::Sr,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::Tfl,SpeakerPosition::Tfr,SpeakerPosition::Tsl,SpeakerPosition::Tsr,SpeakerPosition::Trl,SpeakerPosition::Trr}},
    {AAX_eStemFormat_9_1_6,{SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Lw,SpeakerPosition::Rw,SpeakerPosition::Sl,SpeakerPosition::Sr,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::LFE,SpeakerPosition::Tfl,SpeakerPosition::Tfr,SpeakerPosition::Tsl,SpeakerPosition::Tsr,SpeakerPosition::Trl,SpeakerPosition::Trr}}
  };
  return stems;
}

inline AAX_EStemFormat AmbisonicStem(unsigned order)
{
  static const AAX_EStemFormat values[] = { AAX_eStemFormat_None, AAX_eStemFormat_Ambi_1_ACN,
    AAX_eStemFormat_Ambi_2_ACN, AAX_eStemFormat_Ambi_3_ACN, AAX_eStemFormat_Ambi_4_ACN,
    AAX_eStemFormat_Ambi_5_ACN, AAX_eStemFormat_Ambi_6_ACN, AAX_eStemFormat_Ambi_7_ACN };
  return order >= 1 && order <= 7 ? values[order] : AAX_eStemFormat_None;
}

inline bool FromStem(AAX_EStemFormat stem, BusLayout& layout)
{
  for (unsigned order=1; order<=7; ++order) if (AmbisonicStem(order) == stem)
  { layout=BusLayout::Ambisonic(order,AmbisonicOrdering::ACN,AmbisonicNormalization::SN3D); layout.nativeAPI=LayoutAPI::AAX; layout.native=stem; return true; }
  for (const auto& m : SpeakerStems()) if (m.stem == stem)
  { layout=BusLayout(); layout.kind=LayoutKind::Speakers; for (auto p : m.speakers) { ChannelIdentity channel; channel.speaker=p; layout.channels.push_back(channel); } layout.nativeAPI=LayoutAPI::AAX; layout.native=stem; return true; }
  return false;
}

inline bool ToStem(const BusLayout& layout, AAX_EStemFormat& stem)
{
  stem=AAX_eStemFormat_None;
  if (layout.kind == LayoutKind::Ambisonic)
  {
    if (layout.ordering != AmbisonicOrdering::ACN || layout.normalization != AmbisonicNormalization::SN3D || layout.dimensions != 3 || layout.completeOrder < 1) return false;
    stem=AmbisonicStem(static_cast<unsigned>(layout.completeOrder));
  }
  else if (layout.kind == LayoutKind::Speakers)
  {
    for (const auto& m : SpeakerStems())
    {
      if (m.speakers.size() != layout.channels.size()) continue;
      bool matches=true; size_t i=0;
      for (auto p : m.speakers) if (layout.channels[i++].speaker != p) { matches=false; break; }
      if (matches) { stem=m.stem; break; }
    }
  }
  if (stem == AAX_eStemFormat_None) return false;
  BusLayout roundTrip; return FromStem(stem,roundTrip) && roundTrip.IsValid(64) && layout.IsValid(64) && layout.SameMeaning(roundTrip);
}

inline bool ConfigurationSupported(const BusLayoutConfiguration& c, bool requireAudioSuiteID)
{
  if (!c.aaxNativeID || (requireAudioSuiteID && !c.aaxAudioSuiteID) || c.buses[1].empty() || c.buses[1][0].role != BusRole::Main) return false;
  if (c.buses[1].size() > static_cast<size_t>(kMaxAuxOutputStems + 1) || c.buses[0].size() > 2) return false;
  for (unsigned d=0; d<2; ++d)
    for (size_t b=0; b<c.buses[d].size(); ++b)
    {
      const auto& bus=c.buses[d][b]; AAX_EStemFormat stem;
      if (!ToStem(bus.layout,stem)) return false;
      if (d == 1 && (b == 0 ? bus.role != BusRole::Main : bus.role != BusRole::Auxiliary)) return false;
      if (d == 0 && (b == 0 ? bus.role != BusRole::Main : bus.role != BusRole::Sidechain || stem != AAX_eStemFormat_Mono)) return false;
    }
  return true;
}

inline bool IsRegistered(const BusLayoutConfiguration& c) { return c.aaxNativeID || c.aaxAudioSuiteID; }

inline bool ValidatePolicy(const BusLayoutPolicy& policy, bool requireAudioSuiteID)
{
  if (!policy.IsValid()) return false;
  std::set<uint32_t> ids;
  bool anyRegistered=false;
  for (const auto& c : policy.configurations)
  {
    if (!IsRegistered(c)) continue;
    anyRegistered=true;
    if (!c.aaxNativeID || !ConfigurationSupported(c,requireAudioSuiteID) || !ids.insert(c.aaxNativeID).second) return false;
    if (c.aaxAudioSuiteID && !ids.insert(c.aaxAudioSuiteID).second) return false;
  }
  if (!anyRegistered) return false;
  for (size_t i=0; i<policy.configurations.size(); ++i)
  {
    const auto& a=policy.configurations[i]; if (!IsRegistered(a)) continue;
    AAX_EStemFormat ai=AAX_eStemFormat_None, ao=AAX_eStemFormat_None;
    if ((!a.buses[0].empty() && !ToStem(a.buses[0][0].layout,ai)) || !ToStem(a.buses[1][0].layout,ao)) return false;
    if (a.buses[0].empty()) ai=ao;
    for (size_t j=0; j<i; ++j) { const auto& b=policy.configurations[j]; if (!IsRegistered(b)) continue; AAX_EStemFormat bi=AAX_eStemFormat_None, bo=AAX_eStemFormat_None; if ((!b.buses[0].empty() && !ToStem(b.buses[0][0].layout,bi)) || !ToStem(b.buses[1][0].layout,bo)) return false; if (b.buses[0].empty()) bi=bo; if (ai==bi && ao==bo) return false; }
  }
  return true;
}

inline const BusLayoutConfiguration* FindConfiguration(const BusLayoutPolicy& policy, AAX_EStemFormat input, AAX_EStemFormat output)
{
  const BusLayoutConfiguration* found=nullptr;
  for (const auto& c : policy.configurations)
  {
    if (!IsRegistered(c) || !c.aaxNativeID || !ConfigurationSupported(c, false)) continue;
    AAX_EStemFormat in=AAX_eStemFormat_None, out=AAX_eStemFormat_None;
    if (!c.buses[0].empty() && !ToStem(c.buses[0][0].layout,in)) continue;
    if (!ToStem(c.buses[1][0].layout,out) || out != output) continue;
    if (c.buses[0].empty()) in=out; // AAX instruments advertise their output stem as input.
    if (in != input) continue;
    if (found) return nullptr; // AAX does not expose its registered plug-in type to this wrapper.
    found=&c;
  }
  return found;
}
} // namespace aaxlayouts
} // namespace iplug
