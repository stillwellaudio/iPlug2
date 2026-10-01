#include "IPlugAAXBusLayout.h"
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"failed: %s:%d: %s\\n",__FILE__,__LINE__,#x); return 1; } } while (0)
using namespace iplug;
int main()
{
  for (const auto& entry : aaxlayouts::SpeakerStems()) { BusLayout l; CHECK(aaxlayouts::FromStem(entry.stem,l)); AAX_EStemFormat back; if (!aaxlayouts::ToStem(l,back) || back!=entry.stem) { std::fprintf(stderr, "stem=%d back=%d\n", (int) entry.stem, (int) back); return 1; } }
  for (unsigned order=1; order<=7; ++order) { const auto l=BusLayout::Ambisonic(order,AmbisonicOrdering::ACN,AmbisonicNormalization::SN3D); AAX_EStemFormat stem; CHECK(aaxlayouts::ToStem(l,stem) && stem==aaxlayouts::AmbisonicStem(order)); BusLayout back; CHECK(aaxlayouts::FromStem(stem,back) && l.SameMeaning(back)); }
  AAX_EStemFormat discrete; CHECK(!aaxlayouts::ToStem(BusLayout::Discrete(2), discrete));
  std::puts("AAX bus layout mapping passed");
}
