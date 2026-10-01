#include "IPlugVST3.h"
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace iplug;
using namespace Steinberg;
using namespace Steinberg::Vst;

#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; std::abort(); } } while (0)

static int gCustomBusTypeCalls = 0;

uint64_t GetAPIBusTypeForChannelIOConfig(int, ERoute direction, int busIdx, const IOConfig* config,
                                         WDL_TypedBuf<uint64_t>*)
{
  ++gCustomBusTypeCalls;
  CHECK(config);
  const int channels = config->GetBusInfo(direction, busIdx)->NChans();
  return channels == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
}

static Config LegacyConfig(const char* channelIO)
{
  return Config(0, 0, channelIO, "legacy", "legacy", "test", 0, 1, 2, 0,
                false, false, false, false, 0, false, 0, 0, false, 0, 0, 0, 0, "", "");
}

class LegacyProbe final : public IPlugVST3
{
public:
  explicit LegacyProbe(const char* channelIO)
  : IPlugVST3(InstanceInfo{}, LegacyConfig(channelIO)) {}

  void GetBusName(ERoute direction, int busIdx, int, WDL_String& name) const override
  {
    name.SetFormatted(32, "%s custom %d", direction == iplug::kInput ? "Input" : "Output", busIdx);
  }
};

static void CheckBusInfo(LegacyProbe& probe, BusDirection direction, int index, int channels, const char* name)
{
  BusInfo info;
  CHECK(probe.getBusInfo(kAudio, direction, index, info) == kResultOk);
  CHECK(info.channelCount == channels);
  char ascii[128] {};
  UString(info.name, 128).toAscii(ascii, 128);
  CHECK(!std::strcmp(ascii, name));
}

static void CheckSimpleStereo(LegacyProbe& probe, int sampleSize)
{
  SpeakerArrangement arrangement = SpeakerArr::kStereo;
  CHECK(probe.setBusArrangements(&arrangement, 1, &arrangement, 1) == kResultTrue);
  CHECK(probe.activateBus(kAudio, Steinberg::Vst::kInput, 0, true) == kResultOk);
  CHECK(probe.activateBus(kAudio, Steinberg::Vst::kOutput, 0, true) == kResultOk);
  ProcessSetup setup {};
  setup.symbolicSampleSize = sampleSize;
  setup.sampleRate = 48000.;
  setup.maxSamplesPerBlock = 16;
  CHECK(probe.setupProcessing(setup) == kResultOk);

  AudioBusBuffers input {}, output {};
  input.numChannels = output.numChannels = 2;
  ProcessData data {};
  data.symbolicSampleSize = sampleSize;
  data.numSamples = 16;
  data.numInputs = data.numOutputs = 1;
  data.inputs = &input;
  data.outputs = &output;

  if (sampleSize == kSample32)
  {
    float in[2][16] {}, out[2][16] {};
    float* inPointers[] = {in[0], in[1]};
    float* outPointers[] = {out[0], out[1]};
    for (int c = 0; c < 2; ++c) for (int f = 0; f < 16; ++f) in[c][f] = float(100 * c + f + 1);
    input.channelBuffers32 = inPointers;
    output.channelBuffers32 = outPointers;
    CHECK(probe.process(data) == kResultOk);
    CHECK(!std::memcmp(in, out, sizeof(in)));
  }
  else
  {
    double in[2][16] {}, out[2][16] {};
    double* inPointers[] = {in[0], in[1]};
    double* outPointers[] = {out[0], out[1]};
    for (int c = 0; c < 2; ++c) for (int f = 0; f < 16; ++f) in[c][f] = 100. * c + f + 1.;
    input.channelBuffers64 = inPointers;
    output.channelBuffers64 = outPointers;
    CHECK(probe.process(data) == kResultOk);
    CHECK(!std::memcmp(in, out, sizeof(in)));
  }
}

int main()
{
  LegacyProbe simple("2-2");
  CHECK(!simple.HasBusLayouts());
  CHECK(simple.IPlugVST3ProcessorBase::Initialize(&simple));
  CheckBusInfo(simple, Steinberg::Vst::kInput, 0, 2, "Input custom 0");
  CheckBusInfo(simple, Steinberg::Vst::kOutput, 0, 2, "Output custom 0");
  CheckSimpleStereo(simple, kSample32);
  CheckSimpleStereo(simple, kSample64);

  LegacyProbe multi("2.1-2.2");
  CHECK(!multi.HasBusLayouts());
  CHECK(multi.IPlugVST3ProcessorBase::Initialize(&multi));
  CheckBusInfo(multi, Steinberg::Vst::kInput, 0, 2, "Input custom 0");
  CheckBusInfo(multi, Steinberg::Vst::kInput, 1, 1, "Input custom 1");
  CheckBusInfo(multi, Steinberg::Vst::kOutput, 0, 2, "Output custom 0");
  CheckBusInfo(multi, Steinberg::Vst::kOutput, 1, 2, "Output custom 1");
  SpeakerArrangement inputs[] = {SpeakerArr::kStereo, SpeakerArr::kMono};
  SpeakerArrangement outputs[] = {SpeakerArr::kStereo, SpeakerArr::kStereo};
  CHECK(multi.setBusArrangements(inputs, 2, outputs, 2) == kResultTrue);
  CHECK(gCustomBusTypeCalls >= 6);
  std::cout << "legacy VST3 default-off bus labels, custom types, sidechain and stereo precision passed\n";
}
