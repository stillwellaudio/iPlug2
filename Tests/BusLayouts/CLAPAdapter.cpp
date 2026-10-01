#include "IPlugCLAP.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
using namespace iplug;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; std::abort(); } } while (0)
static const void* GetExtension(const clap_host*, const char*) { return nullptr; }
static void Request(const clap_host*) {}
static LayoutBus Bus(uint32_t id, BusRole role, uint32_t width, int bypass=-1)
{ LayoutBus b; b.id=id; b.role=role; b.layout=BusLayout::Discrete(width); b.bypassInputBus=bypass; return b; }
static Config MakeConfig(const BusLayoutPolicy* policy)
{ return Config(1,0,"8.2.1-8.2 2.8.1-2.8 4.4.1-4.4 8-8 4-4 9-9 16-16 25-25 36-36 49-49 64-64","probe","probe","test",0,1,2,0,false,false,false,false,0,false,0,0,false,0,0,0,0,"","",policy); }
static BusLayoutConfiguration Layout(uint32_t id, uint32_t main, uint32_t aux)
{
  BusLayoutConfiguration c; c.id=id; c.name="layout";
  c.buses[0]={Bus(10,BusRole::Main,main),Bus(11,BusRole::Auxiliary,aux),Bus(12,BusRole::Sidechain,1)};
  c.buses[1]={Bus(20,BusRole::Main,main,0),Bus(21,BusRole::Auxiliary,aux,1)}; return c;
}
class Probe: public IPlugCLAP
{
public:
  Probe(const InstanceInfo& info,const BusLayoutPolicy* p):IPlugCLAP(info,MakeConfig(p)) { GetParam(0)->InitInt("Token",2,0,1000); }
  void OnBusLayoutChanged() override { ++changes; }
  using IPlugProcessor::SetBypassed;
  void ProcessBlock(sample** input,sample** output,int frames) override {++blocks;IPlugProcessor::ProcessBlock(input,output,frames);}
  int blocks=0;
  int changes=0;
};
static void Data(clap_audio_buffer& b,float** p) { b.data32=p; }
static void Data(clap_audio_buffer& b,double** p) { b.data64=p; }
template<typename T> static void Routing(Probe& plug,const clap_plugin* plugin,bool bypass)
{
  constexpr int frames=16;
  const auto& c=*plug.GetActiveBusLayoutConfiguration();
  std::vector<std::vector<T>> input(17,std::vector<T>(frames)),output(16,std::vector<T>(frames,T(-99)));
  std::vector<T*> ins(17),outs(16);
  for (int ch=0;ch<17;++ch) { ins[ch]=input[ch].data();for(int f=0;f<frames;++f) input[ch][f]=T(1000+100*ch+f); }
  for (int ch=0;ch<16;++ch) outs[ch]=output[ch].data();
  std::vector<clap_audio_buffer> ib(c.buses[0].size()),ob(c.buses[1].size());
  int n=0;
  for (size_t b=0;b<ib.size();++b) { ib[b].channel_count=c.buses[0][b].layout.NChannels();Data(ib[b],ins.data()+n);n+=ib[b].channel_count; }
  n=0;for(size_t b=0;b<ob.size();++b) {ob[b].channel_count=c.buses[1][b].layout.NChannels();Data(ob[b],outs.data()+n);n+=ob[b].channel_count;ob[b].constant_mask=~uint64_t(0);}
  ib[0].constant_mask=1; // Host only guarantees the first sample of a constant channel.
  const unsigned main=ib[0].channel_count;
  if(ib.size()>1) ins[main+1]=nullptr;
  clap_process process{};process.frames_count=frames;process.audio_inputs=ib.data();process.audio_inputs_count=ib.size();process.audio_outputs=ob.data();process.audio_outputs_count=ob.size();
  plug.SetBypassed(bypass);
  CHECK(plugin->activate(plugin,48000.,0,frames));CHECK(plugin->start_processing(plugin));
  const int blocks=plug.blocks;
  CHECK(plugin->process(plugin,&process)!=CLAP_PROCESS_ERROR);
  CHECK(plug.blocks==blocks+(bypass ? 0 : 1));
  n=0;for(size_t b=0;b<ob.size();++b) {
    CHECK(ob[b].constant_mask==0);
    for(unsigned ch=0;ch<ob[b].channel_count;++ch,++n) for(int f=0;f<frames;++f) {
      T expected=n==0 ? input[0][0] : input[n][f];
      if(ib.size()>1 && n==int(main+1)) expected=0;
      CHECK(std::memcmp(&output[n][f],&expected,sizeof(T))==0);
    }
  }
  // Wrong topology must fail before processing its otherwise valid parameter event.
  clap_event_param_value event{};event.header.size=sizeof(event);event.header.space_id=CLAP_CORE_EVENT_SPACE_ID;event.header.type=CLAP_EVENT_PARAM_VALUE;event.param_id=0;event.value=73;
  clap_input_events events{};events.ctx=&event;events.size=[](const clap_input_events*)->uint32_t{return 1;};events.get=[](const clap_input_events* e,uint32_t)->const clap_event_header* {return &static_cast<const clap_event_param_value*>(e->ctx)->header;};
  process.in_events=&events;--ob[0].channel_count;ob[0].constant_mask=7;
  CHECK(plugin->process(plugin,&process)==CLAP_PROCESS_ERROR && plug.GetParam(0)->Value()==2);
  CHECK(ob[0].constant_mask==7);
  ++ob[0].channel_count;process.in_events=nullptr;
  process.frames_count=0;CHECK(plugin->process(plugin,&process)!=CLAP_PROCESS_ERROR);
  plugin->stop_processing(plugin);plugin->deactivate(plugin);plug.SetBypassed(false);
}
int main()
{
  BusLayoutPolicy policy;policy.capacities[0]={8,8,1};policy.capacities[1]={8,8};policy.defaultID=42;
  policy.configurations={Layout(42,8,2),Layout(77,2,8)};
  auto single=Layout(90,8,2);single.buses[0].resize(1);single.buses[1].resize(1);policy.configurations.push_back(single);
  policy.accept=[](const BusLayoutConfiguration& c) {return c.SameMeaning(Layout(0,4,4));};
  clap_plugin_descriptor descriptor{};descriptor.clap_version=CLAP_VERSION;descriptor.id="org.test.bus-layout";descriptor.name="probe";descriptor.vendor="test";
  clap_host host{};host.clap_version=CLAP_VERSION;host.name="test";host.version="1";host.get_extension=GetExtension;host.request_restart=Request;host.request_process=Request;host.request_callback=Request;
  Probe plug({&descriptor,&host},&policy);const clap_plugin* plugin=plug.clapPlugin();CHECK(plugin->init(plugin));
  const auto* ports=static_cast<const clap_plugin_audio_ports*>(plugin->get_extension(plugin,CLAP_EXT_AUDIO_PORTS));
  const auto* configs=static_cast<const clap_plugin_audio_ports_config*>(plugin->get_extension(plugin,CLAP_EXT_AUDIO_PORTS_CONFIG));
  const auto* info=static_cast<const clap_plugin_audio_ports_config_info*>(plugin->get_extension(plugin,CLAP_EXT_AUDIO_PORTS_CONFIG_INFO));
  const auto* configurable=static_cast<const clap_plugin_configurable_audio_ports*>(plugin->get_extension(plugin,CLAP_EXT_CONFIGURABLE_AUDIO_PORTS));CHECK(ports&&configs&&info&&configurable);
  CHECK(ports->count(plugin,true)==3&&ports->count(plugin,false)==2&&info->current_config(plugin)==42);
  const clap_audio_port_configuration_request duplicate[]={{true,0,8,nullptr,nullptr},{true,0,8,nullptr,nullptr}};
  CHECK(!configurable->can_apply_configuration(plugin,duplicate,2)&&info->current_config(plugin)==42);
  for(unsigned cycle=0;cycle<6;++cycle) {
    const uint32_t id=cycle%2 ? 77 : 42;CHECK(configs->select(plugin,id));const int changes=plug.changes;
    CHECK(configs->select(plugin,id)&&plug.changes==changes&&info->current_config(plugin)==id);
    Routing<float>(plug,plugin,cycle%2);Routing<double>(plug,plugin,cycle%2);
    CHECK(plugin->activate(plugin,48000.,0,16));CHECK(!configs->select(plugin,id));plugin->deactivate(plugin);
  }
  const auto prior=*plug.GetActiveBusLayoutConfiguration();
  const clap_audio_port_configuration_request bad[]={{true,0,4,nullptr,nullptr}};
  CHECK(!configurable->can_apply_configuration(plugin,bad,1));CHECK(!configurable->apply_configuration(plugin,bad,1));CHECK(*plug.GetActiveBusLayoutConfiguration()==prior);
  const clap_audio_port_configuration_request unlisted[]={{true,0,4,nullptr,nullptr},{true,1,4,nullptr,nullptr},{false,0,4,nullptr,nullptr},{false,1,4,nullptr,nullptr}};
  CHECK(configurable->can_apply_configuration(plugin,unlisted,4));CHECK(*plug.GetActiveBusLayoutConfiguration()==prior);
  CHECK(configurable->apply_configuration(plugin,unlisted,4)&&info->current_config(plugin)==CLAP_INVALID_ID);Routing<double>(plug,plugin,false);
  CHECK(configs->select(plugin,90)&&ports->count(plugin,true)==1&&ports->count(plugin,false)==1);Routing<float>(plug,plugin,false);
  CHECK(configs->select(plugin,42)&&ports->count(plugin,true)==3&&ports->count(plugin,false)==2);Routing<double>(plug,plugin,false);
  CHECK(!configs->select(plugin,999));
  BusLayoutPolicy named;named.capacities[0]={64};named.capacities[1]={64};named.defaultID=1;
  auto add=[&](uint32_t id,BusLayout layout) {
    BusLayoutConfiguration c;c.id=id;c.name="named";
    c.buses[0]={Bus(10,BusRole::Main,layout.NChannels())};c.buses[1]={Bus(20,BusRole::Main,layout.NChannels(),0)};
    c.buses[0][0].layout=c.buses[1][0].layout=layout;named.configurations.push_back(c);
  };
  add(1,BusLayout::Speakers({SpeakerPosition::L,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Rs}));
  add(2,BusLayout::Speakers({SpeakerPosition::L,SpeakerPosition::R,SpeakerPosition::C,SpeakerPosition::Cs}));
  for(unsigned order=1;order<=7;++order) add(10+order,BusLayout::Ambisonic(order,AmbisonicOrdering::ACN,AmbisonicNormalization::SN3D));
  add(30,BusLayout::Ambisonic(1,AmbisonicOrdering::FuMa,AmbisonicNormalization::MaxN));add(40,BusLayout::Discrete(16));
  Probe semantic({&descriptor,&host},&named);const auto* native=semantic.clapPlugin();CHECK(native->init(native));
  const auto* namedConfigs=static_cast<const clap_plugin_audio_ports_config*>(native->get_extension(native,CLAP_EXT_AUDIO_PORTS_CONFIG));
  const auto* surround=static_cast<const clap_plugin_surround*>(native->get_extension(native,CLAP_EXT_SURROUND));
  const auto* ambisonic=static_cast<const clap_plugin_ambisonic*>(native->get_extension(native,CLAP_EXT_AMBISONIC));
  CHECK(namedConfigs&&surround&&ambisonic);
  uint8_t map[64]{};CHECK(surround->get_channel_map(native,true,0,map,64)==4);
  CHECK(map[0]==CLAP_SURROUND_FL&&map[1]==CLAP_SURROUND_FR&&map[2]==CLAP_SURROUND_BL&&map[3]==CLAP_SURROUND_BR);
  CHECK(surround->get_channel_map(native,true,0,map,3)==0);
  CHECK(namedConfigs->select(native,2));CHECK(surround->get_channel_map(native,false,0,map,64)==4);
  CHECK(map[2]==CLAP_SURROUND_FC&&map[3]==CLAP_SURROUND_BC);
  clap_ambisonic_config convention{};
  for(unsigned order=1;order<=7;++order) {
    CHECK(namedConfigs->select(native,10+order));CHECK(ambisonic->get_config(native,true,0,&convention));
    CHECK(convention.ordering==CLAP_AMBISONIC_ORDERING_ACN&&convention.normalization==CLAP_AMBISONIC_NORMALIZATION_SN3D);
    CHECK(ambisonic->is_config_supported(native,&convention));CHECK(surround->get_channel_map(native,true,0,map,64)==0);
  }
  CHECK(namedConfigs->select(native,30)&&ambisonic->get_config(native,false,0,&convention));
  CHECK(convention.ordering==CLAP_AMBISONIC_ORDERING_FUMA&&convention.normalization==CLAP_AMBISONIC_NORMALIZATION_MAXN);
  CHECK(namedConfigs->select(native,40)&&!ambisonic->get_config(native,true,0,&convention));
  std::cout << "CLAP exact routing, conversion, constant/null input, bypass, topology cycles, stable/unlisted IDs and atomic rejection passed\n";
}
