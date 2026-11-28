"""Exercise the production host-resize delegate with scale and layout modes."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class HostResizeScaleTests(unittest.TestCase):
    def test_host_size_preserves_scaling_ui_coordinates(self):
        source = (ROOT / "IGraphics/IGraphicsEditorDelegate.cpp").read_text()
        start = source.index("void IGEditorDelegate::OnParentWindowResize(")
        end = source.index("void IGEditorDelegate::SetScreenScale(", start)
        method = source[start:end]
        constrain_start = source.index("bool IGEditorDelegate::ConstrainHostEditorResize(")
        constrain_end = source.index("void IGEditorDelegate::SendControlValueFromDelegate(", constrain_start)
        constrain_method = source[constrain_start:constrain_end]
        preamble = r'''
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstdio>
#include <cstdint>
enum class EUIResizerMode { Scale, Size };
struct Graphics {
  int width=720, height=464; float drawScale=1.25f, platformScale=1.f;
  EUIResizerMode mode=EUIResizerMode::Scale; int calls=0, layouts=0; bool requestedHost=false, layoutOnResize=false, hasCornerResizer=false;
  float minScale=.5f, maxScale=2.f;
  int Width() const { return width; } int Height() const { return height; }
  int WindowWidth() const { return int(float(width)*drawScale); }
  int WindowHeight() const { return int(float(height)*drawScale); }
  float GetPlatformWindowScale() const { return platformScale; }
  float GetDrawScale() const { return drawScale; }
  EUIResizerMode GetResizerMode() const { return mode; }
  bool GetLayoutOnResize() const { return layoutOnResize; }
  bool HasCornerResizer() const { return hasCornerResizer; }
  float ConstrainDrawScale(float scale) const { return std::max(minScale,std::min(scale,maxScale)); }
  void Resize(int w,int h,float scale,bool platform) {
    scale=ConstrainDrawScale(scale);
    if (w==width && h==height && scale==drawScale) return;
    width=w; height=h; drawScale=scale; requestedHost=platform; ++calls;
    if (layoutOnResize) ++layouts;
  }
};
struct IEditorDelegate {
  bool ConstrainEditorResize(int& w, int& h) const {
    const int origW = w, origH = h;
    w = std::max(240, std::min(w, 2160));
    h = std::max(154, std::min(h, 1392));
    return w == origW && h == origH;
  }
};
struct IGEditorDelegate : IEditorDelegate {
  Graphics* graphics=nullptr;
  Graphics* GetUI() { return graphics; }
  const Graphics* GetUI() const { return graphics; }
  void OnParentWindowResize(int width,int height);
  bool ConstrainHostEditorResize(int& w, int& h) const;
};
'''
        scenarios = r'''
int main() {
  IGEditorDelegate empty; empty.OnParentWindowResize(900,580);
  Graphics graphics; IGEditorDelegate delegate; delegate.graphics=&graphics;
  // Actual REAPER trace: drag request, release snap, then delayed acknowledgments.
  delegate.OnParentWindowResize(907,584);
  assert(graphics.Width()==720 && graphics.Height()==464);
  assert(graphics.WindowWidth()==907 && graphics.WindowHeight()==584);
  delegate.OnParentWindowResize(900,580);
  assert(graphics.Width()==720 && graphics.Height()==464);
  assert(graphics.WindowWidth()==900 && graphics.WindowHeight()==580);
  assert(std::fabs(graphics.drawScale-1.25f)<0.00001f && !graphics.requestedHost);
  // Repeated grow/shrink acknowledgments must not change the logical canvas.
  for (float platform : {1.f,1.25f,1.5f,1.75f,2.f}) {
    graphics.platformScale=platform;
    for (int step : {1250,1511,2000,1749,999,1333,1000}) {
      const float scale=float(step)/1000.f;
      const int w=int(720.f*scale), h=int(464.f*scale);
      delegate.OnParentWindowResize(int(w*platform),int(h*platform));
      assert(graphics.Width()==720 && graphics.Height()==464);
      assert(graphics.WindowWidth()==w && graphics.WindowHeight()==h);
      assert(!graphics.requestedHost);
    }
  }
  // Rounding boundaries can differ by axis (100x140 at 1.05 -> 104x147).
  for (int dpi=100; dpi<=300; ++dpi)
  for (int width : {100,600,720,790,1023}) for (int height : {140,350,400,464,777}) {
    const float platform=float(dpi)/100.f;
    graphics.width=width; graphics.height=height; graphics.platformScale=platform;
    for (int step=501; step<=2000; ++step) {
      const float scale=float(step)/1000.f;
      const int w=int(float(width)*scale), h=int(float(height)*scale);
      graphics.drawScale=1.5f; // Exercise reconstruction, not only current-size echoes.
      delegate.OnParentWindowResize(int(w*platform),int(h*platform));
      assert(graphics.Width()==width && graphics.Height()==height);
      if (graphics.WindowWidth()!=w || graphics.WindowHeight()!=h) {
        std::fprintf(stderr,"roundtrip %dx%d at %.9g: expected %dx%d got %dx%d\n",width,height,scale,w,h,graphics.WindowWidth(),graphics.WindowHeight());
        return 1;
      }
    }
  }
  // Deterministic varied canvases, non-grid draw scales, and integer Windows DPI.
  uint32_t random=0x7167bca1u;
  const auto next=[&]() { random=random*1664525u+1013904223u; return random; };
  for (int sample=0;sample<100000;++sample) {
    const int w=100+int(next()%3901), h=100+int(next()%2901);
    const float scale=.5f+float(next()%1500001)/1000000.f;
    const float platform=float(96+next()%193)/96.f;
    const int pw=int(int(w*scale)*platform), ph=int(int(h*scale)*platform);
    graphics.width=w; graphics.height=h; graphics.drawScale=1.f; graphics.platformScale=platform;
    delegate.OnParentWindowResize(pw,ph);
    assert(graphics.Width()==w && graphics.Height()==h);
    assert(int(graphics.WindowWidth()*platform)==pw && int(graphics.WindowHeight()*platform)==ph);
  }
  // Non-quarter DPI can round division just above the original integer size.
  graphics.width=1021; graphics.height=864; graphics.platformScale=1.3f;
  graphics.drawScale=1.f;
  delegate.OnParentWindowResize(2314,1957);
  assert(graphics.Width()==1021 && graphics.Height()==864);
  assert(int(graphics.WindowWidth()*graphics.platformScale)==2314);
  assert(int(graphics.WindowHeight()*graphics.platformScale)==1957);
  graphics.width=720; graphics.height=464;
  // Responsive editors with no stock resizer retain the default Scale enum.
  // Layout-on-resize must still reshape the canvas and invoke layout.
  graphics.width=1024; graphics.height=768; graphics.drawScale=1.f;
  graphics.layoutOnResize=true; graphics.platformScale=1.f;
  delegate.OnParentWindowResize(1200,768);
  assert(graphics.Width()==1200 && graphics.Height()==768 && graphics.layouts==1);
  // Explicit Scale resizers may also request layout callbacks (IPlugControls).
  graphics.width=720; graphics.height=464; graphics.drawScale=1.f;
  graphics.hasCornerResizer=true;
  delegate.OnParentWindowResize(900,580);
  assert(graphics.Width()==720 && graphics.Height()==464);
  assert(graphics.drawScale==1.25f && graphics.layouts==2);
  // An explicit Size resizer still resizes the logical canvas.
  graphics.mode=EUIResizerMode::Size;
  delegate.OnParentWindowResize(800,600);
  assert(graphics.Width()==800 && graphics.Height()==600 && graphics.layouts==3);
  graphics.mode=EUIResizerMode::Scale;
  graphics.hasCornerResizer=false; graphics.layoutOnResize=false;
  // Fixed-canvas scaling editors ignore out-of-range or nonuniform host rects
  // instead of mutating the logical canvas and poisoning reopen state.
  for (const auto size : {std::pair<int,int>{240,154}, {2160,1392}, {1000,580}}) {
    graphics.width=720; graphics.height=464; graphics.drawScale=1.25f;
    graphics.platformScale=1.f;
    const int prevCalls=graphics.calls;
    delegate.OnParentWindowResize(size.first,size.second);
    assert(graphics.Width()==720 && graphics.Height()==464);
    assert(graphics.drawScale==1.25f && graphics.calls==prevCalls);
  }
  // Catch-22 REAPER FX-chain regression: bogus 408x295 host size followed by
  // CloseWindow()/OpenWindow() must preserve 1120x512 and valid uniform scales.
  {
    graphics.width=1120; graphics.height=512; graphics.drawScale=1.25f;
    graphics.minScale=.5f; graphics.maxScale=4.f;
    graphics.platformScale=1.f;
    for (float platform : {1.f, 2.f}) {
      graphics.platformScale=platform;
      delegate.OnParentWindowResize(int(408*platform), int(295*platform));
      assert(graphics.Width()==1120 && graphics.Height()==512);
      assert(std::fabs(graphics.drawScale-1.25f)<0.00001f);
      // Simulate IGEditorDelegate::CloseWindow() -> OpenWindow() replay.
      const int lastW=graphics.Width(), lastH=graphics.Height();
      const float lastS=graphics.GetDrawScale();
      Graphics reopened;
      reopened.width=1120; reopened.height=512; reopened.drawScale=1.f;
      reopened.minScale=.5f; reopened.maxScale=4.f; reopened.platformScale=platform;
      reopened.Resize(lastW, lastH, lastS, true);
      assert(reopened.Width()==1120 && reopened.Height()==512);
      assert(std::fabs(reopened.drawScale-1.25f)<0.00001f);
    }
    // Valid uniform corner-drag resizes within [0.5, 4.0] still scale cleanly.
    graphics.platformScale=1.f;
    for (float s : {0.5f, 1.f, 1.25f, 1.5f, 1.75f, 2.f, 4.f}) {
      delegate.OnParentWindowResize(int(1120.f*s), int(512.f*s));
      assert(graphics.Width()==1120 && graphics.Height()==512);
      assert(std::fabs(graphics.drawScale-s)<0.0001f);
    }
    graphics.maxScale=2.f;
  }
  // Truncation below a legal endpoint still reconstructs that endpoint scale.
  graphics.width=721; graphics.height=465; graphics.drawScale=1.f;
  delegate.OnParentWindowResize(360,232);
  assert(graphics.Width()==721 && graphics.Height()==465 && graphics.drawScale==.5f);
  // Responsive Scale+layout editors still fall back to logical-canvas resize
  // for out-of-range or nonuniform host rectangles.
  graphics.hasCornerResizer=true; graphics.layoutOnResize=true;
  graphics.platformScale=1.f;
  for (const auto size : {std::pair<int,int>{240,154}, {2160,1392}, {1000,580}}) {
    graphics.width=720; graphics.height=464; graphics.drawScale=1.f;
    delegate.OnParentWindowResize(size.first,size.second);
    assert(graphics.Width()==size.first && graphics.Height()==size.second);
    assert(graphics.WindowWidth()==size.first && graphics.WindowHeight()==size.second);
    assert(graphics.drawScale==1.f);
  }
  graphics.hasCornerResizer=false; graphics.layoutOnResize=false;
  // Layout resizers retain their existing resize-the-canvas behavior.
  graphics.mode=EUIResizerMode::Size; graphics.platformScale=2.f;
  delegate.OnParentWindowResize(1600,1200);
  assert(graphics.Width()==800 && graphics.Height()==600);
  assert(graphics.drawScale==1.f && !graphics.requestedHost);
  // ConstrainHostEditorResize enforces aspect-locked uniform scale bounds
  // for fixed-canvas Scale editors and round-trips through OnParentWindowResize.
  {
    graphics.mode=EUIResizerMode::Scale; graphics.layoutOnResize=false;
    graphics.width=1120; graphics.height=512; graphics.drawScale=1.f;
    graphics.minScale=.5f; graphics.maxScale=2.f;
    for (float platform : {1.f, 1.5f, 2.f}) {
      graphics.platformScale=platform;
      graphics.drawScale=1.f;
      // Arbitrary non-uniform host drag snaps to uniform scale and passes OnParentWindowResize.
      int w=int(1500*platform), h=int(512*platform);
      assert(!delegate.ConstrainHostEditorResize(w, h));
      const float expectedScale = std::min(2.f, 1500.f / 1120.f);
      assert(w == int(int(1120 * expectedScale) * platform));
      assert(h == int(int(512 * expectedScale) * platform));
      assert(delegate.ConstrainHostEditorResize(w, h));
      delegate.OnParentWindowResize(w, h);
      assert(graphics.Width()==1120 && graphics.Height()==512);
      assert(int(graphics.WindowWidth()*platform)==w && int(graphics.WindowHeight()*platform)==h);
      // Out-of-range drag clamps to [minScale, maxScale].
      int bigW=int(4000*platform), bigH=int(2000*platform);
      assert(!delegate.ConstrainHostEditorResize(bigW, bigH));
      assert(bigW == int(int(1120 * 2.f) * platform));
      assert(bigH == int(int(512 * 2.f) * platform));
      int smallW=int(100*platform), smallH=int(50*platform);
      assert(!delegate.ConstrainHostEditorResize(smallW, smallH));
      assert(smallW == int(int(1120 * .5f) * platform));
      assert(smallH == int(int(512 * .5f) * platform));
    }
  }
  std::puts("host resize scale/layout checks passed");
}
'''
        compiler = "/Library/Developer/CommandLineTools/usr/bin/clang++"
        if not Path(compiler).exists():
            compiler = os.environ.get("CXX", "c++")
        with tempfile.TemporaryDirectory(prefix="host-resize-scale-") as temp:
            cpp = Path(temp) / "test.cpp"
            cpp.write_text(preamble + method + constrain_method + scenarios)
            binary = cpp.with_suffix("")
            flags = ["-isysroot", "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"] if Path(compiler).is_absolute() else []
            result = subprocess.run([compiler,"-std=c++17",*flags,str(cpp),"-o",str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result = subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

if __name__ == "__main__":
    unittest.main()
