#include "IPlug/IPlugBypassContract.h"

#include <array>
#include <iostream>

namespace
{
struct Probe
{
  int wetAdvances = 0;
  int bypassAdvances = 0;
  int dryRenders = 0;
  std::array<double, 4> input{{0.25, -0.5, 0.75, -1.0}};
  std::array<double, 4> output{{9.0, 9.0, 9.0, 9.0}};
};

bool Check(bool condition, const char* message)
{
  if (!condition)
    std::cerr << "FAIL iplug-bypass-contract :: " << message << '\n';
  return condition;
}

bool RunCase(const char* name, bool wetAlreadyAdvanced)
{
  Probe probe;
  if (wetAlreadyAdvanced)
    ++probe.wetAdvances;

  iplug::RunHostBypassBlock(
    wetAlreadyAdvanced,
    [&]() { ++probe.bypassAdvances; },
    [&]() {
      ++probe.dryRenders;
      probe.output = probe.input;
    });

  bool passed = true;
  passed &= Check(probe.wetAdvances + probe.bypassAdvances == 1, name);
  passed &= Check(probe.bypassAdvances == (wetAlreadyAdvanced ? 0 : 1), name);
  passed &= Check(probe.dryRenders == 1, name);
  passed &= Check(probe.output == probe.input, name);
  return passed;
}
}

int main()
{
  bool passed = true;
  passed &= RunCase("VST3/AUv2 steady bypass advances once before dry", false);
  passed &= RunCase("AAX entering bypass transition does not double advance", true);
  passed &= RunCase("AAX continuing bypass transition does not double advance", true);
  passed &= RunCase("AAX leaving bypass transition does not double advance", true);
  passed &= RunCase("AAX steady bypass advances once before dry", false);
  if (passed)
    std::cout << "PASS iplug-bypass-contract :: exactly-once advancement and dry output\n";
  return passed ? 0 : 1;
}
