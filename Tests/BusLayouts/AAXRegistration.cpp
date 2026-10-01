#include "IPlugAAXBusLayout.h"
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"failed: %s:%d: %s\\n",__FILE__,__LINE__,#x); return 1; } } while (0)
using namespace iplug;
static LayoutBus Bus(uint32_t id, BusRole role, BusLayout layout) { LayoutBus b; b.id=id; b.role=role; b.layout=std::move(layout); return b; }
static BusLayoutConfiguration Config(uint32_t id, uint32_t native, uint32_t suite, const BusLayout& in, const BusLayout& out)
{ BusLayoutConfiguration c; c.id=id; c.aaxNativeID=native; c.aaxAudioSuiteID=suite; c.buses[0].push_back(Bus(1,BusRole::Main,in)); c.buses[1].push_back(Bus(2,BusRole::Main,out)); return c; }
int main()
{
  const auto stereo=BusLayout::Speakers({SpeakerPosition::L,SpeakerPosition::R});
  const auto surround=BusLayout::Speakers({SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::LFE});
  BusLayoutPolicy p; p.capacities[0]={6}; p.capacities[1]={6}; p.defaultID=11;
  p.configurations.push_back(Config(11,'n001','s001',stereo,stereo));
  p.configurations.push_back(Config(12,'n002','s002',surround,surround));
  CHECK(aaxlayouts::ValidatePolicy(p,true));
  CHECK(aaxlayouts::FindConfiguration(p,AAX_eStemFormat_Stereo,AAX_eStemFormat_Stereo)->id==11);
  BusLayout reused; CHECK(aaxlayouts::FromStem(AAX_eStemFormat_Stereo,reused)); CHECK(aaxlayouts::FromStem(AAX_eStemFormat_Mono,reused)); CHECK(reused.NChannels()==1);
  auto duplicate=p; duplicate.configurations[1].aaxNativeID='n001'; CHECK(!aaxlayouts::ValidatePolicy(duplicate,true));
  auto cross=p; cross.configurations[1].aaxAudioSuiteID='n001'; CHECK(!aaxlayouts::ValidatePolicy(cross,true));
  auto missing=p; missing.configurations[1].aaxAudioSuiteID=0; CHECK(!aaxlayouts::ValidatePolicy(missing,true));
  auto badSC=p; badSC.configurations[0].buses[0].push_back(Bus(3,BusRole::Sidechain,stereo)); badSC.capacities[0].push_back(2); CHECK(!aaxlayouts::ValidatePolicy(badSC,true));
  std::puts("AAX registration validation passed");
}
