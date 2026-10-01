// Standalone headless host probe. No Bitter product IDs, processing or UI.
#if defined(VST3_API)
#include "IPlugVST3.h"
#include "public.sdk/source/main/pluginfactory.h"
using Adapter=iplug::IPlugVST3;
#elif defined(CLAP_API)
#include "IPlugCLAP.h"
using Adapter=iplug::IPlugCLAP;
#elif defined(AU_API)
#include "IPlugAU.h"
using Adapter=iplug::IPlugAU;
#endif
using namespace iplug;
#if defined(AU_API) && defined(BUS_LAYOUT_AU_PARTITION_PROBE)
#include "../AUPartitionPolicy.h"
#endif
static const BusLayoutPolicy& Policy()
{
#if defined(AU_API) && defined(BUS_LAYOUT_AU_PARTITION_PROBE)
  return AUPartitionPolicy();
#else
  static const BusLayoutPolicy policy=[] {
    BusLayoutPolicy p;p.defaultID=2;
#if defined(AU_API)
    p.capacities[0]={64};p.capacities[1]={64};
#else
    p.capacities[0]={64,64,1};p.capacities[1]={64,64};
#endif
    auto add=[&](uint32_t id,const char* name,BusLayout layout) {
      BusLayoutConfiguration c;c.id=id;c.name=name;
#if defined(AU_API)
      const int busCount=1; // auval's format matrix addresses the main bus pair.
#else
      const int busCount=2;
#endif
      for (int d=0;d<2;++d) for (int b=0;b<busCount;++b)
      {LayoutBus bus;bus.id=100*d+b;bus.role=b ? BusRole::Auxiliary : BusRole::Main;
       bus.layout=layout;bus.name=std::string(name)+(b ? " Aux" : " Main");bus.bypassInputBus=d ? b : -1;c.buses[d].push_back(bus);}
#if !defined(AU_API)
      LayoutBus sc;sc.id=10;sc.role=BusRole::Sidechain;sc.layout=BusLayout::Speakers({SpeakerPosition::Mono});sc.name="Sidechain";c.buses[0].push_back(sc);
#endif
      p.configurations.push_back(c);
    };
    add(2,"Stereo",BusLayout::Speakers({SpeakerPosition::L,SpeakerPosition::R}));
    add(4,"Quad",BusLayout::Speakers({SpeakerPosition::L,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Rs}));
    add(5,"LCRS",BusLayout::Speakers({SpeakerPosition::L,SpeakerPosition::R,SpeakerPosition::C,SpeakerPosition::Cs}));
    for (unsigned order=1;order<=7;++order) add(100+order,("HOA "+std::to_string(order)).c_str(),BusLayout::Ambisonic(order,AmbisonicOrdering::ACN,AmbisonicNormalization::SN3D));
#if !defined(VST3_API)
    add(200,"Discrete 16",BusLayout::Discrete(16));
#endif
#if defined(AU_API)
    // A host setting only an ASBD supplies anonymous channels. This transparent
    // consumer explicitly supports matched anonymous pairs; it never calls them
    // speakers/ambisonics. Named and anonymous states remain distinct.
    for (unsigned width:{2,4,9,16,25,36,49,64})
      add(300+width,("Unspecified "+std::to_string(width)).c_str(),BusLayout::Discrete(width,true));
    // AU hosts may restore a layout on only one scope. Explicitly permit an
    // anonymous endpoint alongside a named endpoint, while retaining that
    // missing identity in the active descriptor. Two known endpoints must
    // still have matching meaning; this does not authorize conversion.
    const auto paired=p.configurations;
    for (const auto& pair:paired)
    {
      if (pair.buses[0][0].layout.kind==LayoutKind::Unspecified) continue;
      for (int d=0;d<2;++d)
      {
        auto c=pair;c.id=1000+pair.id*2+d;
        c.name+=" / anonymous ";c.name+=d ? "output" : "input";
        c.buses[d][0].layout=BusLayout::Discrete(c.buses[d][0].layout.NChannels(),true);
        p.configurations.push_back(std::move(c));
      }
    }
#endif
    return p;
  }();return policy;
#endif
}
static Config ProbeConfig()
{
#if defined(AU_API) && defined(BUS_LAYOUT_AU_PARTITION_PROBE)
  const char* shape=kAUPartitionShape;
#elif defined(AU_API)
  const char* shape="2-2 4-4 9-9 16-16 25-25 36-36 49-49 64-64";
#else
  const char* shape="2.2.1-2.2 4.4.1-4.4 9.9.1-9.9 16.16.1-16.16 25.25.1-25.25 36.36.1-36.36 49.49.1-49.49 64.64.1-64.64";
#endif
  return Config(1,1,shape,
    "Bus Layout Probe","Bus Layout Probe","iPlug2 tests",0x10000,0x42735072,0x49503254,0,false,false,false,false,0,false,0,0,false,0,0,0,0,"","",&Policy());
}
class BusLayoutProbe final:public Adapter
{
public:
  explicit BusLayoutProbe(const InstanceInfo& info):Adapter(info,ProbeConfig())
  {
#if defined(CLAP_API)
    mProbeHost=info.mHost;
#endif
    GetParam(0)->InitInt("Probe state token",2,0,1000);MakeDefaultPreset("Default",1);
  }
  // A copied diagnostic snapshot is published only on the negotiation thread.
  // Native bus/property interfaces expose it to the host. It is never preset state.
  BusLayoutConfiguration snapshot;
#if defined(CLAP_API)
  const clap_host* mProbeHost=nullptr;
#endif
  void OnBusLayoutChanged() override
  {if (const auto* c=GetActiveBusLayoutConfiguration()) snapshot=*c;}
  void OnRestoreState() override
  {
    Adapter::OnRestoreState();
#if defined(CLAP_API)
    // State load changes parameter values outside flush/process; tell the host
    // to refresh its cached values as required by the CLAP state contract.
    const auto* params=static_cast<const clap_host_params*>(mProbeHost->get_extension(mProbeHost,CLAP_EXT_PARAMS));
    if (params) params->rescan(mProbeHost,CLAP_PARAM_RESCAN_VALUES);
#endif
  }
  bool SerializeState(IByteChunk& chunk) const override
  {const uint32_t magic=0x42535052;chunk.Put(&magic);return SerializeParams(chunk);}
  int UnserializeState(const IByteChunk& chunk,int pos) override
  {
    uint32_t magic=0;pos=chunk.Get(&magic,pos);
#if defined(VST3_API)
    // The VST3 wrapper appends its bypass flag after plug-in-owned state.
    const int trailer=sizeof(Steinberg::int32);
#else
    const int trailer=0;
#endif
    return pos>=0 && magic==0x42535052 && chunk.Size()==pos+sizeof(double)+trailer ? UnserializeParams(chunk,pos) : -1;
  }
};
#if defined(VST3_API)
using namespace Steinberg;using namespace Steinberg::Vst;
bool InitModule(){return true;}bool DeinitModule(){return true;}
static FUnknown* Create(void*){return static_cast<IAudioProcessor*>(new BusLayoutProbe(InstanceInfo{}));}
BEGIN_FACTORY_DEF("iPlug2 tests","","test@example.invalid")
DEF_CLASS2(INLINE_UID(0x416CBF65,0xBF274A91,0xA952C5D2,0x7F5F3DA7),PClassInfo::kManyInstances,kVstAudioEffectClass,"Bus Layout Probe",kSimpleModeSupported,"Fx|Tools","0.1.0",kVstVersionString,Create)
END_FACTORY
#elif defined(AU_API)
namespace iplug { IPlugAU* MakePlug(void* memory){return new(memory) BusLayoutProbe(InstanceInfo{WDL_String()});} }
extern "C" __attribute__((visibility("default"))) void* BusLayoutProbeFactory(const AudioComponentDescription* desc)
{return IPlugAUFactory<BusLayoutProbe,false>::Factory(desc);}
#elif defined(CLAP_API)
static const char* features[]={CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,CLAP_PLUGIN_FEATURE_UTILITY,nullptr};
static const clap_plugin_descriptor descriptor={CLAP_VERSION,"org.iplug2.tests.bus-layout","Bus Layout Probe","iPlug2 tests","","","","0.1.0","Opt-in layout and transparent-routing probe",features};
static uint32_t Count(const clap_plugin_factory*){return 1;}
static const clap_plugin_descriptor* Descriptor(const clap_plugin_factory*,uint32_t index){return index ? nullptr : &descriptor;}
static const clap_plugin* Create(const clap_plugin_factory*,const clap_host* host,const char* id)
{return !std::strcmp(id,descriptor.id) ? (new BusLayoutProbe(InstanceInfo{&descriptor,host}))->clapPlugin() : nullptr;}
static const clap_plugin_factory factory={Count,Descriptor,Create};
static bool Init(const char*){return true;}static void Deinit(){}
static const void* Factory(const char* id){return !std::strcmp(id,CLAP_PLUGIN_FACTORY_ID) ? &factory : nullptr;}
extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry={CLAP_VERSION,Init,Deinit,Factory};
#endif
