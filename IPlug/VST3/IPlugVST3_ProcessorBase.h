
/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
 */

#pragma once

#include "public.sdk/source/vst/vstbus.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstevents.h"

#include "IPlugAPIBase.h"
#include "IPlugProcessor.h"
#include "IPlugVST3_Defs.h"
#include "IPlugVST3_BusLayout.h"

// Custom bus type function (in global namespace)
#ifdef CUSTOM_BUSTYPE_FUNC
extern uint64_t GetAPIBusTypeForChannelIOConfig(int configIdx, iplug::ERoute dir, int busIdx, const iplug::IOConfig* pConfig, WDL_TypedBuf<uint64_t>* APIBusTypes = nullptr);
#endif

BEGIN_IPLUG_NAMESPACE

// Default bus type function (in iplug namespace)
#ifndef CUSTOM_BUSTYPE_FUNC
uint64_t GetAPIBusTypeForChannelIOConfig(int configIdx, ERoute dir, int busIdx, const IOConfig* pConfig, WDL_TypedBuf<uint64_t>* APIBusTypes = nullptr);
#endif

/** Shared VST3 processor code */
class IPlugVST3ProcessorBase : public IPlugProcessor
{
public:
  IPlugVST3ProcessorBase(Config c, IPlugAPIBase& plug);
  
  template <class T>
  bool Initialize(T* pPlug)
  {
    using namespace Steinberg::Vst;

    if (HasBusLayouts())
    {
      if (!BusLayoutsValid()) return false;
      auto candidate = *GetBusLayoutPolicy()->Find(GetBusLayoutPolicy()->defaultID);
      std::vector<SpeakerArrangement> arrangements[2];
      for (int d=0; d<2; ++d)
        for (auto& b : candidate.buses[d])
        {
          SpeakerArrangement arr;
          if (!vst3layouts::ToArrangement(b.layout, arr)) return false;
          arrangements[d].push_back(arr);
        }
      if (!CanApplyBusLayout(candidate)) return false;
      String128 name;
      for (int d=0; d<2; ++d)
        for (size_t b=0; b<candidate.buses[d].size(); ++b)
        {
          auto& bus=candidate.buses[d][b];
          Steinberg::UString(name,128).fromAscii(bus.name.empty() ? (d ? "Output" : "Input") : bus.name.c_str(),128);
          const int flags=bus.active ? BusInfo::kDefaultActive : 0;
          const BusType type=bus.role==BusRole::Main ? kMain : kAux;
          auto* native=d ? pPlug->addAudioOutput(name,arrangements[d][b],type,flags)
                         : pPlug->addAudioInput(name,arrangements[d][b],type,flags);
          mLayoutNativeBuses[d].push_back(native);
          bus.active=native->isActive();
          vst3layouts::FromArrangement(arrangements[d][b],bus.layout);
        }
      if (DoesMIDIIn()) pPlug->addEventInput(STR16("MIDI Input"),VST3_NUM_MIDI_IN_CHANS);
      if (DoesMIDIOut()) pPlug->addEventOutput(STR16("MIDI Output"),VST3_NUM_MIDI_OUT_CHANS);
      return CommitBusLayout(std::move(candidate));
    }

    Steinberg::Vst::String128 tmpStringBuf;
  
    // TODO: move this to IPlugProcessor::MaxNBuses(x,&x) method;
    int configWithMostInputBuses = 0;
    int configWithMostOutputBuses = 0;
    /*int maxNInBuses =*/ MaxNBuses(ERoute::kInput, &configWithMostInputBuses);
    int maxNOutBuses = MaxNBuses(ERoute::kOutput, &configWithMostOutputBuses);

    // Add the maximum number of input buses in any ioconfig. channel count/API Bus type will be changed later if necessary
    {
      const IOConfig* pConfig = GetIOConfig(configWithMostInputBuses);

      int nIn = pConfig->NBuses(ERoute::kInput);
      for (auto busIdx = 0; busIdx < nIn; busIdx++)
      {
        uint64_t busType = GetAPIBusTypeForChannelIOConfig(configWithMostInputBuses, ERoute::kInput, busIdx, pConfig);

        int flags = busIdx == 0 ? flags = BusInfo::BusFlags::kDefaultActive : flags = 0;
        WDL_String busName;
        GetBusName(ERoute::kInput, busIdx, nIn, busName);
        Steinberg::UString(tmpStringBuf, 128).fromAscii(busName.Get(), 128);
        pPlug->addAudioInput(tmpStringBuf, busType, busIdx > 0 ? kAux : kMain, flags);
      }
    }
    
    // Add the maximum number of output buses in any ioconfig. channel count/API Bus type will be changed later if necessary
    {
      const IOConfig* pConfig = GetIOConfig(configWithMostOutputBuses);
      int nOut = pConfig->NBuses(ERoute::kOutput);

      for (auto busIdx = 0; busIdx < nOut; busIdx++)
      {
        uint64_t busType = GetAPIBusTypeForChannelIOConfig(configWithMostOutputBuses, ERoute::kOutput, busIdx, pConfig);
        int flags = busIdx == 0 ? flags = BusInfo::BusFlags::kDefaultActive : flags = 0;
        WDL_String busName;
        GetBusName(ERoute::kOutput, busIdx, nOut, busName);
        Steinberg::UString(tmpStringBuf, 128).fromAscii(busName.Get(), 128);
        pPlug->addAudioOutput(tmpStringBuf, busType, busIdx > 0 ? kAux : kMain, flags);
      }
    }

    if (IsMidiEffect() && maxNOutBuses == 0)
    {
      int flags = 0;
      Steinberg::UString(tmpStringBuf, 128).fromAscii("Dummy Output", 128);
      pPlug->addAudioOutput(tmpStringBuf, Steinberg::Vst::SpeakerArr::kEmpty, Steinberg::Vst::BusTypes::kMain, flags);
    }
    
    if (DoesMIDIIn())
      pPlug->addEventInput(STR16("MIDI Input"), VST3_NUM_MIDI_IN_CHANS);
    
    if (DoesMIDIOut())
      pPlug->addEventOutput(STR16("MIDI Output"), VST3_NUM_MIDI_OUT_CHANS);
    return true;
  }
  
  // MIDI Processing
  void ProcessMidiIn(Steinberg::Vst::IEventList* pEventList, IPlugQueue<IMidiMsg>& editorQueue, IPlugQueue<IMidiMsg>& processorQueue);
  void ProcessMidiOut(IPlugQueue<SysExData>& sysExQueue, SysExData& sysExBuf, Steinberg::Vst::IEventList* pOutputEvents, Steinberg::int32 numSamples);
  
  // Audio Processing Setup
  template <class T>
  bool SetBusArrangements(T* pPlug, Steinberg::Vst::SpeakerArrangement* pInputBusArrangements, Steinberg::int32 numInBuses, Steinberg::Vst::SpeakerArrangement* pOutputBusArrangements, Steinberg::int32 numOutBuses)
  {
    using namespace Steinberg::Vst;

    if (HasBusLayouts())
    {
      if (!BusLayoutsValid() || mLayoutActivated || numInBuses<0 || numOutBuses<0 ||
          (numInBuses && !pInputBusArrangements) || (numOutBuses && !pOutputBusArrangements) ||
          static_cast<size_t>(numInBuses)!=mLayoutNativeBuses[0].size() ||
          static_cast<size_t>(numOutBuses)!=mLayoutNativeBuses[1].size()) return false;
      // VST3 requests describe the existing bus set. Finite topology selection is
      // available in CLAP; changing VST3 bus counts requires a host restart protocol.
      for (const auto& declaration : GetBusLayoutPolicy()->configurations)
      {
        if (declaration.buses[0].size()!=static_cast<size_t>(numInBuses) ||
            declaration.buses[1].size()!=static_cast<size_t>(numOutBuses)) continue;
        auto candidate=declaration;
        bool mapped=true;
        for (int d=0; d<2 && mapped; ++d)
          for (size_t b=0; b<candidate.buses[d].size(); ++b)
          {
            if (!vst3layouts::FromArrangement(d ? pOutputBusArrangements[b] : pInputBusArrangements[b],candidate.buses[d][b].layout))
            { mapped=false; break; }
            candidate.buses[d][b].active=mLayoutNativeBuses[d][b]->isActive();
          }
        if (!mapped || !CanApplyBusLayout(candidate)) continue;
        for (int d=0; d<2; ++d)
          for (size_t b=0; b<candidate.buses[d].size(); ++b)
          {
            auto* bus=mLayoutNativeBuses[d][b];
            bus->setArrangement(d ? pOutputBusArrangements[b] : pInputBusArrangements[b]);
            bus->setBusType(candidate.buses[d][b].role==BusRole::Main ? kMain : kAux);
            Steinberg::Vst::String128 name {};
            Steinberg::UString(name,128).fromAscii(candidate.buses[d][b].name.c_str());
            bus->setName(std::u16string(name));
          }
        return CommitBusLayout(std::move(candidate));
      }
      return false;
    }
    
    // This would seem to be a bug in Ardour
    if ((pPlug->GetHost() == kHostMixbus32C) || (pPlug->GetHost() == kHostArdour))
    {
      return true;
    }
    
    // disconnect all io pins, they will be reconnected in process
    SetChannelConnections(ERoute::kInput, 0, MaxNChannels(ERoute::kInput), false);
    SetChannelConnections(ERoute::kOutput, 0, MaxNChannels(ERoute::kOutput), false);
        
    const int maxNInBuses = MaxNBuses(ERoute::kInput);
    const int maxNOutBuses = MaxNBuses(ERoute::kOutput);
    
    // if the host is wanting more more buses that the max we have defined in channel io configs, return false
    if(numInBuses > maxNInBuses || numOutBuses > maxNOutBuses)
      return false;
  
    std::vector<int> inputBuses;
    std::vector<int> outputBuses;

    for(int i = 0; i< numInBuses; i++)
    {
      inputBuses.push_back(SpeakerArr::getChannelCount(pInputBusArrangements[i]));
    }
    
    for(int i = 0; i< numOutBuses; i++)
    {
      outputBuses.push_back(SpeakerArr::getChannelCount(pOutputBusArrangements[i]));
    }
    
    const int matchingIdx = GetIOConfigWithChanCounts(inputBuses, outputBuses);
    Steinberg::Vst::String128 tmpStringBuf;
    
    if(matchingIdx > -1)
    {
      pPlug->removeAudioBusses();

      const IOConfig* pConfig = GetIOConfig(matchingIdx);
      const int nIn = pConfig->NBuses(ERoute::kInput);
      
      for (auto inBusIdx = 0; inBusIdx < nIn; inBusIdx++)
      {
        const int flags = inBusIdx == 0 ? BusInfo::BusFlags::kDefaultActive : 0;
        SpeakerArrangement arr = GetAPIBusTypeForChannelIOConfig(matchingIdx, ERoute::kInput, inBusIdx, pConfig);
        
        WDL_String busName;
        GetBusName(ERoute::kInput, inBusIdx, nIn, busName);
        Steinberg::UString(tmpStringBuf, 128).fromAscii(busName.Get(), 128);
        pPlug->addAudioInput(tmpStringBuf, arr, (BusTypes) inBusIdx > 0, flags);
      }
      
      const int nOut = pConfig->NBuses(ERoute::kOutput);
      
      for(auto outBusIdx = 0; outBusIdx < nOut; outBusIdx++)
      {
        int flags = outBusIdx == 0 ? BusInfo::BusFlags::kDefaultActive : 0;
        SpeakerArrangement arr = GetAPIBusTypeForChannelIOConfig(matchingIdx, ERoute::kOutput, outBusIdx, pConfig);

        WDL_String busName;
        GetBusName(ERoute::kOutput, outBusIdx, nOut, busName);
        Steinberg::UString(tmpStringBuf, 128).fromAscii(busName.Get(), 128);
        pPlug->addAudioOutput(tmpStringBuf, arr, (BusTypes) outBusIdx > 0, flags);
      }
      
      return true;
    }
        
    return false;
  }
  
  void AttachBuffers(ERoute direction, int idx, int n, Steinberg::Vst::AudioBusBuffers& pBus, int nFrames, Steinberg::int32 sampleSize);
  bool SetupProcessing(const Steinberg::Vst::ProcessSetup& setup, Steinberg::Vst::ProcessSetup& storedSetup);
  bool CanProcessSampleSize(Steinberg::int32 symbolicSampleSize);
  bool SetProcessing(bool state);
  bool ValidBusLayoutProcess(const Steinberg::Vst::ProcessData& data, const Steinberg::Vst::ProcessSetup& setup) const;
  bool IsLayoutActivated() const { return mLayoutActivated; }
  void SetLayoutActivated(bool active) { mLayoutActivated=active; }
  
  // Audio Processing
  void PrepareProcessContext(Steinberg::Vst::ProcessData& data, Steinberg::Vst::ProcessSetup& setup);
  void ProcessParameterChanges(Steinberg::Vst::ProcessData& data, IPlugQueue<IMidiMsg>& fromProcessor);
  void ProcessAudio(Steinberg::Vst::ProcessData& data, Steinberg::Vst::ProcessSetup& setup, const Steinberg::Vst::BusList& ins, const Steinberg::Vst::BusList& outs);
  void Process(Steinberg::Vst::ProcessData& data, Steinberg::Vst::ProcessSetup& setup, const Steinberg::Vst::BusList& ins, const Steinberg::Vst::BusList& outs, IPlugQueue<IMidiMsg>& fromEditor, IPlugQueue<IMidiMsg>& fromProcessor, IPlugQueue<SysExData>& sysExFromEditor, SysExData& sysExBuf);
  
  // IPlugProcessor overrides
  bool SendMidiMsg(const IMidiMsg& msg) override;

private:
  std::vector<Steinberg::Vst::AudioBus*> mLayoutNativeBuses[2];
  bool mLayoutActivated = false;
  int mMaxNChansForMainInputBus = 0;
  IPlugAPIBase& mPlug;
  Steinberg::Vst::ProcessContext mProcessContext;
  IMidiQueue mMidiOutputQueue;
  bool mSidechainActive = false;
};

END_IPLUG_NAMESPACE
