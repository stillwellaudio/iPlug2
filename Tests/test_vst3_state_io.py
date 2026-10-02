"""Compile the current shared state methods with fault-injecting host streams."""
from pathlib import Path
import os
import subprocess
import tempfile


def test_state_writes():
    source = (Path(__file__).resolve().parents[1] / 'IPlug/VST3/IPlugVST3_Common.h').read_text()
    methods = source[source.index('struct IPlugVST3State'):source.index('// Host')]
    code = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#define TRACE
namespace Steinberg {
using int32=int32_t;
constexpr int kResultOk=0,kResultTrue=0,kResultFalse=1;
struct IBStream {
  enum IStreamSeekMode { kIBSeekSet };
  int mode=0,calls=0,fragment=1000;
  std::vector<unsigned char> bytes;
  int write(void* data,int size,int* written=nullptr) {
    ++calls;
    if(mode==1||(mode==2&&calls==2))return kResultFalse;
    int n=mode==3?0:mode==4?size+1:std::min(size,fragment);
    if(written)*written=n;
    if(n>0&&n<=size)bytes.insert(bytes.end(),(unsigned char*)data,(unsigned char*)data+n);
    return kResultOk;
  }
  int read(void*,int,int* =nullptr){return kResultFalse;}
  int seek(int,IStreamSeekMode){return kResultOk;}
};
}
constexpr int kHostWaveLab=1;
struct IByteChunk {
  std::vector<unsigned char> data;
  void* GetData(){return data.data();}
  int Size()const{return int(data.size());}
  int PutBytes(const void* bytes,int size){auto p=(const unsigned char*)bytes;data.insert(data.end(),p,p+size);return Size();}
};
struct IPlugVST3ControllerBase {template<class T>void UpdateParams(T*,int){};};
struct Plug {
  bool serialize=true;
  bool SerializeState(IByteChunk& chunk){chunk.data={1,2,3,4,5,6,7,8};return serialize;}
  bool GetBypassed(){return true;}
};
'''+methods+r'''
int main(){
  Plug plug;
  assert(!IPlugVST3State::GetState(&plug,nullptr));
  for(int mode:{1,2,3,4}){
    Steinberg::IBStream stream;stream.mode=mode;
    assert(!IPlugVST3State::GetState(&plug,&stream));
  }
  for(int fragment:{1,2,3,1000}){
    Steinberg::IBStream stream;stream.fragment=fragment;
    assert(IPlugVST3State::GetState(&plug,&stream));
    assert(stream.bytes.size()==12);
    for(int i=0;i<8;++i)assert(stream.bytes[i]==i+1);
    int32_t bypass=0;std::memcpy(&bypass,stream.bytes.data()+8,4);assert(bypass==1);
  }
  plug.serialize=false;Steinberg::IBStream stream;
  assert(!IPlugVST3State::GetState(&plug,&stream));assert(stream.calls==0);
}
'''
    with tempfile.TemporaryDirectory() as temp:
        src=Path(temp)/'probe.cpp';exe=Path(temp)/'probe'
        src.write_text(code)
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

if __name__=='__main__':
    test_state_writes()
