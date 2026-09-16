// Real-binary Linux VST3 regression: nested popup callbacks and editor teardown.
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstcontextmenu.h"
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>
using namespace Steinberg;
using namespace Steinberg::Vst;
#define QI(T) if(FUnknownPrivate::iidEqual(iid,T::iid)) { *out=static_cast<T*>(this); addRef(); return kResultOk; }
struct Menu final : IContextMenu
{
  uint32 refs=1;std::function<void()> action;
  explicit Menu(std::function<void()> f):action(std::move(f)){}
  tresult PLUGIN_API queryInterface(const TUID iid, void** out) override { *out=nullptr;QI(IContextMenu);return kNoInterface; }
  uint32 PLUGIN_API addRef() override{return ++refs;}
  uint32 PLUGIN_API release() override{auto r=--refs;if(!r)delete this;return r;}
  int32 PLUGIN_API getItemCount() override{return 0;}
  tresult PLUGIN_API getItem(int32,Item&,IContextMenuTarget**) override{return kResultFalse;}
  tresult PLUGIN_API addItem(const Item&,IContextMenuTarget*) override{return kResultOk;}
  tresult PLUGIN_API removeItem(const Item&,IContextMenuTarget*) override{return kResultOk;}
  tresult PLUGIN_API popup(UCoord,UCoord) override{action();return kResultOk;}
};
struct Host final : IHostApplication,IComponentHandler,IComponentHandler3,IPlugFrame,Linux::IRunLoop
{
  std::atomic<uint32> refs{1};std::vector<Linux::ITimerHandler*> timers;
  bool exposeLoop=true,failTimer=false,failSecondTimer=false,removed=false;unsigned menus=0,starts=0,stops=0;
  int mode=0;Window parent=0;const std::thread::id ui=std::this_thread::get_id();
  tresult PLUGIN_API queryInterface(const TUID iid,void**out) override
  {
    *out=nullptr;QI(IHostApplication);QI(IComponentHandler);QI(IComponentHandler3);QI(IPlugFrame);
    if(exposeLoop){QI(Linux::IRunLoop);}
    if(FUnknownPrivate::iidEqual(iid,FUnknown::iid)){*out=static_cast<IHostApplication*>(this);addRef();return kResultOk;}
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override{return ++refs;}
  uint32 PLUGIN_API release() override{return --refs;}
  tresult PLUGIN_API getName(String128 name) override{const char* n="RunLoop Regression Host";int i=0;for(;n[i];++i)name[i]=n[i];name[i]=0;return kResultOk;}
  tresult PLUGIN_API createInstance(TUID,TUID,void**o) override{*o=nullptr;return kNoInterface;}
  tresult PLUGIN_API beginEdit(ParamID) override{return kResultOk;}
  tresult PLUGIN_API performEdit(ParamID,ParamValue) override{return kResultOk;}
  tresult PLUGIN_API endEdit(ParamID) override{return kResultOk;}
  tresult PLUGIN_API restartComponent(int32) override{return kResultOk;}
  tresult PLUGIN_API resizeView(IPlugView*,ViewRect*) override{return kResultOk;}
  tresult PLUGIN_API registerEventHandler(Linux::IEventHandler*,Linux::FileDescriptor) override{return kNotImplemented;}
  tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler*) override{return kNotImplemented;}
  tresult PLUGIN_API registerTimer(Linux::ITimerHandler*h,Linux::TimerInterval) override
  {if(failTimer || (failSecondTimer && !timers.empty()))return kResultFalse;assert(ui==std::this_thread::get_id());++starts;timers.push_back(h);h->addRef();return kResultOk;}
  tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler*h) override
  {auto it=std::find(timers.begin(),timers.end(),h);assert(it!=timers.end());timers.erase(it);++stops;h->release();return kResultOk;}
  void Pump()
  {auto copy=timers;for(auto*h:copy)h->addRef();for(auto*h:copy){h->onTimer();h->release();}}
  IContextMenu* PLUGIN_API createContextMenu(IPlugView*view,const ParamID*) override
  {
    assert(ui==std::this_thread::get_id());
    return new Menu([this,view]{
      assert(ui==std::this_thread::get_id());++menus;const auto count=menus;
      Pump();assert(menus==count); // modal host loop must not recurse into input/draw
      if(mode==1 || mode==4){assert(view->removed()==kResultOk);removed=true;view->setFrame(nullptr);}
      if(mode==2)view->setFrame(nullptr);
      if(mode==3){view->setFrame(nullptr);view->setFrame(this);}
      if(mode==4){view->setFrame(this);assert(view->attached(reinterpret_cast<void*>(parent),kPlatformTypeX11EmbedWindowID)==kResultOk);removed=false;}
    });
  }
};
void RightClick(Display*d,Window parent,int x,int y)
{
  Window root,p,*children=nullptr;unsigned n=0;assert(XQueryTree(d,parent,&root,&p,&children,&n)&&n);
  Window child=children[n-1];XFree(children);
  XEvent event{};event.xbutton.display=d;event.xbutton.window=child;event.xbutton.root=DefaultRootWindow(d);
  event.xbutton.x=x;event.xbutton.y=y;event.xbutton.same_screen=True;event.xbutton.button=Button3;
  event.xbutton.type=ButtonPress;XSendEvent(d,child,False,ButtonPressMask,&event);
  event.xbutton.type=ButtonRelease;event.xbutton.state=Button3Mask;XSendEvent(d,child,False,ButtonReleaseMask,&event);
  XSync(d,False);
}
int main(int argc,char**argv)
{
  assert(argc==2);XInitThreads();Display*d=XOpenDisplay(nullptr);assert(d);
  Host host;host.parent=XCreateSimpleWindow(d,DefaultRootWindow(d),0,0,1000,525,0,0,0);XMapWindow(d,host.parent);XSync(d,False);
  void*module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!module){std::cerr<<dlerror()<<'\n';return 2;}
  auto entry=reinterpret_cast<bool(*)(void*)>(dlsym(module,"ModuleEntry"));assert(entry&&entry(module));
  auto factory=reinterpret_cast<IPluginFactory*(*)()>(dlsym(module,"GetPluginFactory"))();assert(factory);
  IComponent*component=nullptr;
  for(int i=0;i<factory->countClasses();++i){PClassInfo info{};factory->getClassInfo(i,&info);if(std::strcmp(info.category,kVstAudioEffectClass)==0){assert(factory->createInstance(info.cid,IComponent::iid,(void**)&component)==kResultOk);break;}}
  assert(component);assert(component->initialize(static_cast<IHostApplication*>(&host))==kResultOk);
  IEditController*controller=nullptr;assert(component->queryInterface(IEditController::iid,(void**)&controller)==kResultOk);
  assert(controller->setComponentHandler(&host)==kResultOk);
  IPlugView*view=controller->createView(ViewType::kEditor);assert(view);
  auto attach=[&]{host.removed=false;view->setFrame(&host);assert(view->attached(reinterpret_cast<void*>(host.parent),kPlatformTypeX11EmbedWindowID)==kResultOk);};
  auto settle=[&]{for(int j=0;j<8;++j){std::this_thread::sleep_for(std::chrono::milliseconds(10));host.Pump();}};
  for(int mode=0;mode<=4;++mode)
  {
    for(int repeat=0;repeat<5;++repeat)
    {
      host.mode=mode;attach();assert(host.timers.size()==2);settle();unsigned before=host.menus;
      RightClick(d,host.parent,639,228);settle();assert(host.menus==before+1);
      if(!host.removed)view->removed();view->setFrame(nullptr);assert(host.timers.empty());
    }
    std::cout<<"PASS popup lifecycle mode "<<mode<<std::endl;
  }
  for(int fallback=0;fallback<3;++fallback)
  {
    host.mode=0;host.exposeLoop=fallback!=0;host.failTimer=fallback==1;host.failSecondTimer=fallback==2;
    attach();assert(host.timers.empty());unsigned before=host.menus;RightClick(d,host.parent,639,228);settle();assert(host.menus==before);
    host.exposeLoop=true;host.failTimer=false;host.failSecondTimer=false;view->setFrame(&host);assert(host.timers.size()==2);
    RightClick(d,host.parent,639,228);settle();assert(host.menus==before+1);
    view->removed();view->setFrame(nullptr);assert(host.timers.empty());
  }
  view->release();controller->setComponentHandler(nullptr);controller->release();component->terminate();component->release();factory->release();
  reinterpret_cast<bool(*)()>(dlsym(module,"ModuleExit"))();dlclose(module);
  XDestroyWindow(d,host.parent);XCloseDisplay(d);assert(host.starts==host.stops);
  std::cout<<"PASS: "<<host.menus<<" real-binary menus; normal, removal, null/replaced frame, nested reattach, missing/rejected run loop; "<<host.starts<<" balanced registrations\n";
}
