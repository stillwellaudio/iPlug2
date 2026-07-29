/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for more info.

 ==============================================================================
*/

#pragma once

#include "IPlugPlatform.h"

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

END_IPLUG_NAMESPACE
