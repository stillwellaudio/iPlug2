#pragma once
#include "IPlugBusLayout.h"
#include <AudioToolbox/AudioFormat.h>
#include <cstddef>
#include <cstring>

namespace iplug {
namespace aulayouts {
constexpr size_t kHeaderSize = offsetof(AudioChannelLayout,mChannelDescriptions);
struct SpeakerMapping { AudioChannelLabel native; SpeakerPosition common; };
inline const std::vector<SpeakerMapping>& Speakers()
{
  static const std::vector<SpeakerMapping> values={
    {kAudioChannelLabel_Left,SpeakerPosition::L},{kAudioChannelLabel_Right,SpeakerPosition::R},
    {kAudioChannelLabel_Center,SpeakerPosition::C},{kAudioChannelLabel_LFEScreen,SpeakerPosition::LFE},
    {kAudioChannelLabel_LeftSurround,SpeakerPosition::Ls},{kAudioChannelLabel_RightSurround,SpeakerPosition::Rs},
    {kAudioChannelLabel_LeftCenter,SpeakerPosition::Lc},{kAudioChannelLabel_RightCenter,SpeakerPosition::Rc},
    {kAudioChannelLabel_CenterSurround,SpeakerPosition::Cs},{kAudioChannelLabel_LeftSurroundDirect,SpeakerPosition::Sl},
    {kAudioChannelLabel_RightSurroundDirect,SpeakerPosition::Sr},{kAudioChannelLabel_TopCenterSurround,SpeakerPosition::Tc},
    {kAudioChannelLabel_VerticalHeightLeft,SpeakerPosition::Tfl},{kAudioChannelLabel_VerticalHeightCenter,SpeakerPosition::Tfc},
    {kAudioChannelLabel_VerticalHeightRight,SpeakerPosition::Tfr},{kAudioChannelLabel_TopBackLeft,SpeakerPosition::Trl},
    {kAudioChannelLabel_TopBackCenter,SpeakerPosition::Trc},{kAudioChannelLabel_TopBackRight,SpeakerPosition::Trr},
    {kAudioChannelLabel_LeftWide,SpeakerPosition::Lw},{kAudioChannelLabel_RightWide,SpeakerPosition::Rw},
    {kAudioChannelLabel_LFE2,SpeakerPosition::LFE2},{kAudioChannelLabel_Mono,SpeakerPosition::Mono},
    {kAudioChannelLabel_LeftTopMiddle,SpeakerPosition::Tsl},{kAudioChannelLabel_RightTopMiddle,SpeakerPosition::Tsr}
  };
  return values;
}
inline unsigned CompleteOrder(uint32_t count)
{
  unsigned root=static_cast<unsigned>(std::sqrt(static_cast<double>(count)));
  return root && root*root==count ? root-1 : UINT32_MAX;
}
inline bool FromNative(const void* data, size_t size, uint32_t capacity, BusLayout& result)
{
  if (!data || size<kHeaderSize) return false;
  AudioChannelLayout header{}; std::memcpy(&header,data,kHeaderSize);
  const auto tag=header.mChannelLayoutTag;
  const uint32_t family=tag & 0xffff0000U;
  const uint32_t width=tag & 0xffffU;
  if (family==kAudioChannelLayoutTag_HOA_ACN_SN3D || family==kAudioChannelLayoutTag_HOA_ACN_N3D)
  {
    const unsigned order=CompleteOrder(width);
    if (!width || width>capacity || order==UINT32_MAX) return false;
    result=BusLayout::Ambisonic(order,AmbisonicOrdering::ACN,
      family==kAudioChannelLayoutTag_HOA_ACN_SN3D ? AmbisonicNormalization::SN3D : AmbisonicNormalization::N3D);
  }
  else if (tag==kAudioChannelLayoutTag_Ambisonic_B_Format)
  { result=BusLayout::Ambisonic(1,AmbisonicOrdering::FuMa,AmbisonicNormalization::MaxN); }
  else if (family==kAudioChannelLayoutTag_DiscreteInOrder || family==kAudioChannelLayoutTag_Unknown)
  {
    if (!width || width>capacity) return false;
    result=BusLayout::Discrete(width,family==kAudioChannelLayoutTag_Unknown);
  }
  else
  {
    std::vector<uint8_t> expanded;
    const uint8_t* descriptions=static_cast<const uint8_t*>(data)+kHeaderSize;
    uint32_t count=header.mNumberChannelDescriptions;
    if (tag==kAudioChannelLayoutTag_UseChannelDescriptions)
    {
      if (!count || count>capacity || size<kHeaderSize+static_cast<size_t>(count)*sizeof(AudioChannelDescription)) return false;
    }
    else
    {
      UInt32 needed=0;
      const bool bitmap=tag==kAudioChannelLayoutTag_UseChannelBitmap;
      const auto property=bitmap ? kAudioFormatProperty_ChannelLayoutForBitmap : kAudioFormatProperty_ChannelLayoutForTag;
      const UInt32 argument=bitmap ? header.mChannelBitmap : tag;
      if (AudioFormatGetPropertyInfo(property,sizeof(argument),&argument,&needed)!=noErr ||
          needed<kHeaderSize || needed>kHeaderSize+static_cast<size_t>(capacity)*sizeof(AudioChannelDescription)) return false;
      expanded.resize(needed);
      if (AudioFormatGetProperty(property,sizeof(argument),&argument,&needed,expanded.data())!=noErr) return false;
      AudioChannelLayout layoutHeader{}; std::memcpy(&layoutHeader,expanded.data(),kHeaderSize);
      count=layoutHeader.mNumberChannelDescriptions;
      if (!count || count>capacity || needed<kHeaderSize+static_cast<size_t>(count)*sizeof(AudioChannelDescription)) return false;
      descriptions=expanded.data()+kHeaderSize;
    }
    result=BusLayout(); result.kind=LayoutKind::Speakers;
    bool discrete=true,unspecified=true,acnSN3D=true,acnN3D=true,fuma=count==4;
    for (uint32_t i=0;i<count;++i)
    {
      AudioChannelDescription description{}; std::memcpy(&description,descriptions+i*sizeof(description),sizeof(description));
      ChannelIdentity channel;
      const auto label=description.mChannelLabel;
      discrete &= (label & 0xffff0000U)==kAudioChannelLabel_Discrete_0;
      unspecified &= label==kAudioChannelLabel_Unknown || label==kAudioChannelLabel_Unused;
      acnSN3D &= label==(kAudioChannelLabel_HOA_SN3D | i);
      acnN3D &= label==(kAudioChannelLabel_HOA_N3D | i);
      fuma &= label==kAudioChannelLabel_Ambisonic_W+i;
      for (const auto& mapping:Speakers()) if (label==mapping.native) {channel.speaker=mapping.common;break;}
      if (channel.speaker==SpeakerPosition::Unknown)
      {channel.nativeAPI=LayoutAPI::AU;channel.native=label;}
      // AudioFormat supplies suggested coordinates even for named speakers. Those
      // do not alter a named tag's semantic identity; explicit descriptions do.
      if (tag==kAudioChannelLayoutTag_UseChannelDescriptions)
      {channel.coordinateFlags=description.mChannelFlags;std::copy(description.mCoordinates,description.mCoordinates+3,channel.coordinates);}
      if (label==kAudioChannelLabel_UseCoordinates && !channel.coordinateFlags) return false;
      if (channel.coordinateFlags)
      {
        const auto known=kAudioChannelFlags_RectangularCoordinates | kAudioChannelFlags_SphericalCoordinates | kAudioChannelFlags_Meters;
        if ((channel.coordinateFlags & ~known) ||
            (channel.coordinateFlags & 3)==0 || (channel.coordinateFlags & 3)==3) return false;
      }
      result.channels.push_back(channel);
    }
    if ((acnSN3D || acnN3D) && CompleteOrder(count)!=UINT32_MAX)
      result=BusLayout::Ambisonic(CompleteOrder(count),AmbisonicOrdering::ACN,acnSN3D ? AmbisonicNormalization::SN3D : AmbisonicNormalization::N3D);
    else if (fuma) result=BusLayout::Ambisonic(1,AmbisonicOrdering::FuMa,AmbisonicNormalization::MaxN);
    else if (discrete)
    {
      result.kind=LayoutKind::Discrete;
      for (auto& ch:result.channels) {ch.component=static_cast<uint32_t>(ch.native)&0xffffU;ch.native=0;ch.nativeAPI=LayoutAPI::None;}
    }
    else if (unspecified) result.kind=LayoutKind::Unspecified;
  }
  result.nativeAPI=LayoutAPI::AU;
  result.native=(uint64_t(tag)<<32) | (tag==kAudioChannelLayoutTag_UseChannelBitmap ? header.mChannelBitmap : 0);
  return result.IsValid(capacity);
}
inline bool ToNative(const BusLayout& layout, std::vector<uint8_t>& bytes)
{
  if (!layout.IsValid(65535)) return false;
  AudioChannelLayout header{};
  if (layout.nativeAPI==LayoutAPI::AU)
  {header.mChannelLayoutTag=static_cast<UInt32>(layout.native>>32);header.mChannelBitmap=static_cast<UInt32>(layout.native);}
  else if (layout.kind==LayoutKind::Ambisonic)
  {
    if (layout.dimensions!=3) return false;
    if (layout.ordering==AmbisonicOrdering::ACN &&
        (layout.normalization==AmbisonicNormalization::SN3D || layout.normalization==AmbisonicNormalization::N3D))
      header.mChannelLayoutTag=(layout.normalization==AmbisonicNormalization::SN3D ? kAudioChannelLayoutTag_HOA_ACN_SN3D : kAudioChannelLayoutTag_HOA_ACN_N3D) | layout.NChannels();
    else if (layout.ordering==AmbisonicOrdering::FuMa && layout.completeOrder==1 && layout.normalization==AmbisonicNormalization::MaxN)
      header.mChannelLayoutTag=kAudioChannelLayoutTag_Ambisonic_B_Format;
    else return false;
  }
  else if (layout.kind==LayoutKind::Discrete) header.mChannelLayoutTag=kAudioChannelLayoutTag_DiscreteInOrder | layout.NChannels();
  else if (layout.kind==LayoutKind::Unspecified) header.mChannelLayoutTag=kAudioChannelLayoutTag_Unknown | layout.NChannels();
  else if (layout.kind!=LayoutKind::Speakers) return false;
  if (header.mChannelLayoutTag==kAudioChannelLayoutTag_UseChannelDescriptions)
    header.mNumberChannelDescriptions=layout.NChannels();
  bytes.assign(kHeaderSize+header.mNumberChannelDescriptions*sizeof(AudioChannelDescription),0);
  std::memcpy(bytes.data(),&header,kHeaderSize);
  for (uint32_t c=0;c<header.mNumberChannelDescriptions;++c)
  {
    AudioChannelDescription description{};
    const auto& channel=layout.channels[c];
    if (channel.nativeAPI==LayoutAPI::AU) description.mChannelLabel=static_cast<UInt32>(channel.native);
    else if (layout.kind==LayoutKind::Discrete) description.mChannelLabel=kAudioChannelLabel_Discrete_0 | channel.component;
    else if (layout.kind==LayoutKind::Ambisonic)
    {
      if (layout.ordering==AmbisonicOrdering::ACN)
        description.mChannelLabel=(layout.normalization==AmbisonicNormalization::N3D ? kAudioChannelLabel_HOA_N3D : kAudioChannelLabel_HOA_SN3D) | channel.component;
      else description.mChannelLabel=kAudioChannelLabel_Ambisonic_W+c;
    }
    else
    {for (const auto& mapping:Speakers()) if (mapping.common==channel.speaker) {description.mChannelLabel=mapping.native;break;}}
    if (!description.mChannelLabel && layout.kind!=LayoutKind::Unspecified) return false;
    description.mChannelFlags=channel.coordinateFlags;
    std::copy(channel.coordinates,channel.coordinates+3,description.mCoordinates);
    std::memcpy(bytes.data()+kHeaderSize+c*sizeof(description),&description,sizeof(description));
  }
  BusLayout roundTrip;
  return FromNative(bytes.data(),bytes.size(),layout.NChannels(),roundTrip) && layout.SameMeaning(roundTrip);
}
} // namespace aulayouts
} // namespace iplug
