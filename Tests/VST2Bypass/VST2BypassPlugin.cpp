#include "VST2BypassPlugin.h"

using namespace iplug;

#include "IPlug_include_in_plug_src.h"

namespace
{
// Read by the test through the exported functions below. The test drives one
// instance from a single thread, so plain globals suffice.
bool gHandlesBypass = false;
int gNotifications = 0;
bool gLastNotified = false;
bool gInsideProcess = false;
int gNotificationsOutsideProcess = 0;
int gProcessBlocks = 0;
bool gLastBlockBypassed = false;
VST2BypassPlugin* gPlugin = nullptr;
}

VST2BypassPlugin::VST2BypassPlugin(const InstanceInfo& info)
  : Plugin(info, MakeConfig(1, 1))
  , mHandlesBypass(gHandlesBypass)
{
  gPlugin = this;
  GetParam(0)->InitDouble("Gain", 1.0, 0.0, 1.0, 0.01);
}

void VST2BypassPlugin::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  ++gProcessBlocks;
  gLastBlockBypassed = GetBypassed();
  for (int c = 0; c < 2; ++c)
    for (int i = 0; i < nFrames; ++i)
      outputs[c][i] = inputs[c][i];
}

void VST2BypassPlugin::OnHostBypassChanged(bool bypassed)
{
  ++gNotifications;
  gLastNotified = bypassed;
  if (!gInsideProcess)
    ++gNotificationsOutsideProcess;
}

extern "C"
{
EXPORT void VST2BypassSetHandlesBypass(bool handles) { gHandlesBypass = handles; }
EXPORT void VST2BypassSetInsideProcess(bool inside) { gInsideProcess = inside; }
EXPORT void VST2BypassResetCounts()
{
  gNotifications = gNotificationsOutsideProcess = gProcessBlocks = 0;
  gLastNotified = gLastBlockBypassed = false;
}
EXPORT int VST2BypassNotifications() { return gNotifications; }
EXPORT int VST2BypassNotificationsOutsideProcess() { return gNotificationsOutsideProcess; }
EXPORT bool VST2BypassLastNotified() { return gLastNotified; }
EXPORT int VST2BypassProcessBlocks() { return gProcessBlocks; }
EXPORT bool VST2BypassLastBlockBypassed() { return gLastBlockBypassed; }
EXPORT bool VST2BypassGetBypassed() { return gPlugin && gPlugin->GetBypassed(); }
}
