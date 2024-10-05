/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

#pragma once

#include "IPlugEditorDelegate.h"
#include "IPlugWebView.h"
#include "wdl_base64.h"
#include "json.hpp"
#include <functional>
#include <filesystem>

BEGIN_IPLUG_NAMESPACE

/** This Editor Delegate allows using a platform native web view as the UI for an iPlug plugin */
class WebViewEditorDelegate : public IEditorDelegate
                            , public IWebView
{
  static constexpr int kDefaultMaxJSStringLength = 8192;
  
public:
  WebViewEditorDelegate(int nParams);
  virtual ~WebViewEditorDelegate();
  
  //IEditorDelegate
  void* OpenWindow(void* pParent) override;
  
  void CloseWindow() override
  {
    CloseWebView();
  }

  void SendControlValueFromDelegate(int ctrlTag, double normalizedValue) override
  {
    WDL_String str;
    str.SetFormatted(mMaxJSStringLength, "SCVFD(%i, %f)", ctrlTag, normalizedValue);
    EvaluateJavaScript(str.Get());
  }

  void SendControlMsgFromDelegate(int ctrlTag, int msgTag, int dataSize, const void* pData) override
  {
    WDL_String str;
    std::vector<char> base64;
    base64.resize(GetBase64Length(dataSize) + 1);
    wdl_base64encode(reinterpret_cast<const unsigned char*>(pData), base64.data(), dataSize);
    str.SetFormatted(mMaxJSStringLength, "SCMFD(%i, %i, %i, '%s')", ctrlTag, msgTag, dataSize, base64.data());
    EvaluateJavaScript(str.Get());
  }

  void SendParameterValueFromDelegate(int paramIdx, double value, bool normalized) override
  {
    WDL_String str;
    str.SetFormatted(mMaxJSStringLength, "SPVFD(%i, %f)", paramIdx, value);
    EvaluateJavaScript(str.Get());
  }

  void SendArbitraryMsgFromDelegate(int msgTag, int dataSize, const void* pData) override
  {
    WDL_String str;
    std::vector<char> base64;
    if (dataSize)
    {
      base64.resize(GetBase64Length(dataSize) + 1);
      wdl_base64encode(reinterpret_cast<const unsigned char*>(pData), base64.data(), dataSize);
    }
    str.SetFormatted(mMaxJSStringLength, "SAMFD(%i, %i, '%s')", msgTag, static_cast<int>(base64.size()), base64.data());
    EvaluateJavaScript(str.Get());
  }
  
  void SendMidiMsgFromDelegate(const IMidiMsg& msg) override
  {
    WDL_String str;
    str.SetFormatted(mMaxJSStringLength, "SMMFD(%i, %i, %i)", msg.mStatus, msg.mData1, msg.mData2);
    EvaluateJavaScript(str.Get());
  }
  
  void SendJSONFromDelegate(const nlohmann::json& jsonMessage)
  {
    SendArbitraryMsgFromDelegate(-1, static_cast<int>(jsonMessage.dump().size()), jsonMessage.dump().c_str());
  }

  void OnMessageFromWebView(const char* jsonStr) override
  {
    auto json = nlohmann::json::parse(jsonStr, nullptr, false);
    
    if(json["msg"] == "SPVFUI")
    {
      SendParameterValueFromUI(json["paramIdx"], json["value"]);
    }
    else if (json["msg"] == "BPCFUI")
    {
      BeginInformHostOfParamChangeFromUI(json["paramIdx"]);
    }
    else if (json["msg"] == "EPCFUI")
    {
      EndInformHostOfParamChangeFromUI(json["paramIdx"]);
    }
    else if (json["msg"] == "SAMFUI")
    {
      std::vector<unsigned char> base64;

      if(json.count("data") > 0 && json["data"].is_string())
      {
        auto dStr = json["data"].get<std::string>();
        int dSize = static_cast<int>(dStr.size());
        
        // calculate the exact size of the decoded base64 data
        int numPaddingBytes = 0;
        
        if(dSize >= 2 && dStr[dSize-2] == '=')
          numPaddingBytes = 2;
        else if(dSize >= 1 && dStr[dSize-1] == '=')
          numPaddingBytes = 1;
        
        base64.resize((dSize * 3) / 4 - numPaddingBytes);
        wdl_base64decode(dStr.c_str(), base64.data(), static_cast<int>(base64.size()));
      }

      SendArbitraryMsgFromUI(json["msgTag"], json["ctrlTag"], static_cast<int>(base64.size()), base64.data());
    }
    else if(json["msg"] == "SMMFUI")
    {
      IMidiMsg msg {0, json["statusByte"].get<uint8_t>(),
                       json["dataByte1"].get<uint8_t>(),
                       json["dataByte2"].get<uint8_t>()};
      SendMidiMsgFromUI(msg);
    }
  }

  void Resize(int width, int height);
  
  void OnParentWindowResize(int width, int height) override;

  void OnWebViewReady() override
  {
    if (mEditorInitFunc)
      mEditorInitFunc();
  }
  
  void OnWebContentLoaded() override
  {
    nlohmann::json msg;
    
    msg["id"] = "params";
    std::vector<nlohmann::json> params;
    for (int idx = 0; idx < NParams(); idx++)
    {
      WDL_String jsonStr;
      IParam* pParam = GetParam(idx);
      pParam->GetJSON(jsonStr, idx);
      nlohmann::json paramMsg = nlohmann::json::parse(jsonStr.Get(), nullptr, true);
      params.push_back(paramMsg);
    }
    msg["params"] = params;

    SendJSONFromDelegate(msg);
    
    OnUIOpen();
  }
  
  void SetMaxJSStringLength(int length)
  {
    mMaxJSStringLength = length;
  }

  void SetEnableDevTools(bool enable)
  {
    mEnableDevTools = enable;
  }
  
  bool GetEnableDevTools() const { return mEnableDevTools; }

  void LoadIndexHtml(const char* pathOfPluginSrc)
  {
#ifdef _DEBUG
    namespace fs = std::filesystem;
    
    fs::path mainPath(pathOfPluginSrc);
    fs::path indexRelativePath = mainPath.parent_path() / "Resources" / "web" / "index.html";

    LoadFile(indexRelativePath.string().c_str(), nullptr, true);
#else
    LoadFile("index.html", GetBundleID(), true); // TODO: make this work for windows
#endif
  }

protected:
  int mMaxJSStringLength = kDefaultMaxJSStringLength;
  std::function<void()> mEditorInitFunc = nullptr;
  void* mHelperView = nullptr;
  
private:
  static int GetBase64Length(int dataSize)
  {
    return static_cast<int>(4. * std::ceil((static_cast<double>(dataSize) / 3.)));
  }

#if defined OS_MAC || defined OS_IOS
  void ResizeWebViewAndHelper(float width, float height);
#endif
  bool mEnableDevTools = false;
};

END_IPLUG_NAMESPACE
