#include "IPlugProcessor.h"
#include <cstdlib>
#include <iostream>
#include <cstring>
using namespace iplug;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; std::abort(); } } while (0)

static LayoutBus Bus(uint32_t id, uint32_t width, BusRole role, int source=-1)
{ LayoutBus b; b.id=id; b.role=role; b.layout=BusLayout::Discrete(width); b.bypassInputBus=source; return b; }
static BusLayoutConfiguration ConfigLayout(uint32_t id, unsigned a, unsigned b)
{
  BusLayoutConfiguration c; c.id=id;
  c.buses[0]={Bus(100,a,BusRole::Main), Bus(101,b,BusRole::Auxiliary), Bus(102,1,BusRole::Sidechain)};
  c.buses[1]={Bus(200,a,BusRole::Main,0), Bus(201,b,BusRole::Auxiliary,1)};
  return c;
}
static Config MakeConfig(const char* shape, const BusLayoutPolicy* policy=nullptr)
{ return Config(0,0,shape,"probe","probe","test",0,1,2,0,false,false,false,false,0,false,0,0,false,0,0,0,0,"", "",policy); }
class Processor : public IPlugProcessor
{
public:
  Processor(const Config& c):IPlugProcessor(c,kAPIVST3) { SetBlockSize(32); InitLatencyDelay(); }
  bool SendMidiMsg(const IMidiMsg&) override { return false; }
  void OnBusLayoutChanged() override { ++changes; }
  using IPlugProcessor::ProcessBuffers;
  using IPlugProcessor::PassThroughBuffers;
  using IPlugProcessor::SetLatency;
  using IPlugProcessor::AttachBuffers;
  using IPlugProcessor::SetChannelConnections;
  int changes=0;
};
int main()
{
  CHECK(BusLayout::Discrete(128).IsValid(128));
  const auto hoa10=BusLayout::Ambisonic(10,AmbisonicOrdering::ACN,AmbisonicNormalization::SN3D);
  CHECK(hoa10.NChannels()==121 && hoa10.IsValid(128));
  CHECK(!hoa10.SameMeaning(BusLayout::Discrete(121)));
  for (unsigned n=0; n<=7; ++n)
  {
    auto ambi=BusLayout::Ambisonic(n,AmbisonicOrdering::ACN,AmbisonicNormalization::SN3D);
    CHECK(ambi.IsValid(128));
    CHECK(ambi.NChannels()==(n+1)*(n+1));
    auto n3d=BusLayout::Ambisonic(n,AmbisonicOrdering::ACN,AmbisonicNormalization::N3D);
    CHECK(n3d.IsValid(128) && !n3d.SameMeaning(ambi));
    ambi.channels.pop_back(); CHECK(!ambi.IsValid(128));
  }
  CHECK(!BusLayout::Ambisonic(0,AmbisonicOrdering::ACN,AmbisonicNormalization::SN3D).SameMeaning(BusLayout::Discrete(1)));
  auto fuma=BusLayout::Ambisonic(1,AmbisonicOrdering::FuMa,AmbisonicNormalization::MaxN);
  CHECK(fuma.IsValid(128) && fuma.channels[1].component==3);
  const uint32_t expectedFuMa[]={0,3,1,2,6,7,5,8,4,12,13,11,14,10,15,9};
  for (unsigned n=0;n<=3;++n) for (auto norm:{AmbisonicNormalization::MaxN,AmbisonicNormalization::SN3D,AmbisonicNormalization::N3D})
  { auto f=BusLayout::Ambisonic(n,AmbisonicOrdering::FuMa,norm); CHECK(f.IsValid(128));
    for (unsigned c=0;c<f.NChannels();++c) CHECK(f.channels[c].component==expectedFuMa[c]); }
  CHECK(!BusLayout::Ambisonic(4,AmbisonicOrdering::FuMa,AmbisonicNormalization::MaxN).IsValid(128));
  for (unsigned n=0;n<=10;++n) for (auto norm:{AmbisonicNormalization::SN2D,AmbisonicNormalization::N2D})
  { auto circle=BusLayout::Ambisonic(n,AmbisonicOrdering::ACN,norm,2); CHECK(circle.IsValid(128) && circle.NChannels()==2*n+1); }
  auto quad=BusLayout::Speakers({SpeakerPosition::L,SpeakerPosition::R,SpeakerPosition::Ls,SpeakerPosition::Rs});
  auto lcrs=BusLayout::Speakers({SpeakerPosition::L,SpeakerPosition::C,SpeakerPosition::R,SpeakerPosition::Cs});
  CHECK(!quad.SameMeaning(lcrs) && !quad.SameMeaning(fuma) && !quad.SameMeaning(BusLayout::Discrete(4)));
  auto renamed=quad; renamed.name="different display name"; CHECK(renamed==quad);
  auto reordered=quad; std::swap(reordered.channels[0],reordered.channels[1]); CHECK(!(reordered==quad));

  BusLayoutPolicy p; p.capacities[0]={8,8,1}; p.capacities[1]={8,8};
  p.configurations={ConfigLayout(12,8,2), ConfigLayout(42,2,8)}; p.defaultID=12;
  CHECK(p.IsValid());
  BusLayoutPolicy anonymous;anonymous.capacities[0]={4};anonymous.capacities[1]={4};anonymous.defaultID=1;
  BusLayoutConfiguration pair;pair.id=1;pair.buses[0]={Bus(1,4,BusRole::Main)};pair.buses[1]={Bus(2,4,BusRole::Main,0)};
  pair.buses[0][0].layout=quad;pair.buses[1][0].layout=BusLayout::Discrete(4,true);
  anonymous.configurations={pair};CHECK(anonymous.IsValid()&&anonymous.Accepts(pair));
  auto undeclared=pair;undeclared.buses[1][0].layout=BusLayout::Ambisonic(1,AmbisonicOrdering::ACN,AmbisonicNormalization::SN3D);
  CHECK(!anonymous.Accepts(undeclared)); // equal widths never authorize an undeclared relationship.
  Processor proc(MakeConfig("8.2.1-8.2 2.8.1-2.8",&p));
  CHECK(proc.BusLayoutsValid() && !proc.GetActiveBusLayoutConfiguration());
  CHECK(proc.MaxNChannels(kInput)==17 && proc.MaxNChannels(kOutput)==16);
  CHECK(proc.GetBusBufferOffset(kInput,1)==8 && proc.GetBusBufferOffset(kInput,2)==16);
  CHECK(proc.CommitBusLayout(p.configurations[0]) && proc.changes==1);
  CHECK(proc.CommitBusLayout(p.configurations[0]) && proc.changes==1);
  auto invalid=p.configurations[1]; invalid.buses[1][1].layout=fuma;
  CHECK(!proc.CommitBusLayout(invalid) && proc.GetActiveBusLayoutConfiguration()->id==12 && proc.changes==1);
  invalid=p.configurations[0]; invalid.buses[1][0].bypassInputBus=2; CHECK(!proc.CommitBusLayout(invalid));
  CHECK(!proc.BeginBusLayoutBlock(-1) && !proc.BeginBusLayoutBlock(33));

  for (unsigned cycle=0; cycle<6; ++cycle)
  {
    const auto& config=p.configurations[cycle%2]; CHECK(proc.CommitBusLayout(config));
    std::vector<std::vector<double>> in(17,std::vector<double>(32)), out(16,std::vector<double>(32,-1));
    double* inPointers[17]; double* outPointers[16];
    for (int c=0;c<17;++c) {inPointers[c]=in[c].data(); for (int f=0;f<32;++f) in[c][f]=(c+1)*100+f; }
    for (int c=0;c<16;++c) outPointers[c]=out[c].data();
    CHECK(proc.BeginBusLayoutBlock(32));
    for (int b=0;b<3;++b) CHECK(proc.AttachBusBuffers(kInput,b,config.buses[0][b].layout.NChannels(),inPointers+proc.GetBusBufferOffset(kInput,b),32));
    for (int b=0;b<2;++b) CHECK(proc.AttachBusBuffers(kOutput,b,config.buses[1][b].layout.NChannels(),outPointers+proc.GetBusBufferOffset(kOutput,b),32));
    proc.ProcessBuffers(double(0),32);
    for (int b=0;b<2;++b) for (unsigned c=0;c<config.buses[1][b].layout.NChannels();++c)
    { const int offset=proc.GetBusBufferOffset(kOutput,b); CHECK(!std::memcmp(out[offset+c].data(),in[offset+c].data(),32*sizeof(double))); }
    CHECK(proc.SetBusLayoutActive(kInput,1,false));
    CHECK(proc.BeginBusLayoutBlock(32));
    CHECK(proc.AttachBusBuffers(kInput,0,config.buses[0][0].layout.NChannels(),inPointers,32,1));
    CHECK(proc.AttachBusBuffers(kOutput,0,config.buses[1][0].layout.NChannels(),outPointers,32));
    CHECK(proc.AttachBusBuffers(kOutput,1,config.buses[1][1].layout.NChannels(),outPointers+8,32));
    proc.PassThroughBuffers(double(0),32);
    CHECK(out[0][0]==0 && out[8][0]==0); // silence mask + inactive bus cannot leak old audio.
    CHECK(proc.BeginBusLayoutBlock(0)); proc.ProcessBuffers(double(0),0);
  }
  CHECK(proc.CommitBusLayout(p.configurations[0]));
  proc.SetLatency(3);
  std::vector<std::vector<float>> in(17,std::vector<float>(8)), out(16,std::vector<float>(8));
  float* ins[17]; float* outs[16];
  for (int c=0;c<17;++c) {ins[c]=in[c].data(); in[c][0]=float(c+1);}
  for (int c=0;c<16;++c) outs[c]=out[c].data();
  CHECK(proc.BeginBusLayoutBlock(8));
  CHECK(proc.AttachBusBuffers(kInput,0,8,ins,8)); CHECK(proc.AttachBusBuffers(kInput,1,2,ins+8,8));
  CHECK(proc.AttachBusBuffers(kInput,2,1,ins+16,8));
  CHECK(proc.AttachBusBuffers(kOutput,0,8,outs,8)); CHECK(proc.AttachBusBuffers(kOutput,1,2,outs+8,8));
  proc.PassThroughBuffers(float(0),8);
  CHECK(out[0][0]==0 && out[0][3]==1 && out[8][3]==9 && out[9][3]==10);
  // Legacy allocation and attachment remain packed maximum simultaneous totals.
  Processor legacy(MakeConfig("8.2.1-8.2 2.8.1-2.8"));
  CHECK(!legacy.HasBusLayouts() && legacy.MaxNChannels(kInput)==11 && legacy.MaxNChannels(kOutput)==10);
  BusLayoutPolicy bad=p; bad.configurations[1].id=12;
  Processor badProc(MakeConfig("8.2.1-8.2 2.8.1-2.8",&bad));
  CHECK(badProc.HasBusLayouts() && !badProc.BusLayoutsValid() && !badProc.CommitBusLayout(p.configurations[0]));
  std::cout << "common descriptors, negotiation, reserved offsets and routing passed\n";
}
