/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

#include "IPlugCocoaEditorDelegate.h"
#import "IPlugCocoaViewController.h"

using namespace iplug;

CocoaEditorDelegate::CocoaEditorDelegate(int nParams)
: IEditorDelegate(nParams)
{
}

CocoaEditorDelegate::~CocoaEditorDelegate()
{
}

void* CocoaEditorDelegate::OpenWindow(void* pParent)
{
#ifdef OS_IOS
#if __has_feature(objc_arc)
  IPlugCocoaViewController* vc = (IPlugCocoaViewController*) [(__bridge PLATFORM_VIEW*) pParent nextResponder];
  [vc setEditorDelegate: this];
  mViewController = (__bridge void*) vc;
#else
  IPlugCocoaViewController* vc = (IPlugCocoaViewController*) [(PLATFORM_VIEW*) pParent nextResponder];
  [vc setEditorDelegate: this];
  mViewController = vc;
#endif
#endif
  
  return pParent;
}

void CocoaEditorDelegate::CloseWindow()
{
  mViewController = nil;
}

bool CocoaEditorDelegate::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
#if __has_feature(objc_arc)
  IPlugCocoaViewController* vc = (__bridge IPlugCocoaViewController*) mViewController;
#else
  IPlugCocoaViewController* vc = (IPlugCocoaViewController*) mViewController;
#endif
  NSData* pNSData = [NSData dataWithBytes:pData length:dataSize];
  return [vc onMessage:msgTag : ctrlTag : pNSData];
}

void CocoaEditorDelegate::OnParamChangeUI(int paramIdx, EParamSource source)
{
#if __has_feature(objc_arc)
  IPlugCocoaViewController* vc = (__bridge IPlugCocoaViewController*) mViewController;
#else
  IPlugCocoaViewController* vc = (IPlugCocoaViewController*) mViewController;
#endif
  
  if(vc)
    [vc onParamChangeUI:paramIdx :GetParam(paramIdx)->GetNormalized() ];
}

void CocoaEditorDelegate::OnMidiMsgUI(const IMidiMsg& msg)
{
#if __has_feature(objc_arc)
  IPlugCocoaViewController* vc = (__bridge IPlugCocoaViewController*) mViewController;
#else
  IPlugCocoaViewController* vc = (IPlugCocoaViewController*) mViewController;
#endif
  [vc onMidiMsgUI:msg.mStatus : msg.mData1 : msg.mData2 : msg.mOffset];
}

void CocoaEditorDelegate::OnSysexMsgUI(const ISysEx& msg)
{
#if __has_feature(objc_arc)
  IPlugCocoaViewController* vc = (__bridge IPlugCocoaViewController*) mViewController;
#else
  IPlugCocoaViewController* vc = (IPlugCocoaViewController*) mViewController;
#endif
  NSData* pNSData = [NSData dataWithBytes:msg.mData length:msg.mSize];

  [vc onSysexMsgUI:pNSData : msg.mOffset];
}

void CocoaEditorDelegate::SendControlValueFromDelegate(int ctrlTag, double normalizedValue)
{
#if __has_feature(objc_arc)
  IPlugCocoaViewController* vc = (__bridge IPlugCocoaViewController*) mViewController;
#else
  IPlugCocoaViewController* vc = (IPlugCocoaViewController*) mViewController;
#endif
  [vc sendControlValueFromDelegate:ctrlTag :normalizedValue];
}

void CocoaEditorDelegate::SendControlMsgFromDelegate(int ctrlTag, int msgTag, int dataSize, const void* pData)
{
#if __has_feature(objc_arc)
  IPlugCocoaViewController* vc = (__bridge IPlugCocoaViewController*) mViewController;
#else
  IPlugCocoaViewController* vc = (IPlugCocoaViewController*) mViewController;
#endif
  NSData* pNSData = [NSData dataWithBytes:pData length:dataSize];

  [vc sendControlMsgFromDelegate: ctrlTag : msgTag : pNSData];
}

void CocoaEditorDelegate::SendParameterValueFromDelegate(int paramIdx, double value, bool normalized)
{
#if __has_feature(objc_arc)
  IPlugCocoaViewController* vc = (__bridge IPlugCocoaViewController*) mViewController;
#else
  IPlugCocoaViewController* vc = (IPlugCocoaViewController*) mViewController;
#endif
  [vc sendParameterValueFromDelegate:paramIdx :value :normalized];
  
  IEditorDelegate::SendParameterValueFromDelegate(paramIdx, value, normalized);
}
