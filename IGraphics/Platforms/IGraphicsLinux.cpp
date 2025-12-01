/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

// CRITICAL: Include GLAD FIRST before ANY OpenGL headers  
// Use quotes to ensure we get the local GLAD, not a system one
#include "glad/glad.h"

// Now include IGraphicsLinux.h (which includes IGraphics_select.h which includes GLAD again, but guards prevent double-include)
#include "IGraphicsLinux.h"

// Include X11/GLX headers AFTER GLAD is definitely loaded
// GLX includes GL, but GLAD has already defined __gl_h_, so GL should be skipped
#include <X11/XKBlib.h>
// Define GLX_GLXEXT_LEGACY before including glx.h to prevent some issues
#define GLX_GLXEXT_LEGACY
#include <GL/glx.h>
#undef GLX_GLXEXT_LEGACY

// Save X11's None before it gets undefined (though we undef it in the header)
static constexpr long X11_None = 0L;
#include "IControl.h"

// Skia Headers - use new Skia m131 paths
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"  // Defines GrDirectContexts namespace

using namespace iplug;
using namespace iplug::igraphics;

// --- Key Mapping Helper ---
static IKeyPress XKeyToKeyPress(KeySym keysym) {
  int vk = 0;
  switch (keysym) {
    case XK_Return: vk = kVK_RETURN; break;
    case XK_Escape: vk = kVK_ESCAPE; break;
    case XK_BackSpace: vk = kVK_BACK; break;
    case XK_Tab: vk = kVK_TAB; break;
    case XK_Left: vk = kVK_LEFT; break;
    case XK_Right: vk = kVK_RIGHT; break;
    case XK_Up: vk = kVK_UP; break;
    case XK_Down: vk = kVK_DOWN; break;
    case XK_Shift_L: case XK_Shift_R: vk = kVK_SHIFT; break;
    case XK_Control_L: case XK_Control_R: vk = kVK_CONTROL; break;
    default:
      if (keysym >= XK_a && keysym <= XK_z) vk = keysym - 32;
      else if (keysym >= XK_0 && keysym <= XK_9) vk = keysym;
      break;
  }
  return IKeyPress("", vk);
}

class IGraphicsLinux::Impl {
public:
  Display* mDisplay = nullptr;
  ::Window mWindow = 0;  // Use global namespace Window to avoid X11 conflicts
  ::Window mParentWindow = 0;
  GLXContext mGLContext = nullptr;
  Atom mWmDeleteMessage;
  
  sk_sp<GrDirectContext> mGrContext; 
};

IGraphicsLinux::IGraphicsLinux(IGEditorDelegate& dlg, int w, int h, int fps, float scale)
: IGRAPHICS_DRAW_CLASS(dlg, w, h, fps, scale) 
, mImpl(new Impl) 
{
  XInitThreads();
}

IGraphicsLinux::~IGraphicsLinux() {
  CloseWindow();
  delete mImpl;
}

bool IGraphicsLinux::WindowIsOpen() {
  return mImpl->mWindow != 0;
}

void IGraphicsLinux::PlatformResize(bool mouseOver) {
  // Todo: Check actual window size via XGetGeometry and call OnResize
}

void* IGraphicsLinux::OpenWindow(void* pParent) {
  mImpl->mDisplay = XOpenDisplay(nullptr);
  if (!mImpl->mDisplay) {
    DBGMSG("Cannot connect to X server\n");
    return nullptr;
  }

  int screen = DefaultScreen(mImpl->mDisplay);
  ::Window root = RootWindow(mImpl->mDisplay, screen);

  // 1. Setup GLX Visual - use X11_None instead of None
  GLint att[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, (GLint)X11_None };
  XVisualInfo* vi = glXChooseVisual(mImpl->mDisplay, 0, att);

  if (!vi) {
    DBGMSG("No appropriate visual found\n");
    return nullptr;
  }

  Colormap cmap = XCreateColormap(mImpl->mDisplay, root, vi->visual, AllocNone);

  XSetWindowAttributes swa;
  swa.colormap = cmap;
  swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | 
                   ButtonPressMask | ButtonReleaseMask | 
                   PointerMotionMask | StructureNotifyMask;

  mImpl->mParentWindow = (::Window)pParent;

  if (mImpl->mParentWindow) {
    mImpl->mWindow = XCreateWindow(mImpl->mDisplay, mImpl->mParentWindow, 
                                 0, 0, Width(), Height(), 0, 
                                 vi->depth, InputOutput, vi->visual, 
                                 CWColormap | CWEventMask, &swa);
  } else {
    mImpl->mWindow = XCreateWindow(mImpl->mDisplay, root, 
                                 0, 0, Width(), Height(), 0, 
                                 vi->depth, InputOutput, vi->visual, 
                                 CWColormap | CWEventMask, &swa);
    
    mImpl->mWmDeleteMessage = XInternAtom(mImpl->mDisplay, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(mImpl->mDisplay, mImpl->mWindow, &mImpl->mWmDeleteMessage, 1);
  }

  XMapWindow(mImpl->mDisplay, mImpl->mWindow);
  XStoreName(mImpl->mDisplay, mImpl->mWindow, "iPlug2 Plugin");

  // 2. Create GLX Context
  mImpl->mGLContext = glXCreateContext(mImpl->mDisplay, vi, nullptr, GL_TRUE);
  XFree(vi);

  // 3. Activate Context & Init Skia
  ActivateGLContext();
  
  auto interface = GrGLMakeNativeInterface();
  if (!interface) {
    DBGMSG("Failed to create Skia GL interface\n");
  } else {
    mImpl->mGrContext = GrDirectContexts::MakeGL(interface);
  }

  // 4. Critical iPlug2 Initialization Hooks
  GetDelegate()->LayoutUI(this);
  GetDelegate()->OnUIOpen();
  
  OnViewInitialized(mImpl->mGLContext); 

  // 5. Force initial redraw to clear the window
  SetAllControlsDirty();
  IRECTList rects;
  if (IsDirty(rects)) {
    // GL context is already active from ActivateGLContext() call above
    Draw(rects);
    if (mImpl->mDisplay && mImpl->mWindow) {
      glXSwapBuffers(mImpl->mDisplay, mImpl->mWindow);
    }
  }

  // 6. Start the display timer for event processing and redraw
  int intervalMs = 1000 / FPS();
  mTimer = std::unique_ptr<Timer>(Timer::Create(
    [this](Timer& t) { OnDisplayTimer(); }, 
    intervalMs
  ));

  return (void*)mImpl->mWindow;
}

void IGraphicsLinux::CloseWindow() {
  // Stop the display timer first
  if (mTimer) {
    mTimer->Stop();
    mTimer.reset();
  }
  
  if (mImpl->mDisplay) {
    
    OnViewDestroyed(); 

    if (mImpl->mGLContext) {
      mImpl->mGrContext.reset();
      glXMakeCurrent(mImpl->mDisplay, X11_None, nullptr);
      glXDestroyContext(mImpl->mDisplay, mImpl->mGLContext);
      mImpl->mGLContext = nullptr;
    }
    
    if (mImpl->mWindow) {
      XDestroyWindow(mImpl->mDisplay, mImpl->mWindow);
      mImpl->mWindow = 0;
    }
    
    XCloseDisplay(mImpl->mDisplay);
    mImpl->mDisplay = nullptr;
  }
}

void IGraphicsLinux::ActivateGLContext() {
  if(mImpl->mDisplay && mImpl->mWindow && mImpl->mGLContext) {
    glXMakeCurrent(mImpl->mDisplay, mImpl->mWindow, mImpl->mGLContext);
    // Initialize GLAD after context is current
    static bool gladInitialized = false;
    if (!gladInitialized) {
      if (!gladLoadGL()) {
        DBGMSG("Failed to initialize GLAD\n");
      } else {
        gladInitialized = true;
      }
    }
  }
}

void IGraphicsLinux::DeactivateGLContext() {
  if(mImpl->mDisplay) {
    glXMakeCurrent(mImpl->mDisplay, X11_None, nullptr);
  }
}

bool IGraphicsLinux::PlatformProcessEvents() {
  if (!mImpl->mDisplay || !mImpl->mWindow) return false;

  while (XPending(mImpl->mDisplay) > 0) {
    XEvent event;
    XNextEvent(mImpl->mDisplay, &event);
    
    if (event.xany.window != mImpl->mWindow) continue;

    switch (event.type) {
      case Expose:
        if (event.xexpose.count == 0) {
          SetAllControlsDirty();
        }
        break;
        
      case ButtonPress: {
        IMouseMod modifiers;
        unsigned int state = event.xbutton.state;
        if (state & ShiftMask) modifiers.S = true;
        if (state & ControlMask) modifiers.C = true;
        if (state & Mod1Mask) modifiers.A = true;
        if (event.xbutton.button == Button1) modifiers.L = true;
        if (event.xbutton.button == Button3) modifiers.R = true;
        
        std::vector<IMouseInfo> points;
        IMouseInfo info;
        info.x = (float)event.xbutton.x;
        info.y = (float)event.xbutton.y;
        info.ms = modifiers;
        points.push_back(info);
        OnMouseDown(points);
        break;
      }
        
      case ButtonRelease: {
        IMouseMod modifiers;
        unsigned int state = event.xbutton.state;
        if (state & ShiftMask) modifiers.S = true;
        if (state & ControlMask) modifiers.C = true;
        if (state & Mod1Mask) modifiers.A = true;
        // Set button based on which button was released
        if (event.xbutton.button == Button1) modifiers.L = true;
        if (event.xbutton.button == Button3) modifiers.R = true;
        
        std::vector<IMouseInfo> points;
        IMouseInfo info;
        info.x = (float)event.xbutton.x;
        info.y = (float)event.xbutton.y;
        info.ms = modifiers;
        points.push_back(info);
        OnMouseUp(points);
        break;
      }
        
      case MotionNotify: {
        IMouseMod modifiers;
        unsigned int state = event.xmotion.state;
        if (state & ShiftMask) modifiers.S = true;
        if (state & ControlMask) modifiers.C = true;
        if (state & Mod1Mask) modifiers.A = true;
        if (state & Button1Mask) modifiers.L = true;
        if (state & Button3Mask) modifiers.R = true;
        
        // Check if a button is pressed (dragging)
        if (state & (Button1Mask | Button3Mask)) {
          // Get previous mouse position for delta calculation
          static float prevX = 0, prevY = 0;
          float x = (float)event.xmotion.x;
          float y = (float)event.xmotion.y;
          
          std::vector<IMouseInfo> points;
          IMouseInfo info;
          info.x = x;
          info.y = y;
          info.dX = x - prevX;
          info.dY = y - prevY;
          info.ms = modifiers;
          points.push_back(info);
          
          if (!IsInPlatformTextEntry()) {
            OnMouseDrag(points);
          }
          
          prevX = x;
          prevY = y;
        } else {
          // No button pressed, just mouse over
          OnMouseOver((float)event.xmotion.x, (float)event.xmotion.y, modifiers);
        }
        break;
      }
        
      case KeyPress: {
        IMouseMod modifiers;
        unsigned int state = event.xkey.state;
        if (state & ShiftMask) modifiers.S = true;
        if (state & ControlMask) modifiers.C = true;
        if (state & Mod1Mask) modifiers.A = true;
        
        KeySym keysym = XLookupKeysym(&event.xkey, 0);
        IKeyPress kp = XKeyToKeyPress(keysym);
        if (kp.VK) {
          float mx, my;
          GetMouseLocation(mx, my);
          OnKeyDown(mx, my, kp);
        }
        break;
      }
      
      case ClientMessage:
        if ((Atom)event.xclient.data.l[0] == mImpl->mWmDeleteMessage) {
          CloseWindow();
          return false;
        }
        break;
    }
  }
  return true;
}

void IGraphicsLinux::GetMouseLocation(float& x, float& y) const {
  if (!mImpl->mDisplay) return;
  ::Window root, child;
  int rootX, rootY, winX, winY;
  unsigned int mask;
  if (XQueryPointer(mImpl->mDisplay, mImpl->mWindow, &root, &child, &rootX, &rootY, &winX, &winY, &mask)) {
    x = (float)winX;
    y = (float)winY;
  }
}

void IGraphicsLinux::HideMouseCursor(bool hide, bool lock) {
  // Stub
}

void IGraphicsLinux::MoveMouseCursor(float x, float y) {
  if (mImpl->mDisplay && mImpl->mWindow) {
    XWarpPointer(mImpl->mDisplay, X11_None, mImpl->mWindow, 0, 0, 0, 0, (int)x, (int)y);
  }
}

ECursor IGraphicsLinux::SetMouseCursor(ECursor cursorType) {
  return ECursor::ARROW;
}

// Stubs for Required Pure Virtuals
EMsgBoxResult IGraphicsLinux::ShowMessageBox(const char* str, const char* caption, EMsgBoxType type, IMsgBoxCompletionHandlerFunc completionHandler) {
  DBGMSG("MsgBox: %s - %s\n", caption, str);
  if(completionHandler) completionHandler(EMsgBoxResult::kOK);
  return EMsgBoxResult::kOK;
}

void IGraphicsLinux::ForceEndUserEdit() {}

void IGraphicsLinux::PromptForFile(WDL_String& fileName, WDL_String& path, EFileAction action, const char* ext, IFileDialogCompletionHandlerFunc completionHandler) {
  // Stub - would need GTK or another toolkit for file dialogs
  if (completionHandler) completionHandler(fileName, path);
}

void IGraphicsLinux::OnDisplayTimer()
{
  // 1. Process pending X11 events (mouse, keyboard, expose, etc.)
  PlatformProcessEvents();
  
  // 2. Check if any controls need redrawing
  IRECTList rects;
  if (IsDirty(rects)) {
    // Activate GL context before drawing
    ActivateGLContext();
    
    // Trigger redraw (this calls BeginFrame/EndFrame internally)
    Draw(rects);
    
    // Swap GL buffers
    if (mImpl->mDisplay && mImpl->mWindow) {
      glXSwapBuffers(mImpl->mDisplay, mImpl->mWindow);
    }
    
    // Deactivate GL context after drawing
    DeactivateGLContext();
  }
}

// Simple PlatformFont implementation for Linux
class LinuxFont : public PlatformFont
{
public:
  LinuxFont(IFontDataPtr&& data) : PlatformFont(false), mData(std::move(data)) {}
  
  IFontDataPtr GetFontData() override
  {
    if (mData && mData->IsValid())
    {
      // Return a copy of the font data
      return IFontDataPtr(new IFontData(mData->Get(), mData->GetSize(), mData->GetFaceIdx()));
    }
    return IFontDataPtr(new IFontData());
  }
  
private:
  IFontDataPtr mData;
};

PlatformFontPtr IGraphicsLinux::LoadPlatformFont(const char* fontID, const char* fileNameOrResID)
{
  WDL_String fullPath;
  const EResourceLocation fontLocation = LocateResource(fileNameOrResID, "ttf", fullPath, GetBundleID(), nullptr, nullptr);
  
  if (fontLocation == EResourceLocation::kNotFound)
    return nullptr;
  
  if (fontLocation == EResourceLocation::kAbsolutePath)
  {
    FILE* file = fopen(fullPath.Get(), "rb");
    if (!file)
      return nullptr;
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (size <= 0)
    {
      fclose(file);
      return nullptr;
    }
    
    // Read font data
    std::unique_ptr<uint8_t[]> data(new uint8_t[size]);
    size_t bytesRead = fread(data.get(), 1, size, file);
    fclose(file);
    
    if (bytesRead != static_cast<size_t>(size))
      return nullptr;
    
    // Create font data object
    IFontDataPtr fontData(new IFontData(data.get(), size, 0));
    
    if (!fontData->IsValid())
      return nullptr;
    
    return PlatformFontPtr(new LinuxFont(std::move(fontData)));
  }
  
  return nullptr;
}

PlatformFontPtr IGraphicsLinux::LoadPlatformFont(const char* fontID, void* pData, int dataSize)
{
  if (!pData || dataSize <= 0)
    return nullptr;
  
  // Create font data object
  IFontDataPtr fontData(new IFontData(pData, dataSize, 0));
  
  if (!fontData->IsValid())
    return nullptr;
  
  return PlatformFontPtr(new LinuxFont(std::move(fontData)));
}

PlatformFontPtr IGraphicsLinux::LoadPlatformFont(const char* fontID, const char* fontName, ETextStyle style)
{
  // For system fonts, we could use fontconfig to find them
  // For now, return nullptr - system font loading can be added later if needed
  return nullptr;
}
