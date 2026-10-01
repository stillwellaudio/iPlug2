#include "IPlugCLAPBusLayout.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace iplug;

#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; std::abort(); } } while (0)

int main()
{
  LayoutBus surround;
  surround.id = 40;
  surround.role = BusRole::Main;
  surround.name = "Surround";
  surround.layout = BusLayout::Speakers({SpeakerPosition::L, SpeakerPosition::R,
                                          SpeakerPosition::C, SpeakerPosition::LFE,
                                          SpeakerPosition::Ls, SpeakerPosition::Rs});
  clap_audio_port_info port {};
  CHECK(ClapGetAudioPortInfo(surround, &port));
  CHECK(port.id == 40 && port.channel_count == 6 && !std::strcmp(port.port_type, CLAP_PORT_SURROUND));
  CHECK(port.flags & CLAP_AUDIO_PORT_IS_MAIN);

  const SpeakerPosition positions[] = { SpeakerPosition::L,SpeakerPosition::R,SpeakerPosition::C,SpeakerPosition::LFE,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::Lc,SpeakerPosition::Rc,SpeakerPosition::Cs,SpeakerPosition::Sl,SpeakerPosition::Sr,SpeakerPosition::Tc,SpeakerPosition::Tfl,SpeakerPosition::Tfc,SpeakerPosition::Tfr,SpeakerPosition::Trl,SpeakerPosition::Trc,SpeakerPosition::Trr,SpeakerPosition::Tsl,SpeakerPosition::Tsr };
  for (const auto position : positions) { LayoutBus one; one.layout=BusLayout::Speakers({position}); uint8_t oneMap[1]{}; uint32_t oneCount=0; CHECK(ClapGetSurroundMap(one,oneMap,1,oneCount) && oneCount==1); }
  uint8_t map[6] {};
  uint32_t count = 0;
  CHECK(ClapGetSurroundMap(surround, map, 6, count));
  CHECK(count == 6 && map[0] == CLAP_SURROUND_FL && map[3] == CLAP_SURROUND_LFE && map[5] == CLAP_SURROUND_BR);
  CHECK(!ClapGetSurroundMap(surround, map, 5, count));

  const uint8_t requestedMap[] = {CLAP_SURROUND_FL, CLAP_SURROUND_FR, CLAP_SURROUND_FC,
                                  CLAP_SURROUND_LFE, CLAP_SURROUND_BL, CLAP_SURROUND_BR};
  clap_audio_port_configuration_request request {};
  request.channel_count = 6;
  request.port_type = CLAP_PORT_SURROUND;
  request.port_details = requestedMap;
  BusLayout requested;
  CHECK(ClapSetLayoutFromRequest(requested, request));
  CHECK(requested.SameMeaning(surround.layout));

  request.channel_count = 2;
  request.port_type = CLAP_PORT_STEREO;
  request.port_details = nullptr;
  CHECK(ClapSetLayoutFromRequest(requested, request));
  CHECK(requested.kind == LayoutKind::Speakers && requested.channels[0].speaker == SpeakerPosition::L);
  request.port_type = nullptr;
  CHECK(ClapSetLayoutFromRequest(requested, request) && requested.kind == LayoutKind::Discrete);

  clap_ambisonic_config ambisonicRequest {CLAP_AMBISONIC_ORDERING_ACN, CLAP_AMBISONIC_NORMALIZATION_SN3D};
  request.channel_count = 4;
  request.port_type = CLAP_PORT_AMBISONIC;
  request.port_details = &ambisonicRequest;
  CHECK(ClapSetLayoutFromRequest(requested, request));
  CHECK(requested.kind == LayoutKind::Ambisonic && requested.completeOrder == 1 && requested.NChannels() == 4);
  clap_ambisonic_config ambisonicInfo {};
  LayoutBus ambisonicBus;
  ambisonicBus.layout = requested;
  CHECK(ClapGetAmbisonicConfig(ambisonicBus, &ambisonicInfo));
  CHECK(ambisonicInfo.ordering == CLAP_AMBISONIC_ORDERING_ACN && ambisonicInfo.normalization == CLAP_AMBISONIC_NORMALIZATION_SN3D);
  ambisonicBus.layout=BusLayout::Ambisonic(2,AmbisonicOrdering::FuMa,AmbisonicNormalization::MaxN);
  CHECK(ClapGetAmbisonicConfig(ambisonicBus, &ambisonicInfo));
  for (unsigned order=0; order<=10; ++order)
  {
    auto acn=BusLayout::Ambisonic(order,AmbisonicOrdering::ACN,AmbisonicNormalization::SN3D);
    LayoutBus bus; bus.layout=acn;
    const bool expected=acn.IsValid(128);
    CHECK(ClapGetAmbisonicConfig(bus,&ambisonicInfo) == expected);
  }
  for (unsigned order=0; order<=3; ++order)
  {
    auto fuma=BusLayout::Ambisonic(order,AmbisonicOrdering::FuMa,AmbisonicNormalization::MaxN);
    LayoutBus bus; bus.layout=fuma;
    CHECK(ClapGetAmbisonicConfig(bus,&ambisonicInfo) == fuma.IsValid(128));
  }
  for (const auto norm : {AmbisonicNormalization::SN2D,AmbisonicNormalization::N2D})
  {
    auto twoD=BusLayout::Ambisonic(3,AmbisonicOrdering::ACN,norm,2); LayoutBus bus; bus.layout=twoD;
    CHECK(ClapGetAmbisonicConfig(bus,&ambisonicInfo));
  }
  request.channel_count = 5;
  CHECK(!ClapSetLayoutFromRequest(requested, request));

  BusLayoutConfiguration configuration;
  configuration.id = 9;
  configuration.name = "5.1";
  configuration.buses[0] = {surround};
  LayoutBus output = surround;
  output.id = 41;
  configuration.buses[1] = {output};
  clap_audio_ports_config config {};
  CHECK(ClapGetAudioPortsConfig(configuration, &config));
  CHECK(config.id == 9 && config.has_main_input && config.has_main_output);
  CHECK(config.main_input_channel_count == 6 && config.main_output_channel_count == 6);

  std::cout << "CLAP bus layout mapping passed\n";
}
