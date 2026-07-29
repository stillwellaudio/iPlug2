/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for more info.

 ==============================================================================
*/

#pragma once

#include "IPlugPlatform.h"

#include <algorithm>
#include <cstring>

BEGIN_IPLUG_NAMESPACE

/** Run the wrapper-owned bypass path for one host block.
 *
 * The wet DSP state must advance exactly once per block. When a wrapper has
 * already rendered the wet path for a bypass transition, only the dry path is
 * rendered. Otherwise the plug-in's bypass hook runs before dry pass-through.
 */
template <typename AdvanceWetState, typename RenderDryOutput>
inline void RunHostBypassBlock(bool wetStateAlreadyAdvanced,
                               AdvanceWetState&& advanceWetState,
                               RenderDryOutput&& renderDryOutput)
{
  if (!wetStateAlreadyAdvanced)
    advanceWetState();
  renderDryOutput();
}

/** Route only the main input bus to dry outputs.
 *
 * A mono main input is duplicated across the main output bus. Multi-channel
 * main input channels retain their indices; auxiliary outputs are cleared, so
 * neither a sidechain nor the main input can leak across a bus boundary.
 */
template <typename Sample>
inline void RouteMainInputToOutputs(const Sample* const* inputs,
                                    Sample* const* outputs,
                                    int nMainInputChannels,
                                    int nMainOutputChannels,
                                    int nOutputChannels,
                                    int nFrames)
{
  const int mainOutputs = std::max(
    0, std::min(nMainOutputChannels, nOutputChannels));
  for (int output = 0; output < nOutputChannels; ++output)
  {
    if (outputs == nullptr || outputs[output] == nullptr)
      continue;
    const int input = output >= mainOutputs
      ? -1
      : nMainInputChannels == 1
      ? 0
      : (output < nMainInputChannels ? output : -1);
    if (input >= 0 && inputs != nullptr && inputs[input] != nullptr)
    {
      std::memcpy(outputs[output], inputs[input],
                  static_cast<size_t>(nFrames) * sizeof(Sample));
    }
    else
    {
      std::memset(outputs[output], 0,
                  static_cast<size_t>(nFrames) * sizeof(Sample));
    }
  }
}

END_IPLUG_NAMESPACE
