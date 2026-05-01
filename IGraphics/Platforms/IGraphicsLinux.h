/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

#include "IGraphics_select.h"
#include "IPlugTimer.h"
#include <memory>

// X11 Headers
// Kept here because X11 types (Window, Display, Atom) are used in the class/impl API
#include <X11/Xlib.h>
#include <X11/Xutil.h>

// X11 defines macros that conflict with C++ enum identifiers
// Only undef the ones that actually conflict - Bool and Status are needed by GLX
#undef None
#undef Complex

BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE
/** IGraphics platform class for linux
*   @ingroup PlatformClasses
*/
class IGraphicsLinux final : public IGRAPHICS_DRAW_CLASS
{
public:
  IGraphicsLinux(IGEditorDelegate& dlg, int w, int h, int fps, float scale);
  virtual ~IGraphicsLinux();

  void* OpenWindow(void* pParent) override;
  void CloseWindow() override;
  bool WindowIsOpen() override;
  void PlatformResize(bool mouseOver) override;

  // Event Loop - Call this from your host timer or main loop
  bool PlatformProcessEvents();

  // Key & Cursor
  void HideMouseCursor(bool hide, bool lock) override;
  void MoveMouseCursor(float x, float y) override;
  ECursor SetMouseCursor(ECursor cursorType) override;
  void GetMouseLocation(float& x, float& y) const override;

  // Platform Info
  const char* GetPlatformAPIStr() override { return "X11"; }

  // Dialogs & Interactions (Stubs)
  EMsgBoxResult ShowMessageBox(const char* str, const char* caption, EMsgBoxType type, IMsgBoxCompletionHandlerFunc completionHandler) override;
  void ForceEndUserEdit() override;
  void UpdateTooltips() override {}
  bool RevealPathInExplorerOrFinder(WDL_String& path, bool select = false) override { return false; }
  void PromptForFile(WDL_String& fileName, WDL_String& path, EFileAction action = EFileAction::Open, const char* ext = "", IFileDialogCompletionHandlerFunc completionHandler = nullptr) override;
  void PromptForDirectory(WDL_String& dir, IFileDialogCompletionHandlerFunc completionHandler = nullptr) override {}
  bool PromptForColor(IColor& color, const char* str = "", IColorPickerHandlerFunc func = nullptr) override { return false; }
  bool OpenURL(const char* url, const char* msgWindowTitle, const char* confirmMsg, const char* errMsgOnFailure) override { return false; }
  
  // Clipboard
  bool GetTextFromClipboard(WDL_String& str) override;
  bool SetTextInClipboard(const char* str) override;
  
  // Platform Controls (Stubs)
  IPopupMenu* CreatePlatformPopupMenu(IPopupMenu& menu, const IRECT bounds, bool& isAsync) override { return nullptr; }
  void CreatePlatformTextEntry(int paramIdx, const IText& text, const IRECT& bounds, int length, const char* str) override {}

  // Window
  void* GetWindow() override;
  
  // Font loading
  PlatformFontPtr LoadPlatformFont(const char* fontID, const char* fileNameOrResID) override;
  PlatformFontPtr LoadPlatformFont(const char* fontID, void* pData, int dataSize) override;
  PlatformFontPtr LoadPlatformFont(const char* fontID, const char* fontName, ETextStyle style) override;
  void CachePlatformFont(const char* fontID, const PlatformFontPtr& font) override {}

protected:
  // OpenGL Context Management
  void ActivateGLContext() override;
  void DeactivateGLContext() override;

private:
  class Impl;
  Impl* mImpl = nullptr;
  std::unique_ptr<Timer> mTimer;
  
  void OnDisplayTimer();
  bool HandleXEvent(const XEvent& event);
  void HandleSelectionRequest(const XSelectionRequestEvent& event);
  void HandleXdndEnter(const XClientMessageEvent& event);
  void HandleXdndPosition(const XClientMessageEvent& event);
  void HandleXdndDrop(const XClientMessageEvent& event);
  void HandleXdndLeave(const XClientMessageEvent& event);
  void HandleXdndSelectionNotify(const XSelectionEvent& event);
  void RegisterXdndProxyWindows();
  void RestoreParentXdndProperties();
};

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE
