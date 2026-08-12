#pragma once

#include "IPlug_include_in_plug_hdr.h"

class CLAPAdapterPlugin final : public iplug::Plugin
{
public:
  explicit CLAPAdapterPlugin(const iplug::InstanceInfo& info);
  void ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames) override;
#if defined OS_LINUX
  bool GUIWindowAttach(void* parent) noexcept override;
#endif
};
