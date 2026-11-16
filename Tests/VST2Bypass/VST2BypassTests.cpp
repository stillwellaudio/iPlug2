// Drives the VST2 wrapper through its exported entry point, as a host would,
// and checks host bypass for plug-ins that do and do not opt in through
// HandlesHostBypassInternally().

#include "aeffectx.h"

#include <dlfcn.h>
#include <cstring>
#include <iostream>

namespace
{
int gFailures = 0;

#define CHECK(condition) \
  do \
  { \
    if (!(condition)) \
    { \
      std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #condition "\n"; \
      ++gFailures; \
    } \
  } while (false)

VstIntPtr VSTCALLBACK HostCallback(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
  return opcode == audioMasterVersion ? 2400 : 0;
}

struct Fixture
{
  void* handle = nullptr;
  AEffect* (*pluginMain)(audioMasterCallback) = nullptr;
  void (*setHandlesBypass)(bool) = nullptr;
  void (*setInsideProcess)(bool) = nullptr;
  void (*resetCounts)() = nullptr;
  int (*notifications)() = nullptr;
  int (*notificationsOutsideProcess)() = nullptr;
  bool (*lastNotified)() = nullptr;
  int (*processBlocks)() = nullptr;
  bool (*lastBlockBypassed)() = nullptr;
  bool (*getBypassed)() = nullptr;

  template <class T>
  bool Load(T& fn, const char* name)
  {
    fn = reinterpret_cast<T>(dlsym(handle, name));
    if (!fn)
      std::cerr << "missing symbol " << name << "\n";
    return fn != nullptr;
  }

  bool Open(const char* path)
  {
    handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle)
    {
      std::cerr << "dlopen failed: " << dlerror() << "\n";
      return false;
    }
    return Load(pluginMain, "VSTPluginMain") && Load(setHandlesBypass, "VST2BypassSetHandlesBypass")
      && Load(setInsideProcess, "VST2BypassSetInsideProcess") && Load(resetCounts, "VST2BypassResetCounts")
      && Load(notifications, "VST2BypassNotifications")
      && Load(notificationsOutsideProcess, "VST2BypassNotificationsOutsideProcess")
      && Load(lastNotified, "VST2BypassLastNotified") && Load(processBlocks, "VST2BypassProcessBlocks")
      && Load(lastBlockBypassed, "VST2BypassLastBlockBypassed") && Load(getBypassed, "VST2BypassGetBypassed");
  }
};

constexpr int kFrames = 64;

struct Buffers
{
  float inF[2][kFrames], outF[2][kFrames];
  double inD[2][kFrames], outD[2][kFrames];
  float* inFP[2] = {inF[0], inF[1]};
  float* outFP[2] = {outF[0], outF[1]};
  double* inDP[2] = {inD[0], inD[1]};
  double* outDP[2] = {outD[0], outD[1]};

  Buffers()
  {
    for (int c = 0; c < 2; ++c)
      for (int i = 0; i < kFrames; ++i)
      {
        inF[c][i] = 0.25f * float(c + 1);
        inD[c][i] = 0.25 * double(c + 1);
        outF[c][i] = 0.f;
        outD[c][i] = 0.0;
      }
  }

  bool OutputsMatchInputs() const
  {
    for (int c = 0; c < 2; ++c)
      for (int i = 0; i < kFrames; ++i)
        if (outF[c][i] != inF[c][i])
          return false;
    return true;
  }
};

VstIntPtr Dispatch(AEffect* e, VstInt32 opcode, VstIntPtr value = 0, void* ptr = nullptr, float opt = 0.f)
{
  return e->dispatcher(e, opcode, 0, value, ptr, opt);
}

void Process(Fixture& f, AEffect* e, Buffers& b, bool doublePrecision = false)
{
  f.setInsideProcess(true);
  if (doublePrecision)
    e->processDoubleReplacing(e, b.inDP, b.outDP, kFrames);
  else
    e->processReplacing(e, b.inFP, b.outFP, kFrames);
  f.setInsideProcess(false);
}

AEffect* Start(Fixture& f, bool handlesBypass)
{
  f.setHandlesBypass(handlesBypass);
  f.resetCounts();
  AEffect* e = f.pluginMain(HostCallback);
  if (!e)
    return nullptr;
  Dispatch(e, effOpen);
  Dispatch(e, effSetSampleRate, 0, nullptr, 48000.f);
  Dispatch(e, effSetBlockSize, kFrames);
  Dispatch(e, effMainsChanged, 1);
  return e;
}

void Stop(AEffect* e)
{
  Dispatch(e, effMainsChanged, 0);
  Dispatch(e, effClose);
}

char kBypassCanDo[] = "bypass";

// A plug-in that does not opt in keeps the existing behavior: soft bypass is
// not advertised, effSetBypass is refused and changes nothing, and processing
// continues as before.
void TestWithoutOptIn(Fixture& f)
{
  AEffect* e = Start(f, false);
  CHECK(e != nullptr);
  if (!e)
    return;
  Buffers b;

  CHECK(Dispatch(e, effCanDo, 0, kBypassCanDo) == 0);
  CHECK(Dispatch(e, effSetBypass, 1) == 0);
  CHECK(!f.getBypassed());

  Process(f, e, b);
  CHECK(f.notifications() == 0);
  CHECK(f.processBlocks() == 1);
  CHECK(!f.lastBlockBypassed());
  CHECK(b.OutputsMatchInputs());

  CHECK(Dispatch(e, effSetBypass, 0) == 0);
  Process(f, e, b, true);
  CHECK(f.notifications() == 0);
  CHECK(f.processBlocks() == 2);
  Stop(e);
}

// A plug-in that opts in advertises soft bypass, accepts effSetBypass, keeps
// processing, and is notified once per change on the processing thread.
void TestWithOptIn(Fixture& f)
{
  AEffect* e = Start(f, true);
  CHECK(e != nullptr);
  if (!e)
    return;
  Buffers b;

  CHECK(Dispatch(e, effCanDo, 0, kBypassCanDo) == 1);

  Process(f, e, b);
  CHECK(f.notifications() == 0); // the initial unbypassed state is not a change

  CHECK(Dispatch(e, effSetBypass, 1) == 1);
  CHECK(f.getBypassed());
  CHECK(f.notifications() == 0); // not notified from the dispatcher

  Process(f, e, b);
  CHECK(f.notifications() == 1);
  CHECK(f.lastNotified());
  CHECK(f.lastBlockBypassed());
  CHECK(f.processBlocks() == 2); // processing continues while bypassed
  CHECK(b.OutputsMatchInputs());

  Process(f, e, b);
  CHECK(f.notifications() == 1); // no change, no notification

  CHECK(Dispatch(e, effSetBypass, 1) == 1); // repeated value
  Process(f, e, b);
  CHECK(f.notifications() == 1);

  CHECK(Dispatch(e, effSetBypass, 0) == 1);
  Process(f, e, b, true);
  CHECK(f.notifications() == 2);
  CHECK(!f.lastNotified());
  CHECK(!f.lastBlockBypassed());

  // A change that is reverted before the next block is not a change.
  CHECK(Dispatch(e, effSetBypass, 1) == 1);
  CHECK(Dispatch(e, effSetBypass, 0) == 1);
  Process(f, e, b);
  CHECK(f.notifications() == 2);

  CHECK(Dispatch(e, effSetBypass, 1) == 1);
  Process(f, e, b, true);
  CHECK(f.notifications() == 3);
  CHECK(f.lastNotified());

  CHECK(f.notificationsOutsideProcess() == 0);
  Stop(e);
}
} // namespace

int main(int argc, char** argv)
{
  const char* path = argc > 1 ? argv[1] : VST2_BYPASS_FIXTURE_PATH;
  Fixture f;
  if (!f.Open(path))
    return 2;

  TestWithoutOptIn(f);
  TestWithOptIn(f);

  if (gFailures)
  {
    std::cerr << gFailures << " check(s) failed\n";
    return 1;
  }
  std::cout << "VST2 bypass tests passed\n";
  return 0;
}
