/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

#include "IGraphics_select.h"

// X11 Headers
// Kept here because X11 types (Window, Display, Atom) are used in the class/impl API
#include <X11/Xlib.h>
#include <X11/Xutil.h>

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
  bool RevealPathInExplorerOrFinder(const char* path, bool select) override { return false; }
  void PromptForFile(const char* fileName, EFileAction action, const char* ext, IFileDialogCompletionHandlerFunc completionHandler) override;
  void PromptForColor(IColor& color, const char* str, IColorPickerHandlerFunc completionHandler) override;
  bool OpenURL(const char* url, const char* msgWindowTitle, const char* confirmMsg, const char* errMsgOnFailure) override { return false; }
  
  // Clipboard (Stubs)
  void GetTextFromClipboard(WDL_String& str) override {}
  bool SetTextInClipboard(const char* str) override { return false; }
  
  // Platform Controls (Stubs)
  void* CreatePlatformPopupMenu(IPopupMenu& menu, const IRECT& bounds, bool& isAsync) override { return nullptr; }
  void* CreatePlatformTextEntry(int paramIdx, const IText& text, const IRECT& bounds, int lengthLimit, const char* str) override { return nullptr; }

  bool GetUserOSVersion(WDL_String& str) override { str.Set("Linux (X11)"); return true; }

protected:
  // OpenGL Context Management
  void ActivateGLContext() override;
  void DeactivateGLContext() override;
  
  // This override is required by base class, even if unused in stub
  IPopupMenu* CreatePlatformPopupMenu(IPopupMenu& menu, const IRECT& bounds) override { return nullptr; }

private:
  class Impl;
  Impl* mImpl = nullptr;
};

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE
