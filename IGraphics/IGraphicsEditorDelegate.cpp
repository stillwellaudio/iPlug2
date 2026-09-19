/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#include "IGraphicsEditorDelegate.h"
#include "IGraphics.h"
#include "IControl.h"
#if defined OS_LINUX && (defined VST3_API || defined VST3C_API)
#include "IGraphicsLinux.h"
#endif

using namespace iplug;
using namespace igraphics;

IGEditorDelegate::IGEditorDelegate(int nParams)
: IEditorDelegate(nParams)
{  
}

IGEditorDelegate::~IGEditorDelegate()
{
}

void* IGEditorDelegate::OpenWindow(void* pParent)
{
  IGEditorDelegate::CloseWindow();
  if(!mGraphics)
  {
    mGraphics = std::unique_ptr<IGraphics>(CreateGraphics());
    if (mLastWidth && mLastHeight && mLastScale)
      GetUI()->Resize(mLastWidth, mLastHeight, mLastScale);
  }
  
#if defined OS_LINUX && (defined VST3_API || defined VST3C_API)
  if (mGraphics)
    static_cast<IGraphicsLinux*>(mGraphics.get())->SetHostDriven(mEditorHostDriven);
#endif
  if(mGraphics)
    return mGraphics->OpenWindow(pParent);
  else
    return nullptr;
}

void IGEditorDelegate::CloseWindow()
{
  if (!mClosing)
  {
    mClosing = true;
    IEditorDelegate::CloseWindow();
  
    if (mGraphics)
    {
      mLastWidth = mGraphics->Width();
      mLastHeight = mGraphics->Height();
      mLastScale = mGraphics->GetDrawScale();
      mGraphics->CloseWindow();
      mGraphics = nullptr;
    }
    
    mClosing = false;
  }
}

#if defined OS_LINUX && (defined VST3_API || defined VST3C_API)
void IGEditorDelegate::SetEditorHostDriven(bool enabled)
{
  mEditorHostDriven = enabled;
  if (mGraphics)
    static_cast<IGraphicsLinux*>(mGraphics.get())->SetHostDriven(enabled);
}

void IGEditorDelegate::OnEditorHostFrame(const std::function<void()>& idle, bool draw)
{
  auto graphics = mGraphics; // Host popup callbacks can remove the editor.
  if (graphics)
    static_cast<IGraphicsLinux*>(graphics.get())->OnHostFrame(idle, draw);
}

unsigned IGEditorDelegate::GetEditorFrameInterval() const
{
  return mGraphics ? static_cast<unsigned>(std::max(1, 1000 / std::max(1, mGraphics->FPS()))) : 16;
}
#endif

void IGEditorDelegate::OnParentWindowResize(int width, int height)
{
  if (auto* pGraphics = GetUI()) 
  {
    const auto platformScale = pGraphics->GetPlatformWindowScale();
    const int windowWidth = static_cast<int>(width / platformScale);
    const int windowHeight = static_cast<int>(height / platformScale);

    // Layout-only editors retain the default Scale enum, but an explicitly
    // attached Scale resizer can legitimately request layout callbacks too.
    if (pGraphics->GetResizerMode() == EUIResizerMode::Scale
        && (pGraphics->HasCornerResizer() || !pGraphics->GetLayoutOnResize()))
    {
      // Host callbacks can acknowledge earlier UI requests after the next
      // drag/snap request. Preserve the logical canvas instead of resetting
      // its draw scale and turning that acknowledgment into a layout resize.
      const int logicalWidth = pGraphics->Width();
      const int logicalHeight = pGraphics->Height();
      // Platform dimensions truncate a second time at fractional display DPI.
      // Check the neighboring integer too: division can round just above
      // an integer that already produces the requested platform dimension.
      const auto innerDimension = [&](int dimension) {
        int inner = static_cast<int>(std::ceil(dimension / platformScale));
        if (static_cast<int>((inner - 1) * platformScale) == dimension)
          --inner;
        else if (static_cast<int>(inner * platformScale) < dimension)
          ++inner;
        return inner;
      };
      const int scaledWidth = innerDimension(width);
      const int scaledHeight = innerDimension(height);
      const float scaleX = static_cast<float>(scaledWidth) / logicalWidth;
      const float scaleY = static_cast<float>(scaledHeight) / logicalHeight;
      const auto matches = [&](float scale) {
        return scale == pGraphics->ConstrainDrawScale(scale)
            && static_cast<int>(static_cast<int>(logicalWidth * scale) * platformScale) == width
            && static_cast<int>(static_cast<int>(logicalHeight * scale) * platformScale) == height;
      };
      float drawScale = pGraphics->GetDrawScale();
      if (!matches(drawScale))
      {
        // Division and the two truncated products can round in opposite
        // directions. Check the boundary and adjacent representable scales.
        const float boundary = pGraphics->ConstrainDrawScale(std::max(scaleX, scaleY));
        drawScale = boundary;
        if (!matches(drawScale))
          drawScale = std::nextafter(boundary, 0.f);
        if (!matches(drawScale))
          drawScale = std::nextafter(boundary, boundary + 1.f);
      }
      if (matches(drawScale))
      {
        pGraphics->Resize(logicalWidth, logicalHeight, drawScale, false);
        return;
      }
    }
    // Responsive layouts and host sizes that cannot be represented by a
    // permitted uniform scale keep the existing logical-canvas resize path.
    pGraphics->Resize(windowWidth, windowHeight, 1.0f, false);
  }
}

void IGEditorDelegate::SetScreenScale(float scale)
{
  if (GetUI())
    mGraphics->SetScreenScale(scale);
}

void IGEditorDelegate::SendControlValueFromDelegate(int ctrlTag, double normalizedValue)
{
  if(!mGraphics)
    return;

  IControl* pControl = mGraphics->GetControlWithTag(ctrlTag);
  
  assert(pControl);
  
  if(pControl)
  {
    pControl->SetValueFromDelegate(normalizedValue);
  }
}

void IGEditorDelegate::SendControlMsgFromDelegate(int ctrlTag, int msgTag, int dataSize, const void* pData)
{
  if(!mGraphics)
    return;
  
  IControl* pControl = mGraphics->GetControlWithTag(ctrlTag);
  
  assert(pControl);
  
  if(pControl)
  {
    pControl->OnMsgFromDelegate(msgTag, dataSize, pData);
  }
}

void IGEditorDelegate::SendParameterValueFromDelegate(int paramIdx, double value, bool normalized)
{
  if(mGraphics)
  {
    if (!normalized)
      value = GetParam(paramIdx)->ToNormalized(value);

    for (int c = 0; c < mGraphics->NControls(); c++)
    {
      IControl* pControl = mGraphics->GetControl(c);
      
      int nVals = pControl->NVals();
      
      for(int v = 0; v < nVals; v++)
      {
        if (pControl->GetParamIdx(v) == paramIdx)
        {
          pControl->SetValueFromDelegate(value, v);
          // Could be more than one, don't break until we check them all.
        }
      }

    }
  }
  
  IEditorDelegate::SendParameterValueFromDelegate(paramIdx, value, normalized);
}

void IGEditorDelegate::SendMidiMsgFromDelegate(const IMidiMsg& msg)
{
  if(mGraphics)
  {
    for (auto c = 0; c < mGraphics->NControls(); c++) // TODO: could keep a map
    {
      IControl* pControl = mGraphics->GetControl(c);
      
      if (pControl->GetWantsMidi())
      {
        pControl->OnMidi(msg);
      }
    }
  }
  
  IEditorDelegate::SendMidiMsgFromDelegate(msg);
}

bool IGEditorDelegate::SerializeEditorSize(IByteChunk& data) const
{
  bool savedOK = true;
    
  int width = mGraphics ? mGraphics->Width() : mLastWidth;
  int height = mGraphics ? mGraphics->Height() : mLastHeight;
  float scale = mGraphics ? mGraphics->GetDrawScale() : mLastScale;
    
  savedOK &= (data.Put(&width) > 0);
  savedOK &= (data.Put(&height) > 0);
  savedOK &= (data.Put(&scale) > 0);

  return savedOK;
}

int IGEditorDelegate::UnserializeEditorSize(const IByteChunk& data, int startPos)
{
  int width = 0;
  int height = 0;
  float scale = 0.f;
    
  startPos = data.Get(&width, startPos);
  startPos = data.Get(&height, startPos);
  startPos = data.Get(&scale, startPos);
    
  if (GetUI())
  {
    if (width && height && scale)
      GetUI()->Resize(width, height, scale);
  }
  else
  {
    mLastWidth = width;
    mLastHeight = height;
    mLastScale = scale;
  }
    
  return startPos;
}

bool IGEditorDelegate::SerializeEditorState(IByteChunk& chunk) const
{
  return SerializeEditorSize(chunk);
}

int IGEditorDelegate::UnserializeEditorState(const IByteChunk& chunk, int startPos)
{
  return UnserializeEditorSize(chunk, startPos);
}

bool IGEditorDelegate::OnKeyDown(const IKeyPress& key)
{
  IGraphics* pGraphics = GetUI();
  
  if (pGraphics)
  {
    float x, y;
    pGraphics->GetMouseLocation(x, y);
    return pGraphics->OnKeyDown(x, y, key);
  }
  else
    return false;
}

bool IGEditorDelegate::OnKeyUp(const IKeyPress& key)
{
  IGraphics* pGraphics = GetUI();

  if (pGraphics)
  {
    float x, y;
    pGraphics->GetMouseLocation(x, y);
    return pGraphics->OnKeyUp(x, y, key);
  }
  else
    return false;
}
