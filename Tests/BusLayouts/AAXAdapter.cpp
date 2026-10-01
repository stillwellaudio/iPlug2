#include "Probe/AAXConfig/config.h"
#include "IPlugAAX.h"
#include "IPlugAAXBusLayout.h"
#include <iostream>

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
  std::cout << "AAX adapter link and finite-catalogue probe passed\n";
  return 0;
}
