/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers. 
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

#include <algorithm>
#include <cstdio>

#include "IPlugCLAP.h"
#include "IPlugCLAPBusLayout.h"
#include "IPlugPluginBase.h"
#include "plugin.hxx"
#include "host-proxy.hxx"

// TODO - respond to situations in which parameters can't be pushed (search try_push)
// Keep in the queue or discard?? - up to us?

using namespace iplug;

void ClapNameCopy(char* destination, const char* source)
{
  strncpy(destination, source, CLAP_NAME_SIZE);
  destination[CLAP_NAME_SIZE - 1] = 0;
}


IPlugCLAP::IPlugCLAP(const InstanceInfo& info, const Config& config)
  : IPlugAPIBase(config, kAPICLAP)
  , IPlugProcessor(config, kAPICLAP)
  , ClapPluginHelper(info.mDesc, info.mHost)
{
  Trace(TRACELOC, "%s", config.pluginName);
  
  int version = 0;
  
  if (CStringHasContents(info.mHost->version))
  {
    // Host versions need not contain three numeric components.
    int ver=0, rmaj=0, rmin=0;
    sscanf(info.mHost->version, "%d.%d.%d", &ver, &rmaj, &rmin);
    if (ver>=0 && ver<=0x7fff && rmaj>=0 && rmaj<=0xff && rmin>=0 && rmin<=0xff)
      version = (ver << 16) + (rmaj << 8) + rmin;
  }
  
  // Create space to store audio pointers
  int nChans = RequiredChannels();
  mAudioIO32.Resize(nChans);
  mAudioIO64.Resize(nChans);
  
  SetHost(info.mHost->name, version);
  CreateTimer();
}

uint32_t IPlugCLAP::tailGet() const noexcept
{
  return GetTailIsInfinite() ? std::numeric_limits<uint32_t>::max() : GetTailSize();
}

const void* IPlugCLAP::extension(const char* id) noexcept
{
  if (HasBusLayouts() && BusLayoutsValid())
  {
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO))
    {
      static const clap_plugin_audio_ports_config_info configInfo = {
        ClapCurrentConfig, ClapGetConfigPortInfo,
      };
      return &configInfo;
    }
    if (!std::strcmp(id, CLAP_EXT_AMBISONIC))
    {
      static const clap_plugin_ambisonic ambisonic = {
        ClapAmbisonicIsConfigSupported, ClapAmbisonicGetConfig,
      };
      return &ambisonic;
    }
  }
  return ClapPluginHelper::extension(id);
}

bool IPlugCLAP::GetBusLayoutPortInfo(const BusLayoutConfiguration& configuration,
                                     uint32_t index, bool isInput,
                                     clap_audio_port_info* info) const
{
  const auto& buses = configuration.buses[isInput ? kInput : kOutput];
  return index < buses.size() && ClapGetAudioPortInfo(buses[index], info);
}

clap_id IPlugCLAP::ClapCurrentConfig(const clap_plugin* plugin)
{
  const auto& self = ClapPluginHelper::from(plugin);
  const auto* configuration = static_cast<const IPlugCLAP&>(self).GetActiveBusLayoutConfiguration();
  return configuration ? configuration->id : CLAP_INVALID_ID;
}

bool IPlugCLAP::ClapGetConfigPortInfo(const clap_plugin* plugin, clap_id configID,
                                      uint32_t portIndex, bool isInput,
                                      clap_audio_port_info* info)
{
  const auto& self = ClapPluginHelper::from(plugin);
  const auto* policy = static_cast<const IPlugCLAP&>(self).GetBusLayoutPolicy();
  const auto* configuration = policy ? policy->Find(configID) : nullptr;
  return configuration && ClapConfigurationRepresentable(*configuration) && static_cast<const IPlugCLAP&>(self).GetBusLayoutPortInfo(*configuration, portIndex, isInput, info);
}

bool IPlugCLAP::ClapAmbisonicIsConfigSupported(const clap_plugin* plugin,
                                                const clap_ambisonic_config* config)
{
  if (!config) return false;
  const auto& self = ClapPluginHelper::from(plugin);
  const auto* policy = static_cast<const IPlugCLAP&>(self).GetBusLayoutPolicy();
  if (!policy) return false;
  for (const auto& candidate : policy->configurations) if (ClapConfigurationRepresentable(candidate))
    for (int direction = kInput; direction <= kOutput; ++direction)
      for (const auto& bus : candidate.buses[direction])
      {
        clap_ambisonic_config supported {};
        if (ClapGetAmbisonicConfig(bus, &supported) &&
            supported.ordering == config->ordering && supported.normalization == config->normalization)
          return true;
      }
  return false;
}

bool IPlugCLAP::ClapAmbisonicGetConfig(const clap_plugin* plugin, bool isInput,
                                       uint32_t portIndex, clap_ambisonic_config* config)
{
  const auto& self = ClapPluginHelper::from(plugin);
  const auto* bus = static_cast<const IPlugCLAP&>(self).GetActiveBusLayout(isInput ? kInput : kOutput, static_cast<int>(portIndex));
  return bus && ClapGetAmbisonicConfig(*bus, config);
}

void IPlugCLAP::FlushParamsIfNeeded()
{
  if (!isProcessing())
    runOnMainThread([&](){ if (!isBeingDestroyed()) GetClapHost().paramsRequestFlush(); });
}

void IPlugCLAP::BeginInformHostOfParamChange(int idx)
{
  mParamValuesToHost.PushFromArgs(ParamToHost::Type::Begin, idx, 0.0);
  FlushParamsIfNeeded();
}

void IPlugCLAP::InformHostOfParamChange(int idx, double normalizedValue)
{
  const IParam* pParam = GetParam(idx);
  const bool isDoubleType = pParam->Type() == IParam::kTypeDouble;
  const double value = isDoubleType ? normalizedValue : pParam->FromNormalized(normalizedValue);
  
  mParamValuesToHost.PushFromArgs(ParamToHost::Type::Value, idx, value);
  FlushParamsIfNeeded();
}

void IPlugCLAP::EndInformHostOfParamChange(int idx)
{
  mParamValuesToHost.PushFromArgs(ParamToHost::Type::End, idx, 0.0);
  FlushParamsIfNeeded();
}

bool IPlugCLAP::EditorResize(int viewWidth, int viewHeight)
{
  if (HasUI())
  {
    if (viewWidth != GetEditorWidth() || viewHeight != GetEditorHeight())
    {
      GetClapHost().guiRequestResize(viewWidth, viewHeight);
    }
    
    SetEditorSize(viewWidth, viewHeight);
  }
  
  return true;
}

// IPlugProcessor
void IPlugCLAP::SetTailSize(int samples)
{
  IPlugProcessor::SetTailSize(samples);
  
  if (GetClapHost().canUseTail())
    mTailUpdate = true;
}

void IPlugCLAP::SetLatency(int samples)
{
  IPlugProcessor::SetLatency(samples);
  
  if (GetClapHost().canUseLatency())
  {
    // If active request restart on the main thread (or update the host)
    if (isActive())
    {
      mLatencyUpdate = true;
      runOnMainThread([&](){ if (!isBeingDestroyed()) GetClapHost().requestRestart(); });
    }
    else
    {
      runOnMainThread([&](){ if (!isBeingDestroyed()) GetClapHost().latencyChanged(); });
    }
  }
}

bool IPlugCLAP::SendMidiMsg(const IMidiMsg& msg)
{
  mMidiToHost.Add(msg);
  return true;
}

bool IPlugCLAP::SendSysEx(const ISysEx& msg)
{
  // TODO - don't copy the data
  SysExData data(msg.mOffset, msg.mSize, msg.mData);
  mSysExToHost.Add(data);
  return true;
}

// clap_plugin
bool IPlugCLAP::init() noexcept
{
  if (HasBusLayouts())
  {
    const auto* policy = GetBusLayoutPolicy();
    const auto* configuration = policy ? policy->Find(policy->defaultID) : nullptr;
    return configuration && ClapConfigurationRepresentable(*configuration) && CommitBusLayout(*configuration);
  }

  SetDefaultConfig();
  
  return true;
}

bool IPlugCLAP::activate(double sampleRate, uint32_t minFrameCount, uint32_t maxFrameCount) noexcept
{
  if (HasBusLayouts() && (!GetActiveBusLayoutConfiguration() || !maxFrameCount ||
      !std::isfinite(sampleRate) || sampleRate<=0 || minFrameCount>maxFrameCount ||
      static_cast<uint64_t>(maxFrameCount)*std::max(1,RequiredChannels())>INT32_MAX/sizeof(double))) return false;
  SetBlockSize(maxFrameCount);
  if (HasBusLayouts())
  {
    mConstantAudio32.Resize(RequiredChannels() * static_cast<int>(maxFrameCount));
    mConstantAudio64.Resize(RequiredChannels() * static_cast<int>(maxFrameCount));
  }
  SetSampleRate(sampleRate);
  OnActivate(true);
  OnParamReset(kReset);
  OnReset();

  mHostHasTail = GetClapHost().canUseTail();
  mTailCount = 0;

  return true;
}

void IPlugCLAP::deactivate() noexcept
{
  OnActivate(false);
  
  if (mLatencyUpdate)
  {
    GetClapHost().latencyChanged();
    mLatencyUpdate = false;
  }

  // TODO - should we clear mTailUpdate here or elsewhere?
}

template <typename T>
const T* ClapEventCast(const clap_event_header_t* event)
{
  return reinterpret_cast<const T*>(event);
}

template <typename T>
bool InputIsSilent(const T* data, int nFrames)
{
  // For local tail processing find non-zero inputs
  auto isZero = [](T x) { return x == T(0); };

  return std::find_if_not(data, data + nFrames, isZero) == (data + nFrames);
}

clap_process_status IPlugCLAP::process(const clap_process* pProcess) noexcept
{
  bool checkedFormat64=false;
  if (HasBusLayouts())
  {
  if (!pProcess || (pProcess->audio_inputs_count && !pProcess->audio_inputs) ||
      (pProcess->audio_outputs_count && !pProcess->audio_outputs) || pProcess->frames_count > static_cast<uint32_t>(GetBlockSize()))
    return CLAP_PROCESS_ERROR;
  bool hasFormat=false;
  uint64_t checkedInputs=0, checkedOutputs=0;
  const auto checkBuffers = [&](const clap_audio_buffer* buffers, uint32_t count, uint64_t& channels) {
    for (uint32_t i=0; i<count; ++i) {
      const auto& buffer=buffers[i];
      if (buffer.channel_count && (!buffer.data32 && !buffer.data64)) return false;
      if (buffer.channel_count && buffer.data32 && buffer.data64) return false;
      const bool is64=buffer.data64 != nullptr;
      if (buffer.channel_count && hasFormat && is64 != checkedFormat64) return false;
      if (buffer.channel_count) { checkedFormat64=is64; hasFormat=true; }
      channels += buffer.channel_count;
      if (channels > static_cast<uint64_t>(INT32_MAX)) return false;
    }
    return true;
  };
  if (!checkBuffers(pProcess->audio_inputs,pProcess->audio_inputs_count,checkedInputs) ||
      !checkBuffers(pProcess->audio_outputs,pProcess->audio_outputs_count,checkedOutputs)) return CLAP_PROCESS_ERROR;

  {
    const auto* c=GetActiveBusLayoutConfiguration();
    if (!BusLayoutsValid() || !c || pProcess->audio_inputs_count!=c->buses[0].size() || pProcess->audio_outputs_count!=c->buses[1].size()) return CLAP_PROCESS_ERROR;
    for (uint32_t b=0;b<pProcess->audio_inputs_count;++b) if (pProcess->audio_inputs[b].channel_count!=c->buses[0][b].layout.NChannels()) return CLAP_PROCESS_ERROR;
    for (uint32_t b=0;b<pProcess->audio_outputs_count;++b) if (pProcess->audio_outputs[b].channel_count!=c->buses[1][b].layout.NChannels()) return CLAP_PROCESS_ERROR;
  }
  }
  IMidiMsg msg;
  SysExData sysEx;
  
  // Transport Info
  if (pProcess->transport)
  {
    ITimeInfo timeInfo;

    auto pTransport = pProcess->transport;

    constexpr double beatFactor = static_cast<double>(CLAP_BEATTIME_FACTOR);
    constexpr double secFactor = static_cast<double>(CLAP_SECTIME_FACTOR);

    if (pTransport->flags & CLAP_TRANSPORT_HAS_TEMPO)
      timeInfo.mTempo = pTransport->tempo;
    
    // N.B. If there is no seconds timeline there is no way to get the sample position (the plugin one is not global)
    if (pTransport->flags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE)
      timeInfo.mSamplePos = (GetSampleRate() * static_cast<double>(pTransport->song_pos_seconds) / secFactor);
    
    if (pTransport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE)
    {
      timeInfo.mPPQPos = static_cast<double>(pTransport->song_pos_beats) / beatFactor;
      timeInfo.mLastBar = static_cast<double>(pTransport->bar_start) / beatFactor;
      timeInfo.mCycleStart = static_cast<double>(pTransport->loop_start_beats) / beatFactor;
      timeInfo.mCycleEnd = static_cast<double>(pTransport->loop_end_beats) / beatFactor;
    }

    if (pTransport->flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE)
    {
      timeInfo.mNumerator = static_cast<int>(pTransport->tsig_num);
      timeInfo.mDenominator = static_cast<int>(pTransport->tsig_denom);
    }

    timeInfo.mTransportIsRunning = pTransport->flags & CLAP_TRANSPORT_IS_PLAYING;
    timeInfo.mTransportLoopEnabled = pTransport->flags & CLAP_TRANSPORT_IS_LOOP_ACTIVE;
    
    SetTimeInfo(timeInfo);
  }
  
  // Input Events
  ProcessInputEvents(pProcess->in_events);
  
  while (mMidiMsgsFromEditor.Pop(msg))
  {
    ProcessMidiMsg(msg);
  }
  
  while (mSysExDataFromEditor.Pop(sysEx))
  {
    SendSysEx(ISysEx(sysEx.mOffset, sysEx.mData, sysEx.mSize));
  }
  
  // Do Audio Processing!
  int nIns = 0;
  int nOuts = 0;
  int nFrames = pProcess->frames_count;
  
  // Local tail handling
  bool localTail = !mHostHasTail && !GetTailIsInfinite() && GetTailSize();
  bool insQuiet = true;
  
  // Sum IO channels
  for (uint32_t i = 0; i < pProcess->audio_inputs_count; i++)
    nIns += static_cast<int>(pProcess->audio_inputs[i].channel_count);
  
  for (uint32_t i = 0; i < pProcess->audio_outputs_count; i++)
    nOuts += static_cast<int>(pProcess->audio_outputs[i].channel_count);
  
  bool format64=checkedFormat64;
  if (!HasBusLayouts())
  {
    if (pProcess->audio_inputs_count && pProcess->audio_inputs && pProcess->audio_inputs[0].channel_count)
      format64=pProcess->audio_inputs[0].data64;
    else if (pProcess->audio_outputs_count && pProcess->audio_outputs && pProcess->audio_outputs[0].channel_count)
      format64=pProcess->audio_outputs[0].data64;
  }
      
  // Assert that all formats match
#ifndef NDEBUG
  for (uint32_t i = 0; i < pProcess->audio_inputs_count; i++)
  {
    auto bus = pProcess->audio_inputs[i];
    assert(!bus.channel_count || format64 == static_cast<bool>(bus.data64));
  }
  
  for (uint32_t i = 0; i < pProcess->audio_outputs_count; i++)
  {
    auto bus = pProcess->audio_outputs[i];
    assert(!bus.channel_count ||  format64 == static_cast<bool>(bus.data64));
  }
#endif

  if (HasBusLayouts())
  {
    const auto* configuration = GetActiveBusLayoutConfiguration();
    if (!BusLayoutsValid() || !configuration ||
        pProcess->audio_inputs_count != configuration->buses[kInput].size() ||
        pProcess->audio_outputs_count != configuration->buses[kOutput].size() ||
        !BeginBusLayoutBlock(nFrames))
      return CLAP_PROCESS_ERROR;

    // Conservative tail handling avoids sleeping on an uninspected live input.
    if (nIns) insQuiet=false;
    int constantChannel=0;
    for (uint32_t i = 0; i < pProcess->audio_inputs_count; ++i)
    {
      const auto& buffer = pProcess->audio_inputs[i];
      if (buffer.channel_count != configuration->buses[kInput][i].layout.NChannels()) return CLAP_PROCESS_ERROR;
      if (format64)
      {
        for (uint32_t channel=0; channel<buffer.channel_count; ++channel)
        {
          mAudioIO64.Get()[channel]=buffer.data64[channel];
          if (channel < 64 && (buffer.constant_mask & (uint64_t(1) << channel)))
          {
            double* scratch=mConstantAudio64.Get() + static_cast<size_t>(constantChannel++) * nFrames;
            std::fill(scratch, scratch+nFrames, nFrames && buffer.data64[channel] ? buffer.data64[channel][0] : 0.);
            mAudioIO64.Get()[channel]=scratch;
          }
        }
        if (!AttachBusBuffers(kInput, static_cast<int>(i), static_cast<int>(buffer.channel_count), mAudioIO64.Get(), nFrames)) return CLAP_PROCESS_ERROR;
      }
      else
      {
        for (uint32_t channel=0; channel<buffer.channel_count; ++channel)
        {
          mAudioIO32.Get()[channel]=buffer.data32[channel];
          if (channel < 64 && (buffer.constant_mask & (uint64_t(1) << channel)))
          {
            float* scratch=mConstantAudio32.Get() + static_cast<size_t>(constantChannel++) * nFrames;
            std::fill(scratch, scratch+nFrames, nFrames && buffer.data32[channel] ? buffer.data32[channel][0] : 0.f);
            mAudioIO32.Get()[channel]=scratch;
          }
        }
        if (!AttachBusBuffers(kInput, static_cast<int>(i), static_cast<int>(buffer.channel_count), mAudioIO32.Get(), nFrames)) return CLAP_PROCESS_ERROR;
      }
    }
    for (uint32_t i = 0; i < pProcess->audio_outputs_count; ++i)
    {
      auto& buffer = pProcess->audio_outputs[i];
      if (buffer.channel_count != configuration->buses[kOutput][i].layout.NChannels())
        return CLAP_PROCESS_ERROR;
      const bool attached = format64
        ? AttachBusBuffers(kOutput, static_cast<int>(i), static_cast<int>(buffer.channel_count), buffer.data64, nFrames)
        : AttachBusBuffers(kOutput, static_cast<int>(i), static_cast<int>(buffer.channel_count), buffer.data32, nFrames);
      if (!attached) return CLAP_PROCESS_ERROR;
    }
    for (uint32_t i=0;i<pProcess->audio_outputs_count;++i)
      pProcess->audio_outputs[i].constant_mask=0;

    if (GetBypassed())
    {
      if (format64) PassThroughBuffers(0.0,nFrames);
      else PassThroughBuffers(0.f,nFrames);
    }
    else
    {
      if (format64) ProcessBuffers(0.0,nFrames);
      else ProcessBuffers(0.f,nFrames);
    }
  }
  else
  {
  SetChannelConnections(ERoute::kInput, 0, MaxNChannels(ERoute::kInput), false);
  SetChannelConnections(ERoute::kInput, 0, nIns, true);

  if (nIns > 0)
  {
    // Copy and attach buffer pointers
    if (format64)
    {
      for (uint32_t i = 0, k = 0; i < pProcess->audio_inputs_count; i++)
      {
        auto bus = pProcess->audio_inputs[i];
        
        for (uint32_t j = 0; j < bus.channel_count; j++, k++)
        {
          mAudioIO64.Get()[k] = bus.data64[j];
          
          // For local tail processing check for non-zero inputs

          if (localTail && insQuiet)
            insQuiet = InputIsSilent(bus.data64[j], nFrames);
        }
      }
      
      AttachBuffers(ERoute::kInput, 0, nIns, mAudioIO64.Get(), nFrames);
    }
    else
    {
      for (uint32_t i = 0, k = 0; i < pProcess->audio_inputs_count; i++)
      {
        auto bus = pProcess->audio_inputs[i];

        for (uint32_t j = 0; j < bus.channel_count; j++, k++)
        {
          mAudioIO32.Get()[k] = bus.data32[j];
          
          // For local tail processing check for non-zero inputs
          if (localTail && insQuiet)
            insQuiet = InputIsSilent(bus.data32[j], nFrames);
        }
      }
      
      AttachBuffers(ERoute::kInput, 0, nIns, mAudioIO32.Get(), nFrames);
    }
  }

  SetChannelConnections(ERoute::kOutput, 0, MaxNChannels(ERoute::kOutput), false);
  SetChannelConnections(ERoute::kOutput, 0, nOuts, true);

  if (nOuts > 0)
  {
    // Copy and attach buffer pointers
    if (format64)
    {
      for (uint32_t i = 0, k = 0; i < pProcess->audio_outputs_count; i++)
      {
        auto bus = pProcess->audio_outputs[i];
        for (uint32_t j = 0; j < bus.channel_count; j++, k++)
          mAudioIO64.Get()[k] = bus.data64[j];
      }
      
      AttachBuffers(ERoute::kOutput, 0, nOuts, mAudioIO64.Get(), nFrames);
    }
    else
    {
      for (uint32_t i = 0, k = 0; i < pProcess->audio_outputs_count; i++)
      {
        auto bus = pProcess->audio_outputs[i];
        for (uint32_t j = 0; j < bus.channel_count; j++, k++)
          mAudioIO32.Get()[k] = bus.data32[j];
      }

      AttachBuffers(ERoute::kOutput, 0, nOuts, mAudioIO32.Get(), nFrames);
    }
  }

  if (format64)
    ProcessBuffers(0.0, nFrames);
  else
    ProcessBuffers(0.f, nFrames);
  }
    
  // Send Events Out (Parameters and MIDI)
  ProcessOutputEvents(pProcess->out_events, nFrames);
  
  // Update tail if relevant
  if (mTailUpdate)
  {
    GetClapHost().tailChanged();
    mTailUpdate = false;
  }
  
  // Local tail handling
  if (mHostHasTail)
    return CLAP_PROCESS_TAIL;
  
#if PLUG_TYPE == 0 // Only implement local tail for effects
  // No tail
  if (!GetTailSize())
    return CLAP_PROCESS_CONTINUE_IF_NOT_QUIET;
  
  // Infinite tail
  if (GetTailIsInfinite())
    return CLAP_PROCESS_CONTINUE;
  
  // Finite tail
  mTailCount = insQuiet ? std::min(mTailCount + nFrames, GetTailSize()) : 0;
  
  return mTailCount < GetTailSize() ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;

#else
  return CLAP_PROCESS_CONTINUE;
#endif
}

// clap_plugin_render
bool IPlugCLAP::renderSetMode(clap_plugin_render_mode mode) noexcept
{
  SetRenderingOffline(mode == CLAP_RENDER_OFFLINE);
  return true;
}

// clap_plugin_state
bool IPlugCLAP::stateSave(const clap_ostream* pStream) noexcept
{
  IByteChunk chunk;
  
  if (!SerializeState(chunk))
    return false;
  
  return pStream->write(pStream, chunk.GetData(), chunk.Size()) == chunk.Size();
}

bool IPlugCLAP::stateLoad(const clap_istream* pStream) noexcept
{
  constexpr int bytesPerBlock = 256;
  char buffer[bytesPerBlock];
  int64_t bytesRead = 0;
  
  IByteChunk chunk;
    
  while ((bytesRead = pStream->read(pStream, buffer, bytesPerBlock)) > 0)
    chunk.PutBytes(buffer, static_cast<int>(bytesRead));

  if (bytesRead != 0)
    return false;
      
  bool restoredOK = UnserializeState(chunk, 0) >= 0;
  
  if (restoredOK)
    OnRestoreState();
  
  return restoredOK;
}

// clap_plugin_params
bool IPlugCLAP::paramsInfo(uint32_t paramIdx, clap_param_info* pInfo) const noexcept
{
  assert(MAX_PARAM_NAME_LEN <= CLAP_NAME_SIZE && "iPlug parameter name size exceeds CLAP maximum");
  assert(MAX_PARAM_GROUP_LEN <= CLAP_PATH_SIZE && "iPlug group name size exceeds CLAP maximum");

  const IParam* pParam = GetParam(paramIdx);
  const bool isDoubleType = pParam->Type() == IParam::kTypeDouble;
  
  clap_param_info_flags flags = CLAP_PARAM_REQUIRES_PROCESS; // TO DO - check this with Alex B
  
  if (!isDoubleType)
    flags |= CLAP_PARAM_IS_STEPPED;
  if (pParam->GetCanAutomate())
    flags |= CLAP_PARAM_IS_AUTOMATABLE;
  
  pInfo->id = paramIdx;
  pInfo->flags = flags;
  pInfo->cookie = nullptr;

  strcpy(pInfo->name, pParam->GetName());
  strcpy(pInfo->module, pParam->GetGroup());

  // Values
  pInfo->min_value = isDoubleType ? 0.0 : pParam->GetMin();
  pInfo->max_value = isDoubleType ? 1.0 : pParam->GetMax();
  pInfo->default_value = pParam->GetDefault(isDoubleType);
  
  return true;
}

bool IPlugCLAP::paramsValue(clap_id paramIdx, double* pValue) noexcept
{
  const IParam* pParam = GetParam(paramIdx);
  const bool isDoubleType = pParam->Type() == IParam::kTypeDouble;
  *pValue = isDoubleType ? pParam->GetNormalized() : pParam->Value();
  return true;
}

bool IPlugCLAP::paramsValueToText(clap_id paramIdx, double value, char* display, uint32_t size) noexcept
{
  const IParam* pParam = GetParam(paramIdx);
  const bool isDoubleType = pParam->Type() == IParam::kTypeDouble;

  WDL_String str;
  
  pParam->GetDisplay(value, isDoubleType, str);
  
  // Add Label
  if (CStringHasContents(pParam->GetLabel()))
  {
    str.Append(" ");
    str.Append(pParam->GetLabel());
  }
  
  if (size < str.GetLength())
    return false;
    
  strcpy(display, str.Get());
  return true;
}

bool IPlugCLAP::paramsTextToValue(clap_id paramIdx, const char* display, double* pValue) noexcept
{
  const IParam* pParam = GetParam(paramIdx);
  const bool isDoubleType = pParam->Type() == IParam::kTypeDouble;
  const double paramValue = pParam->StringToValue(display);
  
  *pValue = isDoubleType ? pParam->ToNormalized(paramValue) : paramValue;
  return true;
}

void IPlugCLAP::paramsFlush(const clap_input_events* pInputParamChanges, const clap_output_events* pOutputParamChanges) noexcept
{
  ProcessInputEvents(pInputParamChanges);
  ProcessOutputParams(pOutputParamChanges);
}

void IPlugCLAP::ProcessInputEvents(const clap_input_events* pInputEvents) noexcept
{
  IMidiMsg msg;

  if (pInputEvents)
  {
    for (int i = 0; i < pInputEvents->size(pInputEvents); i++)
    {
      auto pEvent = pInputEvents->get(pInputEvents, i);
      
      if (pEvent->space_id != CLAP_CORE_EVENT_SPACE_ID)
        continue;
      
      switch (pEvent->type)
      {
        case CLAP_EVENT_NOTE_ON:
        {
          // N.B. velocity stored 0-1
          auto pNote = ClapEventCast<clap_event_note>(pEvent);
          auto velocity = static_cast<int>(std::round(pNote->velocity * 127.0));
          msg.MakeNoteOnMsg(pNote->key, velocity, pEvent->time, pNote->channel);
          ProcessMidiMsg(msg);
          mMidiMsgsFromProcessor.Push(msg);
          break;
        }
          
        case CLAP_EVENT_NOTE_OFF:
        {
          auto pNote = ClapEventCast<clap_event_note>(pEvent);
          msg.MakeNoteOffMsg(pNote->key, pEvent->time, pNote->channel);
          ProcessMidiMsg(msg);
          mMidiMsgsFromProcessor.Push(msg);
          break;
        }
          
        case CLAP_EVENT_MIDI:
        {
          auto pMidiEvent = ClapEventCast<clap_event_midi>(pEvent);
          msg = IMidiMsg(pEvent->time, pMidiEvent->data[0], pMidiEvent->data[1], pMidiEvent->data[2]);
          ProcessMidiMsg(msg);
          mMidiMsgsFromProcessor.Push(msg);
          break;
        }
          
        case CLAP_EVENT_MIDI_SYSEX:
        {
          auto pSysexEvent = ClapEventCast<clap_event_midi_sysex>(pEvent);
          ISysEx sysEx(pEvent->time, pSysexEvent->buffer, pSysexEvent->size);
          ProcessSysEx(sysEx);
          mSysExDataFromProcessor.PushFromArgs(sysEx.mOffset, sysEx.mSize, sysEx.mData);
          break;
        }
          
        case CLAP_EVENT_PARAM_VALUE:
        {
          auto pParamValue = ClapEventCast<clap_event_param_value>(pEvent);
          
          int paramIdx = pParamValue->param_id;
          double value = pParamValue->value;
          
          IParam* pParam = GetParam(paramIdx);
          const bool isDoubleType = pParam->Type() == IParam::kTypeDouble;
          
          if (isDoubleType)
            pParam->SetNormalized(value);
          else
            pParam->Set(value);
          
          SendParameterValueFromAPI(paramIdx, value, isDoubleType);
          OnParamChange(paramIdx, EParamSource::kHost, pEvent->time);
          break;
        }
          
        default:
          break;
      }
    }
  }
}
  
void IPlugCLAP::ProcessOutputParams(const clap_output_events* pOutputParamChanges) noexcept
{
  ParamToHost change;
  
  while (mParamValuesToHost.Pop(change))
  {
    // Construct output stream
    bool isValue = change.type() == CLAP_EVENT_PARAM_VALUE;
    
    clap_event_header_t header;

    // N.B. - paramaters output here almost certainly come from the UI
    // They cannot be set with a sample offset (this is a limitation of the current IPlug2 API)
    
    header.size = isValue ? sizeof(clap_event_param_value) : sizeof(clap_event_param_gesture);
    header.time = 0;
    header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    header.type = change.type();
    header.flags = 0;

    if (isValue)
    {
      clap_event_param_value event { header, change.idx(), nullptr, -1, -1, -1, -1, change.value() };
      pOutputParamChanges->try_push(pOutputParamChanges, &event.header);
    }
    else
    {
      clap_event_param_gesture event { header, change.idx() };
      pOutputParamChanges->try_push(pOutputParamChanges, &event.header);
    }
  }
}

void IPlugCLAP::ProcessOutputEvents(const clap_output_events* pOutputEvents, int nFrames) noexcept
{
  // N.B. Midi events and sysEx events are ordered by the respective queues
  // Here we ensure correct ordering between the two queues
  // Parameters can only be sent at the start of each block so are processed first
    
  ProcessOutputParams(pOutputEvents);
  
  if (pOutputEvents)
  {
    clap_event_header_t header;

    while (mMidiToHost.ToDo() || mSysExToHost.ToDo())
    {
      int midiMsgOffset = nFrames;
      int sysExOffset = nFrames;
      
      // Look at the next two items to ensure correct ordering
      if (mMidiToHost.ToDo())
        midiMsgOffset = mMidiToHost.Peek().mOffset;
        
      if (mSysExToHost.ToDo())
        sysExOffset = mSysExToHost.Peek().mOffset;
        
      // Don't move beyond the current frame
      if (std::min(midiMsgOffset, sysExOffset) >= nFrames)
        break;
      
      if (sysExOffset <= midiMsgOffset)
      {
        auto data = mSysExToHost.Peek();
        
        uint32_t dataSize = static_cast<uint32_t>(data.mSize);
        
        header.size = sizeof(clap_event_midi_sysex);
        header.time = data.mOffset;
        header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        header.type = CLAP_EVENT_MIDI_SYSEX;
        header.flags = 0;
        
        clap_event_midi_sysex sysex_event { header, 0, data.mData, dataSize };
        
        pOutputEvents->try_push(pOutputEvents, &sysex_event.header);
        
        mSysExToHost.Remove();
      }
      else
      {
        auto msg = mMidiToHost.Peek();
        auto status = msg.mStatus;
    
        // Construct output stream
        header.size = sizeof(clap_event_param_value);
        header.time = msg.mOffset;
        header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        header.type = CLAP_EVENT_MIDI;
        header.flags = 0;
        
        if (msg.StatusMsg() == IMidiMsg::kNoteOn)
          header.type = CLAP_EVENT_NOTE_ON;
        
        if (msg.StatusMsg() == IMidiMsg::kNoteOff)
          header.type = CLAP_EVENT_NOTE_OFF;

        if (header.type == CLAP_EVENT_NOTE_ON || header.type == CLAP_EVENT_NOTE_OFF)
        {
          int16_t channel = static_cast<int16_t>(msg.Channel());
          clap_event_note note_event { header, -1, 0,  channel, msg.mData1, static_cast<double>(msg.mData2) / 127.0};
          pOutputEvents->try_push(pOutputEvents, &note_event.header);
        }
        else
        {
          clap_event_midi midi_event { header, 0, { status, msg.mData1, msg.mData2 } };
          pOutputEvents->try_push(pOutputEvents, &midi_event.header);
        }
        
        mMidiToHost.Remove();
      }
    }
    
    mMidiToHost.Flush(nFrames);
    mSysExToHost.Flush(nFrames);
  }
}

const char* ClapPortType(uint32_t nChans)
{
  // TODO - add support for surround/ambisonics or allow user setting?
  return nChans == 2 ? CLAP_PORT_STEREO : (nChans == 1 ? CLAP_PORT_MONO : nullptr);
}

bool IPlugCLAP::implementsAudioPorts() const noexcept
{
  if (HasBusLayouts())
    return BusLayoutsValid() && GetActiveBusLayoutConfiguration();
  return MaxNBuses(ERoute::kInput) || MaxNBuses(ERoute::kOutput);
}

uint32_t IPlugCLAP::audioPortsCount(bool isInput) const noexcept
{
  if (const auto* configuration = GetActiveBusLayoutConfiguration())
    return static_cast<uint32_t>(configuration->buses[isInput ? kInput : kOutput].size());
  return NBuses(isInput ? ERoute::kInput : ERoute::kOutput);
}

bool IPlugCLAP::audioPortsInfo(uint32_t index, bool isInput, clap_audio_port_info* pInfo) const noexcept
{
  if (const auto* configuration = GetActiveBusLayoutConfiguration())
    return GetBusLayoutPortInfo(*configuration, index, isInput, pInfo);

  if (index >= audioPortsCount(isInput) || !pInfo)
    return false;

  // TODO - should we use in place pairs?
  WDL_String busName;

  const auto direction = isInput ? ERoute::kInput : ERoute::kOutput;
  const auto nBuses = NBuses(direction);
  const auto nChans = NChannels(direction, index);

  GetBusName(direction, index, nBuses, busName);
  
  constexpr uint32_t bitFlags = CLAP_AUDIO_PORT_SUPPORTS_64BITS
                              | CLAP_AUDIO_PORT_PREFERS_64BITS
                              | CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_SIZE;
  
  pInfo->id = index;
  ClapNameCopy(pInfo->name, busName.Get());
  pInfo->flags = !index ? bitFlags | CLAP_AUDIO_PORT_IS_MAIN : bitFlags;
  pInfo->channel_count = nChans;
  pInfo->port_type = ClapPortType(pInfo->channel_count);
  pInfo->in_place_pair = CLAP_INVALID_ID;
  return true;
}

bool IPlugCLAP::implementsAudioPortsConfig() const noexcept
{
  if (HasBusLayouts())
    return BusLayoutsValid();
  return audioPortsConfigCount();
}

uint32_t IPlugCLAP::audioPortsConfigCount() const noexcept
{
  if (const auto* policy = GetBusLayoutPolicy())
  {
    uint32_t count=0;
    if (BusLayoutsValid()) for (const auto& c:policy->configurations) if (ClapConfigurationRepresentable(c)) ++count;
    return count;
  }
  return static_cast<uint32_t>(NIOConfigs());
}

bool IPlugCLAP::audioPortsGetConfig(uint32_t index, clap_audio_ports_config* pConfig) const noexcept
{
  if (index >= audioPortsConfigCount())
    return false;

  if (const auto* policy = GetBusLayoutPolicy())
  {
    for (const auto& c:policy->configurations) if (ClapConfigurationRepresentable(c))
    { if (!index--) return ClapGetAudioPortsConfig(c,pConfig); }
    return false;
  }
  
  WDL_String configName;

  // TODO - review naming or add option for names...
  auto getNChans = [&](ERoute direction, int bus)
  {
    return static_cast<uint32_t>(NChannels(direction, static_cast<uint32_t>(bus), index));
  };
  
  auto getDirectionName = [&](ERoute direction)
  {
    // N.B. Configs in iPlug2 currently have no names so we reconstruct the strings...
    configName.AppendFormatted(CLAP_NAME_SIZE, "%d", getNChans(direction, 0));
    
    for (int i = 0; i < NBuses(direction, index); i++)
      configName.AppendFormatted(CLAP_NAME_SIZE, ".%d", getNChans(direction, i));
  };
  
  getDirectionName(kInput);
  configName.Append("-");
  getDirectionName(kOutput);

  pConfig->id = index;
  ClapNameCopy(pConfig->name, configName.Get());
 
  pConfig->input_port_count = static_cast<uint32_t>(NBuses(kInput, index));
  pConfig->output_port_count = static_cast<uint32_t>(NBuses(kOutput, index));

  pConfig->has_main_input = pConfig->input_port_count > 1;
  pConfig->main_input_channel_count = pConfig->has_main_input ? getNChans(kInput, 0) : 0;
  pConfig->main_input_port_type = ClapPortType(pConfig->main_input_channel_count);
  
  pConfig->has_main_output = pConfig->output_port_count > 1;
  pConfig->main_output_channel_count = pConfig->has_main_input ? getNChans(kOutput, 0) : 0;
  pConfig->main_output_port_type = ClapPortType(pConfig->main_output_channel_count);

  return true;
}

bool IPlugCLAP::audioPortsSetConfig(clap_id configIdx) noexcept
{
  if (const auto* policy = GetBusLayoutPolicy())
  {
    const auto* configuration=policy->Find(configIdx);
    return !isActive() && configuration && ClapConfigurationRepresentable(*configuration) && CommitBusLayout(*configuration);
  }
  if (configIdx >= audioPortsConfigCount()) return false;
  
  mConfigIdx = static_cast<int>(configIdx);
  
  return true;
}

bool IPlugCLAP::BuildBusLayoutCandidate(const clap_audio_port_configuration_request* requests,
                                        uint32_t requestCount,
                                        BusLayoutConfiguration& candidate) const
{
  const auto* active = GetActiveBusLayoutConfiguration();
  if (!active || (requestCount && !requests)) return false;
  if (requestCount > active->buses[kInput].size() + active->buses[kOutput].size()) return false;
  candidate = *active;
  for (uint32_t i = 0; i < requestCount; ++i)
  {
    const auto& request = requests[i];
    const auto direction=request.is_input ? kInput : kOutput;
    auto& buses = candidate.buses[direction];
    for (uint32_t prior=0; prior<i; ++prior)
      if (requests[prior].is_input == request.is_input && requests[prior].port_index == request.port_index) return false;
    if (request.port_index >= buses.size() || request.channel_count > static_cast<uint32_t>(GetBusBufferCapacity(direction, static_cast<int>(request.port_index))) ||
        !ClapSetLayoutFromRequest(buses[request.port_index].layout, request))
      return false;
  }
  return ClapConfigurationRepresentable(candidate);
}

bool IPlugCLAP::implementsConfigurableAudioPorts() const noexcept
{
  return HasBusLayouts() && BusLayoutsValid() && GetActiveBusLayoutConfiguration();
}

bool IPlugCLAP::configurableAudioPortsCanApplyConfiguration(const clap_audio_port_configuration_request* requests,
                                                             uint32_t requestCount) const noexcept
{
  BusLayoutConfiguration candidate;
  return !isActive() && BuildBusLayoutCandidate(requests, requestCount, candidate) && CanApplyBusLayout(candidate);
}

bool IPlugCLAP::configurableAudioPortsApplyConfiguration(const clap_audio_port_configuration_request* requests,
                                                          uint32_t requestCount) noexcept
{
  BusLayoutConfiguration candidate;
  return !isActive() && BuildBusLayoutCandidate(requests, requestCount, candidate) && CommitBusLayout(std::move(candidate));
}

bool IPlugCLAP::implementsSurround() const noexcept
{
  const auto* policy=GetBusLayoutPolicy();
  if (!policy) return false;
  for (const auto& configuration:policy->configurations) if (ClapConfigurationRepresentable(configuration))
    for (const auto& buses:configuration.buses) for (const auto& bus:buses)
      if (bus.layout.kind==LayoutKind::Speakers && ClapBusLayoutPortType(bus.layout) && !std::strcmp(ClapBusLayoutPortType(bus.layout),CLAP_PORT_SURROUND)) return true;
  return false;
}

bool IPlugCLAP::isChannelMaskSupported(uint64_t channelMask) const noexcept
{
  const auto* policy = GetBusLayoutPolicy();
  if (!policy) return false;
  for (const auto& configuration : policy->configurations) if (ClapConfigurationRepresentable(configuration))
    for (int direction = kInput; direction <= kOutput; ++direction)
      for (const auto& bus : configuration.buses[direction])
      {
        uint64_t supportedMask = 0;
        uint32_t count = 0;
        uint8_t map[64] {};
        if (!ClapGetSurroundMap(bus, map, 64, count)) continue;
        for (uint32_t i = 0; i < count; ++i) supportedMask |= uint64_t(1) << map[i];
        if (supportedMask == channelMask) return true;
      }
  return false;
}

uint32_t IPlugCLAP::getChannelMap(bool isInput, uint32_t portIndex,
                                  uint8_t* channelMap, uint32_t channelMapCapacity) const noexcept
{
  const auto* bus = GetActiveBusLayout(isInput ? kInput : kOutput, static_cast<int>(portIndex));
  uint32_t count = 0;
  return bus && ClapGetSurroundMap(*bus, channelMap, channelMapCapacity, count) ? count : 0;
}

uint32_t IPlugCLAP::notePortsCount(bool isInput) const noexcept
{
  if (isInput)
    return PLUG_DOES_MIDI_IN ? 1 : 0;
  else
    return PLUG_DOES_MIDI_OUT ? 1 : 0;
}

bool IPlugCLAP::notePortsInfo(uint32_t index, bool isInput, clap_note_port_info* pInfo) const noexcept
{
  if (isInput)
  {
    pInfo->id = index;
    pInfo->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
    pInfo->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    ClapNameCopy(pInfo->name, "MIDI Input");
  }
  else
  {
    pInfo->id = index;
    pInfo->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
    pInfo->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    ClapNameCopy(pInfo->name, "MIDI Output");
  }
  return true;
}

// clap_plugin_gui
bool IPlugCLAP::guiIsApiSupported(const char* api, bool isFloating) noexcept
{
#if defined OS_MAC
  return !isFloating && !strcmp(api, CLAP_WINDOW_API_COCOA);
#elif defined OS_WIN
  return !isFloating && !strcmp(api, CLAP_WINDOW_API_WIN32);
#else
#error Not Implemented!
#endif
}

bool IPlugCLAP::guiSetParent(const clap_window* pWindow) noexcept
{
#if defined OS_MAC
  return GUIWindowAttach(pWindow->cocoa);
#elif defined OS_WIN
  return GUIWindowAttach(pWindow->win32);
#else
#error Not Implemented!
#endif
}

bool IPlugCLAP::implementsGui() const noexcept
{
  return HasUI();
}

bool IPlugCLAP::guiCreate(const char* api, bool isFloating) noexcept
{
  return HasUI();
}

void IPlugCLAP::guiDestroy() noexcept
{
  CloseWindow();
  mGUIOpen = false;
  mWindow = nullptr;
}

bool IPlugCLAP::guiShow() noexcept
{
  if (HasUI() && !mGUIOpen)
  {
    OpenWindow(mWindow);
    return true;
  }
  else
  {
    return false;
  }
}

bool IPlugCLAP::guiHide() noexcept
{
  if (HasUI())
  {
    CloseWindow();
    mGUIOpen = false;
    return true;
  }
  else
  {
    return false;
  }
}

bool IPlugCLAP::guiCanResize() const noexcept
{
  return HasUI() && GetHostResizeEnabled();
}

bool IPlugCLAP::guiSetScale(double scale) noexcept
{
  if (HasUI())
  {
    SetScreenScale(static_cast<float>(scale));
    return true;
  }
  else
  {
    return false;
  }
}

bool IPlugCLAP::guiGetSize(uint32_t* pWidth, uint32_t* pHeight) noexcept
{
  TRACE
  
  if (HasUI())
  {
    *pWidth = GetEditorWidth();
    *pHeight = GetEditorHeight();
    
    return true;
  }
  else
  {
    return false;
  }
}

bool IPlugCLAP::GUIWindowAttach(void* pWindow) noexcept
{
  if (HasUI())
  {
    OpenWindow(pWindow);
    mWindow = pWindow;
    mGUIOpen = true;
    return true;
  }
  else
  {
    return false;
  }
}

bool IPlugCLAP::guiAdjustSize(uint32_t* pWidth, uint32_t* pHeight) noexcept
{
  Trace(TRACELOC, "width:%i height:%i\n", *pWidth, *pHeight);
  
  if (HasUI())
  {
    int w = *pWidth;
    int h = *pHeight;
    ConstrainEditorResize(w, h);
    *pWidth = w;
    *pHeight = h;
    
    return true;
  }
  else
  {
    return false;
  }
}

bool IPlugCLAP::guiSetSize(uint32_t width, uint32_t height) noexcept
{
  Trace(TRACELOC, "width:%i height:%i\n", width, height);

  if (HasUI())
  {
    OnParentWindowResize(width, height);
    return true;
  }
  else
  {
    return false;
  }
}

// TODO - wildcards (return as -1 chans...)
void IPlugCLAP::SetDefaultConfig()
{
  auto isMatch = [&](int idx, int chans)
  {
    if (NBuses(ERoute::kOutput, idx) >= 1 && NChannels(ERoute::kOutput, 0, idx) == chans)
    {
      int numBuses = NBuses(ERoute::kInput, idx);
      
      // Instruments are allowed to match with no inputs
      if (IsInstrument() && (numBuses == 0 || NChannels(ERoute::kInput, 0, idx) == 0))
        return true;
      
      // Otherwise IO must match
      return numBuses >= 1 && NChannels(ERoute::kInput, 0, idx) == chans;
    }
    
    return false;
  };
  
  auto testMatches = [&](int chans)
  {
    bool matched = false;
    int configNBusesI = 0;
    int configNBusesO = 0;
    
    for (int i = 0; i < static_cast<int>(audioPortsConfigCount()); i++)
    {
      if (isMatch(i, chans))
      {
        const int nBusesI = NBuses(ERoute::kInput, i);
        const int nBusesO = NBuses(ERoute::kOutput, i);
        
        const bool preferInput = nBusesO < configNBusesI;
        const bool preferOutput = nBusesI < configNBusesO;
        
        if (!matched || preferOutput || (nBusesO == configNBusesO && preferInput))
        {
          matched = true;
          mConfigIdx = i;
          configNBusesI = NBuses(ERoute::kInput, i);
          configNBusesO = NBuses(ERoute::kOutput, i);
        }
      }
    }
    
    return matched;
  };
  
  mConfigIdx = 0;
  
  // If there is track info available try to match
  if (GetClapHost().canUseTrackInfo())
  {
    clap_track_info info;
    GetClapHost().trackInfoGet(&info);

    if (testMatches(info.audio_channel_count) || info.audio_channel_count == 2)
      return;
  }
  
  // Default to stereo if nothing else has succeeded
  testMatches(2);
}

int IPlugCLAP::RequiredChannels() const
{
  return std::max(MaxNChannels(kInput), MaxNChannels(kOutput));
}

uint32_t IPlugCLAP::NBuses(ERoute direction, int configIdx) const
{
  return GetIOConfig(configIdx)->NBuses(direction);
}

uint32_t IPlugCLAP::NChannels(ERoute direction, uint32_t bus, int configIdx) const
{
  return GetIOConfig(configIdx)->NChansOnBusSAFE(direction, static_cast<int>(bus));
}

uint32_t IPlugCLAP::NBuses(ERoute direction) const
{
  return NBuses(direction, mConfigIdx);
}

uint32_t IPlugCLAP::NChannels(ERoute direction, uint32_t bus) const
{
  return NChannels(direction, bus, mConfigIdx);
}
