#pragma once
#include "../config.h"
#include "IPlugBusLayout.h"
#include "IPlugAAXBusLayout.h"
#define PLUG_CHANNEL_IO "2-2 4-4 9-9 16-16 25-25 36-36 49-49 64-64"
#define PLUG_TYPE 0
#define PLUG_LATENCY 0
#define PLUG_VERSION_HEX 0x00010000
#define PLUG_DOES_STATE_CHUNKS 0
#define PLUG_UNIQUE_ID 'tAax'
#define PLUG_MFR_ID 'tMfr'
#define PLUG_BUS_LAYOUTS() AAXProbeLayouts()
#define AAX_PROBE_AUDIOSUITE_ID 'tAsu'
#define AAX_PLUG_MFR_STR "Test"
#define AAX_PLUG_CATEGORY_STR "Effect"
#ifndef AAX_DOES_AUDIOSUITE
#define AAX_DOES_AUDIOSUITE 0
#endif
#if AAX_DOES_AUDIOSUITE
#define AAX_PROBE_NATIVE_ID 'tNts'
#define AAX_TYPE_IDS AAX_PROBE_NATIVE_ID
#define AAX_TYPE_IDS_AUDIOSUITE AAX_PROBE_AUDIOSUITE_ID
#define AAX_PLUG_NAME_STR "AAX Bus Layout AS Probe"
#define BUNDLE_NAME "aaxbuslayoutas"
#else
#define AAX_PROBE_NATIVE_ID 'tNtv'
#define AAX_TYPE_IDS AAX_PROBE_NATIVE_ID
#define AAX_PLUG_NAME_STR "AAX Bus Layout Native Probe"
#define BUNDLE_NAME "aaxbuslayoutnative"
#endif
#define BUNDLE_DOMAIN "org.test"
#define BUNDLE_MFR "test"
// Explicit test-only IDs are stable when catalogue entries are reordered.
static const iplug::BusLayoutPolicy* AAXProbeLayouts()
{
  using namespace iplug;
  struct Entry { uint32_t id; const char* name; AAX_EStemFormat stem;
                 uint32_t nativeID, suiteNativeID, suiteID; };
  static const Entry entries[] = {
    {1, "Stereo", AAX_eStemFormat_Stereo, 'tNtv', 'tNts', 'tAsu'},
    {2, "Quad", AAX_eStemFormat_Quad, 'qNtv', 'qNts', 'qAsu'},
    {3, "LCRS", AAX_eStemFormat_LCRS, 'lNtv', 'lNts', 'lAsu'},
    {4, "9.1.6", AAX_eStemFormat_9_1_6, '9Ntv', '9Nts', '9Asu'},
    {11, "HOA 1", AAX_eStemFormat_Ambi_1_ACN, '1Ntv', '1Nts', '1Asu'},
    {12, "HOA 2", AAX_eStemFormat_Ambi_2_ACN, '2Ntv', '2Nts', '2Asu'},
    {13, "HOA 3", AAX_eStemFormat_Ambi_3_ACN, '3Ntv', '3Nts', '3Asu'},
    {14, "HOA 4", AAX_eStemFormat_Ambi_4_ACN, '4Ntv', '4Nts', '4Asu'},
    {15, "HOA 5", AAX_eStemFormat_Ambi_5_ACN, '5Ntv', '5Nts', '5Asu'},
    {16, "HOA 6", AAX_eStemFormat_Ambi_6_ACN, '6Ntv', '6Nts', '6Asu'},
    {17, "HOA 7", AAX_eStemFormat_Ambi_7_ACN, '7Ntv', '7Nts', '7Asu'}
  };
  static const BusLayoutPolicy policy = [] {
    BusLayoutPolicy p; p.capacities[0] = {64}; p.capacities[1] = {64}; p.defaultID = 1;
    for (const auto& entry : entries) {
      BusLayoutConfiguration c; c.id = entry.id; c.name = entry.name;
#if AAX_DOES_AUDIOSUITE
      c.aaxNativeID = entry.suiteNativeID; c.aaxAudioSuiteID = entry.suiteID;
#else
      c.aaxNativeID = entry.nativeID;
#endif
      LayoutBus input; input.id = 1; input.role = BusRole::Main; input.name = "Main input";
      aaxlayouts::FromStem(entry.stem, input.layout);
      LayoutBus output = input; output.id = 2; output.name = "Main output"; output.bypassInputBus = 0;
      c.buses[0] = {input}; c.buses[1] = {output}; p.configurations.push_back(c);
    }
    return p;
  }();
  return &policy;
}
