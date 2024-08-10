#if defined IGRAPHICS_NANOVG && defined IGRAPHICS_METAL

#include "ShaderControl.h"
#include "nanovg.h"
#include "nanovg_mtl.h"
#import <Metal/Metal.h>
#import "ShaderTypes.h"

ShaderControl::ShaderControl(const IRECT& bounds, int paramIdx)
: IKnobControlBase(bounds, paramIdx)
, mRenderPipeline(nullptr)
, mRenderPassDescriptor(nullptr)
{
  SetTooltip("ShaderControl");
}

void ShaderControl::CleanUp()
{
  if (mFBO)
    mnvgDeleteFramebuffer((MNVGframebuffer*) mFBO);
  
  if (mRenderPassDescriptor)
  {
    MTLRenderPassDescriptor* rpd = (MTLRenderPassDescriptor*) mRenderPassDescriptor;
    [rpd release];
    mRenderPassDescriptor = nullptr;
  }
  
  if (mRenderPipeline)
  {
    id<MTLRenderPipelineState> renderPipeline = (id<MTLRenderPipelineState>) mRenderPipeline;
    [renderPipeline release];
    mRenderPipeline = nullptr;
  }
}

void ShaderControl::Draw(IGraphics& g)
{
  auto* pCtx = static_cast<NVGcontext*>(g.GetDrawContext());
  
  auto w = mRECT.W() * g.GetTotalScale();
  auto h = mRECT.H() * g.GetTotalScale();
    
  if (invalidateFBO)
  {
    CleanUp();
    
    invalidateFBO = false;

    NSError *error;
    auto* fbo = mnvgCreateFramebuffer(pCtx, w, h, 0);
    mFBO = (void*) fbo;
    auto dev = static_cast<id<MTLDevice>>(mnvgDevice(pCtx));
    auto dstTex = static_cast<id<MTLTexture>>(mnvgImageHandle(pCtx, fbo->image));
    
    auto rpd = (MTLRenderPassDescriptor*) mRenderPassDescriptor;

    rpd = [MTLRenderPassDescriptor new];
    rpd.colorAttachments[0].texture = dstTex;
    rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpd.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLLibrary> defaultLibrary = [dev newDefaultLibrary];

    MTLRenderPipelineDescriptor* psd = [[MTLRenderPipelineDescriptor alloc] init];
    psd.label = @"Offscreen Render Pipeline";
    psd.sampleCount = 1;
    psd.vertexFunction = [defaultLibrary newFunctionWithName:@"simpleVertexShader"];
    psd.fragmentFunction = [defaultLibrary newFunctionWithName:@"simpleFragmentShader"];
    psd.colorAttachments[0].pixelFormat = dstTex.pixelFormat;
    mRenderPipeline = [dev newRenderPipelineStateWithDescriptor:psd error:&error];
    [psd release];
    [defaultLibrary release];
    mRenderPassDescriptor = (void*) rpd;
  }

  @autoreleasepool {
    auto commandQueue = static_cast<id<MTLCommandQueue>>(mnvgCommandQueue(pCtx));
    id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
    commandBuffer.label = @"Command Buffer";

    id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:(MTLRenderPassDescriptor*) mRenderPassDescriptor];
  
    renderEncoder.label = @"Offscreen Render Pass";
    [renderEncoder setRenderPipelineState:(id<MTLRenderPipelineState>) mRenderPipeline];

    float val = GetValue();
    
    [renderEncoder setFragmentBytes:&val length:sizeof(float) atIndex:0];

    [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                      vertexStart:0
                      vertexCount:4];

    [renderEncoder endEncoding];

    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
  
    APIBitmap apibmp {reinterpret_cast<MNVGframebuffer*>(mFBO)->image, int(w), int(h), 1, 1.};
    IBitmap bmp {&apibmp, 1, false};
  
    g.DrawFittedBitmap(bmp, mRECT);
  }
}

#endif // IGRAPHICS_METAL
