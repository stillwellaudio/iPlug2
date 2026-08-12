/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for more info.

 ==============================================================================
*/

#include "glad/glad.h"
#define GLX_GLXEXT_LEGACY
#include <GL/glx.h>
#undef GLX_GLXEXT_LEGACY

#include "IGraphicsLinux.h"
#include "IGraphicsLinuxInput.h"

#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/cursorfont.h>

#include "IControl.h"
#include "ITextEntryControl.h"
#include "IPlugPaths.h"
#include "include/gpu/ganesh/GrDirectContext.h"

#include <fontconfig/fontconfig.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

extern char** environ;

using namespace iplug;
using namespace iplug::igraphics;

namespace {

constexpr long kX11None = 0L;

IMouseMod MouseModifiers(unsigned int state, unsigned int button = 0)
{
  IMouseMod modifiers;
  modifiers.S = (state & ShiftMask) != 0;
  modifiers.C = (state & ControlMask) != 0;
  modifiers.A = (state & Mod1Mask) != 0;
  modifiers.L = button == Button1 || (state & Button1Mask) != 0;
  modifiers.R = button == Button3 || (state & Button3Mask) != 0;
  return modifiers;
}

int XKeySymToVK(KeySym keySymbol)
{
  switch (keySymbol)
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
      if (keySymbol >= XK_a && keySymbol <= XK_z)
        return static_cast<int>('A' + (keySymbol - XK_a));
      if (keySymbol >= XK_A && keySymbol <= XK_Z)
        return static_cast<int>(keySymbol);
      if (keySymbol >= XK_0 && keySymbol <= XK_9)
        return static_cast<int>(keySymbol);
      return kVK_NONE;
  }
}

IKeyPress XKeyEventToKeyPress(XIC inputContext, XKeyEvent* event)
{
  char utf8[5] {};
  char buffer[32] {};
  KeySym keySymbol = NoSymbol;
  Status status = 0;

  int length = 0;
  if (inputContext)
  {
    length = Xutf8LookupString(
      inputContext, event, buffer, sizeof(buffer) - 1, &keySymbol, &status);
    if (status == XBufferOverflow)
      length = 0;
  }
  else
  {
    length = XLookupString(event, buffer, sizeof(buffer) - 1, &keySymbol, nullptr);
  }

  if (length > 0)
  {
    buffer[std::min<int>(length, sizeof(buffer) - 1)] = '\0';
    std::strncpy(utf8, buffer, sizeof(utf8) - 1);
  }

  if (keySymbol == NoSymbol)
    keySymbol = XLookupKeysym(event, 0);

  const auto modifiers = MouseModifiers(event->state);
  return IKeyPress(utf8, XKeySymToVK(keySymbol), modifiers.S, modifiers.C, modifiers.A);
}

bool XdndMessageHasUriList(const XClientMessageEvent& message, Atom textUriList)
{
  return static_cast<Atom>(message.data.l[2]) == textUriList
      || static_cast<Atom>(message.data.l[3]) == textUriList
      || static_cast<Atom>(message.data.l[4]) == textUriList;
}

bool WindowTypeListHasAtom(
  Display* display, ::Window source, Atom typeListAtom, Atom wantedAtom)
{
  Atom actualType = 0;
  int actualFormat = 0;
  unsigned long itemCount = 0;
  unsigned long bytesAfter = 0;
  unsigned char* data = nullptr;

  const int result = XGetWindowProperty(
    display,
    source,
    typeListAtom,
    0,
    1024,
    False,
    XA_ATOM,
    &actualType,
    &actualFormat,
    &itemCount,
    &bytesAfter,
    &data);
  if (result != Success || !data)
    return false;

  bool found = false;
  if (actualType == XA_ATOM && actualFormat == 32 && bytesAfter == 0)
  {
    const auto* atoms = reinterpret_cast<Atom*>(data);
    for (unsigned long index = 0; index < itemCount; ++index)
    {
      if (atoms[index] == wantedAtom)
      {
        found = true;
        break;
      }
    }
  }

  XFree(data);
  return found;
}

bool ReadSingleLongProperty(
  Display* display,
  ::Window window,
  Atom property,
  long& value,
  Atom& actualType,
  int& actualFormat)
{
  unsigned long itemCount = 0;
  unsigned long bytesAfter = 0;
  unsigned char* data = nullptr;
  const int result = XGetWindowProperty(
    display,
    window,
    property,
    0,
    1,
    False,
    AnyPropertyType,
    &actualType,
    &actualFormat,
    &itemCount,
    &bytesAfter,
    &data);
  if (result != Success || !data)
    return false;

  const bool valid = itemCount == 1 && actualFormat == 32;
  if (valid)
    value = reinterpret_cast<long*>(data)[0];

  XFree(data);
  return valid;
}

std::vector<::Window> GetAncestorWindows(Display* display, ::Window window)
{
  std::vector<::Window> ancestors;
  std::unordered_set<::Window> visited;
  ::Window current = window;

  for (int depth = 0; display && current && depth < 64; ++depth)
  {
    if (!visited.insert(current).second)
      break;

    ::Window root = 0;
    ::Window parent = 0;
    ::Window* children = nullptr;
    unsigned int childCount = 0;
    if (!XQueryTree(display, current, &root, &parent, &children, &childCount))
      break;

    if (children)
      XFree(children);
    if (!parent || parent == root || parent == current)
      break;

    ancestors.push_back(parent);
    current = parent;
  }

  return ancestors;
}

unsigned int XCursorShape(ECursor cursor)
{
  switch (cursor)
  {
    case ECursor::IBEAM: return XC_xterm;
    case ECursor::WAIT: return XC_watch;
    case ECursor::CROSS: return XC_crosshair;
    case ECursor::UPARROW: return XC_sb_up_arrow;
    case ECursor::SIZENWSE: return XC_bottom_right_corner;
    case ECursor::SIZENESW: return XC_bottom_left_corner;
    case ECursor::SIZEWE: return XC_sb_h_double_arrow;
    case ECursor::SIZENS: return XC_sb_v_double_arrow;
    case ECursor::SIZEALL: return XC_fleur;
    case ECursor::INO: return XC_X_cursor;
    case ECursor::HAND: return XC_hand2;
    case ECursor::APPSTARTING: return XC_watch;
    case ECursor::HELP: return XC_question_arrow;
    case ECursor::ARROW:
    default: return XC_left_ptr;
  }
}

} // namespace

class IGraphicsLinux::Impl
{
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

  std::recursive_mutex mutex;
  Display* display = nullptr;
  ::Window window = 0;
  ::Window parentWindow = 0;
  Colormap colormap = 0;
  GLXContext glContext = nullptr;
  bool viewInitialized = false;
  bool topLevelFallback = false;
  Atom wmDeleteMessage = 0;
  XIM inputMethod = nullptr;
  XIC inputContext = nullptr;
  Cursor cursor = 0;
  Cursor hiddenCursor = 0;
  Atom clipboardAtom = 0;
  Atom utf8StringAtom = 0;
  Atom targetsAtom = 0;
  Atom textAtom = 0;
  Atom iPlugClipboardAtom = 0;
  WDL_String clipboardText;
  Atom xdndAware = 0;
  Atom xdndEnter = 0;
  Atom xdndPosition = 0;
  Atom xdndStatus = 0;
  Atom xdndTypeList = 0;
  Atom xdndActionCopy = 0;
  Atom xdndDrop = 0;
  Atom xdndLeave = 0;
  Atom xdndFinished = 0;
  Atom xdndProxy = 0;
  Atom xdndSelection = 0;
  Atom textUriList = 0;
  Atom iPlugXdndProperty = 0;
  ::Window xdndSource = 0;
  ::Window xdndTarget = 0;
  bool xdndAcceptsUriList = false;
  float xdndDropX = 0.f;
  float xdndDropY = 0.f;
  std::vector<XdndPropertyBackup> xdndPropertyBackups;
  Time lastClickTime = 0;
  float lastClickX = 0.f;
  float lastClickY = 0.f;
  unsigned int lastClickButton = 0;
  float previousMouseX = 0.f;
  float previousMouseY = 0.f;
};

IGraphicsLinux::IGraphicsLinux(
  IGEditorDelegate& delegate, int width, int height, int fps, float scale)
: IGRAPHICS_DRAW_CLASS(delegate, width, height, fps, scale)
, mImpl(std::make_unique<Impl>())
{
  static std::once_flag xlibThreads;
  std::call_once(xlibThreads, [] { XInitThreads(); });
  AttachPopupMenuControl();
  AttachTextEntryControl();
}

IGraphicsLinux::~IGraphicsLinux()
{
  CloseWindow();
}

bool IGraphicsLinux::WindowIsOpen()
{
  std::lock_guard<std::recursive_mutex> lock(mImpl->mutex);
  return mImpl->window != 0;
}

void* IGraphicsLinux::GetWindow()
{
  std::lock_guard<std::recursive_mutex> lock(mImpl->mutex);
  return reinterpret_cast<void*>(mImpl->window);
}

void IGraphicsLinux::PlatformResize(bool parentHasResized)
{
  (void) parentHasResized;
  std::lock_guard<std::recursive_mutex> lock(mImpl->mutex);
  if (mImpl->display && mImpl->window)
  {
    const auto width = static_cast<unsigned int>(
      std::max(1.f, static_cast<float>(WindowWidth()) * GetScreenScale()));
    const auto height = static_cast<unsigned int>(
      std::max(1.f, static_cast<float>(WindowHeight()) * GetScreenScale()));
    XResizeWindow(
      mImpl->display,
      mImpl->window,
      width,
      height);
    XFlush(mImpl->display);
  }
}

void* IGraphicsLinux::OpenWindow(void* parent)
{
  CloseWindow();
  std::lock_guard<std::recursive_mutex> lock(mImpl->mutex);

  mImpl->display = XOpenDisplay(nullptr);
  if (!mImpl->display)
    return nullptr;

  const int screen = DefaultScreen(mImpl->display);
  const ::Window root = RootWindow(mImpl->display, screen);
  GLint attributes[] = {GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, 0};
  XVisualInfo* visual = glXChooseVisual(mImpl->display, screen, attributes);
  if (!visual)
  {
    CloseWindow();
    return nullptr;
  }

  mImpl->colormap = XCreateColormap(mImpl->display, root, visual->visual, AllocNone);
  XSetWindowAttributes windowAttributes {};
  windowAttributes.colormap = mImpl->colormap;
  windowAttributes.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask
                              | ButtonPressMask | ButtonReleaseMask
                              | PointerMotionMask | StructureNotifyMask
                              | FocusChangeMask;

  mImpl->parentWindow = static_cast<::Window>(reinterpret_cast<uintptr_t>(parent));
  mImpl->topLevelFallback = mImpl->parentWindow == 0;
  const ::Window x11Parent = mImpl->parentWindow ? mImpl->parentWindow : root;
  const auto windowWidth = static_cast<unsigned int>(
    std::max(1.f, static_cast<float>(WindowWidth()) * GetScreenScale()));
  const auto windowHeight = static_cast<unsigned int>(
    std::max(1.f, static_cast<float>(WindowHeight()) * GetScreenScale()));
  GetDelegate()->EditorResizeFromUI(
    static_cast<int>(windowWidth), static_cast<int>(windowHeight), true);
  mImpl->window = XCreateWindow(
    mImpl->display,
    x11Parent,
    0,
    0,
    windowWidth,
    windowHeight,
    0,
    visual->depth,
    InputOutput,
    visual->visual,
    CWColormap | CWEventMask,
    &windowAttributes);
  if (!mImpl->window)
  {
    XFree(visual);
    CloseWindow();
    return nullptr;
  }

  if (mImpl->topLevelFallback)
  {
    mImpl->wmDeleteMessage = XInternAtom(mImpl->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(mImpl->display, mImpl->window, &mImpl->wmDeleteMessage, 1);
  }

  mImpl->clipboardAtom = XInternAtom(mImpl->display, "CLIPBOARD", False);
  mImpl->utf8StringAtom = XInternAtom(mImpl->display, "UTF8_STRING", False);
  mImpl->targetsAtom = XInternAtom(mImpl->display, "TARGETS", False);
  mImpl->textAtom = XInternAtom(mImpl->display, "TEXT", False);
  mImpl->iPlugClipboardAtom = XInternAtom(mImpl->display, "IPLUG_CLIPBOARD", False);
  mImpl->xdndAware = XInternAtom(mImpl->display, "XdndAware", False);
  mImpl->xdndEnter = XInternAtom(mImpl->display, "XdndEnter", False);
  mImpl->xdndPosition = XInternAtom(mImpl->display, "XdndPosition", False);
  mImpl->xdndStatus = XInternAtom(mImpl->display, "XdndStatus", False);
  mImpl->xdndTypeList = XInternAtom(mImpl->display, "XdndTypeList", False);
  mImpl->xdndActionCopy = XInternAtom(mImpl->display, "XdndActionCopy", False);
  mImpl->xdndDrop = XInternAtom(mImpl->display, "XdndDrop", False);
  mImpl->xdndLeave = XInternAtom(mImpl->display, "XdndLeave", False);
  mImpl->xdndFinished = XInternAtom(mImpl->display, "XdndFinished", False);
  mImpl->xdndProxy = XInternAtom(mImpl->display, "XdndProxy", False);
  mImpl->xdndSelection = XInternAtom(mImpl->display, "XdndSelection", False);
  mImpl->textUriList = XInternAtom(mImpl->display, "text/uri-list", False);
  mImpl->iPlugXdndProperty = XInternAtom(mImpl->display, "IPLUG_XDND_SELECTION", False);

  long xdndVersion = 5;
  XChangeProperty(
    mImpl->display,
    mImpl->window,
    mImpl->xdndAware,
    XA_ATOM,
    32,
    PropModeReplace,
    reinterpret_cast<unsigned char*>(&xdndVersion),
    1);
  long xdndProxyWindow = static_cast<long>(mImpl->window);
  XChangeProperty(
    mImpl->display,
    mImpl->window,
    mImpl->xdndProxy,
    XA_WINDOW,
    32,
    PropModeReplace,
    reinterpret_cast<unsigned char*>(&xdndProxyWindow),
    1);
  RegisterXdndProxyWindows();

  XSetLocaleModifiers("");
  mImpl->inputMethod = XOpenIM(mImpl->display, nullptr, nullptr, nullptr);
  if (mImpl->inputMethod)
  {
    mImpl->inputContext = XCreateIC(
      mImpl->inputMethod,
      XNInputStyle,
      XIMPreeditNothing | XIMStatusNothing,
      XNClientWindow,
      mImpl->window,
      XNFocusWindow,
      mImpl->window,
      nullptr);
  }

  mImpl->glContext = glXCreateContext(mImpl->display, visual, nullptr, GL_TRUE);
  XFree(visual);
  if (!mImpl->glContext)
  {
    CloseWindow();
    return nullptr;
  }

  XMapWindow(mImpl->display, mImpl->window);
  XStoreName(mImpl->display, mImpl->window, "iPlug2 Plugin");
  ActivateGLContext();
  if (!gladLoadGL())
  {
    CloseWindow();
    return nullptr;
  }

  OnViewInitialized(mImpl->glContext);
  mImpl->viewInitialized = true;
  GetDelegate()->LayoutUI(this);
  GetDelegate()->OnUIOpen();
  SetAllControlsDirty();
  XFlush(mImpl->display);

  const auto intervalMs = static_cast<uint32_t>(std::max(1, 1000 / std::max(1, FPS())));
  mTimer.reset(Timer::Create([this](Timer&) { OnDisplayTimer(); }, intervalMs));
  return reinterpret_cast<void*>(mImpl->window);
}

void IGraphicsLinux::CloseWindow()
{
  if (mTimer)
  {
    mTimer->Stop();
    mTimer.reset();
  }

  std::lock_guard<std::recursive_mutex> lock(mImpl->mutex);
  if (!mImpl->display)
    return;

  if (mImpl->glContext && mImpl->window)
    glXMakeCurrent(mImpl->display, mImpl->window, mImpl->glContext);
  if (mImpl->viewInitialized)
  {
    OnViewDestroyed();
    mImpl->viewInitialized = false;
  }

  RestoreParentXdndProperties();
  mImpl->xdndSource = 0;
  mImpl->xdndTarget = 0;
  mImpl->xdndAcceptsUriList = false;

  if (mImpl->inputContext)
  {
    XDestroyIC(mImpl->inputContext);
    mImpl->inputContext = nullptr;
  }
  if (mImpl->inputMethod)
  {
    XCloseIM(mImpl->inputMethod);
    mImpl->inputMethod = nullptr;
  }
  if (mImpl->cursor)
  {
    XFreeCursor(mImpl->display, mImpl->cursor);
    mImpl->cursor = 0;
  }
  if (mImpl->hiddenCursor)
  {
    XFreeCursor(mImpl->display, mImpl->hiddenCursor);
    mImpl->hiddenCursor = 0;
  }
  if (mImpl->glContext)
  {
    glXMakeCurrent(mImpl->display, kX11None, nullptr);
    glXDestroyContext(mImpl->display, mImpl->glContext);
    mImpl->glContext = nullptr;
  }
  if (mImpl->window)
  {
    XDeleteProperty(mImpl->display, mImpl->window, mImpl->iPlugXdndProperty);
    XDestroyWindow(mImpl->display, mImpl->window);
    mImpl->window = 0;
  }
  if (mImpl->colormap)
  {
    XFreeColormap(mImpl->display, mImpl->colormap);
    mImpl->colormap = 0;
  }

  XCloseDisplay(mImpl->display);
  mImpl->display = nullptr;
  mImpl->parentWindow = 0;
  mImpl->topLevelFallback = false;
  mImpl->wmDeleteMessage = 0;
}

void IGraphicsLinux::ActivateGLContext()
{
  if (mImpl->display && mImpl->window && mImpl->glContext)
    glXMakeCurrent(mImpl->display, mImpl->window, mImpl->glContext);
}

void IGraphicsLinux::DeactivateGLContext()
{
  if (mImpl->display)
    glXMakeCurrent(mImpl->display, kX11None, nullptr);
}

bool IGraphicsLinux::PlatformProcessEvents()
{
  std::lock_guard<std::recursive_mutex> lock(mImpl->mutex);
  if (!mImpl->display || !mImpl->window)
    return false;

  while (XPending(mImpl->display) > 0)
  {
    XEvent event {};
    XNextEvent(mImpl->display, &event);
    if (!HandleXEvent(event))
      return false;
  }

  return true;
}

bool IGraphicsLinux::HandleXEvent(const XEvent& event)
{
  if (event.xany.window != mImpl->window)
  {
    if (event.type != ClientMessage)
      return true;

    if (event.xclient.message_type == mImpl->xdndEnter)
      HandleXdndEnter(event.xclient);
    else if (event.xclient.message_type == mImpl->xdndPosition)
      HandleXdndPosition(event.xclient);
    else if (event.xclient.message_type == mImpl->xdndDrop)
      HandleXdndDrop(event.xclient);
    else if (event.xclient.message_type == mImpl->xdndLeave)
      HandleXdndLeave(event.xclient);
    return true;
  }

  const float scale = GetTotalScale();
  switch (event.type)
  {
    case Expose:
      if (event.xexpose.count == 0)
        SetAllControlsDirty();
      break;

    case ButtonPress:
    {
      const float x = linux_input::DeviceToLogical(event.xbutton.x, scale);
      const float y = linux_input::DeviceToLogical(event.xbutton.y, scale);
      const IMouseMod modifiers = MouseModifiers(event.xbutton.state, event.xbutton.button);

      if (event.xbutton.button == Button4 || event.xbutton.button == Button5)
      {
        OnMouseWheel(x, y, modifiers, event.xbutton.button == Button4 ? 1.f : -1.f);
        break;
      }

      if (event.xbutton.button != Button1 && event.xbutton.button != Button3)
        break;

      XGrabPointer(
        mImpl->display,
        mImpl->window,
        False,
        ButtonReleaseMask | PointerMotionMask,
        GrabModeAsync,
        GrabModeAsync,
        kX11None,
        kX11None,
        event.xbutton.time);

      const Time clickTime = event.xbutton.time;
      const bool isDoubleClick = event.xbutton.button == mImpl->lastClickButton
                              && clickTime - mImpl->lastClickTime < 250
                              && std::abs(x - mImpl->lastClickX) < 5.f
                              && std::abs(y - mImpl->lastClickY) < 5.f;
      if (isDoubleClick)
      {
        mImpl->lastClickTime = 0;
        OnMouseDblClick(x, y, modifiers);
      }
      else
      {
        mImpl->lastClickTime = clickTime;
        mImpl->lastClickX = x;
        mImpl->lastClickY = y;
        mImpl->lastClickButton = event.xbutton.button;
        IMouseInfo info;
        info.x = x;
        info.y = y;
        info.ms = modifiers;
        OnMouseDown({info});
      }

      mImpl->previousMouseX = x;
      mImpl->previousMouseY = y;
      break;
    }

    case ButtonRelease:
    {
      if (event.xbutton.button == Button4 || event.xbutton.button == Button5)
        break;
      if (event.xbutton.button != Button1 && event.xbutton.button != Button3)
        break;

      IMouseInfo info;
      info.x = linux_input::DeviceToLogical(event.xbutton.x, scale);
      info.y = linux_input::DeviceToLogical(event.xbutton.y, scale);
      info.ms = MouseModifiers(event.xbutton.state, event.xbutton.button);
      OnMouseUp({info});
      XUngrabPointer(mImpl->display, event.xbutton.time);
      break;
    }

    case MotionNotify:
    {
      const float x = linux_input::DeviceToLogical(event.xmotion.x, scale);
      const float y = linux_input::DeviceToLogical(event.xmotion.y, scale);
      const IMouseMod modifiers = MouseModifiers(event.xmotion.state);
      if (event.xmotion.state & (Button1Mask | Button3Mask))
      {
        IMouseInfo info;
        info.x = x;
        info.y = y;
        info.dX = x - mImpl->previousMouseX;
        info.dY = y - mImpl->previousMouseY;
        info.ms = modifiers;
        if (!IsInPlatformTextEntry())
          OnMouseDrag({info});
        mImpl->previousMouseX = x;
        mImpl->previousMouseY = y;
      }
      else
      {
        OnMouseOver(x, y, modifiers);
      }
      break;
    }

    case KeyPress:
    case KeyRelease:
    {
      XKeyEvent keyEvent = event.xkey;
      const IKeyPress keyPress = XKeyEventToKeyPress(mImpl->inputContext, &keyEvent);
      if (keyPress.VK == kVK_NONE && keyPress.utf8[0] == '\0')
        break;

      float x = 0.f;
      float y = 0.f;
      GetMouseLocation(x, y);
      if (event.type == KeyPress)
        OnKeyDown(x, y, keyPress);
      else
        OnKeyUp(x, y, keyPress);
      break;
    }

    case FocusIn:
      if (mImpl->inputContext)
        XSetICFocus(mImpl->inputContext);
      break;

    case FocusOut:
      if (mImpl->inputContext)
        XUnsetICFocus(mImpl->inputContext);
      break;

    case SelectionRequest:
      HandleSelectionRequest(event.xselectionrequest);
      break;

    case SelectionNotify:
      if (event.xselection.selection == mImpl->xdndSelection)
        HandleXdndSelectionNotify(event.xselection);
      break;

    case ClientMessage:
      if (event.xclient.message_type == mImpl->xdndEnter)
        HandleXdndEnter(event.xclient);
      else if (event.xclient.message_type == mImpl->xdndPosition)
        HandleXdndPosition(event.xclient);
      else if (event.xclient.message_type == mImpl->xdndDrop)
        HandleXdndDrop(event.xclient);
      else if (event.xclient.message_type == mImpl->xdndLeave)
        HandleXdndLeave(event.xclient);
      else if (mImpl->topLevelFallback
               && mImpl->wmDeleteMessage != 0
               && static_cast<Atom>(event.xclient.data.l[0]) == mImpl->wmDeleteMessage)
      {
        XUnmapWindow(mImpl->display, mImpl->window);
        return false;
      }
      break;
  }

  return true;
}

void IGraphicsLinux::HandleSelectionRequest(const XSelectionRequestEvent& event)
{
  XSelectionEvent selection {};
  selection.type = SelectionNotify;
  selection.display = event.display;
  selection.requestor = event.requestor;
  selection.selection = event.selection;
  selection.target = event.target;
  selection.time = event.time;
  selection.property = static_cast<Atom>(kX11None);
  const Atom property = event.property != static_cast<Atom>(kX11None)
                      ? event.property
                      : event.target;

  if (event.selection == mImpl->clipboardAtom && event.target == mImpl->targetsAtom)
  {
    Atom targets[] = {
      mImpl->targetsAtom, mImpl->utf8StringAtom, XA_STRING, mImpl->textAtom};
    XChangeProperty(
      mImpl->display,
      event.requestor,
      property,
      XA_ATOM,
      32,
      PropModeReplace,
      reinterpret_cast<unsigned char*>(targets),
      sizeof(targets) / sizeof(targets[0]));
    selection.property = property;
  }
  else if (event.selection == mImpl->clipboardAtom
           && (event.target == mImpl->utf8StringAtom
               || event.target == XA_STRING
               || event.target == mImpl->textAtom))
  {
    const char* text = mImpl->clipboardText.Get();
    XChangeProperty(
      mImpl->display,
      event.requestor,
      property,
      event.target,
      8,
      PropModeReplace,
      reinterpret_cast<const unsigned char*>(text),
      std::strlen(text));
    selection.property = property;
  }

  XSendEvent(
    mImpl->display, event.requestor, False, 0, reinterpret_cast<XEvent*>(&selection));
  XFlush(mImpl->display);
}

void IGraphicsLinux::HandleXdndEnter(const XClientMessageEvent& event)
{
  mImpl->xdndSource = static_cast<::Window>(event.data.l[0]);
  mImpl->xdndTarget = event.window ? event.window : mImpl->window;
  mImpl->xdndAcceptsUriList = (event.data.l[1] & 1) != 0
    ? WindowTypeListHasAtom(
        mImpl->display, mImpl->xdndSource, mImpl->xdndTypeList, mImpl->textUriList)
    : XdndMessageHasUriList(event, mImpl->textUriList);
}

void IGraphicsLinux::HandleXdndPosition(const XClientMessageEvent& event)
{
  if (!mImpl->display || !mImpl->window)
    return;

  mImpl->xdndSource = static_cast<::Window>(event.data.l[0]);
  if (event.window)
    mImpl->xdndTarget = event.window;

  const int rootX = linux_input::XdndRootX(event.data.l[2]);
  const int rootY = linux_input::XdndRootY(event.data.l[2]);
  ::Window child = 0;
  int windowX = 0;
  int windowY = 0;
  XTranslateCoordinates(
    mImpl->display,
    DefaultRootWindow(mImpl->display),
    mImpl->window,
    rootX,
    rootY,
    &windowX,
    &windowY,
    &child);
  mImpl->xdndDropX = linux_input::DeviceToLogical(windowX, GetTotalScale());
  mImpl->xdndDropY = linux_input::DeviceToLogical(windowY, GetTotalScale());

  XClientMessageEvent reply {};
  reply.type = ClientMessage;
  reply.display = mImpl->display;
  reply.window = mImpl->xdndSource;
  reply.message_type = mImpl->xdndStatus;
  reply.format = 32;
  reply.data.l[0] = mImpl->xdndTarget ? mImpl->xdndTarget : mImpl->window;
  reply.data.l[1] = mImpl->xdndAcceptsUriList ? 1 : 0;
  reply.data.l[4] = mImpl->xdndAcceptsUriList ? mImpl->xdndActionCopy : kX11None;
  XSendEvent(
    mImpl->display,
    mImpl->xdndSource,
    False,
    NoEventMask,
    reinterpret_cast<XEvent*>(&reply));
  XFlush(mImpl->display);
}

void IGraphicsLinux::HandleXdndDrop(const XClientMessageEvent& event)
{
  if (!mImpl->xdndAcceptsUriList)
  {
    FinishXdnd(false);
    return;
  }

  XConvertSelection(
    mImpl->display,
    mImpl->xdndSelection,
    mImpl->textUriList,
    mImpl->iPlugXdndProperty,
    mImpl->window,
    static_cast<Time>(event.data.l[2]));
  XFlush(mImpl->display);
}

void IGraphicsLinux::HandleXdndLeave(const XClientMessageEvent& event)
{
  if (static_cast<::Window>(event.data.l[0]) == mImpl->xdndSource)
  {
    mImpl->xdndSource = 0;
    mImpl->xdndTarget = 0;
    mImpl->xdndAcceptsUriList = false;
  }
}

void IGraphicsLinux::HandleXdndSelectionNotify(const XSelectionEvent& event)
{
  bool success = false;
  if (event.property != static_cast<Atom>(kX11None))
  {
    Atom actualType = 0;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char* data = nullptr;
    const int result = XGetWindowProperty(
      mImpl->display,
      mImpl->window,
      event.property,
      0,
      1024 * 1024,
      True,
      AnyPropertyType,
      &actualType,
      &actualFormat,
      &itemCount,
      &bytesAfter,
      &data);
    if (result == Success && data && actualFormat == 8 && bytesAfter == 0)
    {
      const std::string payload(reinterpret_cast<const char*>(data), itemCount);
      const auto paths = linux_input::ParseTextUriList(payload);
      if (paths.size() == 1)
      {
        OnDrop(paths[0].c_str(), mImpl->xdndDropX, mImpl->xdndDropY);
        success = true;
      }
      else if (paths.size() > 1)
      {
        std::vector<const char*> pathPointers;
        pathPointers.reserve(paths.size());
        for (const auto& path : paths)
          pathPointers.push_back(path.c_str());
        OnDropMultiple(pathPointers, mImpl->xdndDropX, mImpl->xdndDropY);
        success = true;
      }
    }
    if (data)
      XFree(data);
  }

  FinishXdnd(success);
}

void IGraphicsLinux::FinishXdnd(bool success)
{
  if (mImpl->display && mImpl->xdndSource)
  {
    XClientMessageEvent finished {};
    finished.type = ClientMessage;
    finished.display = mImpl->display;
    finished.window = mImpl->xdndSource;
    finished.message_type = mImpl->xdndFinished;
    finished.format = 32;
    finished.data.l[0] = mImpl->xdndTarget ? mImpl->xdndTarget : mImpl->window;
    finished.data.l[1] = success ? 1 : 0;
    finished.data.l[2] = success ? mImpl->xdndActionCopy : kX11None;
    XSendEvent(
      mImpl->display,
      mImpl->xdndSource,
      False,
      NoEventMask,
      reinterpret_cast<XEvent*>(&finished));
    XFlush(mImpl->display);
  }

  mImpl->xdndSource = 0;
  mImpl->xdndTarget = 0;
  mImpl->xdndAcceptsUriList = false;
}

void IGraphicsLinux::RegisterXdndProxyWindows()
{
  if (!mImpl->display || !mImpl->window)
    return;

  const auto ancestors = GetAncestorWindows(mImpl->display, mImpl->window);
  mImpl->xdndPropertyBackups.clear();
  mImpl->xdndPropertyBackups.reserve(ancestors.size());
  long xdndVersion = 5;
  long xdndProxyWindow = static_cast<long>(mImpl->window);

  for (const ::Window window : ancestors)
  {
    Impl::XdndPropertyBackup backup;
    backup.window = window;
    backup.hadAware = ReadSingleLongProperty(
      mImpl->display,
      window,
      mImpl->xdndAware,
      backup.awareValue,
      backup.awareType,
      backup.awareFormat);
    backup.hadProxy = ReadSingleLongProperty(
      mImpl->display,
      window,
      mImpl->xdndProxy,
      backup.proxyValue,
      backup.proxyType,
      backup.proxyFormat);
    mImpl->xdndPropertyBackups.push_back(backup);

    XChangeProperty(
      mImpl->display,
      window,
      mImpl->xdndAware,
      XA_ATOM,
      32,
      PropModeReplace,
      reinterpret_cast<unsigned char*>(&xdndVersion),
      1);
    XChangeProperty(
      mImpl->display,
      window,
      mImpl->xdndProxy,
      XA_WINDOW,
      32,
      PropModeReplace,
      reinterpret_cast<unsigned char*>(&xdndProxyWindow),
      1);
  }

  XFlush(mImpl->display);
}

void IGraphicsLinux::RestoreParentXdndProperties()
{
  if (!mImpl->display)
    return;

  for (auto iterator = mImpl->xdndPropertyBackups.rbegin();
       iterator != mImpl->xdndPropertyBackups.rend();
       ++iterator)
  {
    const auto& backup = *iterator;
    if (backup.hadAware)
    {
      XChangeProperty(
        mImpl->display,
        backup.window,
        mImpl->xdndAware,
        backup.awareType,
        backup.awareFormat,
        PropModeReplace,
        reinterpret_cast<const unsigned char*>(&backup.awareValue),
        1);
    }
    else
    {
      XDeleteProperty(mImpl->display, backup.window, mImpl->xdndAware);
    }

    if (backup.hadProxy)
    {
      XChangeProperty(
        mImpl->display,
        backup.window,
        mImpl->xdndProxy,
        backup.proxyType,
        backup.proxyFormat,
        PropModeReplace,
        reinterpret_cast<const unsigned char*>(&backup.proxyValue),
        1);
    }
    else
    {
      XDeleteProperty(mImpl->display, backup.window, mImpl->xdndProxy);
    }
  }

  XFlush(mImpl->display);
  mImpl->xdndPropertyBackups.clear();
}

void IGraphicsLinux::GetMouseLocation(float& x, float& y) const
{
  std::lock_guard<std::recursive_mutex> lock(mImpl->mutex);
  if (!mImpl->display || !mImpl->window)
    return;

  ::Window root = 0;
  ::Window child = 0;
  int rootX = 0;
  int rootY = 0;
  int windowX = 0;
  int windowY = 0;
  unsigned int mask = 0;
  if (XQueryPointer(
        mImpl->display,
        mImpl->window,
        &root,
        &child,
        &rootX,
        &rootY,
        &windowX,
        &windowY,
        &mask))
  {
    x = linux_input::DeviceToLogical(windowX, GetTotalScale());
    y = linux_input::DeviceToLogical(windowY, GetTotalScale());
  }
}

void IGraphicsLinux::HideMouseCursor(bool hide, bool lockCursor)
{
  (void) lockCursor;
  std::lock_guard<std::recursive_mutex> lock(mImpl->mutex);
  if (!mImpl->display || !mImpl->window)
    return;

  if (hide)
  {
    if (!mImpl->hiddenCursor)
    {
      const char emptyData[] = {0};
      const Pixmap emptyPixmap = XCreateBitmapFromData(
        mImpl->display, mImpl->window, emptyData, 1, 1);
      XColor color {};
      mImpl->hiddenCursor = XCreatePixmapCursor(
        mImpl->display, emptyPixmap, emptyPixmap, &color, &color, 0, 0);
      XFreePixmap(mImpl->display, emptyPixmap);
    }
    XDefineCursor(mImpl->display, mImpl->window, mImpl->hiddenCursor);
  }
  else if (mImpl->cursor)
  {
    XDefineCursor(mImpl->display, mImpl->window, mImpl->cursor);
  }
  else
  {
    XUndefineCursor(mImpl->display, mImpl->window);
  }
  XFlush(mImpl->display);
}

void IGraphicsLinux::MoveMouseCursor(float x, float y)
{
  std::lock_guard<std::recursive_mutex> lock(mImpl->mutex);
  if (!mImpl->display || !mImpl->window)
    return;

  XWarpPointer(
    mImpl->display,
    kX11None,
    mImpl->window,
    0,
    0,
    0,
    0,
    static_cast<int>(linux_input::LogicalToDevice(x, GetTotalScale())),
    static_cast<int>(linux_input::LogicalToDevice(y, GetTotalScale())));
  XFlush(mImpl->display);
}

ECursor IGraphicsLinux::SetMouseCursor(ECursor cursorType)
{
  std::lock_guard<std::recursive_mutex> lock(mImpl->mutex);
  if (!mImpl->display || !mImpl->window)
    return cursorType;

  const Cursor cursor = XCreateFontCursor(mImpl->display, XCursorShape(cursorType));
  if (cursor)
  {
    XDefineCursor(mImpl->display, mImpl->window, cursor);
    if (mImpl->cursor)
      XFreeCursor(mImpl->display, mImpl->cursor);
    mImpl->cursor = cursor;
    XFlush(mImpl->display);
  }
  return cursorType;
}

bool IGraphicsLinux::SetTextInClipboard(const char* str)
{
  std::lock_guard<std::recursive_mutex> lock(mImpl->mutex);
  if (!mImpl->display || !mImpl->window || !str)
    return false;

  mImpl->clipboardText.Set(str);
  XSetSelectionOwner(
    mImpl->display, mImpl->clipboardAtom, mImpl->window, CurrentTime);
  XFlush(mImpl->display);
  return XGetSelectionOwner(mImpl->display, mImpl->clipboardAtom) == mImpl->window;
}

bool IGraphicsLinux::GetTextFromClipboard(WDL_String& str)
{
  std::lock_guard<std::recursive_mutex> lock(mImpl->mutex);
  if (!mImpl->display || !mImpl->window)
    return false;

  const ::Window owner = XGetSelectionOwner(mImpl->display, mImpl->clipboardAtom);
  if (owner == static_cast<::Window>(kX11None))
    return false;
  if (owner == mImpl->window)
  {
    str.Set(mImpl->clipboardText.Get());
    return true;
  }

  XConvertSelection(
    mImpl->display,
    mImpl->clipboardAtom,
    mImpl->utf8StringAtom,
    mImpl->iPlugClipboardAtom,
    mImpl->window,
    CurrentTime);
  XFlush(mImpl->display);

  for (int attempt = 0; attempt < 100; ++attempt)
  {
    while (XPending(mImpl->display))
    {
      XEvent event {};
      XNextEvent(mImpl->display, &event);
      if (event.type == SelectionNotify
          && event.xselection.selection == mImpl->clipboardAtom)
      {
        if (event.xselection.property == static_cast<Atom>(kX11None))
          return false;

        Atom type = 0;
        int format = 0;
        unsigned long itemCount = 0;
        unsigned long bytesAfter = 0;
        unsigned char* data = nullptr;
        const int result = XGetWindowProperty(
          mImpl->display,
          mImpl->window,
          mImpl->iPlugClipboardAtom,
          0,
          1024 * 1024,
          True,
          AnyPropertyType,
          &type,
          &format,
          &itemCount,
          &bytesAfter,
          &data);
        const bool valid = result == Success && data && format == 8 && bytesAfter == 0;
        if (valid)
          str.Set(reinterpret_cast<const char*>(data), static_cast<int>(itemCount));
        if (data)
          XFree(data);
        return valid;
      }

      if (!HandleXEvent(event))
        return false;
    }

    usleep(1000);
  }

  return false;
}

void IGraphicsLinux::OnDisplayTimer()
{
  std::lock_guard<std::recursive_mutex> lock(mImpl->mutex);
  if (!PlatformProcessEvents() || !mImpl->display || !mImpl->window)
    return;

  IRECTList dirtyRectangles;
  if (!IsDirty(dirtyRectangles))
    return;

  ActivateGLContext();
  Draw(dirtyRectangles);
  glXSwapBuffers(mImpl->display, mImpl->window);
  DeactivateGLContext();
}

IPopupMenu* IGraphicsLinux::CreatePlatformPopupMenu(
  IPopupMenu& menu, const IRECT bounds, bool& isAsync)
{
  (void) menu;
  (void) bounds;
  isAsync = false;
  return nullptr;
}

void IGraphicsLinux::CreatePlatformTextEntry(
  int paramIdx, const IText& text, const IRECT& bounds, int length, const char* str)
{
  (void) paramIdx;
  (void) text;
  (void) bounds;
  (void) length;
  (void) str;
}

void IGraphicsLinux::ForceEndUserEdit()
{
  if (auto* textEntry = GetTextEntryControl())
    textEntry->CommitEdit();
}

EMsgBoxResult IGraphicsLinux::ShowMessageBox(
  const char* str,
  const char* caption,
  EMsgBoxType type,
  IMsgBoxCompletionHandlerFunc completionHandler)
{
  (void) str;
  (void) caption;
  const EMsgBoxResult result = type == kMB_OK ? kOK : kCANCEL;
  if (completionHandler)
    completionHandler(result);
  return result;
}

bool IGraphicsLinux::LaunchWithXdgOpen(const char* target)
{
  if (!target || !target[0])
    return false;

  const std::string targetCopy(target);
  std::thread([targetCopy] {
    pid_t process = 0;
    char program[] = "xdg-open";
    char* arguments[] = {
      program, const_cast<char*>(targetCopy.c_str()), nullptr};
    if (posix_spawnp(&process, program, nullptr, nullptr, arguments, environ) == 0)
    {
      int status = 0;
      while (waitpid(process, &status, 0) == -1 && errno == EINTR)
      {
      }
    }
  }).detach();
  return true;
}

bool IGraphicsLinux::OpenURL(
  const char* url,
  const char* msgWindowTitle,
  const char* confirmMsg,
  const char* errMsgOnFailure)
{
  (void) msgWindowTitle;
  (void) confirmMsg;
  (void) errMsgOnFailure;
  return LaunchWithXdgOpen(url);
}

bool IGraphicsLinux::RevealPathInExplorerOrFinder(WDL_String& path, bool select)
{
  (void) select;
  return LaunchWithXdgOpen(path.Get());
}

void IGraphicsLinux::PromptForFile(
  WDL_String& fileName,
  WDL_String& path,
  EFileAction action,
  const char* ext,
  IFileDialogCompletionHandlerFunc completionHandler)
{
  (void) action;
  (void) ext;
  fileName.Set("");
  path.Set("");
  if (completionHandler)
    completionHandler(fileName, path);
}

void IGraphicsLinux::PromptForDirectory(
  WDL_String& dir, IFileDialogCompletionHandlerFunc completionHandler)
{
  dir.Set("");
  if (completionHandler)
  {
    WDL_String fileName;
    completionHandler(fileName, dir);
  }
}

bool IGraphicsLinux::PromptForColor(
  IColor& color, const char* str, IColorPickerHandlerFunc completionHandler)
{
  (void) color;
  (void) str;
  (void) completionHandler;
  return false;
}

class LinuxFont final : public PlatformFont
{
public:
  explicit LinuxFont(IFontDataPtr&& data)
  : PlatformFont(false)
  , mData(std::move(data))
  {
  }

  IFontDataPtr GetFontData() override
  {
    if (mData && mData->IsValid())
      return IFontDataPtr(
        new IFontData(mData->Get(), mData->GetSize(), mData->GetFaceIdx()));
    return IFontDataPtr(new IFontData());
  }

private:
  IFontDataPtr mData;
};

PlatformFontPtr IGraphicsLinux::LoadPlatformFont(
  const char* fontID, const char* fileNameOrResID)
{
  (void) fontID;
  WDL_String fullPath;
  const auto location = LocateResource(
    fileNameOrResID, "ttf", fullPath, GetBundleID(), nullptr, nullptr);
  if (location != EResourceLocation::kAbsolutePath)
    return nullptr;

  FILE* file = std::fopen(fullPath.Get(), "rb");
  if (!file)
    return nullptr;
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  if (size <= 0)
  {
    std::fclose(file);
    return nullptr;
  }

  std::vector<uint8_t> data(static_cast<size_t>(size));
  const size_t bytesRead = std::fread(data.data(), 1, data.size(), file);
  std::fclose(file);
  if (bytesRead != data.size())
    return nullptr;

  IFontDataPtr fontData(new IFontData(data.data(), data.size(), 0));
  if (!fontData->IsValid())
    return nullptr;
  return PlatformFontPtr(new LinuxFont(std::move(fontData)));
}

PlatformFontPtr IGraphicsLinux::LoadPlatformFont(
  const char* fontID, void* data, int dataSize)
{
  (void) fontID;
  if (!data || dataSize <= 0)
    return nullptr;

  IFontDataPtr fontData(new IFontData(data, dataSize, 0));
  if (!fontData->IsValid())
    return nullptr;
  return PlatformFontPtr(new LinuxFont(std::move(fontData)));
}

PlatformFontPtr IGraphicsLinux::LoadPlatformFont(
  const char* fontID, const char* fontName, ETextStyle style)
{
  if (!fontName || !fontName[0])
    return nullptr;

  FcPattern* pattern = FcNameParse(reinterpret_cast<const FcChar8*>(fontName));
  if (!pattern)
    return nullptr;
  if (style == ETextStyle::Bold)
    FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
  else if (style == ETextStyle::Italic)
    FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);

  FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);
  FcResult result = FcResultNoMatch;
  FcPattern* match = FcFontMatch(nullptr, pattern, &result);
  FcPatternDestroy(pattern);
  if (!match)
    return nullptr;

  FcChar8* filePath = nullptr;
  PlatformFontPtr font;
  if (FcPatternGetString(match, FC_FILE, 0, &filePath) == FcResultMatch && filePath)
    font = LoadPlatformFont(fontID, reinterpret_cast<const char*>(filePath));
  FcPatternDestroy(match);
  return font;
}
