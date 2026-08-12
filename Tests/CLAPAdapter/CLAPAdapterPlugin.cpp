#include "CLAPAdapterPlugin.h"

using namespace iplug;

#include "IPlug_include_in_plug_src.h"

namespace
{
CLAPAdapterPlugin* gLastPlugin = nullptr;
#if defined OS_LINUX
uintptr_t gLastParent = 0;
#endif
}

CLAPAdapterPlugin::CLAPAdapterPlugin(const iplug::InstanceInfo& info)
  : Plugin(info, MakeConfig(2, 1))
{
  gLastPlugin = this;
  GetParam(0)->InitDouble("Gain", 1.0, 0.0, 1.0, 0.01);
  GetParam(1)->InitEnum("Mode", 0, {"Clip", "Limit"});
}

extern "C" void TriggerCLAPAdapterParamChange()
{
  if (gLastPlugin)
    gLastPlugin->InformHostOfParamChange(0, 0.5);
}

#if defined OS_LINUX
extern "C" bool CLAPAdapterGuiApiSupported(const char* api, bool isFloating)
{
  return gLastPlugin && gLastPlugin->guiIsApiSupported(api, isFloating);
}

extern "C" bool CLAPAdapterSetX11Parent(uint64_t parent)
{
  clap_window window {};
  window.api = CLAP_WINDOW_API_X11;
  window.x11 = parent;
  return gLastPlugin && gLastPlugin->guiSetParent(&window);
}

extern "C" uintptr_t CLAPAdapterLastParent()
{
  return gLastParent;
}

bool CLAPAdapterPlugin::GUIWindowAttach(void* parent) noexcept
{
  gLastParent = reinterpret_cast<uintptr_t>(parent);
  return true;
}
#endif

void CLAPAdapterPlugin::ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames)
{
  const double logProbe = log(1.0);
  const int outputChannels = NOutChansConnected();
  const int inputChannels = NInChansConnected();

  for (int channel = 0; channel < outputChannels; ++channel)
  {
    for (int frame = 0; frame < nFrames; ++frame)
      outputs[channel][frame] = channel < inputChannels ? inputs[channel][frame] + logProbe : 0.0;
  }
}
