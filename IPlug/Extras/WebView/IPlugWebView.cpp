/*
==============================================================================

This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

See LICENSE.txt for  more info.

==============================================================================
*/

#include "IPlugWebView.h"
#include "IPlugPaths.h"
#include <string>
#include <windows.h>
#include <shlobj.h>
//#include <shlwapi.h>
#include <cassert>

using namespace iplug;
using namespace Microsoft::WRL;

extern float GetScaleForHWND(HWND hWnd);

IWebView::IWebView(bool opaque)
: mOpaque(opaque)
{
}

IWebView::~IWebView()
{
  CloseWebView();
}

typedef HRESULT (*TCCWebView2EnvWithOptions)(
  PCWSTR browserExecutableFolder, PCWSTR userDataFolder, PCWSTR additionalBrowserArguments,
  ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environment_created_handler);

void* IWebView::OpenWebView(void* pParent, float x, float y, float w, float h, float scale, bool enableDevTools)
{
  mParentWnd = (HWND)pParent;

  float ss = GetScaleForHWND(mParentWnd);

  x *= ss;
  y *= ss;
  w *= ss;
  h *= ss;

  WDL_String cachePath;
  WebViewCachePath(cachePath);
  int bufSize = UTF8ToUTF16Len(cachePath.Get());
  std::vector<WCHAR> cachePathWide(bufSize);
  UTF8ToUTF16(cachePathWide.data(), cachePath.Get(), IPLUG_WIN_MAX_WIDE_PATH);

  auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
  options->put_AllowSingleSignOnUsingOSPrimaryAccount(FALSE);
  options->put_ExclusiveUserDataFolderAccess(FALSE);
  // options->put_Language(m_language.c_str());
  options->put_IsCustomCrashReportingEnabled(FALSE);

  // Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions4> options4;
  // if (options.As(&options4) == S_OK)
  //{
  //   // const WCHAR* allowedOrigins[1] = {L"https://*.example.com"};
  //   auto customSchemeRegistration = Microsoft::WRL::Make<CoreWebView2CustomSchemeRegistration>(L"iplug");
  //   customSchemeRegistration->put_TreatAsSecure(TRUE);
  //   // customSchemeRegistration->SetAllowedOrigins(1, allowedOrigins);
  //   customSchemeRegistration->put_HasAuthorityComponent(TRUE);
  //   ICoreWebView2CustomSchemeRegistration *registrations[1] = {customSchemeRegistration.Get()};
  //   options4->SetCustomSchemeRegistrations(1, static_cast<ICoreWebView2CustomSchemeRegistration **>(registrations));
  // }

  CreateCoreWebView2EnvironmentWithOptions(
    nullptr, cachePathWide.data(), options.Get(),
    Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([&, x, y, w, h](
                                                                           HRESULT result,
                                                                           ICoreWebView2Environment* env) -> HRESULT {
      mWebViewEnvironment = env;
      mWebViewEnvironment->CreateCoreWebView2Controller(
        mParentWnd,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
          [&, x, y, w, h, enableDevTools](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
            if (controller != nullptr)
            {
              mWebViewCtrlr = controller;
              mWebViewCtrlr->get_CoreWebView2(&mCoreWebView);
            }
            
            if (mWebViewWnd == nullptr) {
              return S_OK;
            }

            mWebViewCtrlr->put_IsVisible(mShowOnLoad);

            ICoreWebView2Settings* Settings;
            mCoreWebView->get_Settings(&Settings);
            Settings->put_IsScriptEnabled(TRUE);
            Settings->put_AreDefaultScriptDialogsEnabled(TRUE);
            Settings->put_IsWebMessageEnabled(TRUE);
            Settings->put_AreDefaultContextMenusEnabled(enableDevTools);
            Settings->put_AreDevToolsEnabled(enableDevTools);

            // this script adds a function IPlugSendMsg that is used to call the platform webview messaging function in JS
            mCoreWebView->AddScriptToExecuteOnDocumentCreated(
              L"function IPlugSendMsg(m) {window.chrome.webview.postMessage(m)};",
              Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>([this](HRESULT error,
                                                                                                PCWSTR id) -> HRESULT {
                return S_OK;
              }).Get());

            mCoreWebView->add_WebMessageReceived(
              Callback<ICoreWebView2WebMessageReceivedEventHandler>([this](
                                                                      ICoreWebView2* sender,
                                                                      ICoreWebView2WebMessageReceivedEventArgs* args) {
                wil::unique_cotaskmem_string jsonString;
                args->get_WebMessageAsJson(&jsonString);
                std::wstring jsonWString = jsonString.get();
                WDL_String cStr;
                UTF16ToUTF8(cStr, jsonWString.c_str());
                OnMessageFromWebView(cStr.Get());
                return S_OK;
              }).Get(),
              &mWebMessageReceivedToken);

            mCoreWebView->add_NavigationCompleted(
              Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [this](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                  BOOL success;
                  args->get_IsSuccess(&success);
                  if (success)
                  {
                    OnWebContentLoaded();
                  }
                  return S_OK;
                })
                .Get(),
              &mNavigationCompletedToken);

            if (!mOpaque)
            {
              wil::com_ptr<ICoreWebView2Controller2> controller2 = mWebViewCtrlr.query<ICoreWebView2Controller2>();
              COREWEBVIEW2_COLOR color;
              memset(&color, 0, sizeof(COREWEBVIEW2_COLOR));
              controller2->put_DefaultBackgroundColor(color);
            }

            mWebViewCtrlr->put_Bounds({(LONG)x, (LONG)y, (LONG)(x + w), (LONG)(y + h)});
            OnWebViewReady();
            return S_OK;
          })
          .Get());

      return S_OK;
    }).Get());

  return mParentWnd;
}

void IWebView::CloseWebView()
{
  if (mWebViewCtrlr.get() != nullptr)
  {
    mWebViewCtrlr->Close();
    mWebViewCtrlr = nullptr;
    mCoreWebView = nullptr;
    mWebViewEnvironment = nullptr;
  }
}

void IWebView::HideWebView(bool hide)
{
  if (mWebViewCtrlr.get() != nullptr)
  {
    mWebViewCtrlr->put_IsVisible(!hide);
  }
  else
  {
    // the controller is set asynchonously, so we store the state 
    // to apply it when the controller is created
    mShowOnLoad = !hide;
  }
}

void IWebView::LoadHTML(const char* html)
{
  if (mWebViewWnd)
  {
    int bufSize = UTF8ToUTF16Len(html);
    std::vector<WCHAR> htmlWide(bufSize);
    UTF8ToUTF16(htmlWide.data(), html, bufSize);
    mWebViewWnd->NavigateToString(htmlWide.data());
  }
}

void IWebView::LoadURL(const char* url)
{
  if (mWebViewWnd)
  {
    int bufSize = UTF8ToUTF16Len(url);
    std::vector<WCHAR> urlWide(bufSize);
    UTF8ToUTF16(urlWide.data(), url, bufSize);
    mWebViewWnd->Navigate(urlWide.data());
  }
}

void IWebView::LoadFile(const char* fileName, const char* bundleID, bool /*useCustomScheme*/)
{
  if (mCoreWebView)
  {
    wil::com_ptr<ICoreWebView2_3> webView3 = mCoreWebView.try_query<ICoreWebView2_3>();
    if (webView3)
    {
      WDL_String webFolder{fileName};
      webFolder.remove_filepart();
      int bufSize = UTF8ToUTF16Len(fullStr.Get());
      std::vector<WCHAR> webFolderWide(bufSize);
      UTF8ToUTF16(webFolderWide(), fullStr.Get(), IPLUG_WIN_MAX_WIDE_PATH);

      webView3->SetVirtualHostNameToFolderMapping(
        L"iplug.example", webFolderWide, COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
    }

    WDL_String baseName{fileName};
    WDL_String root{fileName};
    root.remove_filepart();
    mWebRoot.Set(root.Get());

    WDL_String fullStr;
    fullStr.SetFormatted(MAX_WIN32_PATH_LEN, "https://iplug.example/%s", baseName.get_filepart());
    // fullStr.SetFormatted(MAX_WIN32_PATH_LEN, useCustomScheme ? "iplug://%s" : "file://%s", fileName);
    int bufSize = UTF8ToUTF16Len(fullStr.Get());
    std::vector<WCHAR> fileUrlWide(bufSize);
    UTF8ToUTF16(fileUrlWide.data(), fullStr.Get(), bufSize);
    mWebViewWnd->Navigate(fileUrlWide.data());
  }
}


void IWebView::ReloadPageContent()
{
  if (mCoreWebView)
  {
    mCoreWebView->Reload();
  }
}

void IWebView::EvaluateJavaScript(const char* scriptStr, completionHandlerFunc func)
{
  if (mCoreWebView)
  {
    int bufSize = UTF8ToUTF16Len(scriptStr);
    std::vector<WCHAR> scriptWide(bufSize);
    UTF8ToUTF16(scriptWide.data(), scriptStr, bufSize);

    mCoreWebView->ExecuteScript(
      scriptWide.data(), Callback<ICoreWebView2ExecuteScriptCompletedHandler>([func](HRESULT errorCode,
                                                                              LPCWSTR resultObjectAsJson) -> HRESULT {
                    if (func && resultObjectAsJson)
                    {
                      WDL_String str;
                      UTF16ToUTF8(str, resultObjectAsJson);
                      func(str.Get());
                    }
                    return S_OK;
                  }).Get());
  }
}

void IWebView::EnableScroll(bool enable)
{
  /* NO-OP */
}

void IWebView::EnableInteraction(bool enable)
{
  /* NO-OP */
}

void IWebView::SetWebViewBounds(float x, float y, float w, float h, float scale)
{
  if (mWebViewCtrlr)
  {
    float ss = GetScaleForHWND(mParentWnd);

    x *= ss;
    y *= ss;
    w *= ss;
    h *= ss;

    mWebViewCtrlr->SetBoundsAndZoomFactor({(LONG)x, (LONG)y, (LONG)(x + w), (LONG)(y + h)}, scale);
  }
}
