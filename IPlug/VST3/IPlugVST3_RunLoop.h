#pragma once

#include "pluginterfaces/gui/iplugview.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>
#include <utility>
#include <unistd.h>
#include <sys/syscall.h>

namespace iplug {

inline void TraceVST3RunLoop(const char* event, const void* object, unsigned count = 0)
{
  if (std::getenv("IPLUG_TRACE_VST3_RUNLOOP"))
    std::fprintf(stderr, "IPLUG_RUNLOOP %s object=%p pid=%d tid=%ld count=%u\n",
                 event, object, getpid(), syscall(SYS_gettid), count);
}

// The registration owns an independent handler. A callback can unregister and
// destroy its registration (or the view) without deleting its active stack.
// Start/Stop and callbacks belong to the host UI thread.
class VST3RunLoopTimer
{
  class Handler final : public Steinberg::Linux::ITimerHandler
  {
  public:
    explicit Handler(std::function<void()> callback)
    : mCallback(std::move(callback)), mThread(std::this_thread::get_id()) {}

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** object) override
    {
      if (!object) return Steinberg::kInvalidArgument;
      *object = nullptr;
      if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)
          || Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Linux::ITimerHandler::iid))
      {
        *object = static_cast<Steinberg::Linux::ITimerHandler*>(this);
        addRef();
        return Steinberg::kResultOk;
      }
      return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return ++mReferences; }
    Steinberg::uint32 PLUGIN_API release() override
    {
      const auto remaining = --mReferences;
      if (!remaining) delete this;
      return remaining;
    }
    void PLUGIN_API onTimer() override
    {
      // Do not call UI code if a host violates the run-loop thread contract.
      if (std::this_thread::get_id() != mThread)
      {
        TraceVST3RunLoop("wrong-thread-rejected", this);
        return;
      }
      if (mInCallback || !mCallback) return;
      addRef();
      mInCallback = true;
      ++mCalls;
      if (mCalls == 1) TraceVST3RunLoop("first-timer", this);
      auto callback = mCallback;
      callback();
      mInCallback = false;
      release();
    }
    void Cancel()
    {
      mCallback = {};
      TraceVST3RunLoop("timer-stop", this, mCalls);
    }
  private:
    std::atomic<Steinberg::uint32> mReferences {1};
    std::function<void()> mCallback;
    const std::thread::id mThread;
    unsigned mCalls = 0;
    bool mInCallback = false;
  };

public:
  VST3RunLoopTimer() = default;
  ~VST3RunLoopTimer() { Stop(); }
  VST3RunLoopTimer(const VST3RunLoopTimer&) = delete;
  VST3RunLoopTimer& operator=(const VST3RunLoopTimer&) = delete;

  bool Start(Steinberg::Linux::IRunLoop* loop, unsigned interval, std::function<void()> callback)
  {
    Stop();
    if (!loop) return false;
    mLoop = loop;
    mLoop->addRef();
    mHandler = new Handler(std::move(callback));
    if (mLoop->registerTimer(mHandler, interval) != Steinberg::kResultOk)
    {
      mHandler->Cancel();
      mHandler->release();
      mHandler = nullptr;
      mLoop->release();
      mLoop = nullptr;
      return false;
    }
    TraceVST3RunLoop("timer-start", mHandler, interval);
    return true;
  }
  void Stop()
  {
    auto* handler = mHandler;
    auto* loop = mLoop;
    mHandler = nullptr;
    mLoop = nullptr;
    if (handler)
    {
      handler->Cancel();
      loop->unregisterTimer(handler);
      handler->release();
      loop->release();
    }
  }
private:
  Steinberg::Linux::IRunLoop* mLoop = nullptr;
  Handler* mHandler = nullptr;
};
} // namespace iplug
