/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for more info.

 ==============================================================================
*/

#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace iplug {

/** Interruptible Linux timer worker.
 *
 * Linux plug-in formats do not provide a process-global UI run loop. Format
 * adapters may replace this worker with a host run-loop timer in the future;
 * this implementation guarantees that Stop() interrupts a pending wait and
 * that callbacks have completed before Stop() returns from another thread.
 */
class LinuxTimerWorker
{
public:
  using Callback = std::function<void()>;

  LinuxTimerWorker(Callback callback, std::chrono::milliseconds interval)
  : mState(std::make_shared<State>(std::move(callback), interval))
  , mThread([state = mState] { Run(state); })
  {
  }

  ~LinuxTimerWorker()
  {
    Stop();
  }

  LinuxTimerWorker(const LinuxTimerWorker&) = delete;
  LinuxTimerWorker& operator=(const LinuxTimerWorker&) = delete;

  void Stop()
  {
    std::lock_guard<std::mutex> stopLock(mStopMutex);
    if (mStopped)
      return;

    mStopped = true;
    const auto state = mState;
    if (state)
    {
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->stopping = true;
      }
      state->condition.notify_all();
    }

    if (mThread.joinable())
    {
      if (mThread.get_id() == std::this_thread::get_id())
        mThread.detach();
      else
        mThread.join();
    }

    mState.reset();
  }

private:
  struct State
  {
    State(Callback callback, std::chrono::milliseconds interval)
    : callback(std::move(callback))
    , interval(interval)
    {
    }

    Callback callback;
    std::chrono::milliseconds interval;
    std::mutex mutex;
    std::condition_variable condition;
    bool stopping = false;
  };

  static void Run(const std::shared_ptr<State>& state)
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    while (!state->stopping)
    {
      if (state->condition.wait_for(
            lock, state->interval, [&state] { return state->stopping; }))
        break;

      lock.unlock();
      state->callback();
      lock.lock();
    }
  }

  std::shared_ptr<State> mState;
  std::thread mThread;
  std::mutex mStopMutex;
  bool mStopped = false;
};

} // namespace iplug
