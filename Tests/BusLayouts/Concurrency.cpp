#include "IPlugProcessor.h"
#include "RealtimeGuard.h"
#include <array>
#include <iostream>
#include <memory>
#include <thread>
using namespace iplug;
#define CHECK(x) do {if (!(x)) {std::cerr<<__LINE__<<": "<<#x<<'\n';std::abort();}} while(0)
static LayoutBus Bus(unsigned id,unsigned width,BusRole role,int source=-1)
{LayoutBus b;b.id=id;b.layout=BusLayout::Discrete(width);b.role=role;b.bypassInputBus=source;return b;}
static BusLayoutConfiguration Layout(unsigned id,unsigned a,unsigned b)
{BusLayoutConfiguration c;c.id=id;c.buses[0]={Bus(1,a,BusRole::Main),Bus(2,b,BusRole::Auxiliary),Bus(3,1,BusRole::Sidechain)};c.buses[1]={Bus(4,a,BusRole::Main,0),Bus(5,b,BusRole::Auxiliary,1)};return c;}
class Processor:public IPlugProcessor
{
public:
  Processor(const BusLayoutPolicy* policy):IPlugProcessor(Config(0,0,"8.2.1-8.2 2.8.1-2.8","probe","probe","test",0,1,2,0,false,false,false,false,0,false,0,0,false,0,0,0,0,"","",policy),kAPIAU) {SetBlockSize(32);}
  bool SendMidiMsg(const IMidiMsg&) override {return false;}
  using IPlugProcessor::ProcessBuffers;
};
static void Render(Processor& processor,unsigned instance)
{
  sample input[17][32]{},output[16][32]{};sample* ins[17];sample* outs[16];
  for (int c=0;c<17;++c) {ins[c]=input[c];for(int f=0;f<32;++f)input[c][f]=sample(10000*instance+100*c+f+1);}
  for (int c=0;c<16;++c) outs[c]=output[c];
  for (unsigned block=0;block<100;++block)
  {
    {
      realtimecheck::Scope guard;
      CHECK(processor.BeginBusLayoutBlock(32));
      for (int b=0;b<3;++b)
        if (b!=1 || block%2) CHECK(processor.AttachBusBuffers(kInput,b,processor.GetActiveBusChannelCount(kInput,b),ins+processor.GetBusBufferOffset(kInput,b),32));
      for (int b=0;b<2;++b) CHECK(processor.AttachBusBuffers(kOutput,b,processor.GetActiveBusChannelCount(kOutput,b),outs+processor.GetBusBufferOffset(kOutput,b),32));
      processor.ProcessBuffers(sample(0),32);
    }
    for (int b=0;b<2;++b) for (int c=0;c<processor.GetActiveBusChannelCount(kOutput,b);++c)
    {int flat=processor.GetBusBufferOffset(kOutput,b)+c;for(int f=0;f<32;++f)CHECK(output[flat][f]==((b==1 && !(block%2)) ? sample(0) : input[flat][f]));}
  }
}
int main()
{
  BusLayoutPolicy policy;policy.defaultID=1;policy.capacities[0]={8,8,1};policy.capacities[1]={8,8};policy.configurations={Layout(1,8,2),Layout(2,2,8)};
  CHECK(policy.IsValid());std::array<std::unique_ptr<Processor>,4> instances;
  for (auto& instance:instances) instance.reset(new Processor(&policy));
  for (unsigned cycle=0;cycle<12;++cycle)
  {
    // Host lifecycle joins render before negotiation, then transfers ownership
    // to each audio thread. Multiple instances share only immutable policy.
    for (auto& instance:instances) CHECK(instance->CommitBusLayout(policy.configurations[cycle%2]));
    std::array<std::thread,4> renderers;
    for (unsigned i=0;i<renderers.size();++i) renderers[i]=std::thread(Render,std::ref(*instances[i]),i);
    for (auto& renderer:renderers) renderer.join();
  }
  std::cout<<"four concurrent instances, serialized lifecycle handoff, missing-input clearing and C++ heap guards passed\n";
}
