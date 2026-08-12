#include "IPlug/IPlugLinuxTimer.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
using iplug::LinuxTimerWorker;

namespace {

int gFailures = 0;

void Expect(bool condition, const char* label)
{
  if (!condition)
  {
    std::cerr << "failed: " << label << '\n';
    ++gFailures;
  }
}

void TestStopInterruptsWaitAndPreventsCallbacks()
{
  std::atomic<int> callbackCount {0};
  LinuxTimerWorker timer([&callbackCount] { ++callbackCount; }, 500ms);

  const auto started = std::chrono::steady_clock::now();
  timer.Stop();
  const auto elapsed = std::chrono::steady_clock::now() - started;

  Expect(elapsed < 100ms, "Stop interrupts a pending interval");
  std::this_thread::sleep_for(550ms);
  Expect(callbackCount == 0, "no callback occurs after Stop returns");
  timer.Stop();
  Expect(callbackCount == 0, "Stop is idempotent");
}

void TestStopWaitsForActiveCallback()
{
  std::atomic<bool> callbackStarted {false};
  std::atomic<bool> releaseCallback {false};
  std::atomic<bool> stopReturned {false};

  LinuxTimerWorker timer(
    [&] {
      callbackStarted = true;
      while (!releaseCallback)
        std::this_thread::yield();
    },
    20ms);

  while (!callbackStarted)
    std::this_thread::yield();

  std::thread stopper([&] {
    timer.Stop();
    stopReturned = true;
  });

  std::this_thread::sleep_for(10ms);
  Expect(!stopReturned, "Stop waits for an active callback");
  releaseCallback = true;
  stopper.join();
  Expect(stopReturned, "Stop returns after the active callback completes");
}

void TestStopFromCallbackDoesNotDeadlock()
{
  std::atomic<bool> callbackReturned {false};
  LinuxTimerWorker* timerPointer = nullptr;
  LinuxTimerWorker timer(
    [&] {
      timerPointer->Stop();
      callbackReturned = true;
    },
    1ms);
  timerPointer = &timer;

  const auto deadline = std::chrono::steady_clock::now() + 1s;
  while (!callbackReturned && std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();

  Expect(callbackReturned, "Stop from callback completes without deadlock");
}

} // namespace

int main()
{
  TestStopInterruptsWaitAndPreventsCallbacks();
  TestStopWaitsForActiveCallback();
  TestStopFromCallbackDoesNotDeadlock();
  return gFailures == 0 ? 0 : 1;
}
