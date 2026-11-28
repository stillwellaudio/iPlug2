/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
 */

/**
 * @file
 * @brief IPlugProcessor implementation.
 */

#include "IPlugProcessor.h"

#ifdef OS_WIN
#define strtok_r strtok_s
#endif

using namespace iplug;

IPlugProcessor::IPlugProcessor(const Config& config, EAPI plugAPI)
: mBusLayoutPolicy((plugAPI==kAPIVST2 || plugAPI==kAPIVST3 || plugAPI==kAPICLAP || plugAPI==kAPIAU || plugAPI==kAPIAAX) ? config.busLayouts : nullptr)
, mPlugType((EIPlugPluginType) config.plugType)
, mDoesMIDIIn(config.plugDoesMidiIn)
, mDoesMIDIOut(config.plugDoesMidiOut)
, mDoesMPE(config.plugDoesMPE)
, mLatency(config.latency)
{
  int totalNInBuses, totalNOutBuses;
  int totalNInChans, totalNOutChans;

  ParseChannelIOStr(config.channelIOStr, mIOConfigs, totalNInChans, totalNOutChans, totalNInBuses, totalNOutBuses);

  if (HasBusLayouts())
  {
    mBusLayoutsValid = mBusLayoutPolicy->IsValid();
    if (mBusLayoutsValid)
      for (const auto& c : mBusLayoutPolicy->configurations)
        if (BusLayoutNumericShape(c) < 0) mBusLayoutsValid = false;
    // Invalid opt-in declarations fail closed; never advertise the legacy path.
    totalNInChans = totalNOutChans = 0;
    if (mBusLayoutsValid)
    {
      int* totals[] = {&totalNInChans, &totalNOutChans};
      for (int d=0; d<2; ++d)
        for (auto capacity : mBusLayoutPolicy->capacities[d])
        { mBusBufferOffsets[d].push_back(*totals[d]); *totals[d] += static_cast<int>(capacity); }
    }
  }

  mScratchData[ERoute::kInput].Resize(totalNInChans);
  mScratchData[ERoute::kOutput].Resize(totalNOutChans);

  sample** ppInData = mScratchData[ERoute::kInput].Get();

  for (auto i = 0; i < totalNInChans; ++i, ++ppInData)
  {
    IChannelData<>* pInChannel = new IChannelData<>;
    pInChannel->mConnected = false;
    pInChannel->mData = ppInData;
    mChannelData[ERoute::kInput].Add(pInChannel);
  }

  sample** ppOutData = mScratchData[ERoute::kOutput].Get();

  for (auto i = 0; i < totalNOutChans; ++i, ++ppOutData)
  {
    IChannelData<>* pOutChannel = new IChannelData<>;
    pOutChannel->mConnected = false;
    pOutChannel->mData = ppOutData;
    pOutChannel->mIncomingData = nullptr;
    mChannelData[ERoute::kOutput].Add(pOutChannel);
  }
}

IPlugProcessor::~IPlugProcessor()
{
  TRACE

  mChannelData[ERoute::kInput].Empty(true);
  mChannelData[ERoute::kOutput].Empty(true);
  mIOConfigs.Empty(true);
}

void IPlugProcessor::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  if (HasBusLayouts())
  { PassThroughBusLayouts(inputs, outputs, nFrames, false); return; }
  const int nIn = mChannelData[ERoute::kInput].GetSize();
  const int nOut = mChannelData[ERoute::kOutput].GetSize();

  int j = 0;
  for (int i = 0; i < nOut; ++i)
  {
    if (i < nIn)
    {
      memcpy(outputs[i], inputs[i], nFrames * sizeof(sample));
      j++;
    }
  }
  // zero remaining outs
  for (/* same j */; j < nOut; ++j)
  {
    memset(outputs[j], 0, nFrames * sizeof(sample));
  }
}

void IPlugProcessor::ProcessWhileBypassed(sample** inputs, int nFrames)
{
  (void) inputs;
  (void) nFrames;
}

void IPlugProcessor::ProcessMidiMsg(const IMidiMsg& msg)
{
  SendMidiMsg(msg);
}

bool IPlugProcessor::SendMidiMsgs(WDL_TypedBuf<IMidiMsg>& msgs)
{
  bool rc = true;
  int n = msgs.GetSize();

  for (auto i = 0; i < n; ++i)
    rc &= SendMidiMsg(msgs.Get()[i]);

  return rc;
}

double IPlugProcessor::GetSamplesPerBeat() const
{
  const double tempo = GetTempo();

  if (tempo > 0.0)
    return GetSampleRate() * 60.0 / tempo;

  return 0.0;
}

#pragma mark -

void IPlugProcessor::GetBusName(ERoute direction, int busIdx, int nBuses, WDL_String& str) const
{
  if(direction == ERoute::kInput)
  {
    if(nBuses == 1)
    {
      str.Set("Input");
    }
    else if(nBuses == 2)
    {
      if(busIdx == 0)
        str.Set("Main");
      else
        str.Set("Aux");
    }
    else
    {
      str.SetFormatted(MAX_BUS_NAME_LEN, "Input %i", busIdx);
    }
  }
  else
  {
    if(nBuses == 1)
    {
      str.Set("Output");
    }
    else
    {
      str.SetFormatted(MAX_BUS_NAME_LEN, "Output %i", busIdx);
    }
  }
}

int IPlugProcessor::MaxNBuses(ERoute direction, int* pConfigIdxWithTheMostBuses) const
{
  int maxNBuses = 0;
  int configWithMostBuses = 0;

  for (auto configIdx = 0; configIdx < NIOConfigs(); configIdx++)
  {
    IOConfig* pIConfig = mIOConfigs.Get(configIdx);
    int nBuses = pIConfig->NBuses(direction);
    
    if(nBuses >= maxNBuses)
    {
      maxNBuses = nBuses;
      configWithMostBuses = configIdx;
    }
  }
  
  if(pConfigIdxWithTheMostBuses)
    *pConfigIdxWithTheMostBuses = configWithMostBuses;

  return maxNBuses;
}

int IPlugProcessor::GetIOConfigWithChanCounts(std::vector<int>& inputBuses, std::vector<int>& outputBuses)
{
  int nInputBuses = static_cast<int>(inputBuses.size());
  int nOutputBuses = static_cast<int>(outputBuses.size());

  for (auto configIdx = 0; configIdx < NIOConfigs(); configIdx++)
  {
    const IOConfig* pConfig = GetIOConfig(configIdx);
    
    if(pConfig->NBuses(ERoute::kInput) == nInputBuses && pConfig->NBuses(ERoute::kOutput) == nOutputBuses)
    {
      bool match = true;
      
      for (int inputBusIdx = 0; inputBusIdx < nInputBuses; inputBusIdx++)
      {
        match &= inputBuses[inputBusIdx] == pConfig->GetBusInfo(ERoute::kInput, inputBusIdx)->NChans();
      }
      
      if(match)
      {
        for (int outputBusIdx = 0; outputBusIdx < nOutputBuses; outputBusIdx++)
        {
          match &= outputBuses[outputBusIdx] == pConfig->GetBusInfo(ERoute::kOutput, outputBusIdx)->NChans();
        }
      }
      
      if(match)
        return configIdx;
    }
  }
  
  return -1;
}

int IPlugProcessor::MaxNChannelsForBus(ERoute direction, int busIdx) const
{
  if (HasBusLayouts()) return GetBusBufferCapacity(direction,busIdx);
  if(HasWildcardBus(direction))
    return -1;

  const int maxNBuses = MaxNBuses(direction);
  std::vector<int> maxChansOnBuses;
  maxChansOnBuses.resize(maxNBuses);

  //find the maximum channel count for each input or output bus
  for (auto configIdx = 0; configIdx < NIOConfigs(); configIdx++)
  {
    const IOConfig* pIOConfig = GetIOConfig(configIdx);

    for (int bus = 0; bus < maxNBuses; bus++)
      maxChansOnBuses[bus] = std::max(pIOConfig->NChansOnBusSAFE(direction, bus), maxChansOnBuses[bus]);
  }

  return maxChansOnBuses.size() > 0 ? maxChansOnBuses[busIdx] : 0;
}

int IPlugProcessor::NChannelsConnected(ERoute direction) const
{
  const WDL_PtrList<IChannelData<>>& channelData = mChannelData[direction];

  int count = 0;
  for (auto i = 0; i<channelData.GetSize(); i++)
  {
    count += (int) IsChannelConnected(direction, i);
  }

  return count;
}

bool IPlugProcessor::LegalIO(int NInputChans, int NOutputChans) const
{
  bool legal = false;

  for (auto i = 0; i < NIOConfigs() && !legal; ++i)
  {
    const IOConfig* pIO = GetIOConfig(i);
    legal = ((NInputChans < 0 || NInputChans == pIO->GetTotalNChannels(ERoute::kInput)) && (NOutputChans < 0 || NOutputChans == pIO->GetTotalNChannels(ERoute::kOutput)));
  }

  Trace(TRACELOC, "%d:%d:%s", NInputChans, NOutputChans, (legal ? "legal" : "illegal"));
  return legal;
}

void IPlugProcessor::LimitToStereoIO()
{
  if (MaxNChannels(ERoute::kInput) > 2)
    SetChannelConnections(ERoute::kInput, 2, MaxNChannels(ERoute::kInput) - 2, false);

  if (MaxNChannels(ERoute::kOutput) > 2)
    SetChannelConnections(ERoute::kOutput, 2, MaxNChannels(ERoute::kOutput) - 2, true);
}

void IPlugProcessor::SetChannelLabel(ERoute direction, int idx, const char* formatStr, bool zeroBased)
{
  if (idx >= 0 && idx < MaxNChannels(direction))
    mChannelData[direction].Get(idx)->mLabel.SetFormatted(MAX_CHAN_NAME_LEN, formatStr, idx+(!zeroBased));
}

void IPlugProcessor::SetLatency(int samples)
{
  if (HasBusLayouts() && samples<0) return;
  mLatency = samples;

  if (mLatencyDelay)
    mLatencyDelay->SetDelayTime(mLatency);
  if (HasBusLayouts()) ResizeBusLayoutDelay();
}

//static
int IPlugProcessor::ParseChannelIOStr(const char* IOStr, WDL_PtrList<IOConfig>& channelIOList, int& totalNInChans, int& totalNOutChans, int& totalNInBuses, int& totalNOutBuses)
{
  bool foundAWildcard = false;
  int IOConfigIndex = 0;

  DBGMSG("\nBEGIN IPLUG CHANNEL IO PARSER --------------------------------------------------\n");
  // lamda function to iterate through the period separated buses and check that none have 0 channel count
  auto ParseBusToken = [&foundAWildcard, &IOConfigIndex](ERoute busDir, char* pBusStr, char* pBusStrEnd, int& NBuses, int& NChans, IOConfig* pConfig)
  {
    while (pBusStr != NULL)
    {
      auto NChanOnBus = 0;

      if(strcmp(pBusStr, "*") == 0)
      {
        foundAWildcard = true;
        NChanOnBus = -MAX_BUS_CHANS; // we put a negative number in here which will be picked up in the api classes in order to deal with NxN or NxM routings
      }
      else if (sscanf(pBusStr, "%d", &NChanOnBus) == 1)
        ; //don't do anything
      else
      {
        DBGMSG("Error: something wrong in the %s part of this io string: %s.\n", RoutingDirStrs[busDir], pBusStr);
        assert(0);
      }
      NChans += NChanOnBus;

      pBusStr = strtok_r(NULL, ".", &pBusStrEnd);

      if(NChanOnBus)
      {
        pConfig->AddBusInfo(busDir, NChanOnBus);
        NBuses++;
      }
      else if(NBuses > 0)
      {
        DBGMSG("Error: with multiple %s buses you can't define one with no channels!\n", RoutingDirStrs[busDir]);
        assert(NChanOnBus > 0);
      }
    }
  };

  totalNInChans = 0; totalNOutChans = 0;
  totalNInBuses = 0; totalNOutBuses = 0;

  char* pChannelIOStr = strdup(IOStr);

  char* pIOStrEnd;
  char* pIOStr = strtok_r(pChannelIOStr, " ", &pIOStrEnd); // a single IO string

  WDL_PtrList<WDL_String> IOStrlist;

  // iterate through the space separated IO configs
  while (pIOStr != NULL)
  {
    IOConfig* pConfig = new IOConfig();

    int NInChans = 0, NOutChans = 0;
    int NInBuses = 0, NOutBuses = 0;

    char* pIStr = strtok(pIOStr, "-"); // Input buses part of string
    char* pOStr = strtok(NULL, "-");   // Output buses part of string

    WDL_String* thisIOStr  = new WDL_String();
    thisIOStr->SetFormatted(256, "%s-%s", pIStr, pOStr);

    for (auto str = 0; str < IOStrlist.GetSize(); str++)
    {
      if(strcmp(IOStrlist.Get(str)->Get(), thisIOStr->Get()) == 0)
      {
        DBGMSG("Error: Duplicate IO string. %s\n", thisIOStr->Get());
        assert(0);
      }
    }

    IOStrlist.Add(thisIOStr);

    char* pIBusStrEnd;
    char* pIBusStr = strtok_r(pIStr, ".", &pIBusStrEnd); // a single input bus

    ParseBusToken(ERoute::kInput, pIBusStr, pIBusStrEnd, NInBuses, NInChans, pConfig);

    char* pOBusStrEnd;
    char* pOBusStr = strtok_r(pOStr, ".", &pOBusStrEnd);

    ParseBusToken(ERoute::kOutput, pOBusStr, pOBusStrEnd, NOutBuses, NOutChans, pConfig);

    if(foundAWildcard == true && IOConfigIndex > 0)
    {
      DBGMSG("Error: You can only have a single IO config when using wild cards.\n");
      assert(0);
    }

    DBGMSG("Channel I/O #%i - %s\n", IOConfigIndex + 1, thisIOStr->Get());
    DBGMSG("               - input bus count: %i, output bus count %i\n", NInBuses, NOutBuses);
    for (auto i = 0; i < NInBuses; i++)
      DBGMSG("               - channel count on input bus %i: %i\n", i + 1, pConfig->NChansOnBusSAFE(ERoute::kInput, i));
    for (auto i = 0; i < NOutBuses; i++)
      DBGMSG("               - channel count on output bus %i: %i\n", i + 1, pConfig->NChansOnBusSAFE(ERoute::kOutput, i));
    DBGMSG("               - input channel count across all buses: %i, output channel count across all buses %i\n\n", NInChans, NOutChans);

    totalNInChans = std::max(totalNInChans, NInChans);
    totalNOutChans = std::max(totalNOutChans, NOutChans);
    totalNInBuses = std::max(totalNInBuses, NInBuses);
    totalNOutBuses = std::max(totalNOutBuses, NOutBuses);

    channelIOList.Add(pConfig);

    IOConfigIndex++;

    pIOStr = strtok_r(NULL, " ", &pIOStrEnd); // go to next io string
  }

  free(pChannelIOStr);
  IOStrlist.Empty(true);
  DBGMSG("%i I/O configs detected\n", IOConfigIndex);
  DBGMSG("Total # in chans: %i, Total # out chans: %i \n\n", totalNInChans, totalNOutChans);
  DBGMSG("END IPLUG CHANNEL IO PARSER --------------------------------------------------\n");

  return IOConfigIndex;
}

int IPlugProcessor::GetAUPluginType() const
{
  if (mPlugType == EIPlugPluginType::kEffect)
  {
    if (DoesMIDIIn())
      return 'aumf';
    else
      return 'aufx';
  }
  else if (mPlugType == EIPlugPluginType::kInstrument)
  {
    return 'aumu';
  }
  else if (mPlugType == EIPlugPluginType::kMIDIEffect)
  {
    return 'aumi';
  }
  else if (mPlugType == EIPlugPluginType::kGenerator)
  {
    return 'augn';
  }
  else
    return 'aufx';
}

#pragma mark -

void IPlugProcessor::SetChannelConnections(ERoute direction, int idx, int n, bool connected)
{
  WDL_PtrList<IChannelData<>>& channelData = mChannelData[direction];

  const auto endIdx = std::min(idx + n, channelData.GetSize());

  for (auto i = idx; i < endIdx; ++i)
  {
    IChannelData<>* pChannel = channelData.Get(i);
    pChannel->mConnected = connected;

    if (!connected)
      *(pChannel->mData) = pChannel->mScratchBuf.Get();
  }
}

void IPlugProcessor::InitLatencyDelay()
{
  if (HasBusLayouts()) { ResizeBusLayoutDelay(); return; }
  if (MaxNChannels(ERoute::kInput))
  {
    mLatencyDelay = std::unique_ptr<NChanDelayLine<PLUG_SAMPLE_DST>>(new NChanDelayLine<PLUG_SAMPLE_DST>(MaxNChannels(ERoute::kInput), MaxNChannels(ERoute::kOutput)));
    mLatencyDelay->SetDelayTime(GetLatency());
  }
}

void IPlugProcessor::AttachBuffers(ERoute direction, int idx, int n, PLUG_SAMPLE_DST** ppData, int)
{
  WDL_PtrList<IChannelData<>>& channelData = mChannelData[direction];

  const auto endIdx = std::min(idx + n, channelData.GetSize());

  for (auto i = idx; i < endIdx; ++i)
  {
    IChannelData<>* pChannel = channelData.Get(i);

    if (pChannel->mConnected)
      *(pChannel->mData) = *(ppData++);
  }
}

void IPlugProcessor::AttachBuffers(ERoute direction, int idx, int n, PLUG_SAMPLE_SRC** ppData, int nFrames)
{
  WDL_PtrList<IChannelData<>>& channelData = mChannelData[direction];

  const auto endIdx = std::min(idx + n, channelData.GetSize());

  for (auto i = idx; i < endIdx; ++i)
  {
    IChannelData<>* pChannel = channelData.Get(i);

    if (pChannel->mConnected)
    {
      if (direction == ERoute::kInput)
      {
        PLUG_SAMPLE_DST* pScratch = pChannel->mScratchBuf.Get();
        CastCopy(pScratch, *(ppData++), nFrames);
        *(pChannel->mData) = pScratch;
      }
      else // output
      {
        *(pChannel->mData) = pChannel->mScratchBuf.Get();
        pChannel->mIncomingData = *(ppData++);
      }
    }
  }
}

void IPlugProcessor::PassThroughBuffers(PLUG_SAMPLE_DST type, int nFrames)
{
  if (HasBusLayouts())
  { PassThroughBusLayouts(GetScratchData(kInput), GetScratchData(kOutput), nFrames, true); return; }
  if (mLatency && mLatencyDelay)
    mLatencyDelay->ProcessBlock(mScratchData[ERoute::kInput].Get(), mScratchData[ERoute::kOutput].Get(), nFrames);
  else
    IPlugProcessor::ProcessBlock(mScratchData[ERoute::kInput].Get(), mScratchData[ERoute::kOutput].Get(), nFrames);
}

void IPlugProcessor::PassThroughBuffers(PLUG_SAMPLE_SRC type, int nFrames)
{
  // for PLUG_SAMPLE_SRC bit buffers, first run the delay (if mLatency) on the PLUG_SAMPLE_DST IPlug buffers
  PassThroughBuffers(PLUG_SAMPLE_DST(0.), nFrames);

  int i, n = MaxNChannels(ERoute::kOutput);
  IChannelData<>** ppOutChannel = mChannelData[ERoute::kOutput].GetList();

  for (i = 0; i < n; ++i, ++ppOutChannel)
  {
    IChannelData<>* pOutChannel = *ppOutChannel;
    if (pOutChannel->mConnected)
    {
      CastCopy(pOutChannel->mIncomingData, *(pOutChannel->mData), nFrames);
    }
  }
}

void IPlugProcessor::ProcessBuffers(PLUG_SAMPLE_DST type, int nFrames)
{
  ProcessBlock(mScratchData[ERoute::kInput].Get(), mScratchData[ERoute::kOutput].Get(), nFrames);
}

void IPlugProcessor::ProcessBuffers(PLUG_SAMPLE_SRC type, int nFrames)
{
  ProcessBuffers((PLUG_SAMPLE_DST) 0, nFrames);
  int i, n = MaxNChannels(ERoute::kOutput);
  IChannelData<>** ppOutChannel = mChannelData[ERoute::kOutput].GetList();

  for (i = 0; i < n; ++i, ++ppOutChannel)
  {
    IChannelData<>* pOutChannel = *ppOutChannel;

    if (pOutChannel->mConnected)
    {
      CastCopy(pOutChannel->mIncomingData, *(pOutChannel->mData), nFrames);
    }
  }
}

void IPlugProcessor::ProcessBuffersAccumulating(int nFrames)
{
  ProcessBuffers((PLUG_SAMPLE_DST) 0, nFrames);
  int i, n = MaxNChannels(ERoute::kOutput);
  IChannelData<>** ppOutChannel = mChannelData[ERoute::kOutput].GetList();

  for (i = 0; i < n; ++i, ++ppOutChannel)
  {
    IChannelData<>* pOutChannel = *ppOutChannel;
    if (pOutChannel->mConnected)
    {
      PLUG_SAMPLE_SRC* pDest = pOutChannel->mIncomingData;
      PLUG_SAMPLE_DST* pSrc = *(pOutChannel->mData); // TODO : check this: PLUG_SAMPLE_DST will allways be float, because this is only for VST2 accumulating
      for (int j = 0; j < nFrames; ++j, ++pDest, ++pSrc)
      {
        *pDest += (PLUG_SAMPLE_SRC) *pSrc;
      }
    }
  }
}

void IPlugProcessor::ZeroScratchBuffers()
{
  int i, nIn = MaxNChannels(ERoute::kInput), nOut = MaxNChannels(ERoute::kOutput);

  for (i = 0; i < nIn; ++i)
  {
    IChannelData<>* pInChannel = mChannelData[ERoute::kInput].Get(i);
    memset(pInChannel->mScratchBuf.Get(), 0, mBlockSize * sizeof(PLUG_SAMPLE_DST));
  }

  for (i = 0; i < nOut; ++i)
  {
    IChannelData<>* pOutChannel = mChannelData[ERoute::kOutput].Get(i);
    memset(pOutChannel->mScratchBuf.Get(), 0, mBlockSize * sizeof(PLUG_SAMPLE_DST));
  }
}

void IPlugProcessor::SetBlockSize(int blockSize)
{
  if (blockSize != mBlockSize)
  {
    int i, nIn = MaxNChannels(ERoute::kInput), nOut = MaxNChannels(ERoute::kOutput);

    for (i = 0; i < nIn; ++i)
    {
      IChannelData<>* pInChannel = mChannelData[ERoute::kInput].Get(i);
      pInChannel->mScratchBuf.Resize(blockSize);
      memset(pInChannel->mScratchBuf.Get(), 0, blockSize * sizeof(PLUG_SAMPLE_DST));
    }

    for (i = 0; i < nOut; ++i)
    {
      IChannelData<>* pOutChannel = mChannelData[ERoute::kOutput].Get(i);
      pOutChannel->mScratchBuf.Resize(blockSize);
      memset(pOutChannel->mScratchBuf.Get(), 0, blockSize * sizeof(PLUG_SAMPLE_DST));
    }

    mBlockSize = blockSize;
  }
}

const LayoutBus* IPlugProcessor::GetActiveBusLayout(ERoute direction, int bus) const
{
  if (!mHasActiveBusLayout || bus < 0 || static_cast<size_t>(bus) >= mActiveBusLayout.buses[direction].size()) return nullptr;
  return &mActiveBusLayout.buses[direction][bus];
}

int IPlugProcessor::GetBusBufferOffset(ERoute direction, int bus) const
{
  if (bus < 0 || static_cast<size_t>(bus) >= mBusBufferOffsets[direction].size()) return -1;
  return mBusBufferOffsets[direction][bus];
}

int IPlugProcessor::GetBusBufferCapacity(ERoute direction, int bus) const
{
  if (!mBusLayoutsValid || bus < 0 || static_cast<size_t>(bus) >= mBusLayoutPolicy->capacities[direction].size()) return 0;
  return static_cast<int>(mBusLayoutPolicy->capacities[direction][bus]);
}

int IPlugProcessor::GetActiveBusChannelCount(ERoute direction, int bus) const
{
  const auto* b = GetActiveBusLayout(direction, bus);
  return b ? static_cast<int>(b->layout.NChannels()) : 0;
}

int IPlugProcessor::BusLayoutNumericShape(const BusLayoutConfiguration& candidate) const
{
  for (int i=0; i<NIOConfigs(); ++i)
  {
    const auto* numeric = GetIOConfig(i);
    bool matches = true;
    for (int d=0; d<2 && matches; ++d)
    {
      const auto dir = static_cast<ERoute>(d);
      if (candidate.buses[d].size() != static_cast<size_t>(numeric->NBuses(dir))) { matches = false; break; }
      for (size_t b=0; b<candidate.buses[d].size(); ++b)
      {
        const int width = numeric->NChansOnBusSAFE(dir, static_cast<int>(b));
        if (width >= 0 && static_cast<uint32_t>(width) != candidate.buses[d][b].layout.NChannels()) { matches=false; break; }
      }
    }
    if (matches) return i;
  }
  return -1;
}

bool IPlugProcessor::CanApplyBusLayout(const BusLayoutConfiguration& candidate) const
{
  return mBusLayoutsValid && BusLayoutNumericShape(candidate) >= 0 && mBusLayoutPolicy->Accepts(candidate);
}

bool IPlugProcessor::CommitBusLayout(BusLayoutConfiguration candidate)
{
  if (!CanApplyBusLayout(candidate)) return false;
  candidate.id = mBusLayoutPolicy->ListedID(candidate);
  if (mHasActiveBusLayout && mActiveBusLayout == candidate) return true;
  mActiveBusLayout = std::move(candidate);
  mHasActiveBusLayout = true;
  std::fill(mBusLayoutDelay.begin(), mBusLayoutDelay.end(), sample(0));
  mBusLayoutDelayPosition = 0;
  OnBusLayoutChanged();
  return true;
}

bool IPlugProcessor::SetBusLayoutActive(ERoute direction, int bus, bool active)
{
  if (!GetActiveBusLayout(direction, bus)) return false;
  auto candidate = mActiveBusLayout;
  candidate.buses[direction][bus].active = active;
  return CommitBusLayout(std::move(candidate));
}

bool IPlugProcessor::BeginBusLayoutBlock(int nFrames)
{
  if (!mHasActiveBusLayout || nFrames < 0 || nFrames > GetBlockSize()) return false;
  for (int d=0; d<2; ++d)
    for (int c=0; c<MaxNChannels(static_cast<ERoute>(d)); ++c)
    {
      auto* ch = mChannelData[d].Get(c);
      ch->mConnected = false;
      ch->mIncomingData = nullptr;
      *ch->mData = ch->mScratchBuf.Get();
      if (nFrames) memset(*ch->mData, 0, sizeof(sample) * nFrames);
    }
  return true;
}

bool IPlugProcessor::AttachBusBuffers(ERoute direction, int bus, int count,
                                     PLUG_SAMPLE_DST** buffers, int nFrames, uint64_t silenceMask)
{
  const auto* b = GetActiveBusLayout(direction, bus);
  if (!b || count < 0 || static_cast<uint32_t>(count) != b->layout.NChannels() || nFrames < 0 || nFrames > GetBlockSize()) return false;
  if (!b->active || !nFrames) return true;
  const int offset = GetBusBufferOffset(direction, bus);
  for (int c=0; c<count; ++c)
  {
    auto* ch = mChannelData[direction].Get(offset+c);
    if (buffers && buffers[c] && !(direction == kInput && c < 64 && (silenceMask & (uint64_t(1) << c))))
    { ch->mConnected = true; *ch->mData = buffers[c]; }
  }
  return true;
}

bool IPlugProcessor::AttachBusBuffers(ERoute direction, int bus, int count,
                                     PLUG_SAMPLE_SRC** buffers, int nFrames, uint64_t silenceMask)
{
  const auto* b = GetActiveBusLayout(direction, bus);
  if (!b || count < 0 || static_cast<uint32_t>(count) != b->layout.NChannels() || nFrames < 0 || nFrames > GetBlockSize()) return false;
  if (!b->active || !nFrames) return true;
  const int offset = GetBusBufferOffset(direction, bus);
  for (int c=0; c<count; ++c)
  {
    auto* ch = mChannelData[direction].Get(offset+c);
    if (buffers && buffers[c] && !(direction == kInput && c < 64 && (silenceMask & (uint64_t(1) << c))))
    {
      ch->mConnected = true;
      if (direction == kInput) CastCopy(ch->mScratchBuf.Get(), buffers[c], nFrames);
      else ch->mIncomingData = buffers[c];
    }
  }
  return true;
}

void IPlugProcessor::ResizeBusLayoutDelay()
{
  const size_t channels = static_cast<size_t>(MaxNChannels(kOutput));
  const size_t latency = static_cast<size_t>(std::max(0, mLatency));
  if (latency && channels > mBusLayoutDelay.max_size() / latency) return;
  mBusLayoutDelay.assign(channels * latency, sample(0));
  mBusLayoutDelayPosition = 0;
}

void IPlugProcessor::PassThroughBusLayouts(sample** inputs, sample** outputs, int nFrames, bool delay)
{
  if (!mHasActiveBusLayout || nFrames <= 0 || nFrames > GetBlockSize()) return;
  const bool delayed = delay && mLatency > 0 && !mBusLayoutDelay.empty();
  for (int frame=0; frame<nFrames; ++frame)
  {
    for (size_t b=0; b<mBusBufferOffsets[kOutput].size(); ++b)
    {
      const auto* output = GetActiveBusLayout(kOutput, static_cast<int>(b));
      const auto* source = output ? GetActiveBusLayout(kInput, output->bypassInputBus) : nullptr;
      const int outOffset = GetBusBufferOffset(kOutput, static_cast<int>(b));
      const int inOffset = source ? GetBusBufferOffset(kInput, output->bypassInputBus) : -1;
      for (int c=0; c<GetBusBufferCapacity(kOutput, static_cast<int>(b)); ++c)
      {
        const int flat = outOffset+c;
        const bool routed = output && output->active && source && source->active &&
          c < static_cast<int>(output->layout.NChannels()) && c < static_cast<int>(source->layout.NChannels());
        const sample input = routed ? inputs[inOffset+c][frame] : sample(0);
        if (delayed)
        {
          const size_t position = static_cast<size_t>(flat) * mLatency + mBusLayoutDelayPosition;
          outputs[flat][frame] = mBusLayoutDelay[position];
          mBusLayoutDelay[position] = input;
        }
        else outputs[flat][frame] = input;
      }
    }
    if (delayed && ++mBusLayoutDelayPosition == mLatency) mBusLayoutDelayPosition = 0;
  }
}
