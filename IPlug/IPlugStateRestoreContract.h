/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for more info.

 ==============================================================================
*/

#pragma once

#include "IPlugPlatform.h"

BEGIN_IPLUG_NAMESPACE

/** Apply state before committing preset metadata or issuing callbacks. */
template <typename ValidateAndApplyState,
          typename CommitPresetMetadata,
          typename NotifyStateRestored>
inline bool RunValidatedStateTransaction(
  ValidateAndApplyState&& validateAndApplyState,
  CommitPresetMetadata&& commitPresetMetadata,
  NotifyStateRestored&& notifyStateRestored)
{
  if (!validateAndApplyState())
    return false;
  commitPresetMetadata();
  notifyStateRestored();
  return true;
}

END_IPLUG_NAMESPACE
