#include "IPlug/VST3/IPlugVST3_RunLoop.h"
#include <cassert>
#include <iostream>
#include <memory>

using namespace Steinberg;
struct Host final : Linux::IRunLoop
{
  uint32 refs = 1;
  unsigned starts = 0, stops = 0;
  bool fail = false;
  Linux::ITimerHandler* handler = nullptr;
  tresult PLUGIN_API queryInterface(const TUID, void** p) override { *p=nullptr; return kNoInterface; }
  uint32 PLUGIN_API addRef() override { return ++refs; }
  uint32 PLUGIN_API release() override { return --refs; }
  tresult PLUGIN_API registerEventHandler(Linux::IEventHandler*, Linux::FileDescriptor) override { return kNotImplemented; }
  tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler*) override { return kNotImplemented; }
  tresult PLUGIN_API registerTimer(Linux::ITimerHandler* h, Linux::TimerInterval ms) override
  {
    assert(ms && !handler); ++starts;
    if (fail) return kResultFalse;
    handler=h;h->addRef();return kResultOk;
  }
  tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* h) override
  {
    assert(handler==h);++stops;handler=nullptr;h->release();return kResultOk;
  }
  void Tick() { assert(handler); auto* h=handler;h->addRef();h->onTimer();h->release(); }
  ~Host() { assert(!handler && refs==1); }
};

int main()
{
  Host host; int calls=0;
  {
    iplug::VST3RunLoopTimer timer;
    assert(!timer.Start(nullptr,16,[&]{++calls;}));
    host.fail=true; assert(!timer.Start(&host,16,[&]{++calls;}));
    assert(host.refs==1 && !host.handler);host.fail=false;
    assert(timer.Start(&host,16,[&]{++calls;host.Tick();}));
    host.Tick(); assert(calls==1); // nested host event loop must not recurse
    std::thread wrong([&]{host.Tick();});wrong.join();assert(calls==1);
    auto* queued=host.handler;queued->addRef();timer.Stop();queued->onTimer();queued->release();
    assert(calls==1 && !host.handler);timer.Stop();
    for(int i=0;i<1000;++i)
    {
      assert(timer.Start(&host,16,[&]{++calls;}));host.Tick();
      assert(timer.Start(&host,20,[&]{++calls;}));host.Tick();timer.Stop();
    }
    assert(calls==2001);
  }
  {
    auto timer=std::make_unique<iplug::VST3RunLoopTimer>();
    timer->Start(&host,16,[&]{ ++calls; timer.reset(); });
    host.Tick();assert(!timer && !host.handler && calls==2002);
  }
  assert(host.stops+1==host.starts); // the failed registration isn't unregistered
  std::cout<<"PASS: registration failure, nested callback, wrong-thread rejection, queued callback cancellation, 1000 replacements, destruction inside callback; "<<calls<<" callbacks\n";
}
