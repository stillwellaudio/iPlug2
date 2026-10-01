/* CLAP translation for the opt-in common bus-layout model. */
#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>

#include "IPlugBusLayout.h"
#include "clap/clap.h"

namespace iplug {

inline void ClapBusLayoutNameCopy(char* destination, const char* source)
{
  std::strncpy(destination, source ? source : "", CLAP_NAME_SIZE);
  destination[CLAP_NAME_SIZE - 1] = 0;
}

inline uint8_t ClapSurroundChannel(SpeakerPosition position)
{
  switch (position)
  {
    case SpeakerPosition::L: return CLAP_SURROUND_FL;
    case SpeakerPosition::R: return CLAP_SURROUND_FR;
    case SpeakerPosition::C: return CLAP_SURROUND_FC;
    case SpeakerPosition::LFE: return CLAP_SURROUND_LFE;
    case SpeakerPosition::Ls: return CLAP_SURROUND_BL;
    case SpeakerPosition::Rs: return CLAP_SURROUND_BR;
    case SpeakerPosition::Lc: return CLAP_SURROUND_FLC;
    case SpeakerPosition::Rc: return CLAP_SURROUND_FRC;
    case SpeakerPosition::Cs: return CLAP_SURROUND_BC;
    case SpeakerPosition::Sl: return CLAP_SURROUND_SL;
    case SpeakerPosition::Sr: return CLAP_SURROUND_SR;
    case SpeakerPosition::Tc: return CLAP_SURROUND_TC;
    case SpeakerPosition::Tfl: return CLAP_SURROUND_TFL;
    case SpeakerPosition::Tfc: return CLAP_SURROUND_TFC;
    case SpeakerPosition::Tfr: return CLAP_SURROUND_TFR;
    case SpeakerPosition::Trl: return CLAP_SURROUND_TBL;
    case SpeakerPosition::Trc: return CLAP_SURROUND_TBC;
    case SpeakerPosition::Trr: return CLAP_SURROUND_TBR;
    case SpeakerPosition::Tsl: return CLAP_SURROUND_TSL;
    case SpeakerPosition::Tsr: return CLAP_SURROUND_TSR;
    default: return UINT8_MAX;
  }
}

inline const char* ClapBusLayoutPortType(const BusLayout& layout)
{
  if (layout.kind == LayoutKind::Ambisonic) return CLAP_PORT_AMBISONIC;
  if (layout.kind != LayoutKind::Speakers) return nullptr;
  if (layout.channels.size() == 1 && layout.channels[0].speaker == SpeakerPosition::Mono) return CLAP_PORT_MONO;
  if (layout.channels.size() == 2 && layout.channels[0].speaker == SpeakerPosition::L && layout.channels[1].speaker == SpeakerPosition::R) return CLAP_PORT_STEREO;
  for (const auto& channel : layout.channels) if (ClapSurroundChannel(channel.speaker) == UINT8_MAX) return nullptr;
  return CLAP_PORT_SURROUND;
}

inline bool ClapGetAudioPortInfo(const LayoutBus& bus, clap_audio_port_info* info)
{
  if (!info) return false;
  constexpr uint32_t flags = CLAP_AUDIO_PORT_SUPPORTS_64BITS
                           | CLAP_AUDIO_PORT_PREFERS_64BITS
                           | CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_SIZE;
  info->id = bus.id;
  ClapBusLayoutNameCopy(info->name, bus.name.c_str());
  info->flags = flags | (bus.role == BusRole::Main ? CLAP_AUDIO_PORT_IS_MAIN : 0);
  info->channel_count = bus.layout.NChannels();
  info->port_type = ClapBusLayoutPortType(bus.layout);
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}

inline bool ClapGetAudioPortsConfig(const BusLayoutConfiguration& configuration,
                                    clap_audio_ports_config* config)
{
  if (!config) return false;
  const auto& inputs = configuration.buses[0];
  const auto& outputs = configuration.buses[1];
  const LayoutBus* mainInput = inputs.empty() ? nullptr : &inputs.front();
  const LayoutBus* mainOutput = outputs.empty() ? nullptr : &outputs.front();
  if (mainInput && mainInput->role != BusRole::Main) mainInput = nullptr;
  if (mainOutput && mainOutput->role != BusRole::Main) mainOutput = nullptr;

  config->id = configuration.id;
  ClapBusLayoutNameCopy(config->name, configuration.name.c_str());
  config->input_port_count = static_cast<uint32_t>(inputs.size());
  config->output_port_count = static_cast<uint32_t>(outputs.size());
  config->has_main_input = mainInput != nullptr;
  config->main_input_channel_count = mainInput ? mainInput->layout.NChannels() : 0;
  config->main_input_port_type = mainInput ? ClapBusLayoutPortType(mainInput->layout) : nullptr;
  config->has_main_output = mainOutput != nullptr;
  config->main_output_channel_count = mainOutput ? mainOutput->layout.NChannels() : 0;
  config->main_output_port_type = mainOutput ? ClapBusLayoutPortType(mainOutput->layout) : nullptr;
  return true;
}

inline bool ClapGetAmbisonicConfig(const LayoutBus& bus, clap_ambisonic_config* config)
{
  if (!config || bus.layout.kind != LayoutKind::Ambisonic || !bus.layout.IsValid(bus.layout.NChannels()) ||
      (bus.layout.ordering == AmbisonicOrdering::FuMa && bus.layout.completeOrder > 3)) return false;
  switch (bus.layout.ordering)
  {
    case AmbisonicOrdering::ACN: config->ordering = CLAP_AMBISONIC_ORDERING_ACN; break;
    case AmbisonicOrdering::FuMa: config->ordering = CLAP_AMBISONIC_ORDERING_FUMA; break;
    default: return false;
  }
  switch (bus.layout.normalization)
  {
    case AmbisonicNormalization::MaxN: config->normalization = CLAP_AMBISONIC_NORMALIZATION_MAXN; break;
    case AmbisonicNormalization::SN3D: config->normalization = CLAP_AMBISONIC_NORMALIZATION_SN3D; break;
    case AmbisonicNormalization::N3D: config->normalization = CLAP_AMBISONIC_NORMALIZATION_N3D; break;
    case AmbisonicNormalization::SN2D: config->normalization = CLAP_AMBISONIC_NORMALIZATION_SN2D; break;
    case AmbisonicNormalization::N2D: config->normalization = CLAP_AMBISONIC_NORMALIZATION_N2D; break;
    default: return false;
  }
  return true;
}

inline bool ClapConfigurationRepresentable(const BusLayoutConfiguration& configuration)
{
  for (const auto& buses:configuration.buses) for (const auto& bus:buses)
  {
    if (bus.layout.kind==LayoutKind::Speakers && !ClapBusLayoutPortType(bus.layout)) return false;
    if (bus.layout.kind==LayoutKind::Speakers || bus.layout.kind==LayoutKind::Discrete)
      for (size_t c=0;c<bus.layout.channels.size();++c)
      {
        const auto& channel=bus.layout.channels[c];
        if (channel.nativeAPI!=LayoutAPI::None || channel.native || channel.coordinateFlags ||
            channel.coordinates[0]!=0.f || channel.coordinates[1]!=0.f || channel.coordinates[2]!=0.f ||
            channel.component!=(bus.layout.kind==LayoutKind::Discrete ? c : 0)) return false;
      }

    if (bus.layout.kind==LayoutKind::NativeUnknown || bus.layout.kind==LayoutKind::Unspecified) return false;
    if (bus.layout.kind==LayoutKind::Ambisonic)
    { clap_ambisonic_config config {}; if (!ClapGetAmbisonicConfig(bus,&config) || !bus.layout.IsValid(bus.layout.NChannels())) return false; }
  }
  return true;
}

inline bool ClapGetSurroundMap(const LayoutBus& bus, uint8_t* map, uint32_t capacity, uint32_t& count)
{
  count = 0;
  if (bus.layout.kind != LayoutKind::Speakers || capacity < bus.layout.NChannels()) return false;
  for (uint32_t i = 0; i < bus.layout.NChannels(); ++i)
  {
    const auto channel = ClapSurroundChannel(bus.layout.channels[i].speaker);
    if (channel == UINT8_MAX) return false;
    if (map) map[i] = channel;
  }
  count = bus.layout.NChannels();
  return true;
}

inline bool ClapSetLayoutFromRequest(BusLayout& layout, const clap_audio_port_configuration_request& request)
{
  if (!request.channel_count) return false;
  if (!request.port_type) { layout=BusLayout::Discrete(request.channel_count); return true; }
  if (!std::strcmp(request.port_type, CLAP_PORT_MONO))
  {
    if (request.channel_count != 1) return false;
    layout = BusLayout::Speakers({SpeakerPosition::Mono});
    return true;
  }
  if (!std::strcmp(request.port_type, CLAP_PORT_STEREO))
  {
    if (request.channel_count != 2) return false;
    layout = BusLayout::Speakers({SpeakerPosition::L, SpeakerPosition::R});
    return true;
  }
  if (!std::strcmp(request.port_type, CLAP_PORT_SURROUND))
  {
    const auto* map = static_cast<const uint8_t*>(request.port_details);
    if (!map) return false;
    BusLayout result;
    result.kind = LayoutKind::Speakers;
    result.channels.resize(request.channel_count);
    for (uint32_t i = 0; i < request.channel_count; ++i)
    {
      auto& channel = result.channels[i];
      switch (map[i])
      {
        case CLAP_SURROUND_FL: channel.speaker = SpeakerPosition::L; break;
        case CLAP_SURROUND_FR: channel.speaker = SpeakerPosition::R; break;
        case CLAP_SURROUND_FC: channel.speaker = SpeakerPosition::C; break;
        case CLAP_SURROUND_LFE: channel.speaker = SpeakerPosition::LFE; break;
        case CLAP_SURROUND_BL: channel.speaker = SpeakerPosition::Ls; break;
        case CLAP_SURROUND_BR: channel.speaker = SpeakerPosition::Rs; break;
        case CLAP_SURROUND_FLC: channel.speaker = SpeakerPosition::Lc; break;
        case CLAP_SURROUND_FRC: channel.speaker = SpeakerPosition::Rc; break;
        case CLAP_SURROUND_BC: channel.speaker = SpeakerPosition::Cs; break;
        case CLAP_SURROUND_SL: channel.speaker = SpeakerPosition::Sl; break;
        case CLAP_SURROUND_SR: channel.speaker = SpeakerPosition::Sr; break;
        case CLAP_SURROUND_TC: channel.speaker = SpeakerPosition::Tc; break;
        case CLAP_SURROUND_TFL: channel.speaker = SpeakerPosition::Tfl; break;
        case CLAP_SURROUND_TFC: channel.speaker = SpeakerPosition::Tfc; break;
        case CLAP_SURROUND_TFR: channel.speaker = SpeakerPosition::Tfr; break;
        case CLAP_SURROUND_TBL: channel.speaker = SpeakerPosition::Trl; break;
        case CLAP_SURROUND_TBC: channel.speaker = SpeakerPosition::Trc; break;
        case CLAP_SURROUND_TBR: channel.speaker = SpeakerPosition::Trr; break;
        case CLAP_SURROUND_TSL: channel.speaker = SpeakerPosition::Tsl; break;
        case CLAP_SURROUND_TSR: channel.speaker = SpeakerPosition::Tsr; break;
        default: return false;
      }
    }
    layout = result;
    return true;
  }
  if (!std::strcmp(request.port_type, CLAP_PORT_AMBISONIC))
  {
    const auto* config = static_cast<const clap_ambisonic_config*>(request.port_details);
    if (!config) return false;
    AmbisonicOrdering ordering;
    AmbisonicNormalization normalization;
    switch (config->ordering)
    {
      case CLAP_AMBISONIC_ORDERING_ACN: ordering = AmbisonicOrdering::ACN; break;
      case CLAP_AMBISONIC_ORDERING_FUMA: ordering = AmbisonicOrdering::FuMa; break;
      default: return false;
    }
    switch (config->normalization)
    {
      case CLAP_AMBISONIC_NORMALIZATION_MAXN: normalization = AmbisonicNormalization::MaxN; break;
      case CLAP_AMBISONIC_NORMALIZATION_SN3D: normalization = AmbisonicNormalization::SN3D; break;
      case CLAP_AMBISONIC_NORMALIZATION_N3D: normalization = AmbisonicNormalization::N3D; break;
      case CLAP_AMBISONIC_NORMALIZATION_SN2D: normalization = AmbisonicNormalization::SN2D; break;
      case CLAP_AMBISONIC_NORMALIZATION_N2D: normalization = AmbisonicNormalization::N2D; break;
      default: return false;
    }
    const unsigned dimensions = (normalization == AmbisonicNormalization::SN2D || normalization == AmbisonicNormalization::N2D) ? 2 : 3;
    const double root = dimensions == 2 ? (static_cast<double>(request.channel_count) - 1.) / 2. : std::sqrt(static_cast<double>(request.channel_count)) - 1.;
    const unsigned order = static_cast<unsigned>(root);
    if (root != static_cast<double>(order)) return false;
    BusLayout result = BusLayout::Ambisonic(order, ordering, normalization, dimensions);
    if (result.NChannels() != request.channel_count) return false;
    layout = result;
    return true;
  }
  return false;
}

} // namespace iplug
