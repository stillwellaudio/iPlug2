#pragma once
#include "IPlugBusLayout.h"
// schOPE-shaped test policy: main passthrough plus an independently sized sidechain.
static constexpr const char* kAUPartitionShape="1-1 1.1-1 1.2-1 2-2 2.1-2 2.2-2 3-3 3.1-3 4-4";
static const iplug::BusLayoutPolicy& AUPartitionPolicy()
{
  using namespace iplug;
  static const BusLayoutPolicy policy=[] {
    BusLayoutPolicy p;p.defaultID=6;p.capacities[0]={4,2};p.capacities[1]={4};
    const unsigned widths[][2]={{1,0},{1,1},{1,2},{2,0},{2,1},{2,2},{3,0},{3,1},{4,0}};
    for (unsigned i=0;i<9;++i)
    {
      BusLayoutConfiguration c;c.id=i+1;LayoutBus main;main.id=0;main.role=BusRole::Main;main.layout=BusLayout::Discrete(widths[i][0],true);
      c.buses[0].push_back(main);
      if (widths[i][1]) {LayoutBus aux;aux.id=1;aux.role=BusRole::Sidechain;aux.layout=BusLayout::Discrete(widths[i][1],true);c.buses[0].push_back(aux);}
      LayoutBus out=main;out.id=100;out.bypassInputBus=0;c.buses[1].push_back(out);p.configurations.push_back(c);
    }
    p.accept=[](const BusLayoutConfiguration& c) {return !c.buses[0].empty() && c.buses[1].size()==1 && c.buses[0][0].layout.NChannels()==c.buses[1][0].layout.NChannels();};
    return p;
  }();return policy;
}
