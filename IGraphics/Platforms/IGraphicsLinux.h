/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for more info.

 ==============================================================================
*/

#pragma once

#include "IGraphics_select.h"
#include "IPlugTimer.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <memory>

#undef None
#undef Complex
#undef Bool
#undef CurrentTime

BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE

/** X11/XWayland platform implementation for Linux plug-in editors. */
class IGraphicsLinux final : public IGRAPHICS_DRAW_CLASS
{
public:
  IGraphicsLinux(IGEditorDelegate& delegate, int width, int height, int fps, float scale);
  ~IGraphicsLinux() override;

  void* OpenWindow(void* parent) override;
  void CloseWindow() override;
  bool WindowIsOpen() override;
  void PlatformResize(bool parentHasResized) override;
  void* GetWindow() override;
  float GetPlatformWindowScale() const override { return GetScreenScale(); }

  bool PlatformProcessEvents();

  void HideMouseCursor(bool hide, bool lock) override;
  void MoveMouseCursor(float x, float y) override;
  ECursor SetMouseCursor(ECursor cursorType) override;
  void GetMouseLocation(float& x, float& y) const override;

  const char* GetPlatformAPIStr() override { return "X11"; }
  void UpdateTooltips() override {}
  void ForceEndUserEdit() override;

  EMsgBoxResult ShowMessageBox(
    const char* str,
    const char* caption,
    EMsgBoxType type,
    IMsgBoxCompletionHandlerFunc completionHandler) override;

  bool RevealPathInExplorerOrFinder(WDL_String& path, bool select = false) override;
  bool OpenURL(
    const char* url,
    const char* msgWindowTitle,
    const char* confirmMsg,
    const char* errMsgOnFailure) override;
  void PromptForFile(
    WDL_String& fileName,
    WDL_String& path,
    EFileAction action = EFileAction::Open,
    const char* ext = "",
    IFileDialogCompletionHandlerFunc completionHandler = nullptr) override;
  void PromptForDirectory(
    WDL_String& dir,
    IFileDialogCompletionHandlerFunc completionHandler = nullptr) override;
  bool PromptForColor(
    IColor& color,
    const char* str = "",
    IColorPickerHandlerFunc completionHandler = nullptr) override;

  bool GetTextFromClipboard(WDL_String& str) override;
  bool SetTextInClipboard(const char* str) override;

  IPopupMenu* CreatePlatformPopupMenu(
    IPopupMenu& menu, const IRECT bounds, bool& isAsync) override;
  void CreatePlatformTextEntry(
    int paramIdx, const IText& text, const IRECT& bounds, int length, const char* str) override;

  PlatformFontPtr LoadPlatformFont(
    const char* fontID, const char* fileNameOrResID) override;
  PlatformFontPtr LoadPlatformFont(
    const char* fontID, void* data, int dataSize) override;
  PlatformFontPtr LoadPlatformFont(
    const char* fontID, const char* fontName, ETextStyle style) override;
  void CachePlatformFont(const char* fontID, const PlatformFontPtr& font) override {}

protected:
  void ActivateGLContext() override;
  void DeactivateGLContext() override;

private:
  class Impl;
  std::unique_ptr<Impl> mImpl;
  std::unique_ptr<Timer> mTimer;

  void OnDisplayTimer();
  bool HandleXEvent(const XEvent& event);
  void HandleSelectionRequest(const XSelectionRequestEvent& event);
  void HandleXdndEnter(const XClientMessageEvent& event);
  void HandleXdndPosition(const XClientMessageEvent& event);
  void HandleXdndDrop(const XClientMessageEvent& event);
  void HandleXdndLeave(const XClientMessageEvent& event);
  void HandleXdndSelectionNotify(const XSelectionEvent& event);
  void FinishXdnd(bool success);
  void RegisterXdndProxyWindows();
  void RestoreParentXdndProperties();
  bool LaunchWithXdgOpen(const char* target);
};

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE
