#include "IPlugShaderUI.h"
#include "IPlug_include_in_plug_src.h"

#if IPLUG_EDITOR
#include "ShaderControl.h"
#endif

#include <filesystem>

enum EParam
{
  kParamDummy = 0,
  kNumParams
};

enum EControlTags
{
  kCtrlTagTestControl = 0
};

IPlugShaderUI::IPlugShaderUI(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, 1))
{
  GetParam(kParamDummy)->InitPercentage("Dummy", 100.f);
  
#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };
  
  mLayoutFunc = [&](IGraphics* pGraphics) {
    IRECT bounds = pGraphics->GetBounds();

    if (pGraphics->NControls())
    {
      pGraphics->GetBackgroundControl()->SetRECT(bounds);
      DBGMSG("SELECTED: W %i, H%i\n", pGraphics->Width(), pGraphics->Height());
      
      return;
    }

    pGraphics->EnableMouseOver(true);
    pGraphics->EnableTooltips(true);
    pGraphics->EnableMultiTouch(true);
    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    
    pGraphics->AttachCornerResizer(EUIResizerMode::Scale, true);
    pGraphics->AttachPanelBackground(COLOR_WHITE);
    pGraphics->AttachControl(new ShaderControl(bounds.GetCentredInside(100,100).GetHShifted(100), kParamDummy));
    pGraphics->AttachControl(new ShaderControl(bounds.GetCentredInside(100,100).GetHShifted(200), kParamDummy));
    pGraphics->AttachControl(new ShaderControl(bounds.GetCentredInside(100,100), kParamDummy));
  };
  
#endif
}

void IPlugShaderUI::OnHostSelectedViewConfiguration(int width, int height)
{
  DBGMSG("SELECTED: W %i, H%i\n", width, height);
//  const float scale = (float) height / (float) PLUG_HEIGHT;
  
//  if(GetUI())
//    GetUI()->Resize(width, height, 1);
}

bool IPlugShaderUI::OnHostRequestingSupportedViewConfiguration(int width, int height)
{
  DBGMSG("SUPPORTED: W %i, H%i\n", width, height); return true;
}
