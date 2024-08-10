#pragma once

#include "IControl.h"
#if defined IGRAPHICS_NANOVG

/**
 * @file
 * @copydoc ShaderControl
 */

using namespace iplug;
using namespace igraphics;

class ShaderControl : public IKnobControlBase
{
public:
  ShaderControl(const IRECT& bounds, int paramIdx);
  
  ~ShaderControl()
  {
    CleanUp();
  }
  
  void OnResize() override
  {
    invalidateFBO = true;
  }

  void OnRescale() override
  {
    invalidateFBO = true;
  }
  
  void Draw(IGraphics& g) override;

private:
  void CleanUp();

  void* mFBO = nullptr;
  bool invalidateFBO = true;

#ifdef IGRAPHICS_METAL
  void* mRenderPassDescriptor = nullptr;
  void* mRenderPipeline = nullptr;
#else
  int mInitialFBO = 0;
#endif
};

#else
#error "Not Supported"
#endif
