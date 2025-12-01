/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#include "IGraphicsLinux.h"
#include "IControl.h"

// Key mapping headers
#include <X11/XKBlib.h>
#include <GL/glx.h>

// Skia Headers
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/gl/GrGLInterface.h"

using namespace iplug;
using namespace iplug::igraphics;

// --- Key Mapping Helper ---
static int XKeyToVK(KeySym keysym) {
  switch (keysym) {
    case XK_Return: return kVK_RETURN;
    case XK_Escape: return kVK_ESCAPE;
    case XK_BackSpace: return kVK_BACK;
    case XK_Tab: return kVK_TAB;
    case XK_Left: return kVK_LEFT;
    case XK_Right: return kVK_RIGHT;
    case XK_Up: return kVK_UP;
    case XK_Down: return kVK_DOWN;
    case XK_Shift_L: case XK_Shift_R: return kVK_SHIFT;
    case XK_Control_L: case XK_Control_R: return kVK_CONTROL;
    default:
      if (keysym >= XK_a && keysym <= XK_z) return keysym - 32;
      if (keysym >= XK_0 && keysym <= XK_9) return keysym;
      return 0;
  }
}

class IGraphicsLinux::Impl {
public:
  Display* mDisplay = nullptr;
  Window mWindow = 0;
  Window mParentWindow = 0;
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
    if (mImpl->mDisplay && mImpl->mWindow) {
        // Todo: Check actual window size via XGetGeometry and call OnResize
    }
}

void* IGraphicsLinux::OpenWindow(void* pParent) {
  mImpl->mDisplay = XOpenDisplay(nullptr);
  if (!mImpl->mDisplay) {
    DBGMSG("Cannot connect to X server\n");
    return nullptr;
  }

  int screen = DefaultScreen(mImpl->mDisplay);
  Window root = RootWindow(mImpl->mDisplay, screen);

  // 1. Setup GLX Visual
  GLint att[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None };
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

  mImpl->mParentWindow = (Window)pParent;

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
      mImpl->mGrContext = GrDirectContext::MakeGL(interface);
  }

  // 4. Critical iPlug2 Initialization Hooks
  GetDelegate()->LayoutUI(this);
  GetDelegate()->OnUIOpen();
  
  OnViewInitialized(mImpl->mGLContext); 

  return (void*)mImpl->mWindow;
}

void IGraphicsLinux::CloseWindow() {
  if (mImpl->mDisplay) {
    
    OnViewDestroyed(); 

    if (mImpl->mGLContext) {
      mImpl->mGrContext.reset();
      glXMakeCurrent(mImpl->mDisplay, None, nullptr);
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
  }
}

void IGraphicsLinux::DeactivateGLContext() {
  if(mImpl->mDisplay) {
    glXMakeCurrent(mImpl->mDisplay, None, nullptr);
  }
}

bool IGraphicsLinux::PlatformProcessEvents() {
  if (!mImpl->mDisplay || !mImpl->mWindow) return false;

  while (XPending(mImpl->mDisplay) > 0) {
    XEvent event;
    XNextEvent(mImpl->mDisplay, &event);
    
    if (event.xany.window != mImpl->mWindow) continue;

    IMouseMod modifiers; 
    if (event.xkey.state & ShiftMask) modifiers.S = true;
    if (event.xkey.state & ControlMask) modifiers.C = true;
    if (event.xkey.state & Mod1Mask) modifiers.A = true; 

    // Update global state if iPlug2 has such a requirement, 
    // otherwise pass to specific event handlers.
    
    switch (event.type) {
      case Expose:
        if (event.xexpose.count == 0) Draw(GetDrawRect()); 
        break;
        
      case ButtonPress:
        OnMouseDown({(float)event.xbutton.x, (float)event.xbutton.y}, modifiers);
        break;
        
      case ButtonRelease:
        OnMouseUp({(float)event.xbutton.x, (float)event.xbutton.y}, modifiers);
        break;
        
      case MotionNotify:
        OnMouseOver({(float)event.xmotion.x, (float)event.xmotion.y}, modifiers);
        break;
        
      case KeyPress: {
        KeySym keysym = XLookupKeysym(&event.xkey, 0);
        int vk = XKeyToVK(keysym);
        if (vk) {
            // FIX: Get current mouse location for the key event
            float mx, my;
            GetMouseLocation(mx, my);
            // NOTE: Depending on iPlug2 version, OnKeyDown signature is (x, y, vk)
            // Modifiers are typically tracked globally or via a separate setter.
            OnKeyDown(mx, my, vk); 
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
  Window root, child;
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
       XWarpPointer(mImpl->mDisplay, None, mImpl->mWindow, 0, 0, 0, 0, (int)x, (int)y);
   }
}

ECursor IGraphicsLinux::SetMouseCursor(ECursor cursorType) {
  return ECursor::ARROW;
}

// Stubs for Required Pure Virtuals
EMsgBoxResult IGraphicsLinux::ShowMessageBox(const char* str, const char* caption, EMsgBoxType type, IMsgBoxCompletionHandlerFunc completionHandler) {
  DBGMSG("MsgBox: %s - %s\n", caption, str);
  // Using generic OK. Check if your version requires EMsgBoxResult::OK
  if(completionHandler) completionHandler(kMsgBoxResultOK);
  return kMsgBoxResultOK;
}

void IGraphicsLinux::ForceEndUserEdit() {}

void IGraphicsLinux::PromptForFile(const char* fileName, EFileAction action, const char* ext, IFileDialogCompletionHandlerFunc completionHandler) {
  // Stub
}

void IGraphicsLinux::PromptForColor(IColor& color, const char* str, IColorPickerHandlerFunc completionHandler) {
  // Stub
}