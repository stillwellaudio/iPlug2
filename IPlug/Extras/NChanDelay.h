/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers. 
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

#pragma once
#include "IPlugPlatform.h"

#include <algorithm>

BEGIN_IPLUG_NAMESPACE

// A static delayline used to delay bypassed signals to match mLatency in AAX/VST3/AU
template<typename T>
class NChanDelayLine
{
public:
  NChanDelayLine(int nInputChans = 2, int nOutputChans = 2)
  : mNInChans(nInputChans)
  , mNOutChans(nOutputChans)
  {}

  void SetDelayTime(int delayTimeSamples)
  {
    mDTSamples = delayTimeSamples;
    mBuffer.Resize(mNOutChans * delayTimeSamples);
    mWriteAddress = 0;
    ClearBuffer();
  }

  void ClearBuffer()
  {
    memset(mBuffer.Get(), 0, mNOutChans * mDTSamples * sizeof(T));
  }

  void ProcessBlock(T** inputs, T** outputs, int nFrames)
  {
    ProcessBlock(inputs, outputs, nFrames, static_cast<int>(mNInChans));
  }

  void ProcessBlock(T** inputs,
                    T** outputs,
                    int nFrames,
                    int nMainInputChannels)
  {
    T* buffer = mBuffer.Get();
    const int mainInputs = std::clamp(
      nMainInputChannels, 0, static_cast<int>(mNInChans));

    for (auto s = 0 ; s < nFrames; ++s)
    {
      for (uint32_t c = 0; c < mNOutChans; c++)
      {
        const int outputChannel = static_cast<int>(c);
        const int inputChannel = mainInputs == 1
          ? 0
          : (outputChannel < mainInputs ? outputChannel : -1);
        const T input = inputChannel >= 0 ? inputs[inputChannel][s] : T{};
        const uint32_t offset = c * mDTSamples;
        outputs[c][s] = buffer[offset + mWriteAddress];
        buffer[offset + mWriteAddress] = input;
      }

      mWriteAddress++;
      mWriteAddress %= mDTSamples;
    }
  }

private:
  WDL_TypedBuf<T> mBuffer;
  uint32_t mNInChans, mNOutChans;
  uint32_t mWriteAddress = 0;
  uint32_t mDTSamples = 0;
} WDL_FIXALIGN;

END_IPLUG_NAMESPACE
