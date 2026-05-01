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
#include <X11/Xatom.h>
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

#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <unistd.h>

using namespace iplug;
using namespace iplug::igraphics;

// --- Key Mapping Helper ---
static void XStateToModifiers(unsigned int state, bool& shift, bool& control, bool& alt)
{
  shift = (state & ShiftMask) != 0;
  control = (state & ControlMask) != 0;
  alt = (state & Mod1Mask) != 0;
}

static int XKeySymToVK(KeySym keysym)
{
  switch (keysym)
  {
    case XK_Return:
    case XK_KP_Enter: return kVK_RETURN;
    case XK_Escape: return kVK_ESCAPE;
    case XK_BackSpace: return kVK_BACK;
    case XK_Tab: return kVK_TAB;
    case XK_Delete: return kVK_DELETE;
    case XK_Left: return kVK_LEFT;
    case XK_Right: return kVK_RIGHT;
    case XK_Up: return kVK_UP;
    case XK_Down: return kVK_DOWN;
    case XK_Page_Up: return kVK_PRIOR;
    case XK_Page_Down: return kVK_NEXT;
    case XK_Home: return kVK_HOME;
    case XK_End: return kVK_END;
    case XK_space: return kVK_SPACE;
    case XK_Shift_L:
    case XK_Shift_R: return kVK_SHIFT;
    case XK_Control_L:
    case XK_Control_R: return kVK_CONTROL;
    case XK_Alt_L:
    case XK_Alt_R:
    case XK_Meta_L:
    case XK_Meta_R: return kVK_MENU;
    case XK_KP_0: return kVK_NUMPAD0;
    case XK_KP_1: return kVK_NUMPAD1;
    case XK_KP_2: return kVK_NUMPAD2;
    case XK_KP_3: return kVK_NUMPAD3;
    case XK_KP_4: return kVK_NUMPAD4;
    case XK_KP_5: return kVK_NUMPAD5;
    case XK_KP_6: return kVK_NUMPAD6;
    case XK_KP_7: return kVK_NUMPAD7;
    case XK_KP_8: return kVK_NUMPAD8;
    case XK_KP_9: return kVK_NUMPAD9;
    default:
      if (keysym >= XK_a && keysym <= XK_z)
        return static_cast<int>('A' + (keysym - XK_a));
      if (keysym >= XK_A && keysym <= XK_Z)
        return static_cast<int>(keysym);
      if (keysym >= XK_0 && keysym <= XK_9)
        return static_cast<int>(keysym);
      return kVK_NONE;
  }
}

static IKeyPress XKeyEventToKeyPress(XIC inputContext, XKeyEvent* event)
{
  char utf8[5] = { 0 };
  char buffer[32] = { 0 };
  KeySym keysym = NoSymbol;
  Status status = 0;

  int len = 0;
  if (inputContext)
  {
    len = Xutf8LookupString(inputContext, event, buffer, sizeof(buffer) - 1, &keysym, &status);
    if (status == XBufferOverflow)
      len = 0;
  }
  else
  {
    len = XLookupString(event, buffer, sizeof(buffer) - 1, &keysym, nullptr);
  }

  if (len > 0)
  {
    buffer[len] = '\0';
    strncpy(utf8, buffer, sizeof(utf8) - 1);
  }

  if (keysym == NoSymbol)
    keysym = XLookupKeysym(event, 0);

  bool shift = false;
  bool control = false;
  bool alt = false;
  XStateToModifiers(event->state, shift, control, alt);

  return IKeyPress(utf8, XKeySymToVK(keysym), shift, control, alt);
}

static bool XdndMessageHasUriList(const XClientMessageEvent& msg, Atom textUriList)
{
  return static_cast<Atom>(msg.data.l[2]) == textUriList ||
         static_cast<Atom>(msg.data.l[3]) == textUriList ||
         static_cast<Atom>(msg.data.l[4]) == textUriList;
}

static bool WindowTypeListHasAtom(Display* display, ::Window source, Atom typeListAtom, Atom wantedAtom)
{
  Atom actualType = 0;
  int actualFormat = 0;
  unsigned long itemCount = 0;
  unsigned long bytesAfter = 0;
  unsigned char* data = nullptr;

  const int result = XGetWindowProperty(display, source, typeListAtom, 0, 1024, False, XA_ATOM,
                                        &actualType, &actualFormat, &itemCount, &bytesAfter, &data);
  if (result != Success || !data)
    return false;

  bool found = false;
  if (actualType == XA_ATOM && actualFormat == 32)
  {
    Atom* atoms = reinterpret_cast<Atom*>(data);
    for (unsigned long i = 0; i < itemCount; ++i)
    {
      if (atoms[i] == wantedAtom)
      {
        found = true;
        break;
      }
    }
  }

  XFree(data);
  return found;
}

static bool ReadSingleLongProperty(Display* display, ::Window window, Atom property, long& value, Atom& actualType, int& actualFormat)
{
  unsigned long itemCount = 0;
  unsigned long bytesAfter = 0;
  unsigned char* data = nullptr;

  const int result = XGetWindowProperty(display, window, property, 0, 1, False, AnyPropertyType,
                                        &actualType, &actualFormat, &itemCount, &bytesAfter, &data);
  if (result != Success || !data)
    return false;

  const bool valid = itemCount == 1 && actualFormat == 32;
  if (valid)
    value = reinterpret_cast<long*>(data)[0];

  XFree(data);
  return valid;
}

static std::vector<::Window> GetAncestorWindows(Display* display, ::Window window)
{
  std::vector<::Window> ancestors;
  if (!display || !window)
    return ancestors;

  ::Window current = window;
  for (;;)
  {
    ::Window root = 0;
    ::Window parent = 0;
    ::Window* children = nullptr;
    unsigned int childCount = 0;

    if (!XQueryTree(display, current, &root, &parent, &children, &childCount))
      break;

    if (children)
      XFree(children);

    if (!parent || parent == root)
      break;

    ancestors.push_back(parent);
    current = parent;
  }

  return ancestors;
}

static int HexNibble(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  return -1;
}

static std::string PercentDecode(const std::string& input)
{
  std::string output;
  output.reserve(input.size());

  for (size_t i = 0; i < input.size(); ++i)
  {
    if (input[i] == '%' && i + 2 < input.size())
    {
      const int hi = HexNibble(input[i + 1]);
      const int lo = HexNibble(input[i + 2]);
      if (hi >= 0 && lo >= 0)
      {
        output.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }

    output.push_back(input[i]);
  }

  return output;
}

static std::vector<std::string> ParseTextUriList(const char* data)
{
  std::vector<std::string> paths;
  if (!data)
    return paths;

  std::string text(data);
  size_t start = 0;
  while (start < text.size())
  {
    size_t end = text.find_first_of("\r\n", start);
    if (end == std::string::npos)
      end = text.size();

    std::string line = text.substr(start, end - start);
    if (!line.empty() && line[0] != '#')
    {
      const char prefix[] = "file://";
      if (line.compare(0, strlen(prefix), prefix) == 0)
      {
        std::string path = line.substr(strlen(prefix));
        if (path.compare(0, 10, "localhost/") == 0)
          path.erase(0, 9);
        paths.push_back(PercentDecode(path));
      }
    }

    start = end + 1;
    while (start < text.size() && (text[start] == '\r' || text[start] == '\n'))
      ++start;
  }

  return paths;
}

class IGraphicsLinux::Impl {
public:
  struct XdndPropertyBackup
  {
    ::Window window = 0;
    bool hadAware = false;
    bool hadProxy = false;
    long awareValue = 0;
    long proxyValue = 0;
    Atom awareType = 0;
    Atom proxyType = 0;
    int awareFormat = 0;
    int proxyFormat = 0;
  };

  Display* mDisplay = nullptr;
  ::Window mWindow = 0;  // Use global namespace Window to avoid X11 conflicts
  ::Window mParentWindow = 0;
  GLXContext mGLContext = nullptr;
  Atom mWmDeleteMessage = 0;
  XIM mInputMethod = nullptr;
  XIC mInputContext = nullptr;
  Atom mClipboardAtom = 0;
  Atom mUtf8StringAtom = 0;
  Atom mTargetsAtom = 0;
  Atom mTextAtom = 0;
  Atom mIPlugClipboardAtom = 0;
  WDL_String mClipboardText;
  Atom mXdndAware = 0;
  Atom mXdndEnter = 0;
  Atom mXdndPosition = 0;
  Atom mXdndStatus = 0;
  Atom mXdndTypeList = 0;
  Atom mXdndActionCopy = 0;
  Atom mXdndDrop = 0;
  Atom mXdndLeave = 0;
  Atom mXdndFinished = 0;
  Atom mXdndProxy = 0;
  Atom mXdndSelection = 0;
  Atom mTextUriList = 0;
  Atom mIPlugXdndProperty = 0;
  ::Window mXdndSource = 0;
  ::Window mXdndTarget = 0;
  bool mXdndAcceptsUriList = false;
  float mXdndDropX = 0.f;
  float mXdndDropY = 0.f;
  std::vector<XdndPropertyBackup> mXdndPropertyBackups;
  
  sk_sp<GrDirectContext> mGrContext;
  
  // Double-click detection
  Time mLastClickTime = 0;
  float mLastClickX = 0;
  float mLastClickY = 0;
  unsigned int mLastClickButton = 0;
  
  // Mouse drag tracking (per-instance, not static)
  float mPrevMouseX = 0;
  float mPrevMouseY = 0;
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

void* IGraphicsLinux::GetWindow() {
  return mImpl ? (void*)mImpl->mWindow : nullptr;
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
                   PointerMotionMask | StructureNotifyMask |
                   FocusChangeMask;

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

  mImpl->mClipboardAtom = XInternAtom(mImpl->mDisplay, "CLIPBOARD", False);
  mImpl->mUtf8StringAtom = XInternAtom(mImpl->mDisplay, "UTF8_STRING", False);
  mImpl->mTargetsAtom = XInternAtom(mImpl->mDisplay, "TARGETS", False);
  mImpl->mTextAtom = XInternAtom(mImpl->mDisplay, "TEXT", False);
  mImpl->mIPlugClipboardAtom = XInternAtom(mImpl->mDisplay, "IPLUG_CLIPBOARD", False);
  mImpl->mXdndAware = XInternAtom(mImpl->mDisplay, "XdndAware", False);
  mImpl->mXdndEnter = XInternAtom(mImpl->mDisplay, "XdndEnter", False);
  mImpl->mXdndPosition = XInternAtom(mImpl->mDisplay, "XdndPosition", False);
  mImpl->mXdndStatus = XInternAtom(mImpl->mDisplay, "XdndStatus", False);
  mImpl->mXdndTypeList = XInternAtom(mImpl->mDisplay, "XdndTypeList", False);
  mImpl->mXdndActionCopy = XInternAtom(mImpl->mDisplay, "XdndActionCopy", False);
  mImpl->mXdndDrop = XInternAtom(mImpl->mDisplay, "XdndDrop", False);
  mImpl->mXdndLeave = XInternAtom(mImpl->mDisplay, "XdndLeave", False);
  mImpl->mXdndFinished = XInternAtom(mImpl->mDisplay, "XdndFinished", False);
  mImpl->mXdndProxy = XInternAtom(mImpl->mDisplay, "XdndProxy", False);
  mImpl->mXdndSelection = XInternAtom(mImpl->mDisplay, "XdndSelection", False);
  mImpl->mTextUriList = XInternAtom(mImpl->mDisplay, "text/uri-list", False);
  mImpl->mIPlugXdndProperty = XInternAtom(mImpl->mDisplay, "IPLUG_XDND_SELECTION", False);

  long xdndVersion = 5;
  XChangeProperty(mImpl->mDisplay, mImpl->mWindow, mImpl->mXdndAware,
                  XA_ATOM, 32, PropModeReplace,
                  reinterpret_cast<unsigned char*>(&xdndVersion), 1);
  long xdndProxyWindow = static_cast<long>(mImpl->mWindow);
  XChangeProperty(mImpl->mDisplay, mImpl->mWindow, mImpl->mXdndProxy,
                  XA_WINDOW, 32, PropModeReplace,
                  reinterpret_cast<unsigned char*>(&xdndProxyWindow), 1);

  RegisterXdndProxyWindows();

  XSetLocaleModifiers("");
  mImpl->mInputMethod = XOpenIM(mImpl->mDisplay, nullptr, nullptr, nullptr);
  if (mImpl->mInputMethod)
  {
    mImpl->mInputContext = XCreateIC(mImpl->mInputMethod,
                                     XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                                     XNClientWindow, mImpl->mWindow,
                                     XNFocusWindow, mImpl->mWindow,
                                     nullptr);
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
      RestoreParentXdndProperties();

      if (mImpl->mInputContext) {
        XDestroyIC(mImpl->mInputContext);
        mImpl->mInputContext = nullptr;
      }

      if (mImpl->mInputMethod) {
        XCloseIM(mImpl->mInputMethod);
        mImpl->mInputMethod = nullptr;
      }

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

    if (!HandleXEvent(event))
      return false;
  }
  return true;
}

bool IGraphicsLinux::HandleXEvent(const XEvent& event)
{
  if (event.xany.window != mImpl->mWindow)
  {
    if (event.type == ClientMessage &&
        (event.xclient.message_type == mImpl->mXdndEnter ||
         event.xclient.message_type == mImpl->mXdndPosition ||
         event.xclient.message_type == mImpl->mXdndDrop ||
         event.xclient.message_type == mImpl->mXdndLeave))
    {
      if (event.xclient.message_type == mImpl->mXdndEnter)
        HandleXdndEnter(event.xclient);
      else if (event.xclient.message_type == mImpl->mXdndPosition)
        HandleXdndPosition(event.xclient);
      else if (event.xclient.message_type == mImpl->mXdndDrop)
        HandleXdndDrop(event.xclient);
      else if (event.xclient.message_type == mImpl->mXdndLeave)
        HandleXdndLeave(event.xclient);
    }
    return true;
  }

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

      float x = (float)event.xbutton.x;
      float y = (float)event.xbutton.y;

      // Double-click detection (250ms threshold, 5 pixel tolerance)
      Time clickTime = event.xbutton.time;
      bool isDoubleClick = false;

      if (event.xbutton.button == mImpl->mLastClickButton &&
          (clickTime - mImpl->mLastClickTime) < 250 &&
          (x - mImpl->mLastClickX) > -5.f && (x - mImpl->mLastClickX) < 5.f &&
          (y - mImpl->mLastClickY) > -5.f && (y - mImpl->mLastClickY) < 5.f)
      {
        isDoubleClick = true;
        mImpl->mLastClickTime = 0; // Reset to prevent triple-click as double
      }
      else
      {
        mImpl->mLastClickTime = clickTime;
        mImpl->mLastClickX = x;
        mImpl->mLastClickY = y;
        mImpl->mLastClickButton = event.xbutton.button;
      }

      // Initialize drag tracking position for smooth first drag
      mImpl->mPrevMouseX = x;
      mImpl->mPrevMouseY = y;

      if (isDoubleClick) {
        OnMouseDblClick(x, y, modifiers);
      } else {
        std::vector<IMouseInfo> points;
        IMouseInfo info;
        info.x = x;
        info.y = y;
        info.ms = modifiers;
        points.push_back(info);
        OnMouseDown(points);
      }
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
        float x = (float)event.xmotion.x;
        float y = (float)event.xmotion.y;

        std::vector<IMouseInfo> points;
        IMouseInfo info;
        info.x = x;
        info.y = y;
        info.dX = x - mImpl->mPrevMouseX;
        info.dY = y - mImpl->mPrevMouseY;
        info.ms = modifiers;
        points.push_back(info);

        if (!IsInPlatformTextEntry()) {
          OnMouseDrag(points);
        }

        mImpl->mPrevMouseX = x;
        mImpl->mPrevMouseY = y;
      } else {
        // No button pressed, just mouse over
        OnMouseOver((float)event.xmotion.x, (float)event.xmotion.y, modifiers);
      }
      break;
    }

    case KeyPress: {
      XKeyEvent keyEvent = event.xkey;
      IKeyPress kp = XKeyEventToKeyPress(mImpl->mInputContext, &keyEvent);
      if (kp.VK != kVK_NONE || kp.utf8[0] != '\0') {
        float mx, my;
        GetMouseLocation(mx, my);
        OnKeyDown(mx, my, kp);
      }
      break;
    }

    case KeyRelease: {
      XKeyEvent keyEvent = event.xkey;
      IKeyPress kp = XKeyEventToKeyPress(mImpl->mInputContext, &keyEvent);
      if (kp.VK != kVK_NONE || kp.utf8[0] != '\0') {
        float mx, my;
        GetMouseLocation(mx, my);
        OnKeyUp(mx, my, kp);
      }
      break;
    }

    case FocusIn:
      if (mImpl->mInputContext)
        XSetICFocus(mImpl->mInputContext);
      break;

    case FocusOut:
      if (mImpl->mInputContext)
        XUnsetICFocus(mImpl->mInputContext);
      break;

    case SelectionRequest:
      HandleSelectionRequest(event.xselectionrequest);
      break;

    case SelectionNotify:
      if (event.xselection.selection == mImpl->mXdndSelection)
        HandleXdndSelectionNotify(event.xselection);
      break;

    case ClientMessage:
      if (event.xclient.message_type == mImpl->mXdndEnter) {
        HandleXdndEnter(event.xclient);
        break;
      }
      if (event.xclient.message_type == mImpl->mXdndPosition) {
        HandleXdndPosition(event.xclient);
        break;
      }
      if (event.xclient.message_type == mImpl->mXdndDrop) {
        HandleXdndDrop(event.xclient);
        break;
      }
      if (event.xclient.message_type == mImpl->mXdndLeave) {
        HandleXdndLeave(event.xclient);
        break;
      }
      if ((Atom)event.xclient.data.l[0] == mImpl->mWmDeleteMessage) {
        CloseWindow();
        return false;
      }
      break;
  }

  return true;
}

void IGraphicsLinux::HandleSelectionRequest(const XSelectionRequestEvent& event)
{
  XSelectionEvent sel = {};
  sel.type = SelectionNotify;
  sel.display = event.display;
  sel.requestor = event.requestor;
  sel.selection = event.selection;
  sel.target = event.target;
  sel.time = event.time;
  sel.property = static_cast<Atom>(X11_None);

  const Atom property = event.property != static_cast<Atom>(X11_None) ? event.property : event.target;

  if (event.selection == mImpl->mClipboardAtom && event.target == mImpl->mTargetsAtom)
  {
    Atom targets[] = { mImpl->mTargetsAtom, mImpl->mUtf8StringAtom, XA_STRING, mImpl->mTextAtom };
    XChangeProperty(mImpl->mDisplay, event.requestor, property, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(targets), sizeof(targets) / sizeof(targets[0]));
    sel.property = property;
  }
  else if (event.selection == mImpl->mClipboardAtom &&
           (event.target == mImpl->mUtf8StringAtom || event.target == XA_STRING || event.target == mImpl->mTextAtom))
  {
    const char* text = mImpl->mClipboardText.Get();
    XChangeProperty(mImpl->mDisplay, event.requestor, property, event.target, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(text), strlen(text));
    sel.property = property;
  }

  XSendEvent(mImpl->mDisplay, event.requestor, False, 0, reinterpret_cast<XEvent*>(&sel));
  XFlush(mImpl->mDisplay);
}

void IGraphicsLinux::HandleXdndEnter(const XClientMessageEvent& event)
{
  mImpl->mXdndSource = static_cast<::Window>(event.data.l[0]);
  mImpl->mXdndTarget = event.window ? event.window : mImpl->mWindow;
  const bool hasMoreTypes = (event.data.l[1] & 1) != 0;
  mImpl->mXdndAcceptsUriList = false;

  if (hasMoreTypes)
  {
    mImpl->mXdndAcceptsUriList = WindowTypeListHasAtom(
      mImpl->mDisplay, mImpl->mXdndSource, mImpl->mXdndTypeList, mImpl->mTextUriList);
  }
  else
  {
    mImpl->mXdndAcceptsUriList = XdndMessageHasUriList(event, mImpl->mTextUriList);
  }

}

void IGraphicsLinux::HandleXdndPosition(const XClientMessageEvent& event)
{
  if (!mImpl->mDisplay || !mImpl->mWindow)
    return;

  mImpl->mXdndSource = static_cast<::Window>(event.data.l[0]);
  if (event.window)
    mImpl->mXdndTarget = event.window;

  const int rootX = static_cast<int>((event.data.l[2] >> 16) & 0xffff);
  const int rootY = static_cast<int>(event.data.l[2] & 0xffff);

  ::Window child = 0;
  int winX = 0;
  int winY = 0;
  XTranslateCoordinates(mImpl->mDisplay, DefaultRootWindow(mImpl->mDisplay),
                        mImpl->mWindow, rootX, rootY, &winX, &winY, &child);

  const float scale = GetTotalScale();
  mImpl->mXdndDropX = static_cast<float>(winX) / scale;
  mImpl->mXdndDropY = static_cast<float>(winY) / scale;

  XClientMessageEvent reply = {};
  reply.type = ClientMessage;
  reply.display = mImpl->mDisplay;
  reply.window = mImpl->mXdndSource;
  reply.message_type = mImpl->mXdndStatus;
  reply.format = 32;
  reply.data.l[0] = mImpl->mXdndTarget ? mImpl->mXdndTarget : mImpl->mWindow;
  reply.data.l[1] = mImpl->mXdndAcceptsUriList ? 1 : 0;
  reply.data.l[2] = 0;
  reply.data.l[3] = 0;
  reply.data.l[4] = mImpl->mXdndAcceptsUriList ? mImpl->mXdndActionCopy : X11_None;

  XSendEvent(mImpl->mDisplay, mImpl->mXdndSource, False, NoEventMask, reinterpret_cast<XEvent*>(&reply));
  XFlush(mImpl->mDisplay);
}

void IGraphicsLinux::HandleXdndDrop(const XClientMessageEvent& event)
{
  if (!mImpl->mXdndAcceptsUriList)
  {
    XClientMessageEvent finished = {};
    finished.type = ClientMessage;
    finished.display = mImpl->mDisplay;
    finished.window = mImpl->mXdndSource;
    finished.message_type = mImpl->mXdndFinished;
    finished.format = 32;
    finished.data.l[0] = mImpl->mXdndTarget ? mImpl->mXdndTarget : mImpl->mWindow;
    finished.data.l[1] = 0;
    finished.data.l[2] = X11_None;
    XSendEvent(mImpl->mDisplay, mImpl->mXdndSource, False, NoEventMask, reinterpret_cast<XEvent*>(&finished));
    XFlush(mImpl->mDisplay);
    return;
  }

  XConvertSelection(mImpl->mDisplay, mImpl->mXdndSelection, mImpl->mTextUriList,
                    mImpl->mIPlugXdndProperty, mImpl->mWindow,
                    static_cast<Time>(event.data.l[2]));
  XFlush(mImpl->mDisplay);
}

void IGraphicsLinux::HandleXdndLeave(const XClientMessageEvent& event)
{
  if (static_cast<::Window>(event.data.l[0]) != mImpl->mXdndSource)
    return;

  mImpl->mXdndSource = 0;
  mImpl->mXdndTarget = 0;
  mImpl->mXdndAcceptsUriList = false;
}

void IGraphicsLinux::HandleXdndSelectionNotify(const XSelectionEvent& event)
{
  bool success = false;

  if (event.property != static_cast<Atom>(X11_None))
  {
    Atom actualType = 0;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char* data = nullptr;

    const int result = XGetWindowProperty(mImpl->mDisplay, mImpl->mWindow, event.property,
                                          0, 1024 * 1024, True, AnyPropertyType,
                                          &actualType, &actualFormat, &itemCount, &bytesAfter, &data);
    if (result == Success && data && actualFormat == 8)
    {
      std::string payload(reinterpret_cast<const char*>(data), itemCount);
      std::vector<std::string> paths = ParseTextUriList(payload.c_str());

      if (paths.size() == 1)
      {
        OnDrop(paths[0].c_str(), mImpl->mXdndDropX, mImpl->mXdndDropY);
        success = true;
      }
      else if (paths.size() > 1)
      {
        std::vector<const char*> pathPtrs;
        pathPtrs.reserve(paths.size());
        for (const std::string& path : paths)
          pathPtrs.push_back(path.c_str());
        OnDropMultiple(pathPtrs, mImpl->mXdndDropX, mImpl->mXdndDropY);
        success = true;
      }
    }

    if (data)
      XFree(data);
  }

  XClientMessageEvent finished = {};
  finished.type = ClientMessage;
  finished.display = mImpl->mDisplay;
  finished.window = mImpl->mXdndSource;
  finished.message_type = mImpl->mXdndFinished;
  finished.format = 32;
  finished.data.l[0] = mImpl->mXdndTarget ? mImpl->mXdndTarget : mImpl->mWindow;
  finished.data.l[1] = success ? 1 : 0;
  finished.data.l[2] = success ? mImpl->mXdndActionCopy : X11_None;
  XSendEvent(mImpl->mDisplay, mImpl->mXdndSource, False, NoEventMask, reinterpret_cast<XEvent*>(&finished));
  XFlush(mImpl->mDisplay);
}

void IGraphicsLinux::RegisterXdndProxyWindows()
{
  if (!mImpl->mDisplay || !mImpl->mWindow)
    return;

  long xdndVersion = 5;
  long xdndProxyWindow = static_cast<long>(mImpl->mWindow);
  const std::vector<::Window> ancestors = GetAncestorWindows(mImpl->mDisplay, mImpl->mWindow);

  mImpl->mXdndPropertyBackups.clear();
  mImpl->mXdndPropertyBackups.reserve(ancestors.size());

  for (::Window window : ancestors)
  {
    IGraphicsLinux::Impl::XdndPropertyBackup backup;
    backup.window = window;
    backup.hadAware = ReadSingleLongProperty(mImpl->mDisplay, window, mImpl->mXdndAware,
                                             backup.awareValue, backup.awareType, backup.awareFormat);
    backup.hadProxy = ReadSingleLongProperty(mImpl->mDisplay, window, mImpl->mXdndProxy,
                                             backup.proxyValue, backup.proxyType, backup.proxyFormat);
    mImpl->mXdndPropertyBackups.push_back(backup);

    XChangeProperty(mImpl->mDisplay, window, mImpl->mXdndAware,
                    XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&xdndVersion), 1);
    XChangeProperty(mImpl->mDisplay, window, mImpl->mXdndProxy,
                    XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&xdndProxyWindow), 1);

  }

  XFlush(mImpl->mDisplay);
}

void IGraphicsLinux::RestoreParentXdndProperties()
{
  if (!mImpl->mDisplay)
    return;

  for (auto it = mImpl->mXdndPropertyBackups.rbegin(); it != mImpl->mXdndPropertyBackups.rend(); ++it)
  {
    const IGraphicsLinux::Impl::XdndPropertyBackup& backup = *it;

    if (backup.hadAware)
    {
      XChangeProperty(mImpl->mDisplay, backup.window, mImpl->mXdndAware,
                      backup.awareType, backup.awareFormat, PropModeReplace,
                      reinterpret_cast<const unsigned char*>(&backup.awareValue), 1);
    }
    else
    {
      XDeleteProperty(mImpl->mDisplay, backup.window, mImpl->mXdndAware);
    }

    if (backup.hadProxy)
    {
      XChangeProperty(mImpl->mDisplay, backup.window, mImpl->mXdndProxy,
                      backup.proxyType, backup.proxyFormat, PropModeReplace,
                      reinterpret_cast<const unsigned char*>(&backup.proxyValue), 1);
    }
    else
    {
      XDeleteProperty(mImpl->mDisplay, backup.window, mImpl->mXdndProxy);
    }

  }

  XFlush(mImpl->mDisplay);
  mImpl->mXdndPropertyBackups.clear();
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

bool IGraphicsLinux::SetTextInClipboard(const char* str)
{
  if (!mImpl->mDisplay || !mImpl->mWindow || !str)
    return false;

  mImpl->mClipboardText.Set(str);
  XSetSelectionOwner(mImpl->mDisplay, mImpl->mClipboardAtom, mImpl->mWindow, CurrentTime);
  return XGetSelectionOwner(mImpl->mDisplay, mImpl->mClipboardAtom) == mImpl->mWindow;
}

bool IGraphicsLinux::GetTextFromClipboard(WDL_String& str)
{
  if (!mImpl->mDisplay || !mImpl->mWindow)
    return false;

  ::Window owner = XGetSelectionOwner(mImpl->mDisplay, mImpl->mClipboardAtom);
  if (owner == static_cast<::Window>(X11_None))
    return false;

  if (owner == mImpl->mWindow)
  {
    str.Set(mImpl->mClipboardText.Get());
    return true;
  }

  XConvertSelection(mImpl->mDisplay, mImpl->mClipboardAtom, mImpl->mUtf8StringAtom,
                    mImpl->mIPlugClipboardAtom, mImpl->mWindow, CurrentTime);
  XFlush(mImpl->mDisplay);

  for (int i = 0; i < 100; ++i)
  {
    while (XPending(mImpl->mDisplay))
    {
      XEvent event;
      XNextEvent(mImpl->mDisplay, &event);

      if (event.type == SelectionNotify && event.xselection.selection == mImpl->mClipboardAtom)
      {
        if (event.xselection.property == static_cast<Atom>(X11_None))
          return false;

        Atom type = 0;
        int format = 0;
        unsigned long nitems = 0;
        unsigned long bytesAfter = 0;
        unsigned char* data = nullptr;

        const int result = XGetWindowProperty(mImpl->mDisplay, mImpl->mWindow, mImpl->mIPlugClipboardAtom,
                                              0, 1024 * 1024, True, AnyPropertyType,
                                              &type, &format, &nitems, &bytesAfter, &data);
        if (result == Success && data && format == 8)
        {
          str.Set(reinterpret_cast<const char*>(data), static_cast<int>(nitems));
          XFree(data);
          return true;
        }

        if (data)
          XFree(data);
        return false;
      }

      if (!HandleXEvent(event))
        return false;
    }

    usleep(1000);
  }

  return false;
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
