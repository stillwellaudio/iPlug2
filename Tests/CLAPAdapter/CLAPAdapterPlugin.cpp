#include "CLAPAdapterPlugin.h"

using namespace iplug;

#include "IPlug_include_in_plug_src.h"

namespace
{
CLAPAdapterPlugin* gLastPlugin = nullptr;
bool gZeroParameters = false;
int gParentResizeCount = 0;
#if defined OS_LINUX
uintptr_t gLastParent = 0;
bool gOpenWindowSucceeds = true;
#endif
}

CLAPAdapterPlugin::CLAPAdapterPlugin(const iplug::InstanceInfo& info)
  : Plugin(info, MakeConfig(gZeroParameters ? 0 : 2, 3))
{
  gLastPlugin = this;
  if (!gZeroParameters)
  {
    GetParam(0)->InitDouble("Gain", 1.0, 0.0, 1.0, 0.01);
    GetParam(1)->InitEnum("Mode", 0, {"Clip", "Limit"});
  }
  MakePreset("Quiet", 0.25, 0);
  MakePreset("Loud", 0.75, 1);
}

extern "C" void CLAPAdapterSetZeroParameters(bool enabled) { gZeroParameters = enabled; }
extern "C" int CLAPAdapterCurrentPreset() { return gLastPlugin->GetCurrentPresetIdx(); }

extern "C" void TriggerCLAPAdapterParamChange()
{
  if (gLastPlugin)
    gLastPlugin->InformHostOfParamChange(0, 0.5);
}

extern "C" void TriggerCLAPAdapterLatencyChange(int samples)
{
  if (gLastPlugin)
    gLastPlugin->SetLatency(samples);
}

extern "C" bool TriggerCLAPAdapterEditorResize(int width, int height)
{
  return gLastPlugin && gLastPlugin->EditorResizeFromUI(width, height, true);
}

extern "C" int CLAPAdapterParentResizeCount() { return gParentResizeCount; }
extern "C" void CLAPAdapterResetParentResizeCount() { gParentResizeCount = 0; }

void CLAPAdapterPlugin::OnParentWindowResize(int width, int height)
{
  ++gParentResizeCount;
  // IGraphics updates the child view here, not the adapter's stored size.
}

#if defined OS_LINUX
extern "C" uintptr_t CLAPAdapterLastParent()
{
  return gLastParent;
}

extern "C" void CLAPAdapterSetOpenWindowSucceeds(bool succeeds)
{
  gOpenWindowSucceeds = succeeds;
}

void* CLAPAdapterPlugin::OpenWindow(void* parent)
{
  gLastParent = reinterpret_cast<uintptr_t>(parent);
  return gOpenWindowSucceeds ? reinterpret_cast<void*>(UINTPTR_MAX) : nullptr;
}

void CLAPAdapterPlugin::CloseWindow()
{
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
