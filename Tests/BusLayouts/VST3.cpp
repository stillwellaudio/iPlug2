#include "IPlugVST3.h"
#include "RealtimeGuard.h"
#include <cstdlib>
#include <iostream>
#include <cstring>
using namespace iplug;
using namespace Steinberg;
using namespace Steinberg::Vst;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; std::abort(); } } while (0)
static BusLayoutConfiguration StereoConfig(uint32_t id, SpeakerArrangement arrangement)
{
  BusLayout layout; CHECK(vst3layouts::FromArrangement(arrangement,layout));
  BusLayoutConfiguration c; c.id=id;
  for (int d=0;d<2;++d) for (int b=0;b<3;++b)
  {LayoutBus bus; bus.id=uint32_t(d*10+b); bus.role=b ? BusRole::Auxiliary : BusRole::Main;
   bus.layout=layout; bus.name="Bus "+std::to_string(b); bus.bypassInputBus=d ? b : -1; c.buses[d].push_back(bus);}
  return c;
}
class Probe : public IPlugVST3
{
public:
  Probe(const BusLayoutPolicy* policy):IPlugVST3(InstanceInfo{},Config(0,0,"4.4.4-4.4.4 9.9.9-9.9.9 16.16.16-16.16.16 25.25.25-25.25.25 36.36.36-36.36.36 49.49.49-49.49.49 64.64.64-64.64.64", "Layout Probe","Layout Probe","Test",0,1,2,0,false,false,false,false,0,false,0,0,false,0,0,0,0,"", "",policy)) {}
  void OnBusLayoutChanged() override { ++changes; }
  int changes=0;
};
int main()
{
  // Every named arrangement defined by this pinned header is exercised separately
  // by the generated table in VST3Named.inc, including same-width alternatives.
  const SpeakerArrangement arrangements[]={
#include "VST3Named.inc"
  };
  for (auto native:arrangements)
  {
    if (!native) continue;
    BusLayout layout; CHECK(vst3layouts::FromArrangement(native,layout));
    SpeakerArrangement output; CHECK(vst3layouts::ToArrangement(layout,output)); CHECK(native==output);
  }
  SpeakerArrangement custom=kSpeakerL | kSpeakerR | kSpeakerTc | kSpeakerLfe2;
  BusLayout mapped; CHECK(vst3layouts::FromArrangement(custom,mapped));
  SpeakerArrangement back; CHECK(vst3layouts::ToArrangement(mapped,back) && back==custom);
  CHECK(!vst3layouts::ToArrangement(BusLayout::Discrete(4),back));
  BusLayoutPolicy policy; policy.capacities[0]={64,64,64}; policy.capacities[1]={64,64,64};
  policy.defaultID=17; policy.configurations={StereoConfig(17,SpeakerArr::k40Music),StereoConfig(99,SpeakerArr::k40Cine)};
  for (unsigned order=1;order<=7;++order) policy.configurations.push_back(StereoConfig(200+order,vst3layouts::AmbisonicArrangement(order)));
  Probe probe(&policy);
  const auto process=[&](ProcessData& data) {realtimecheck::Scope guard;return probe.process(data);};
  CHECK(probe.IPlugVST3ProcessorBase::Initialize(&probe)); // Exact same bus initialization used by adapter.
  CHECK(probe.changes==1);
  for (int d=0;d<2;++d) for (int b=0;b<3;++b) CHECK(probe.activateBus(kAudio,d,b,true)==kResultOk);
  SpeakerArrangement inputs[3],outputs[3];
  for (const auto& config:policy.configurations)
  {
    for (int b=0;b<3;++b) {CHECK(vst3layouts::ToArrangement(config.buses[0][b].layout,inputs[b])); outputs[b]=inputs[b];}
    CHECK(probe.setBusArrangements(inputs,3,outputs,3)==kResultTrue);
    const int changes=probe.changes;
    CHECK(probe.setBusArrangements(inputs,3,outputs,3)==kResultTrue && probe.changes==changes);
    for (int b=0;b<3;++b)
    { SpeakerArrangement actual; BusInfo info;
      CHECK(probe.getBusArrangement(Steinberg::Vst::kOutput,b,actual)==kResultOk && actual==outputs[b]);
      CHECK(probe.getBusInfo(kAudio,Steinberg::Vst::kOutput,b,info)==kResultOk && info.channelCount==SpeakerArr::getChannelCount(actual)); }
    const int width=SpeakerArr::getChannelCount(inputs[0]);
    std::vector<std::vector<double>> in(width*3,std::vector<double>(16)),out(width*3,std::vector<double>(16));
    std::vector<double*> inp(width*3),outp(width*3);
    for (int c=0;c<width*3;++c) {inp[c]=in[c].data();outp[c]=out[c].data(); for (int f=0;f<16;++f) in[c][f]=100*c+f+1;}
    AudioBusBuffers inBuses[3]{},outBuses[3]{};
    for (int b=0;b<3;++b) {inBuses[b].numChannels=outBuses[b].numChannels=width;inBuses[b].channelBuffers64=inp.data()+b*width;outBuses[b].channelBuffers64=outp.data()+b*width;}
    ProcessSetup setup{}; setup.symbolicSampleSize=kSample64;setup.sampleRate=48000;setup.maxSamplesPerBlock=32;
    CHECK(probe.setupProcessing(setup)==kResultOk);
    ProcessData data{}; data.symbolicSampleSize=kSample64;data.numSamples=16;data.numInputs=data.numOutputs=3;data.inputs=inBuses;data.outputs=outBuses;
    CHECK(process(data)==kResultOk);
    for (int c=0;c<width*3;++c) CHECK(!std::memcmp(in[c].data(),out[c].data(),16*sizeof(double)));
    data.numInputs=1;CHECK(process(data)==kInvalidArgument);data.numInputs=3;
    data.numOutputs=1;CHECK(process(data)==kInvalidArgument);data.numOutputs=3;
    auto* savedOutput=outBuses[2].channelBuffers64;
    outBuses[2].channelBuffers64=nullptr;CHECK(process(data)==kInvalidArgument);
    outBuses[2].channelBuffers64=savedOutput;
    auto* savedChannel=savedOutput[0];savedOutput[0]=nullptr;CHECK(process(data)==kInvalidArgument);savedOutput[0]=savedChannel;
    outBuses[2].numChannels=width-1;CHECK(process(data)==kInvalidArgument);outBuses[2].numChannels=width;
    data.numSamples=0;CHECK(process(data)==kResultOk);
    std::vector<std::vector<float>> fin(width*3,std::vector<float>(16)),fout(width*3,std::vector<float>(16,-1));
    std::vector<float*> fp(width*3),fo(width*3);
    for (int c=0;c<width*3;++c) {fp[c]=fin[c].data();fo[c]=fout[c].data();for (int f=0;f<16;++f) fin[c][f]=float(c*100+f+1);}
    for (int b=0;b<3;++b) {inBuses[b].channelBuffers32=fp.data()+b*width;outBuses[b].channelBuffers32=fo.data()+b*width;}
    setup.symbolicSampleSize=kSample32;CHECK(probe.setupProcessing(setup)==kResultOk);
    data.symbolicSampleSize=kSample32;data.numSamples=16;CHECK(process(data)==kResultOk);
    for (int c=0;c<width*3;++c) CHECK(!std::memcmp(fin[c].data(),fout[c].data(),16*sizeof(float)));
    inBuses[1].silenceFlags=1;CHECK(process(data)==kResultOk);CHECK(fout[width][0]==0 && fout[width+1][0]==fin[width+1][0]);
    CHECK(probe.activateBus(kAudio,Steinberg::Vst::kOutput,2,false)==kResultOk);
    CHECK(process(data)==kResultOk);
    CHECK(probe.activateBus(kAudio,Steinberg::Vst::kInput,2,false)==kResultOk);
    data.numInputs=data.numOutputs=2;inBuses[1].silenceFlags=0;
    CHECK(process(data)==kResultOk); // SDK permits omitted trailing inactive buses.
    for (int c=0;c<width*2;++c) CHECK(!std::memcmp(fin[c].data(),fout[c].data(),16*sizeof(float)));
    data.numInputs=1;CHECK(process(data)==kInvalidArgument);data.numInputs=2;
    data.numOutputs=1;CHECK(process(data)==kInvalidArgument);data.numOutputs=2;
    CHECK(probe.activateBus(kAudio,Steinberg::Vst::kOutput,2,true)==kResultOk);
    CHECK(process(data)==kInvalidArgument);data.numOutputs=3;
    CHECK(probe.activateBus(kAudio,Steinberg::Vst::kInput,2,true)==kResultOk);
    CHECK(process(data)==kInvalidArgument);data.numInputs=3;
  }
  const auto prior=*probe.GetActiveBusLayoutConfiguration();
  const int previousChanges=probe.changes;
  outputs[2]=SpeakerArr::kStereo;
  CHECK(probe.setBusArrangements(inputs,3,outputs,3)==kResultFalse);
  CHECK(*probe.GetActiveBusLayoutConfiguration()==prior && probe.changes==previousChanges);
  CHECK(probe.setBusArrangements(nullptr,3,outputs,3)==kResultFalse);
  CHECK(probe.setBusArrangements(inputs,-1,outputs,3)==kResultFalse);
  CHECK(probe.setBusArrangements(inputs,2,outputs,3)==kResultFalse);
  CHECK(probe.setActive(true)==kResultOk);
  CHECK(probe.activateBus(kAudio,Steinberg::Vst::kInput,0,false)==kResultFalse);
  CHECK(probe.setBusArrangements(inputs,3,inputs,3)==kResultFalse);
  CHECK(probe.setActive(false)==kResultOk);
  std::cout << "VST3 named/custom mapping, exact negotiation, 3-bus routing and atomic rejection passed\n";
}
