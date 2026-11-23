#include "AAXConfig/config.h"
#include "IPlugAAX.h"

using namespace iplug;

namespace
{
Config ProbeConfig()
{
  return Config(0, 0, PLUG_CHANNEL_IO, AAX_PLUG_NAME_STR, AAX_PLUG_NAME_STR,
                AAX_PLUG_MFR_STR, PLUG_VERSION_HEX, PLUG_UNIQUE_ID, PLUG_MFR_ID,
                PLUG_LATENCY, PLUG_DOES_MIDI_IN, PLUG_DOES_MIDI_OUT, PLUG_DOES_MPE,
                PLUG_DOES_STATE_CHUNKS, PLUG_TYPE, PLUG_HAS_UI, PLUG_WIDTH, PLUG_HEIGHT,
                false, 0, 0, 0, 0, BUNDLE_DOMAIN "." BUNDLE_MFR "." BUNDLE_NAME, "",
                AAXProbeLayouts());
}

class AAXProbe final : public IPlugAAX
{
public:
  explicit AAXProbe(const InstanceInfo& info)
  : IPlugAAX(info, ProbeConfig()) {}
};
}

namespace iplug
{
IPlugAAX* MakePlug(const InstanceInfo& info)
{
  return new AAXProbe(info);
}
}
