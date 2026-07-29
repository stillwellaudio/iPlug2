#include "IPlug/IPlugBypassContract.h"
#include "IPlug/IPlugStateRestoreContract.h"
#include "heapbuf.h"
#include "IPlug/Extras/NChanDelay.h"

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

bool CheckRoute(const char* name,
                int mainInputs,
                int outputs,
                const std::array<double, 4>& expectedLeft,
                const std::array<double, 4>& expectedRight)
{
  std::array<double, 4> mainLeft{{0.25, -0.5, 0.75, -1.0}};
  std::array<double, 4> mainRight{{1.0, 0.5, -0.5, -1.0}};
  std::array<double, 4> sidechain{{8.0, 8.0, 8.0, 8.0}};
  std::array<double, 4> outputLeft{{9.0, 9.0, 9.0, 9.0}};
  std::array<double, 4> outputRight{{9.0, 9.0, 9.0, 9.0}};
  std::array<const double*, 3> inputPointers{{
    mainLeft.data(), mainRight.data(), sidechain.data()}};
  std::array<double*, 2> outputPointers{{
    outputLeft.data(), outputRight.data()}};

  iplug::RouteMainInputToOutputs(
    inputPointers.data(), outputPointers.data(),
    mainInputs, outputs, outputs, 4);

  bool passed = true;
  passed &= Check(outputLeft == expectedLeft, name);
  passed &= Check(outputRight == expectedRight, name);
  return passed;
}

bool CheckAuxOutputsAreCleared()
{
  std::array<double, 4> mainLeft{{0.25, -0.5, 0.75, -1.0}};
  std::array<double, 4> sidechain{{8.0, 8.0, 8.0, 8.0}};
  std::array<double, 4> outputLeft{{9.0, 9.0, 9.0, 9.0}};
  std::array<double, 4> outputRight{{9.0, 9.0, 9.0, 9.0}};
  std::array<double, 4> outputAuxLeft{{9.0, 9.0, 9.0, 9.0}};
  std::array<double, 4> outputAuxRight{{9.0, 9.0, 9.0, 9.0}};
  std::array<const double*, 2> inputPointers{{
    mainLeft.data(), sidechain.data()}};
  std::array<double*, 4> outputPointers{{
    outputLeft.data(), outputRight.data(),
    outputAuxLeft.data(), outputAuxRight.data()}};

  iplug::RouteMainInputToOutputs(
    inputPointers.data(), outputPointers.data(), 1, 2, 4, 4);

  const std::array<double, 4> silence{{0.0, 0.0, 0.0, 0.0}};
  bool passed = true;
  passed &= Check(outputLeft == mainLeft,
                  "1-2.2 direct route maps mono to main left");
  passed &= Check(outputRight == mainLeft,
                  "1-2.2 direct route maps mono to main right");
  passed &= Check(outputAuxLeft == silence,
                  "1-2.2 direct route clears auxiliary left");
  passed &= Check(outputAuxRight == silence,
                  "1-2.2 direct route clears auxiliary right");
  return passed;
}

bool CheckDelayedMonoRoute()
{
  std::array<double, 6> mainLeft{{0.25, -0.5, 0.75, -1.0, 0.5, -0.25}};
  std::array<double, 6> sidechain{{8.0, 8.0, 8.0, 8.0, 8.0, 8.0}};
  std::array<double, 6> outputLeft{{9.0, 9.0, 9.0, 9.0, 9.0, 9.0}};
  std::array<double, 6> outputRight{{9.0, 9.0, 9.0, 9.0, 9.0, 9.0}};
  std::array<double, 6> outputAuxLeft{{9.0, 9.0, 9.0, 9.0, 9.0, 9.0}};
  std::array<double, 6> outputAuxRight{{9.0, 9.0, 9.0, 9.0, 9.0, 9.0}};
  std::array<double*, 2> inputPointers{{mainLeft.data(), sidechain.data()}};
  std::array<double*, 4> outputPointers{{
    outputLeft.data(), outputRight.data(),
    outputAuxLeft.data(), outputAuxRight.data()}};

  iplug::NChanDelayLine<double> delay(2, 4);
  delay.SetDelayTime(2);
  delay.ProcessBlock(inputPointers.data(), outputPointers.data(), 6, 1, 2);

  const std::array<double, 6> expected{{0.0, 0.0, 0.25, -0.5, 0.75, -1.0}};
  const std::array<double, 6> silence{{0.0, 0.0, 0.0, 0.0, 0.0, 0.0}};
  bool passed = true;
  passed &= Check(outputLeft == expected,
                  "latency-compensated 1-2.2 maps mono to main left");
  passed &= Check(outputRight == expected,
                  "latency-compensated 1-2.2 maps mono to main right");
  passed &= Check(outputAuxLeft == silence,
                  "latency-compensated 1-2.2 clears auxiliary left");
  passed &= Check(outputAuxRight == silence,
                  "latency-compensated 1-2.2 clears auxiliary right");
  return passed;
}

bool CheckFailedStateIsTransactional()
{
  struct State
  {
    uint64_t seed = 17;
    std::array<double, 2> parameters{{0.25, 0.75}};
    int currentPreset = 3;
    int callbacks = 0;
  } state;
  const State before = state;

  const bool restored = iplug::RunValidatedStateTransaction(
    [&]() { return false; },
    [&]() { state.currentPreset = 7; },
    [&]() { ++state.callbacks; });

  bool passed = true;
  passed &= Check(!restored, "malformed AU state reports failure");
  passed &= Check(state.seed == before.seed, "failed AU state preserves seed");
  passed &= Check(state.parameters == before.parameters,
                  "failed AU state preserves parameters");
  passed &= Check(state.currentPreset == before.currentPreset,
                  "failed AU state preserves current preset");
  passed &= Check(state.callbacks == before.callbacks,
                  "failed AU state suppresses callbacks");
  return passed;
}

bool CheckSuccessfulStateOrdering()
{
  std::array<int, 3> order{{0, 0, 0}};
  int position = 0;
  const bool restored = iplug::RunValidatedStateTransaction(
    [&]() {
      order[static_cast<size_t>(position++)] = 1;
      return true;
    },
    [&]() { order[static_cast<size_t>(position++)] = 2; },
    [&]() { order[static_cast<size_t>(position++)] = 3; });
  return Check(restored, "valid AU state reports success") &&
         Check(order == std::array<int, 3>{{1, 2, 3}},
               "AU state validates before preset commit and callback");
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
  const std::array<double, 4> left{{0.25, -0.5, 0.75, -1.0}};
  const std::array<double, 4> right{{1.0, 0.5, -0.5, -1.0}};
  const std::array<double, 4> untouched{{9.0, 9.0, 9.0, 9.0}};
  passed &= CheckRoute("1-1 dry route", 1, 1, left, untouched);
  passed &= CheckRoute("1-2 duplicates main mono input", 1, 2, left, left);
  passed &= CheckRoute("2-2 preserves stereo inputs", 2, 2, left, right);
  passed &= CheckAuxOutputsAreCleared();
  passed &= CheckDelayedMonoRoute();
  passed &= CheckFailedStateIsTransactional();
  passed &= CheckSuccessfulStateOrdering();
  if (passed)
    std::cout << "PASS iplug-bypass-contract :: advancement, layout routing, and AU state transaction\n";
  return passed ? 0 : 1;
}
