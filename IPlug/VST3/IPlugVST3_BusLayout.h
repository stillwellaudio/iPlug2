#pragma once
#include "IPlugBusLayout.h"
#include "pluginterfaces/vst/vstspeaker.h"

namespace iplug {
namespace vst3layouts {
using namespace Steinberg::Vst;
struct SpeakerMapping { Speaker native; SpeakerPosition common; };
inline const std::vector<SpeakerMapping>& Speakers()
{
  static const std::vector<SpeakerMapping> mapping = {
    {kSpeakerL,SpeakerPosition::L},{kSpeakerR,SpeakerPosition::R},{kSpeakerC,SpeakerPosition::C},
    {kSpeakerLfe,SpeakerPosition::LFE},{kSpeakerLs,SpeakerPosition::Ls},{kSpeakerRs,SpeakerPosition::Rs},
    {kSpeakerLc,SpeakerPosition::Lc},{kSpeakerRc,SpeakerPosition::Rc},{kSpeakerCs,SpeakerPosition::Cs},
    {kSpeakerSl,SpeakerPosition::Sl},{kSpeakerSr,SpeakerPosition::Sr},{kSpeakerTc,SpeakerPosition::Tc},
    {kSpeakerTfl,SpeakerPosition::Tfl},{kSpeakerTfc,SpeakerPosition::Tfc},{kSpeakerTfr,SpeakerPosition::Tfr},
    {kSpeakerTrl,SpeakerPosition::Trl},{kSpeakerTrc,SpeakerPosition::Trc},{kSpeakerTrr,SpeakerPosition::Trr},
    {kSpeakerLfe2,SpeakerPosition::LFE2},{kSpeakerM,SpeakerPosition::Mono},
    {kSpeakerTsl,SpeakerPosition::Tsl},{kSpeakerTsr,SpeakerPosition::Tsr},
    {kSpeakerLcs,SpeakerPosition::Lcs},{kSpeakerRcs,SpeakerPosition::Rcs},
    {kSpeakerBfl,SpeakerPosition::Bfl},{kSpeakerBfc,SpeakerPosition::Bfc},{kSpeakerBfr,SpeakerPosition::Bfr},
    {kSpeakerPl,SpeakerPosition::Pl},{kSpeakerPr,SpeakerPosition::Pr},
    {kSpeakerBsl,SpeakerPosition::Bsl},{kSpeakerBsr,SpeakerPosition::Bsr},
    {kSpeakerBrl,SpeakerPosition::Brl},{kSpeakerBrc,SpeakerPosition::Brc},{kSpeakerBrr,SpeakerPosition::Brr},
    {kSpeakerLw,SpeakerPosition::Lw},{kSpeakerRw,SpeakerPosition::Rw}
  };
  return mapping;
}
inline SpeakerArrangement AmbisonicArrangement(unsigned order)
{
  const SpeakerArrangement values[] = {0,SpeakerArr::kAmbi1stOrderACN,SpeakerArr::kAmbi2cdOrderACN,
    SpeakerArr::kAmbi3rdOrderACN,SpeakerArr::kAmbi4thOrderACN,SpeakerArr::kAmbi5thOrderACN,
    SpeakerArr::kAmbi6thOrderACN,SpeakerArr::kAmbi7thOrderACN};
  return order <= 7 ? values[order] : 0;
}
inline bool FromArrangement(SpeakerArrangement arr, BusLayout& result)
{
  if (!arr) return false;
  if (SpeakerArr::isAmbisonics(arr))
  {
    unsigned order=1;
    while (order<=7 && AmbisonicArrangement(order)!=arr) ++order;
    result=BusLayout::Ambisonic(order,AmbisonicOrdering::ACN,AmbisonicNormalization::SN3D);
  }
  else
  {
    result=BusLayout(); result.kind=LayoutKind::Speakers;
    // Partial/mixed ACN masks must not be relabeled as ordinary speakers.
    const bool mixedACN=(arr & SpeakerArr::kAmbi4thOrderACN)!=0;
    if (mixedACN) result.kind=LayoutKind::NativeUnknown;
    const int count=SpeakerArr::getChannelCount(arr);
    for (int c=0;c<count;++c)
    {
      const Speaker speaker=SpeakerArr::getSpeaker(arr,c);
      ChannelIdentity channel;
      bool known=false;
      if (!mixedACN) for (const auto& m:Speakers())
        if (m.native==speaker) {channel.speaker=m.common; known=true; break;}
      if (!known) {channel.nativeAPI=LayoutAPI::VST3; channel.native=speaker;}
      result.channels.push_back(channel);
    }
  }
  result.nativeAPI=LayoutAPI::VST3; result.native=arr;
  return result.IsValid(64);
}
inline bool ToArrangement(const BusLayout& layout, SpeakerArrangement& arr)
{
  arr=0;
  if (!layout.IsValid(64)) return false;
  if (layout.kind==LayoutKind::Ambisonic)
  {
    if (layout.ordering!=AmbisonicOrdering::ACN || layout.normalization!=AmbisonicNormalization::SN3D || layout.dimensions!=3)
      return false;
    arr=AmbisonicArrangement(static_cast<unsigned>(layout.completeOrder));
  }
  else if (layout.kind==LayoutKind::Speakers)
  {
    for (const auto& channel:layout.channels)
    {
      Speaker value=0;
      if (channel.nativeAPI==LayoutAPI::VST3) value=channel.native;
      else for (const auto& m:Speakers()) if (m.common==channel.speaker) {value=m.native; break;}
      if (!value || (arr & value)) return false;
      arr |= value;
    }
  }
  else if (layout.kind==LayoutKind::NativeUnknown && layout.nativeAPI==LayoutAPI::VST3) arr=layout.native;
  else return false; // VST3 masks cannot express a positive-width anonymous/discrete bus.
  BusLayout roundTrip;
  return FromArrangement(arr,roundTrip) && layout.SameMeaning(roundTrip);
}
} // namespace vst3layouts
} // namespace iplug
