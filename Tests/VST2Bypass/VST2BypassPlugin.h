#pragma once

#include "IPlug_include_in_plug_hdr.h"

class VST2BypassPlugin final : public iplug::Plugin
{
public:
  explicit VST2BypassPlugin(const iplug::InstanceInfo& info);
  void ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames) override;
  bool HandlesHostBypassInternally() const override { return mHandlesBypass; }
  void OnHostBypassChanged(bool bypassed) override;

private:
  const bool mHandlesBypass;
};
