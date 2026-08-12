#include "CLAPAdapterPlugin.h"

using namespace iplug;

#include "IPlug_include_in_plug_src.h"

namespace
{
CLAPAdapterPlugin* gLastPlugin = nullptr;
}

CLAPAdapterPlugin::CLAPAdapterPlugin(const iplug::InstanceInfo& info)
  : iplug::Plugin(info, MakeConfig(1, 1))
{
  gLastPlugin = this;
  GetParam(0)->InitDouble("Gain", 1.0, 0.0, 1.0, 0.01);
}

extern "C" void TriggerCLAPAdapterParamChange()
{
  if (gLastPlugin)
    gLastPlugin->InformHostOfParamChange(0, 0.5);
}

void CLAPAdapterPlugin::ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames)
{
  const int outputChannels = NOutChansConnected();
  const int inputChannels = NInChansConnected();

  for (int channel = 0; channel < outputChannels; ++channel)
  {
    for (int frame = 0; frame < nFrames; ++frame)
      outputs[channel][frame] = channel < inputChannels ? inputs[channel][frame] : 0.0;
  }
}
