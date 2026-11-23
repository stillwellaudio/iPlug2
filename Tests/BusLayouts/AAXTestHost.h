#pragma once

// Test-owned implementations of the installed SDK interfaces. Exercise the SDK's
// versioned wrappers and real Initialize/Uninitialize lifecycle without test hooks.
#include "AAX_IACFController.h"
#include "IPlugAAX_Parameters.h"
#include "AAX_IMIDINode.h"
#include "AAX_IACFTransport.h"
#include "AAX_IACFAutomationDelegate.h"
#include "AAX_UIDs.h"
#include "AAX_IString.h"
#include <cstring>

class AAXTestHost final : public AAX_IACFController_V3,
                          public AAX_IACFTransport,
                          public AAX_IACFAutomationDelegate
{
public:
  AAX_EStemFormat input = AAX_eStemFormat_Stereo, output = AAX_eStemFormat_Stereo;
  AAX_Result inputResult = AAX_SUCCESS, outputResult = AAX_SUCCESS;
  acfUInt32 references = 1;
  int32_t latency = 0;
  int64_t state = 0;
  IACFUnknown* Unknown() { return static_cast<AAX_IACFController*>(this); }
  ACFRESULT ACFMETHODCALLTYPE QueryInterface(const acfIID& iid, void** result) override
  {
    *result = nullptr;
    auto matches = [&](const acfIID& other) { return std::memcmp(&iid, &other, sizeof(iid)) == 0; };
    if (matches(IID_IACFUnknown)) *result = Unknown();
    else if (matches(IID_IAAXControllerV1)) *result = static_cast<AAX_IACFController*>(this);
    else if (matches(IID_IAAXControllerV2)) *result = static_cast<AAX_IACFController_V2*>(this);
    else if (matches(IID_IAAXControllerV3)) *result = static_cast<AAX_IACFController_V3*>(this);
    else if (matches(IID_IAAXTransportV1)) *result = static_cast<AAX_IACFTransport*>(this);
    else if (matches(IID_IAAXAutomationDelegateV1)) *result = static_cast<AAX_IACFAutomationDelegate*>(this);
    else return ACF_E_NOINTERFACE;
    AddRef();
    return ACF_OK;
  }
  acfUInt32 ACFMETHODCALLTYPE AddRef() override { return ++references; }
  acfUInt32 ACFMETHODCALLTYPE Release() override { return --references; }
  AAX_Result GetEffectID(AAX_IString* value) const override { *value = "bus-layout-test"; return AAX_SUCCESS; }
  AAX_Result GetSampleRate(AAX_CSampleRate* value) const override { *value = 48000.; return AAX_SUCCESS; }
  AAX_Result GetInputStemFormat(AAX_EStemFormat* value) const override { if (inputResult == AAX_SUCCESS) *value = input; return inputResult; }
  AAX_Result GetOutputStemFormat(AAX_EStemFormat* value) const override { if (outputResult == AAX_SUCCESS) *value = output; return outputResult; }
  AAX_Result GetSignalLatency(int32_t* value) const override { *value = latency; return AAX_SUCCESS; }
  AAX_Result SetSignalLatency(int32_t value) override { latency = value; return AAX_SUCCESS; }
  AAX_Result GetCycleCount(AAX_EProperty, AAX_CPropertyValue* value) const override { *value = 0; return AAX_SUCCESS; }
  AAX_Result GetTODLocation(AAX_CTimeOfDay* value) const override { *value = 0; return AAX_SUCCESS; }
  AAX_Result SetCycleCount(AAX_EProperty*, AAX_CPropertyValue*, int32_t) override { return AAX_SUCCESS; }
  AAX_Result PostPacket(AAX_CFieldIndex index, const void* data, uint32_t size) override
  {
    if (index == AAX_FIELD_INDEX(iplug::AAX_SIPlugRenderInfo, mCurrentStateNum) && size == sizeof(state))
      std::memcpy(&state, data, size);
    return AAX_SUCCESS;
  }
  AAX_Result GetCurrentMeterValue(AAX_CTypeID, float* value) const override { *value = 0; return AAX_SUCCESS; }
  AAX_Result GetMeterPeakValue(AAX_CTypeID, float* value) const override { *value = 0; return AAX_SUCCESS; }
  AAX_Result ClearMeterPeakValue(AAX_CTypeID) const override { return AAX_SUCCESS; }
  AAX_Result GetMeterClipped(AAX_CTypeID, AAX_CBoolean* value) const override { *value = false; return AAX_SUCCESS; }
  AAX_Result ClearMeterClipped(AAX_CTypeID) const override { return AAX_SUCCESS; }
  AAX_Result GetMeterCount(uint32_t* value) const override { *value = 2; return AAX_SUCCESS; }
  AAX_Result GetNextMIDIPacket(AAX_CFieldIndex*, AAX_CMidiPacket*) override { return AAX_ERROR_UNIMPLEMENTED; }
  AAX_Result SendNotification(AAX_CTypeID, const void*, uint32_t) override { return AAX_SUCCESS; }
  AAX_Result GetHybridSignalLatency(int32_t* value) const override { *value = 0; return AAX_SUCCESS; }
  AAX_Result GetCurrentAutomationTimestamp(AAX_CTransportCounter* value) const override { *value = 0; return AAX_SUCCESS; }
  AAX_Result GetHostName(AAX_IString* value) const override { *value = "Test host"; return AAX_SUCCESS; }
  AAX_Result GetPlugInTargetPlatform(AAX_CTargetPlatform* value) const override { *value = kAAX_eTargetPlatform_Native; return AAX_SUCCESS; }
  AAX_Result GetIsAudioSuite(AAX_CBoolean* value) const override { *value = AAX_DOES_AUDIOSUITE; return AAX_SUCCESS; }
  AAX_Result GetCurrentTempo(double* value) const override { *value = 120.; return AAX_SUCCESS; }
  AAX_Result GetCurrentMeter(int32_t* num, int32_t* denom) const override { *num = *denom = 4; return AAX_SUCCESS; }
  AAX_Result IsTransportPlaying(bool* value) const override { *value = true; return AAX_SUCCESS; }
  AAX_Result GetCurrentTickPosition(int64_t* value) const override { *value = 0; return AAX_SUCCESS; }
  AAX_Result GetCurrentLoopPosition(bool* looping, int64_t* start, int64_t* end) const override { *looping = false; *start = *end = 0; return AAX_SUCCESS; }
  AAX_Result GetCurrentNativeSampleLocation(int64_t* value) const override { *value = 0; return AAX_SUCCESS; }
  AAX_Result GetCustomTickPosition(int64_t* value, int64_t) const override { *value = 0; return AAX_SUCCESS; }
  AAX_Result GetBarBeatPosition(int32_t* bar, int32_t* beat, int64_t* ticks, int64_t) const override { *bar = *beat = 1; *ticks = 0; return AAX_SUCCESS; }
  AAX_Result GetTicksPerQuarter(uint32_t* value) const override { *value = 960000; return AAX_SUCCESS; }
  AAX_Result GetCurrentTicksPerBeat(uint32_t* value) const override { *value = 960000; return AAX_SUCCESS; }
  AAX_Result RegisterParameter(AAX_CParamID) override { return AAX_SUCCESS; }
  AAX_Result UnregisterParameter(AAX_CParamID) override { return AAX_SUCCESS; }
  AAX_Result PostSetValueRequest(AAX_CParamID, double) const override { return AAX_SUCCESS; }
  AAX_Result PostCurrentValue(AAX_CParamID, double) const override { return AAX_SUCCESS; }
  AAX_Result PostTouchRequest(AAX_CParamID) override { return AAX_SUCCESS; }
  AAX_Result PostReleaseRequest(AAX_CParamID) override { return AAX_SUCCESS; }
  AAX_Result GetTouchState(AAX_CParamID, AAX_CBoolean* value) override { *value = false; return AAX_SUCCESS; }
};

class AAXTestTransportNode final : public AAX_IMIDINode
{
public:
  explicit AAXTestTransportNode(AAX_ITransport* transport) : mTransport(transport) {}
  AAX_CMidiStream* GetNodeBuffer() override { return &mStream; }
  AAX_Result PostMIDIPacket(AAX_CMidiPacket*) override { return AAX_SUCCESS; }
  AAX_ITransport* GetTransport() override { return mTransport; }
private:
  AAX_ITransport* mTransport;
  AAX_CMidiStream mStream{};
};
