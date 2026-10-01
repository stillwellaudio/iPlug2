#include "Probe/AAXConfig/config.h"
#include "IPlugAAX.h"
#include "IPlugAAXBusLayout.h"
#include "AAXTestHost.h"
#include "AAX_CString.h"
#include <iostream>
#include <cstdlib>
#include <cmath>

#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; std::abort(); } } while (0)
using namespace iplug;

namespace
{
BusLayoutPolicy RoutingPolicy()
{
  BusLayoutPolicy p;
  p.capacities[0] = {64, 1}; p.capacities[1] = {64, 4, 2}; p.defaultID = 1;
  auto add = [&](AAX_EStemFormat stem)
  {
    BusLayoutConfiguration c;
    c.id = static_cast<uint32_t>(p.configurations.size() + 1);
    c.name = "Routing " + std::to_string(c.id);
    c.aaxNativeID = 0x74520000 + c.id;
#if AAX_DOES_AUDIOSUITE
    c.aaxAudioSuiteID = 0x74530000 + c.id;
#endif
    LayoutBus main; main.id = 1; main.role = BusRole::Main; main.name = "Main";
    CHECK(aaxlayouts::FromStem(stem, main.layout));
    LayoutBus sidechain; sidechain.id = 2; sidechain.role = BusRole::Sidechain; sidechain.name = "Sidechain";
    CHECK(aaxlayouts::FromStem(AAX_eStemFormat_Mono, sidechain.layout));
    c.buses[kInput] = {main, sidechain};
    main.id = 3; main.bypassInputBus = 0;
    LayoutBus aux; aux.id = 4; aux.role = BusRole::Auxiliary; aux.name = "Stereo aux";
    CHECK(aaxlayouts::FromStem(AAX_eStemFormat_Stereo, aux.layout));
    LayoutBus mono = aux; mono.id = 5; mono.name = "Mono aux";
    CHECK(aaxlayouts::FromStem(AAX_eStemFormat_Mono, mono.layout));
    c.buses[kOutput] = {main, aux, mono}; p.configurations.push_back(c);
  };
  for (const auto& entry : aaxlayouts::SpeakerStems()) add(entry.stem);
  for (unsigned order = 1; order <= 7; ++order) add(aaxlayouts::AmbisonicStem(order));
  return p;
}

std::string NumericShapes(const BusLayoutPolicy& policy)
{
  std::string shapes;
  for (const auto& c : policy.configurations)
  {
    if (!shapes.empty()) shapes += " ";
    for (int d = 0; d < 2; ++d)
    {
      if (d) shapes += "-";
      if (c.buses[d].empty()) shapes += "0";
      for (size_t b = 0; b < c.buses[d].size(); ++b)
      {
        if (b) shapes += ".";
        shapes += std::to_string(c.buses[d][b].layout.NChannels());
      }
    }
  }
  return shapes;
}

class RoutingProbe final : public IPlugAAX
{
public:
  RoutingProbe(const BusLayoutPolicy* policy, const char* shapes, bool instrument = false, int latency = 0, bool transparent = false)
  : IPlugAAX(InstanceInfo{}, Config(1, 0, shapes, "Routing", "Routing", "Test", 0, 1, 2,
      latency, false, false, false, true, instrument ? 1 : 0, false, 0, 0, false, 0, 0, 0, 0, "", "", policy)), mTransparent(transparent)
  { GetParam(0)->InitDouble("Gain", 2., 0., 4., 0.01); }
  ~RoutingProbe() { if (initialized) Uninitialize(); }
  AAX_Result Start(AAXTestHost& host) { initialized = true; return Initialize(host.Unknown()); }
  AAX_ITransport* TestTransport() { return AAX_CEffectParameters::Transport(); }
  void PrepareBlock(int frames) { SetBlockSize(frames); }
  void OnBusLayoutChanged() override { ++changes; }
  void ProcessBlock(sample** inputs, sample** outputs, int frames) override
  {
    ++renders;
    if (mTransparent) { IPlugProcessor::ProcessBlock(inputs, outputs, frames); return; }
    const auto* active = GetActiveBusLayoutConfiguration();
    const int side = active->buses[kInput].size() > 1 ? GetBusBufferOffset(kInput, 1) : -1;
    for (size_t b = 0; b < active->buses[kOutput].size(); ++b)
      for (unsigned c = 0; c < active->buses[kOutput][b].layout.NChannels(); ++c)
        for (int f = 0; f < frames; ++f)
        {
          const sample sc = side < 0 ? 0. : inputs[side][f];
          outputs[GetBusBufferOffset(kOutput, static_cast<int>(b)) + c][f] =
            b ? (b * 10 + c) * 0.25 + sc :
              (active->buses[kInput].empty() ? 0.5 : inputs[c][f] * GetParam(0)->Value()) + sc;
        }
  }
  int changes = 0, renders = 0;
private:
  bool initialized = false;
  bool mTransparent;
};

struct RenderBlock
{
  static constexpr float sentinel = -77.f;
  static constexpr int storageFrames = 2048;
  std::vector<std::vector<float>> input, output;
  std::vector<float*> inputs, outputs;
  int32_t frames = 64, sidechain;
  int64_t state = 0;
  float meterValues[2]{};
  float* meters[2] = {meterValues, meterValues + 1};
  AAXTestTransportNode transport;
  AAX_SIPlugPrivateData privateData{};
  AAX_SIPlugRenderInfo info{};
  RenderBlock(RoutingProbe& probe, int width, bool hasInput = true)
  : input(hasInput ? width + 1 : 0, std::vector<float>(storageFrames + 2, sentinel)),
    output(width + 3, std::vector<float>(storageFrames + 2, sentinel)),
    sidechain(hasInput ? width : 0), transport(probe.TestTransport())
  {
    for (size_t c = 0; c < input.size(); ++c)
    {
      inputs.push_back(input[c].data() + 1);
      for (int f = 0; f < storageFrames; ++f) inputs.back()[f] = float((c + 1) * 0.125 + f / 8192.);
    }
    for (auto& channel : output) outputs.push_back(channel.data() + 1);
    info.mAudioInputs = inputs.empty() ? nullptr : inputs.data(); info.mAudioOutputs = outputs.data();
    info.mNumSamples = &frames; info.mCurrentStateNum = &state; info.mSideChainP = &sidechain;
    info.mTransportNode = &transport; info.mMeters = meters; info.mPrivateData = &privateData;
    CHECK(probe.ResetFieldData(AAX_FIELD_INDEX(AAX_SIPlugRenderInfo, mPrivateData), &privateData, sizeof(privateData)) == AAX_SUCCESS);
    CHECK(privateData.mMonolithicParametersPtr == &probe);
  }
  void Render()
  {
    AAX_SIPlugRenderInfo* instances[] = {&info};
    AAX_CIPlugParameters::StaticRenderAudio(instances, instances + 1);
  }
  void Clear() { for (auto& channel : output) std::fill(channel.begin(), channel.end(), sentinel); }
  void Guards() const
  {
    for (const auto& channel : input) CHECK(channel.front() == sentinel && channel.back() == sentinel);
    for (const auto& channel : output)
    {
      CHECK(channel.front() == sentinel && channel.back() == sentinel);
      CHECK(channel[frames + 1] == sentinel);
    }
  }
  void Wet(int width, bool sc, double gain = 2.) const
  {
    for (int c = 0; c < width + 3; ++c)
      for (int f = 0; f < frames; ++f)
      {
        const double side = sc ? input[width][f + 1] : 0.;
        const double expected = c < width ? input[c][f + 1] * gain + side :
          (c < width + 2 ? 10 + c - width : 20) * 0.25 + side;
        CHECK(output[c][f + 1] == static_cast<float>(expected));
      }
    Guards();
  }
};

void CheckRouting()
{
  auto policy = RoutingPolicy(); const auto shapes = NumericShapes(policy);
  CHECK(policy.configurations.size() == 34 && aaxlayouts::ValidatePolicy(policy, AAX_DOES_AUDIOSUITE));
  for (const auto& c : policy.configurations)
  {
    AAXTestHost host; CHECK(aaxlayouts::ToStem(c.buses[kInput][0].layout, host.input)); host.output = host.input;
    {
      RoutingProbe probe(&policy, shapes.c_str()); CHECK(probe.Start(host) == AAX_SUCCESS);
      CHECK(probe.changes == 1 && probe.GetActiveBusLayoutConfiguration()->id == c.id);
      CHECK(probe.GetActiveBusLayout(kInput, 0)->layout == c.buses[kInput][0].layout);
      AAX_CBoolean isSuite = false; CHECK(probe.Controller()->GetIsAudioSuite(&isSuite) == AAX_SUCCESS && isSuite == AAX_DOES_AUDIOSUITE);
      CHECK(probe.GetBusBufferOffset(kInput, 1) == 64 && probe.GetBusBufferOffset(kOutput, 2) == 68);
      const int width = static_cast<int>(c.buses[kInput][0].layout.NChannels());
      RenderBlock block(probe, width); block.Render(); block.Wet(width, true);
      block.sidechain = 0; block.Clear(); block.Render(); block.Wet(width, false);
      block.inputs[0] = nullptr; block.Clear(); block.Render();
      for (int f = 0; f < block.frames; ++f) CHECK(block.output[0][f + 1] == 0.f);
      block.inputs[0] = block.input[0].data() + 1;
      block.outputs[0] = nullptr; block.Clear(); block.Render();
      CHECK(block.output[0][1] == RenderBlock::sentinel);
      block.outputs[0] = block.output[0].data() + 1;
      block.Clear(); const int beforeMismatch = probe.renders;
      const auto oldOutput = host.output;
      host.output = width == 2 ? AAX_eStemFormat_Mono : AAX_eStemFormat_Stereo;
      block.Render(); CHECK(probe.renders == beforeMismatch && block.output[0][1] == RenderBlock::sentinel);
      host.output = oldOutput;
      block.sidechain = width + 1; block.Clear(); const int renders = probe.renders; block.Render();
      CHECK(probe.renders == renders && block.output[0][1] == RenderBlock::sentinel);
      block.sidechain = width;
      // A host block larger than the prepared capacity must clear outputs without growing it.
      probe.PrepareBlock(64); block.frames = 65; block.Clear(); block.Render();
      CHECK(probe.renders == renders && probe.GetBlockSize() == 64);
      for (const auto& channel : block.output) for (int f = 1; f <= 65; ++f) CHECK(channel[f] == 0.f);
      block.Guards(); block.frames = 64;
      CHECK(probe.UpdateParameterNormalizedValue("1", 0.25, AAX_eUpdateSource_Unspecified) == AAX_SUCCESS);
      block.Clear(); block.Render(); block.Wet(width, true, 1.);
      // Bypass routes only the main input. Sidechain and aux channels must never bleed into dry.
      AAX_CString bypass; CHECK(probe.GetParameterIDFromIndex(0, &bypass) == AAX_SUCCESS);
      CHECK(probe.UpdateParameterNormalizedValue(bypass.CString(), 1., AAX_eUpdateSource_Unspecified) == AAX_SUCCESS);
      for (int n = 0; n < 9; ++n)
      {
        block.Clear(); block.Render();
        for (int channel = 0; channel < width + 3; ++channel)
          for (int f = 0; f < 64; ++f)
          {
            const double dry = channel < width ? block.input[channel][f + 1] : 0.;
            const double wet = (channel < width ? block.input[channel][f + 1] :
              (channel < width + 2 ? 10 + channel - width : 20) * 0.25) + block.input[width][f + 1];
            const double t = std::min(1., (n * 64 + f) / 480.);
            CHECK(std::fabs(block.output[channel][f + 1] - (wet * (1. - t) + dry * t)) < 0.00001);
          }
        block.Guards();
      }
      CHECK(probe.UpdateParameterNormalizedValue(bypass.CString(), 0., AAX_eUpdateSource_Unspecified) == AAX_SUCCESS);
      for (int n = 0; n < 9; ++n) { block.Clear(); block.Render(); }
      block.Wet(width, true, 1.);
      block.frames = 0; block.Clear(); block.Render(); CHECK(block.output[0][1] == RenderBlock::sentinel);
    }
    CHECK(host.references == 1);
  }
  // Default-off instances retain the existing block-size and packed stereo path.
  {
    AAXTestHost host; RoutingProbe probe(nullptr, "2-2", false, 0, true);
    CHECK(probe.Start(host) == AAX_SUCCESS && !probe.HasBusLayouts());
    CHECK(probe.GetBlockSize() == DEFAULT_BLOCK_SIZE);
    RenderBlock block(probe, 2); block.Render();
    for (int c = 0; c < 2; ++c) for (int f = 0; f < 64; ++f)
      CHECK(block.output[c][f + 1] == block.input[c][f + 1]);
    block.Guards();
  }
  // The standalone host probe inherits the framework's default transparent DSP.
  {
    AAXTestHost host; host.input = host.output = AAX_eStemFormat_Ambi_7_ACN;
    RoutingProbe probe(&policy, shapes.c_str(), false, 0, true); CHECK(probe.Start(host) == AAX_SUCCESS);
    CHECK(probe.GetBlockSize() == (1 << AAX_eAudioBufferLengthNative_Max));
    RenderBlock block(probe, 64); block.Render();
    for (int c = 0; c < 67; ++c) for (int f = 0; f < 64; ++f)
      CHECK(block.output[c][f + 1] == (c < 64 ? block.input[c][f + 1] : 0.f));
    block.Guards();
    for (int frames : {32, 128, 256, 512, 1024})
    {
      block.frames = frames; block.Clear(); block.Render();
      for (int c = 0; c < 67; ++c) for (int f = 0; f < block.frames; ++f)
        CHECK(block.output[c][f + 1] == (c < 64 ? block.input[c][f + 1] : 0.f));
      block.Guards();
    }
  }
  // Exercise the array-of-instances entry point with independent controller/DSP state.
  {
    AAXTestHost host1, host2;
    RoutingProbe first(&policy, shapes.c_str()), second(&policy, shapes.c_str());
    CHECK(first.Start(host1) == AAX_SUCCESS && second.Start(host2) == AAX_SUCCESS);
    CHECK(second.UpdateParameterNormalizedValue("1", 0.25, AAX_eUpdateSource_Unspecified) == AAX_SUCCESS);
    RenderBlock a(first, 2), b(second, 2);
    AAX_SIPlugRenderInfo* instances[] = {&a.info, &b.info};
    AAX_CIPlugParameters::StaticRenderAudio(instances, instances + 2);
    CHECK(first.renders == 1 && second.renders == 1); a.Wet(2, true); b.Wet(2, true, 1.);
    a.info.mPrivateData = nullptr; a.Clear();
    AAX_CIPlugParameters::StaticRenderAudio(instances, instances + 2);
    CHECK(first.renders == 1 && second.renders == 2 && a.output[0][1] == RenderBlock::sentinel);
  }
  // The AAX instrument convention reports the output stem as its input stem.
  {
    auto instrument = policy; instrument.configurations = {policy.configurations[1]};
    instrument.configurations[0].buses[kInput].clear(); instrument.capacities[kInput].clear(); instrument.defaultID = 2;
    instrument.configurations[0].buses[kOutput][0].bypassInputBus = -1;
    const auto instrumentShapes = NumericShapes(instrument);
    CHECK(aaxlayouts::ValidatePolicy(instrument, AAX_DOES_AUDIOSUITE));
    AAXTestHost host;
    { RoutingProbe probe(&instrument, instrumentShapes.c_str(), true); CHECK(probe.Start(host) == AAX_SUCCESS);
      RenderBlock block(probe, 2, false); block.Render();
      CHECK(block.output[0][1] == 0.5f && block.output[1][1] == 0.5f);
      CHECK(block.output[2][1] == 2.5f && block.output[3][1] == 2.75f && block.output[4][1] == 5.f);
      AAX_CString bypass; CHECK(probe.GetParameterIDFromIndex(0, &bypass) == AAX_SUCCESS);
      CHECK(probe.UpdateParameterNormalizedValue(bypass.CString(), 1., AAX_eUpdateSource_Unspecified) == AAX_SUCCESS);
      for (int n = 0; n < 9; ++n) { block.Clear(); block.Render(); }
      for (const auto& channel : block.output) for (int f = 1; f <= 64; ++f) CHECK(channel[f] == 0.f);
      block.Guards(); }
    CHECK(host.references == 1);
  }
  // Dry latency spans host blocks; auxiliary outputs remain silent while bypassed.
  {
    AAXTestHost host;
    { RoutingProbe probe(&policy, shapes.c_str(), false, 3); CHECK(probe.Start(host) == AAX_SUCCESS);
      RenderBlock block(probe, 2); block.Render();
      AAX_CString bypass; CHECK(probe.GetParameterIDFromIndex(0, &bypass) == AAX_SUCCESS);
      CHECK(probe.UpdateParameterNormalizedValue(bypass.CString(), 1., AAX_eUpdateSource_Unspecified) == AAX_SUCCESS);
      for (int n = 0; n < 9; ++n) { block.Clear(); block.Render(); }
      for (int c = 0; c < 5; ++c) for (int f = 0; f < 64; ++f)
        CHECK(block.output[c][f + 1] == (c < 2 ? block.input[c][(f + 61) % 64 + 1] : 0.f));
      block.Guards(); }
    CHECK(host.references == 1);
  }
  // Controller failures, unknown stems and ambiguous semantic pairs must fail initialization.
  for (int failure = 0; failure < 4; ++failure)
  {
    AAXTestHost host;
    if (failure == 0) host.inputResult = AAX_ERROR_UNIMPLEMENTED;
    if (failure == 1) host.outputResult = AAX_ERROR_UNIMPLEMENTED;
    if (failure == 2) host.output = AAX_eStemFormat_None;
    auto invalid = policy;
    if (failure == 3)
    {
      auto duplicate = invalid.configurations[1]; duplicate.id = 999; duplicate.aaxNativeID = 999; duplicate.aaxAudioSuiteID = 998;
      duplicate.buses[kOutput][1].layout = duplicate.buses[kOutput][2].layout;
      invalid.configurations.push_back(duplicate);
    }
    const auto invalidShapes = NumericShapes(invalid);
    { RoutingProbe probe(&invalid, invalidShapes.c_str()); CHECK(probe.Start(host) == AAX_ERROR_INVALID_ARGUMENT); CHECK(probe.changes == 0); }
    CHECK(host.references == 1);
  }
}
}

namespace iplug
{
IPlugAAX* MakePlug(const InstanceInfo&)
{
  return nullptr;
}
}

int main()
{
  const auto* policy = PLUG_BUS_LAYOUTS();
  if (!policy || policy->configurations.size() != 11 ||
      policy->configurations[0].aaxNativeID != AAX_PROBE_NATIVE_ID ||
      !iplug::aaxlayouts::ValidatePolicy(*policy, AAX_DOES_AUDIOSUITE))
    return 1;
#if AAX_DOES_AUDIOSUITE
  if (policy->configurations[0].aaxAudioSuiteID != AAX_PROBE_AUDIOSUITE_ID)
    return 1;
#else
  if (policy->configurations[0].aaxAudioSuiteID != 0)
    return 1;
#endif
  auto reordered = *policy;
  std::reverse(reordered.configurations.begin(), reordered.configurations.end());
  if (!iplug::aaxlayouts::ValidatePolicy(reordered, AAX_DOES_AUDIOSUITE)) return 1;
  for (const auto& c : policy->configurations)
  {
    AAX_EStemFormat in, out;
    if (!iplug::aaxlayouts::ToStem(c.buses[0][0].layout, in) ||
        !iplug::aaxlayouts::ToStem(c.buses[1][0].layout, out)) return 1;
    const auto* selected = iplug::aaxlayouts::FindConfiguration(reordered, in, out);
    if (!selected || selected->id != c.id || selected->aaxNativeID != c.aaxNativeID ||
        selected->aaxAudioSuiteID != c.aaxAudioSuiteID) return 1;
  }
  CheckRouting();
  std::cout << "AAX SDK controller lifecycle and static render: 27 speaker stems, HOA 1-7, sidechain, 2 aux buses, bypass/fade/latency, null channels, parameter change, instrument, instance batch, rejection and block bounds passed\n";
  return 0;
}
