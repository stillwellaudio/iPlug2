#include "IPlugAU.h"
#include "IPlugAU_BusLayout.h"
#include "RealtimeGuard.h"
#include "AUPartitionPolicy.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace iplug;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; std::abort(); } } while (0)
static const BusLayoutPolicy* sPolicy=nullptr;
static const char* sShape="4.4.1-4.4 8.2.1-8.2 2.8.1-2.8";
class Probe : public IPlugAU
{
public:
  Probe():IPlugAU(InstanceInfo{WDL_String()},Config(0,0,sShape,"probe","probe","test",0,1,2,0,false,false,false,false,0,false,0,0,false,0,0,0,0,"","",sPolicy)) {}
  int changes=0;
  void OnBusLayoutChanged() override {++changes;}
};
namespace iplug { IPlugAU* MakePlug(void* memory) {return new (memory) Probe();} }
static BusLayout NativeLayout(AudioChannelLayoutTag tag)
{ AudioChannelLayout layout{};layout.mChannelLayoutTag=tag;BusLayout result;CHECK(aulayouts::FromNative(&layout,aulayouts::kHeaderSize,128,result));return result; }
static BusLayoutConfiguration Configuration(uint32_t id,const BusLayout& a,const BusLayout& b)
{
  BusLayoutConfiguration c;c.id=id;
  for (int d=0;d<2;++d) for (int i=0;i<2;++i)
  {LayoutBus bus;bus.id=d*10+i;bus.role=i ? BusRole::Auxiliary : BusRole::Main;bus.layout=i ? b : a;bus.bypassInputBus=d ? i : -1;c.buses[d].push_back(bus);}
  LayoutBus sidechain;sidechain.id=100;sidechain.role=BusRole::Sidechain;sidechain.layout=BusLayout::Discrete(1);c.buses[0].push_back(sidechain);
  return c;
}
static void AdapterTest()
{
  BusLayoutPolicy policy;policy.defaultID=42;policy.capacities[0]={8,8,1};policy.capacities[1]={8,8};
  const auto quad=NativeLayout(kAudioChannelLayoutTag_Quadraphonic);
  const auto lcrs=NativeLayout(kAudioChannelLayoutTag_MPEG_4_0_A);
  policy.configurations={Configuration(42,quad,quad),Configuration(99,lcrs,lcrs),
    Configuration(101,BusLayout::Ambisonic(1,AmbisonicOrdering::ACN,AmbisonicNormalization::SN3D),BusLayout::Ambisonic(1,AmbisonicOrdering::ACN,AmbisonicNormalization::SN3D)),
    Configuration(144,BusLayout::Discrete(4),BusLayout::Discrete(4)),
    Configuration(201,BusLayout::Discrete(8),BusLayout::Discrete(2)),
    Configuration(202,BusLayout::Discrete(2),BusLayout::Discrete(8))};
  CHECK(policy.IsValid());sPolicy=&policy;
  auto* factory=IPlugAUFactory<Probe,false>::Factory(nullptr);CHECK(factory->Open(factory,nullptr)==noErr);
  auto* storage=reinterpret_cast<AudioComponentPlugInInstance*>(factory);
  auto* plug=reinterpret_cast<Probe*>(&storage->mInstanceStorage);
  const auto getInfo=reinterpret_cast<OSStatus(*)(void*,AudioUnitPropertyID,AudioUnitScope,AudioUnitElement,UInt32*,Boolean*)>(factory->Lookup(kAudioUnitGetPropertyInfoSelect));
  const auto get=reinterpret_cast<OSStatus(*)(void*,AudioUnitPropertyID,AudioUnitScope,AudioUnitElement,void*,UInt32*)>(factory->Lookup(kAudioUnitGetPropertySelect));
  const auto set=reinterpret_cast<OSStatus(*)(void*,AudioUnitPropertyID,AudioUnitScope,AudioUnitElement,const void*,UInt32)>(factory->Lookup(kAudioUnitSetPropertySelect));
  const auto initialize=reinterpret_cast<OSStatus(*)(void*)>(factory->Lookup(kAudioUnitInitializeSelect));
  const auto uninitialize=reinterpret_cast<OSStatus(*)(void*)>(factory->Lookup(kAudioUnitUninitializeSelect));
  const auto render=reinterpret_cast<OSStatus(*)(void*,AudioUnitRenderActionFlags*,const AudioTimeStamp*,UInt32,UInt32,AudioBufferList*)>(factory->Lookup(kAudioUnitRenderSelect));
  UInt32 size=0;Boolean writable=false;
  UInt32 capabilitySize=0;CHECK(getInfo(factory,kAudioUnitProperty_SupportedNumChannels,kAudioUnitScope_Global,0,&capabilitySize,&writable)==noErr);
  std::vector<AUChannelInfo> capabilities(capabilitySize/sizeof(AUChannelInfo));
  CHECK(get(factory,kAudioUnitProperty_SupportedNumChannels,kAudioUnitScope_Global,0,capabilities.data(),&capabilitySize)==noErr);
  CHECK(capabilities[0].inChannels==4 && capabilities[0].outChannels==4);

  CHECK(getInfo(factory,kAudioUnitProperty_AudioChannelLayout,kAudioUnitScope_Input,0,&size,&writable)==noErr && writable && size==aulayouts::kHeaderSize);
  std::vector<uint8_t> bytes(size,0xa5);UInt32 shortSize=size-1;
  CHECK(get(factory,kAudioUnitProperty_AudioChannelLayout,kAudioUnitScope_Input,0,bytes.data(),&shortSize)==kAudioUnitErr_InvalidPropertyValue && bytes[0]==0xa5);
  CHECK(initialize(factory)==noErr && plug->changes==1);
  CHECK(uninitialize(factory)==noErr);
  const auto setLayout=[&](int d,int b,const BusLayout& layout)
  {std::vector<uint8_t> native;CHECK(aulayouts::ToNative(layout,native));UInt32 count=native.size();return set(factory,kAudioUnitProperty_AudioChannelLayout,d ? kAudioUnitScope_Output : kAudioUnitScope_Input,b,native.data(),count);};
  const auto setWidth=[&](int d,int b,int width)
  {AudioStreamBasicDescription stream{};UInt32 count=sizeof(stream);CHECK(get(factory,kAudioUnitProperty_StreamFormat,d ? kAudioUnitScope_Output : kAudioUnitScope_Input,b,&stream,&count)==noErr);stream.mChannelsPerFrame=width;return set(factory,kAudioUnitProperty_StreamFormat,d ? kAudioUnitScope_Output : kAudioUnitScope_Input,b,&stream,count);};
  CHECK(setLayout(0,0,lcrs)==noErr);CHECK(initialize(factory)!=noErr);CHECK(plug->GetActiveBusLayoutConfiguration()->id==42);
  for (int d=0;d<2;++d) for (int b=0;b<2;++b) CHECK(setLayout(d,b,lcrs)==noErr);
  CHECK(initialize(factory)==noErr && plug->GetActiveBusLayoutConfiguration()->id==99);
  const int changes=plug->changes;CHECK(uninitialize(factory)==noErr);CHECK(initialize(factory)==noErr && plug->changes==changes);CHECK(uninitialize(factory)==noErr);
  for (int d=0;d<2;++d) {CHECK(setWidth(d,0,8)==noErr);CHECK(setLayout(d,0,BusLayout::Discrete(8))==noErr);CHECK(setWidth(d,1,2)==noErr);CHECK(setLayout(d,1,BusLayout::Discrete(2))==noErr);}
  CHECK(initialize(factory)==noErr && plug->GetActiveBusLayoutConfiguration()->id==201);CHECK(uninitialize(factory)==noErr);
  for (int d=0;d<2;++d) {CHECK(setWidth(d,0,2)==noErr);CHECK(setLayout(d,0,BusLayout::Discrete(2))==noErr);CHECK(setWidth(d,1,8)==noErr);CHECK(setLayout(d,1,BusLayout::Discrete(8))==noErr);}
  int inputScale=1;
  auto inputCallback=[](void* context,AudioUnitRenderActionFlags*,const AudioTimeStamp*,UInt32 bus,UInt32 frames,AudioBufferList* list)->OSStatus
  {const int scale=*static_cast<int*>(context);for (UInt32 c=0;c<list->mNumberBuffers;++c) {auto* data=static_cast<float*>(list->mBuffers[c].mData);for (UInt32 f=0;f<frames;++f)data[f]=scale*(1000*bus+100*c+f+1);}return noErr;};
  for (int b=0;b<3;++b) {AURenderCallbackStruct callback{inputCallback,&inputScale};UInt32 count=sizeof(callback);CHECK(set(factory,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,b,&callback,count)==noErr);}
  CHECK(initialize(factory)==noErr && plug->GetActiveBusLayoutConfiguration()->id==202);
  AURenderCallbackStruct activeCallback{inputCallback,&inputScale};UInt32 callbackSize=sizeof(activeCallback);
  // AU hosts (including auval) may replace input callbacks after initialization.
  CHECK(set(factory,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&activeCallback,callbackSize)==noErr);
  for (int cycle=0;cycle<3;++cycle)
  {
    AudioTimeStamp time{};time.mFlags=kAudioTimeStampSampleTimeValid;time.mSampleTime=cycle*16;
    // Reverse output request order must still run DSP once and return every channel.
    for (int b:{1,0})
    {
      struct List {UInt32 count;AudioBuffer buffers[8];} output{};
      output.count=b ? 8 : 2;float data[8][16]{};
      for (unsigned c=0;c<output.count;++c) {output.buffers[c].mNumberChannels=1;output.buffers[c].mDataByteSize=sizeof(data[c]);output.buffers[c].mData=data[c];}
      AudioUnitRenderActionFlags flags=0;
      CHECK(render(factory,&flags,&time,b,16,reinterpret_cast<AudioBufferList*>(&output))==noErr);
      for (unsigned c=0;c<output.count;++c) for (int f=0;f<16;++f) CHECK(data[c][f]==1000*b+100*c+f+1);
    }
  }
  UInt32 offline=1;inputScale=2;
  CHECK(set(factory,kAudioUnitProperty_OfflineRender,kAudioUnitScope_Global,0,&offline,sizeof(offline))==noErr);
  AudioTimeStamp reused{};reused.mFlags=kAudioTimeStampSampleTimeValid;reused.mSampleTime=32;
  struct RepeatList {UInt32 count;AudioBuffer buffers[2];} repeated{};repeated.count=2;float fresh[2][16]{};
  for(unsigned c=0;c<2;++c){repeated.buffers[c].mNumberChannels=1;repeated.buffers[c].mDataByteSize=sizeof(fresh[c]);repeated.buffers[c].mData=fresh[c];}
  AudioUnitRenderActionFlags reusedFlags=0;
  CHECK(render(factory,&reusedFlags,&reused,0,16,reinterpret_cast<AudioBufferList*>(&repeated))==noErr);
  CHECK(fresh[0][0]==2&&fresh[1][15]==232); // live-mode cache must not enter an offline cycle.
  CHECK(uninitialize(factory)==noErr);
  // A layout property can supply the width during sequential inactive setup.
  CHECK(setLayout(0,0,BusLayout::Discrete(8))==noErr);
  AudioStreamBasicDescription changed{};UInt32 changedSize=sizeof(changed);
  CHECK(get(factory,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&changed,&changedSize)==noErr&&changed.mChannelsPerFrame==8);
  CHECK(initialize(factory)!=noErr); // Other buses still describe the old complete shape.
  CHECK(setLayout(0,0,BusLayout::Discrete(2))==noErr);
  CHECK(set(factory,kAudioUnitProperty_AudioChannelLayout,kAudioUnitScope_Input,0,nullptr,0)==noErr);
  size=123;CHECK(getInfo(factory,kAudioUnitProperty_AudioChannelLayout,kAudioUnitScope_Input,0,&size,&writable)==noErr&&size==0&&writable);
  AudioChannelLayout unset{};size=sizeof(unset);
  CHECK(get(factory,kAudioUnitProperty_AudioChannelLayout,kAudioUnitScope_Input,0,&unset,&size)==kAudioUnitErr_PropertyNotInUse);
  CHECK(setLayout(0,0,BusLayout::Discrete(2))==noErr&&initialize(factory)==noErr);
  CHECK(uninitialize(factory)==noErr);CHECK(factory->Close(factory)==noErr);
  std::cout << "AU actual factory/property setup, atomic activation and multibus rendering passed\n";
}


// Exercise the actual factory/render path at the adapter's channel limit, not
// only mapping round trips. Samples identify every delivered channel separately.
static void WideAdapterTest(const std::vector<BusLayout>& layouts)
{
  BusLayoutPolicy policy;policy.defaultID=0;policy.capacities[0]={128};policy.capacities[1]={128};
  std::string shapes;
  for (size_t i=0;i<layouts.size();++i)
  {
    BusLayoutConfiguration configuration;configuration.id=static_cast<uint32_t>(i);
    for (int d=0;d<2;++d)
    {LayoutBus bus;bus.id=d;bus.role=BusRole::Main;bus.layout=layouts[i];bus.bypassInputBus=d ? 0 : -1;configuration.buses[d].push_back(bus);}
    policy.configurations.push_back(configuration);
    shapes+=std::to_string(layouts[i].NChannels())+"-"+std::to_string(layouts[i].NChannels())+" ";
  }
  sPolicy=&policy;sShape=shapes.c_str();
  auto* factory=IPlugAUFactory<Probe,false>::Factory(nullptr);CHECK(factory->Open(factory,nullptr)==noErr);
  auto* storage=reinterpret_cast<AudioComponentPlugInInstance*>(factory);
  auto* plug=reinterpret_cast<Probe*>(&storage->mInstanceStorage);
  const auto set=reinterpret_cast<OSStatus(*)(void*,AudioUnitPropertyID,AudioUnitScope,AudioUnitElement,const void*,UInt32)>(factory->Lookup(kAudioUnitSetPropertySelect));
  const auto initialize=reinterpret_cast<OSStatus(*)(void*)>(factory->Lookup(kAudioUnitInitializeSelect));
  const auto uninitialize=reinterpret_cast<OSStatus(*)(void*)>(factory->Lookup(kAudioUnitUninitializeSelect));
  const auto render=reinterpret_cast<OSStatus(*)(void*,AudioUnitRenderActionFlags*,const AudioTimeStamp*,UInt32,UInt32,AudioBufferList*)>(factory->Lookup(kAudioUnitRenderSelect));
  int calls=0;
  auto callback=[](void* context,AudioUnitRenderActionFlags*,const AudioTimeStamp*,UInt32,UInt32 frames,AudioBufferList* list)->OSStatus
  {++*static_cast<int*>(context);for (UInt32 c=0;c<list->mNumberBuffers;++c) for (UInt32 f=0;f<frames;++f)
    static_cast<float*>(list->mBuffers[c].mData)[f]=float(100*c+f+1);return noErr;};
  AURenderCallbackStruct input{callback,&calls};
  CHECK(set(factory,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&input,sizeof(input))==noErr);
  for (const auto& layout:layouts)
  {
    std::vector<uint8_t> native;CHECK(aulayouts::ToNative(layout,native));
    for (auto scope:{kAudioUnitScope_Input,kAudioUnitScope_Output})
      CHECK(set(factory,kAudioUnitProperty_AudioChannelLayout,scope,0,native.data(),native.size())==noErr);
    CHECK(initialize(factory)==noErr);
    CHECK(plug->GetActiveBusLayout(kInput,0)->layout==layout);
    CHECK(plug->GetActiveBusLayout(kOutput,0)->layout==layout);
    for (const auto& alternative:layouts)
      if (alternative.NChannels()==layout.NChannels() && !alternative.SameMeaning(layout))
      {
        const auto prior=*plug->GetActiveBusLayoutConfiguration();const int changes=plug->changes;
        std::vector<uint8_t> other;CHECK(aulayouts::ToNative(alternative,other));
        CHECK(set(factory,kAudioUnitProperty_AudioChannelLayout,kAudioUnitScope_Input,0,other.data(),other.size())==kAudioUnitErr_InvalidPropertyValue);
        CHECK(*plug->GetActiveBusLayoutConfiguration()==prior && plug->changes==changes);break;
      }
    for (const auto& alias:layouts)
      if (alias.SameMeaning(layout) && !(alias==layout))
      {
        std::vector<uint8_t> other;CHECK(aulayouts::ToNative(alias,other));const int changes=plug->changes;
        CHECK(set(factory,kAudioUnitProperty_AudioChannelLayout,kAudioUnitScope_Input,0,other.data(),other.size())==noErr);
        CHECK(plug->GetActiveBusLayout(kInput,0)->layout==alias && plug->changes==changes+1);
        CHECK(set(factory,kAudioUnitProperty_AudioChannelLayout,kAudioUnitScope_Input,0,other.data(),other.size())==noErr && plug->changes==changes+1);
        CHECK(set(factory,kAudioUnitProperty_AudioChannelLayout,kAudioUnitScope_Input,0,native.data(),native.size())==noErr);
        CHECK(plug->GetActiveBusLayout(kInput,0)->layout==layout);break;
      }
    struct List {UInt32 count;AudioBuffer buffers[128];} output{};
    float samples[128][16]{};output.count=layout.NChannels();
    for (UInt32 c=0;c<output.count;++c)
    {output.buffers[c].mNumberChannels=1;output.buffers[c].mDataByteSize=sizeof(samples[c]);output.buffers[c].mData=samples[c];}
    AudioTimeStamp time{};time.mFlags=kAudioTimeStampSampleTimeValid;time.mSampleTime=calls*16;
    AudioUnitRenderActionFlags flags=0;const int before=calls;
    {realtimecheck::Scope guard;CHECK(render(factory,&flags,&time,0,16,reinterpret_cast<AudioBufferList*>(&output))==noErr && calls==before+1);}
    for (UInt32 c=0;c<output.count;++c) for (int f=0;f<16;++f) CHECK(samples[c][f]==float(100*c+f+1));
    {realtimecheck::Scope guard;CHECK(render(factory,&flags,&time,0,16,reinterpret_cast<AudioBufferList*>(&output))==noErr && calls==before+1);}
    // Reject a malformed late channel before pulling inputs or running DSP.
    output.buffers[output.count-1].mDataByteSize=sizeof(float);
    time.mSampleTime+=16;
    CHECK(render(factory,&flags,&time,0,16,reinterpret_cast<AudioBufferList*>(&output))!=noErr && calls==before+1);
    CHECK(uninitialize(factory)==noErr);
  }
  CHECK(factory->Close(factory)==noErr);sPolicy=nullptr;
  std::cout << "AU distinct-channel rendering passed for " << layouts.size() << " named/discrete/HOA layouts through 128 channels\n";
}

static void StrictWidthAdvertisementTest()
{
  BusLayoutPolicy policy;policy.defaultID=2;policy.capacities[0]={2};policy.capacities[1]={2};
  for (auto tag:{kAudioChannelLayoutTag_Mono,kAudioChannelLayoutTag_Stereo})
  {BusLayoutConfiguration c;auto layout=NativeLayout(tag);c.id=layout.NChannels();for(int d=0;d<2;++d){LayoutBus bus;bus.id=d;bus.role=BusRole::Main;bus.layout=layout;bus.bypassInputBus=d ? 0 : -1;c.buses[d].push_back(bus);}policy.configurations.push_back(c);}
  sPolicy=&policy;sShape="1-1 2-2";
  auto* factory=IPlugAUFactory<Probe,false>::Factory(nullptr);CHECK(factory->Open(factory,nullptr)==noErr);
  const auto get=reinterpret_cast<OSStatus(*)(void*,AudioUnitPropertyID,AudioUnitScope,AudioUnitElement,void*,UInt32*)>(factory->Lookup(kAudioUnitGetPropertySelect));
  const auto set=reinterpret_cast<OSStatus(*)(void*,AudioUnitPropertyID,AudioUnitScope,AudioUnitElement,const void*,UInt32)>(factory->Lookup(kAudioUnitSetPropertySelect));
  AUChannelInfo capabilities[2]{};UInt32 size=sizeof(capabilities);
  CHECK(get(factory,kAudioUnitProperty_SupportedNumChannels,kAudioUnitScope_Global,0,capabilities,&size)==noErr);
  CHECK(size==sizeof(AUChannelInfo) && capabilities[0].inChannels==2 && capabilities[0].outChannels==2);
  // Named mono requires an explicit layout; a width-only change stays anonymous.
  for(auto scope:{kAudioUnitScope_Input,kAudioUnitScope_Output})
  {AudioStreamBasicDescription stream{};size=sizeof(stream);CHECK(get(factory,kAudioUnitProperty_StreamFormat,scope,0,&stream,&size)==noErr);stream.mChannelsPerFrame=1;CHECK(set(factory,kAudioUnitProperty_StreamFormat,scope,0,&stream,sizeof(stream))==noErr);}
  const auto initialize=reinterpret_cast<OSStatus(*)(void*)>(factory->Lookup(kAudioUnitInitializeSelect));CHECK(initialize(factory)!=noErr);
  AudioChannelLayout mono{};mono.mChannelLayoutTag=kAudioChannelLayoutTag_Mono;
  for(auto scope:{kAudioUnitScope_Input,kAudioUnitScope_Output}) CHECK(set(factory,kAudioUnitProperty_AudioChannelLayout,scope,0,&mono,aulayouts::kHeaderSize)==noErr);
  CHECK(initialize(factory)==noErr);
  CHECK(factory->Close(factory)==noErr);
  // A common-valid input-only declaration is not an AU output-producing unit.
  policy.configurations.resize(1);policy.configurations[0].id=1;policy.defaultID=1;policy.capacities[1].clear();policy.configurations[0].buses[1].clear();sShape="1-0";
  CHECK(policy.IsValid());factory=IPlugAUFactory<Probe,false>::Factory(nullptr);CHECK(factory->Open(factory,nullptr)==noErr);
  const auto invalidGet=reinterpret_cast<OSStatus(*)(void*,AudioUnitPropertyID,AudioUnitScope,AudioUnitElement,void*,UInt32*)>(factory->Lookup(kAudioUnitGetPropertySelect));
  size=sizeof(capabilities);CHECK(invalidGet(factory,kAudioUnitProperty_SupportedNumChannels,kAudioUnitScope_Global,0,capabilities,&size)==kAudioUnitErr_InvalidPropertyValue);
  CHECK(factory->Close(factory)==noErr);sPolicy=nullptr;
}

static void PartitionAdvertisementTest()
{
  sPolicy=&AUPartitionPolicy();sShape=kAUPartitionShape;
  auto* factory=IPlugAUFactory<Probe,false>::Factory(nullptr);CHECK(factory->Open(factory,nullptr)==noErr);
  const auto getInfo=reinterpret_cast<OSStatus(*)(void*,AudioUnitPropertyID,AudioUnitScope,AudioUnitElement,UInt32*,Boolean*)>(factory->Lookup(kAudioUnitGetPropertyInfoSelect));
  const auto get=reinterpret_cast<OSStatus(*)(void*,AudioUnitPropertyID,AudioUnitScope,AudioUnitElement,void*,UInt32*)>(factory->Lookup(kAudioUnitGetPropertySelect));
  const auto set=reinterpret_cast<OSStatus(*)(void*,AudioUnitPropertyID,AudioUnitScope,AudioUnitElement,const void*,UInt32)>(factory->Lookup(kAudioUnitSetPropertySelect));
  const auto initialize=reinterpret_cast<OSStatus(*)(void*)>(factory->Lookup(kAudioUnitInitializeSelect));
  const auto uninitialize=reinterpret_cast<OSStatus(*)(void*)>(factory->Lookup(kAudioUnitUninitializeSelect));
  const auto widths=[&]() {
    UInt32 size=0;Boolean writable=false;CHECK(getInfo(factory,kAudioUnitProperty_SupportedNumChannels,kAudioUnitScope_Global,0,&size,&writable)==noErr);
    std::vector<AUChannelInfo> result(size/sizeof(AUChannelInfo));CHECK(get(factory,kAudioUnitProperty_SupportedNumChannels,kAudioUnitScope_Global,0,result.data(),&size)==noErr);return result;
  };
  auto initial=widths();CHECK(initial.size()==2 && initial[0].inChannels==1 && initial[0].outChannels==1 && initial[1].inChannels==2 && initial[1].outChannels==2);
  // auval changes the main pair while preserving the default auxiliary width.
  for (const auto& pair:initial)
  {
    for (int d=0;d<2;++d)
    {AudioStreamBasicDescription stream{};UInt32 size=sizeof(stream);auto scope=d ? kAudioUnitScope_Output : kAudioUnitScope_Input;
      CHECK(get(factory,kAudioUnitProperty_StreamFormat,scope,0,&stream,&size)==noErr);stream.mChannelsPerFrame=d ? pair.outChannels : pair.inChannels;
      CHECK(set(factory,kAudioUnitProperty_StreamFormat,scope,0,&stream,sizeof(stream))==noErr);}
    CHECK(initialize(factory)==noErr);CHECK(uninitialize(factory)==noErr);
  }
  struct Notifications {int widths=0,inputTags=0,outputTags=0;} notifications;
  const auto listener=[](void* context,AudioUnit,AudioUnitPropertyID prop,AudioUnitScope scope,AudioUnitElement) {
    auto& n=*static_cast<Notifications*>(context);
    if(prop==kAudioUnitProperty_SupportedNumChannels) {CHECK(scope==kAudioUnitScope_Global);++n.widths;}
    else if(scope==kAudioUnitScope_Input) ++n.inputTags;
    else {CHECK(scope==kAudioUnitScope_Output);++n.outputTags;}
  };
  const auto addListener=reinterpret_cast<OSStatus(*)(void*,AudioUnitPropertyID,AudioUnitPropertyListenerProc,void*)>(factory->Lookup(kAudioUnitAddPropertyListenerSelect));
  for(auto prop:{kAudioUnitProperty_SupportedNumChannels,kAudioUnitProperty_SupportedChannelLayoutTags}) CHECK(addListener(factory,prop,listener,&notifications)==noErr);
  AudioStreamBasicDescription auxiliary{};UInt32 auxiliarySize=sizeof(auxiliary);
  CHECK(get(factory,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,1,&auxiliary,&auxiliarySize)==noErr);
  auxiliary.mChannelsPerFrame=1;CHECK(set(factory,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,1,&auxiliary,sizeof(auxiliary))==noErr);
  auto narrower=widths();CHECK(narrower.size()==3 && narrower.back().inChannels==3 && narrower.back().outChannels==3);
  CHECK(notifications.widths==1 && notifications.inputTags==1 && notifications.outputTags==1);
  UInt32 count=1;CHECK(set(factory,kAudioUnitProperty_ElementCount,kAudioUnitScope_Input,0,&count,sizeof(count))==noErr);
  CHECK(notifications.widths==2 && notifications.inputTags==2 && notifications.outputTags==2);
  CHECK(set(factory,kAudioUnitProperty_ElementCount,kAudioUnitScope_Input,0,&count,sizeof(count))==noErr);
  CHECK(notifications.widths==2 && notifications.inputTags==2 && notifications.outputTags==2);
  auto single=widths();CHECK(single.size()==4);
  for (const auto& pair:single) CHECK(pair.inChannels==pair.outChannels && pair.inChannels>=1 && pair.inChannels<=4);
  UInt32 size=0;Boolean writable=false;CHECK(getInfo(factory,kAudioUnitProperty_SupportedChannelLayoutTags,kAudioUnitScope_Input,0,&size,&writable)==noErr);
  std::vector<AudioChannelLayoutTag> tags(size/sizeof(AudioChannelLayoutTag));CHECK(get(factory,kAudioUnitProperty_SupportedChannelLayoutTags,kAudioUnitScope_Input,0,tags.data(),&size)==noErr);
  CHECK(std::find(tags.begin(),tags.end(),kAudioChannelLayoutTag_Unknown | 4)!=tags.end());
  CHECK(factory->Close(factory)==noErr);sPolicy=nullptr;
  std::cout<<"AU main-pair capability metadata matches fixed auxiliary partitions and grows after topology selection\n";
}

int main()
{
  const AudioChannelLayoutTag namedTags[]={
#include "AUNamed.inc"
  };
  std::vector<BusLayout> renderLayouts;
  for (auto tag:namedTags)
  {
    if (!(tag & 0xffff)) continue;
    AudioChannelLayout native{}; native.mChannelLayoutTag=tag;
    BusLayout decoded; CHECK(aulayouts::FromNative(&native,aulayouts::kHeaderSize,128,decoded));
    std::vector<uint8_t> encoded; CHECK(aulayouts::ToNative(decoded,encoded));
    BusLayout roundTrip; CHECK(aulayouts::FromNative(encoded.data(),encoded.size(),128,roundTrip));
    CHECK(decoded==roundTrip && decoded.NChannels()==(tag & 0xffff));
    renderLayouts.push_back(decoded);
  }
  AudioChannelLayout bitmap{};bitmap.mChannelLayoutTag=kAudioChannelLayoutTag_UseChannelBitmap;
  bitmap.mChannelBitmap=kAudioChannelBit_Left | kAudioChannelBit_Right | kAudioChannelBit_TopCenterSurround;
  BusLayout bits; CHECK(aulayouts::FromNative(&bitmap,aulayouts::kHeaderSize,128,bits));
  CHECK(bits.NChannels()==3 && bits.channels[2].speaker==SpeakerPosition::Tc);
  for (unsigned order=0;order<=10;++order) for (auto normalization:{AmbisonicNormalization::SN3D,AmbisonicNormalization::N3D})
  {
    const auto layout=BusLayout::Ambisonic(order,AmbisonicOrdering::ACN,normalization);
    std::vector<uint8_t> bytes;CHECK(aulayouts::ToNative(layout,bytes));
    AudioChannelLayoutTag tag;std::memcpy(&tag,bytes.data(),sizeof(tag));CHECK((tag & 0xffff)==layout.NChannels());
    BusLayout result;CHECK(aulayouts::FromNative(bytes.data(),bytes.size(),128,result));CHECK(layout.SameMeaning(result));
    CHECK(!aulayouts::FromNative(bytes.data(),2,128,result));
    renderLayouts.push_back(result);
  }
  struct Descriptions {AudioChannelLayoutTag tag;AudioChannelBitmap bitmap;UInt32 count;AudioChannelDescription channels[2];} desc{};
  desc.tag=kAudioChannelLayoutTag_UseChannelDescriptions;desc.count=2;
  for(int i=0;i<2;++i) {desc.channels[i].mChannelLabel=kAudioChannelLabel_UseCoordinates;desc.channels[i].mChannelFlags=kAudioChannelFlags_RectangularCoordinates;desc.channels[i].mCoordinates[0]=float(i+1);}
  BusLayout coordinates;CHECK(aulayouts::FromNative(&desc,sizeof(desc),128,coordinates));
  std::vector<uint8_t> coordinatesNative;CHECK(aulayouts::ToNative(coordinates,coordinatesNative));
  BusLayout coordinatesBack;CHECK(aulayouts::FromNative(coordinatesNative.data(),coordinatesNative.size(),128,coordinatesBack));CHECK(coordinates==coordinatesBack);
  CHECK(!aulayouts::FromNative(&desc,sizeof(desc)-1,128,coordinatesBack));
  desc.count=UINT32_MAX;CHECK(!aulayouts::FromNative(&desc,sizeof(desc),128,coordinatesBack));desc.count=2;
  desc.channels[0].mCoordinates[0]=std::numeric_limits<float>::infinity();CHECK(!aulayouts::FromNative(&desc,sizeof(desc),128,coordinatesBack));
  auto layout=BusLayout::Ambisonic(1,AmbisonicOrdering::FuMa,AmbisonicNormalization::MaxN);
  std::vector<uint8_t> bytes;CHECK(aulayouts::ToNative(layout,bytes));
  BusLayout result;CHECK(aulayouts::FromNative(bytes.data(),bytes.size(),128,result));CHECK(result.SameMeaning(layout));
  auto speaker=BusLayout::Speakers({SpeakerPosition::L,SpeakerPosition::R,SpeakerPosition::LFE2,SpeakerPosition::Tsl});
  CHECK(aulayouts::ToNative(speaker,bytes));CHECK(aulayouts::FromNative(bytes.data(),bytes.size(),128,result));CHECK(result.SameMeaning(speaker));
  CHECK(aulayouts::ToNative(BusLayout::Discrete(128),bytes));CHECK(aulayouts::FromNative(bytes.data(),bytes.size(),128,result));
  CHECK(result.NChannels()==128 && result.kind==LayoutKind::Discrete);
  // Independent native-order expectations from the CoreAudioTypes SDK tag contract.
  CHECK(NativeLayout(kAudioChannelLayoutTag_MPEG_5_1_A).SameMeaning(BusLayout::Speakers({SpeakerPosition::L,SpeakerPosition::R,SpeakerPosition::C,SpeakerPosition::LFE,SpeakerPosition::Ls,SpeakerPosition::Rs})));
  CHECK(NativeLayout(kAudioChannelLayoutTag_MPEG_5_1_B).SameMeaning(BusLayout::Speakers({SpeakerPosition::L,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::C,SpeakerPosition::LFE})));
  CHECK(NativeLayout(kAudioChannelLayoutTag_MPEG_5_1_C).SameMeaning(BusLayout::Speakers({SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::LFE})));
  CHECK(NativeLayout(kAudioChannelLayoutTag_MPEG_5_1_D).SameMeaning(BusLayout::Speakers({SpeakerPosition::C,SpeakerPosition::L,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Rs,SpeakerPosition::LFE})));
  AdapterTest();
  renderLayouts.push_back(NativeLayout(kAudioChannelLayoutTag_DiscreteInOrder | 128));
  renderLayouts.push_back(NativeLayout(kAudioChannelLayoutTag_Unknown | 128));
  WideAdapterTest(renderLayouts);
  PartitionAdvertisementTest();
  StrictWidthAdvertisementTest();
  std::cout << "AU mapping passed\n";
}
