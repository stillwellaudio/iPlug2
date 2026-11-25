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
#include <limits>
#include <stdexcept>
#define IPLUG_VST3_STATE_MAX_BYTES 256
#define TRACE
namespace Steinberg {
using int32=int32_t;
constexpr int kResultOk=0,kResultTrue=0,kResultFalse=1;
struct IBStream {
  enum IStreamSeekMode { kIBSeekSet };
  int mode=0,calls=0,fragment=1000,pos=0,reads=0,readMode=0;
  std::vector<unsigned char> bytes;
  int write(void* data,int size,int* written=nullptr) {
    ++calls;
    if(mode==1||(mode==2&&calls==2))return kResultFalse;
    int n=mode==3?0:mode==4?size+1:std::min(size,fragment);
    if(written)*written=n;
    if(n>0&&n<=size)bytes.insert(bytes.end(),(unsigned char*)data,(unsigned char*)data+n);
    return kResultOk;
  }
  int read(void* buffer,int size,int* count=nullptr){
    ++reads;
    if(readMode==1)throw std::bad_alloc();
    if(readMode==2){if(count)*count=size+1;return kResultOk;}
    if(readMode==4){if(count)*count=-1;return kResultOk;}
    int n=std::min({size,fragment,int(bytes.size())-pos});
    if(n>0)std::memcpy(buffer,bytes.data()+pos,n);pos+=n;if(count)*count=n;
    return readMode==3&&count?kResultFalse:kResultOk;
  }
  int seek(int offset,IStreamSeekMode){pos=offset;return kResultOk;}
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
  bool serialize=true,parsed=false,restored=false;int host=0;
  virtual ~Plug()=default;
  int GetHost(){return host;}
  int UnserializeState(const IByteChunk& chunk,int){parsed=true;return chunk.Size()>=12?8:-1;}
  void OnRestoreState(){restored=true;}
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
  for(int fragment:{1,3,128}){
    Steinberg::IBStream input;input.bytes.resize(12);input.fragment=fragment;Plug dest;
    assert(IPlugVST3State::SetState(&dest,&input));assert(dest.parsed&&dest.restored);
  }
  {Steinberg::IBStream input;input.bytes.resize(256);Plug dest;
   assert(IPlugVST3State::SetState(&dest,&input));}
  {Steinberg::IBStream input;input.bytes.resize(257);Plug dest;
   assert(!IPlugVST3State::SetState(&dest,&input));assert(!dest.parsed);assert(input.reads<=3);}
  {Steinberg::IBStream input;input.bytes.resize(1024*1024);Plug dest;
   assert(!IPlugVST3State::SetState(&dest,&input));assert(!dest.parsed);assert(input.reads<=3);}
  for(int mode:{1,2,4}){
    Steinberg::IBStream input;input.readMode=mode;Plug dest;
    assert(!IPlugVST3State::SetState(&dest,&input));assert(!dest.parsed);
  }
  {Plug dest;assert(!IPlugVST3State::SetState(&dest,nullptr));}
  {Steinberg::IBStream input;input.bytes.resize(12);input.readMode=3;Plug dest;dest.host=kHostWaveLab;
   assert(IPlugVST3State::SetState(&dest,&input));}
  plug.serialize=false;Steinberg::IBStream stream;
  assert(!IPlugVST3State::GetState(&plug,&stream));assert(stream.calls==0);
}
'''
    with tempfile.TemporaryDirectory() as temp:
        src=Path(temp)/'probe.cpp';exe=Path(temp)/'probe'
        src.write_text(code)
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

def test_byte_chunk_allocation_failure():
    source=(Path(__file__).resolve().parents[1]/'IPlug/IPlugStructs.h').read_text()
    start=source.index('  inline int PutBytes(')
    end=source.index('\n  /**',start)
    code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
struct Buffer {
  uint8_t bytes[16]{1,2,3,4};int size=4;bool fail=false;
  int GetSize(){return size;}
  uint8_t* Get(){return bytes;}
  uint8_t* Resize(int n){if(fail)return bytes;assert(n<=16);size=n;return bytes;}
};
struct Chunk {Buffer mBytes;
'''+source[start:end]+r'''
};
int main(){uint8_t incoming[4]{5,6,7,8};Chunk chunk;
chunk.mBytes.fail=true;assert(chunk.PutBytes(incoming,4)==-1);
assert(chunk.mBytes.size==4&&chunk.mBytes.bytes[0]==1);
chunk.mBytes.fail=false;assert(chunk.PutBytes(nullptr,1)==-1);
assert(chunk.PutBytes(incoming,-1)==-1);
assert(chunk.PutBytes(nullptr,0)==4);
assert(chunk.PutBytes(incoming,4)==8);assert(chunk.mBytes.bytes[4]==5);
chunk.mBytes.size=std::numeric_limits<int>::max();
assert(chunk.PutBytes(incoming,1)==-1);
}
'''
    with tempfile.TemporaryDirectory() as temp:
        src=Path(temp)/'probe.cpp';exe=Path(temp)/'probe';src.write_text(code)
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',
                        '-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)


if __name__=='__main__':
    test_state_writes()
    test_byte_chunk_allocation_failure()
