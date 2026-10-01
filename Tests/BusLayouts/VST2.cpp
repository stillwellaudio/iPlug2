#include "IPlugVST2.h"
#include <cstdlib>
#include <iostream>
using namespace iplug;
#define CHECK(x) do { if(!(x)){std::cerr<<__LINE__<<" "<<#x<<'\n';return 1;} }while(0)
static VstIntPtr VSTCALLBACK Host(AEffect*,VstInt32,VstInt32,VstIntPtr,void*,float){return 0;}
static Config C(const BusLayoutPolicy* p){return Config(0,0,"2-2","v","v","m",0,1,2,0,false,false,false,false,0,false,0,0,false,0,0,0,0,"","",p);}
int main(){ BusLayoutPolicy p; p.capacities[0]={2};p.capacities[1]={2};p.defaultID=9; BusLayoutConfiguration c;c.id=9;c.buses[0]={};c.buses[1]={}; LayoutBus i;i.id=1;i.role=BusRole::Main;i.layout=BusLayout::Speakers({SpeakerPosition::L,SpeakerPosition::R}); LayoutBus o=i;o.id=2;o.bypassInputBus=0;c.buses[0]={i};c.buses[1]={o};p.configurations={c}; IPlugVST2 plug({Host},C(&p));AEffect& e=plug.GetAEffect();CHECK(e.dispatcher(&e,effOpen,0,0,nullptr,0)==0);VstSpeakerArrangement* in=nullptr;VstSpeakerArrangement*out=nullptr;CHECK(e.dispatcher(&e,effGetSpeakerArrangement,0,(VstIntPtr)&in,&out,0)==1);CHECK(in&&out&&in->numChannels==2&&in->speakers[0].type==kSpeakerL);float a[8],b[8],x[8]{},y[8]{}; for(int n=0;n<8;++n){a[n]=float(n+1);b[n]=float(10+n);} float* ip[]={a,b};float*op[]={x,y}; e.processReplacing(&e,ip,op,8); for(int n=0;n<8;++n) CHECK(x[n]==a[n]&&y[n]==b[n]);
VstSpeakerArrangement bad=*in; bad.numChannels=1; CHECK(e.dispatcher(&e,effSetSpeakerArrangement,0,(VstIntPtr)&bad,out,0)==0); CHECK(e.dispatcher(&e,effSetSpeakerArrangement,0,(VstIntPtr)in,out,0)==1);
double da[8],db[8],dx[8]{},dy[8]{};for(int n=0;n<8;++n){da[n]=n+2;db[n]=n+12;}double* dip[]={da,db};double*dop[]={dx,dy};e.processDoubleReplacing(&e,dip,dop,8);for(int n=0;n<8;++n) CHECK(dx[n]==da[n]&&dy[n]==db[n]);std::cout<<"VST2 bus layout adapter passed\n";}
