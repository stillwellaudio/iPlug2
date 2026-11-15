"""Compile the shared VST3 state methods and IByteChunk::PutBytes against fault-injecting stubs.

Run: python3 Tests/test_vst3_state_io.py   (needs a C++17 compiler with ASan/UBSan; CXX overrides c++)
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SANITIZE = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']


def compile_and_run(code):
    with tempfile.TemporaryDirectory(prefix='iplug-vst3-state-') as temp:
        src = Path(temp) / 'probe.cpp'
        exe = Path(temp) / 'probe'
        src.write_text(code)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-g', '-Wall', '-Wextra', '-Werror',
                        *SANITIZE, str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


class VST3StateIOTests(unittest.TestCase):
    def test_state_streams(self):
        source = (ROOT / 'IPlug/VST3/IPlugVST3_Common.h').read_text()
        methods = source[source.index('struct IPlugVST3State'):source.index('// Host')]
        code = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#define TRACE
namespace Steinberg {
using int32 = int32_t;
constexpr int kResultOk = 0, kResultTrue = 0, kResultFalse = 1;
struct IBStream {
  enum IStreamSeekMode { kIBSeekSet };
  // readMode: 0 normal; 2 reports one byte more than requested; 3 returns
  // kResultFalse with a short read that reaches the end; 4 reports a
  // negative count.
  // reportWritten: false leaves numBytesWritten untouched.
  int calls = 0, fragment = 1000, pos = 0, reads = 0, readMode = 0;
  bool reportWritten = true;
  std::vector<unsigned char> bytes;
  int write(void* data, int size, int* written = nullptr) {
    ++calls;
    if (written && reportWritten) *written = size;
    bytes.insert(bytes.end(), (unsigned char*) data, (unsigned char*) data + size);
    return kResultOk;
  }
  int read(void* buffer, int size, int* count = nullptr) {
    ++reads;
    if (readMode == 2) { if (count) *count = size + 1; return kResultOk; }
    if (readMode == 4) { if (count) *count = -1; return kResultOk; }
    int n = std::min({size, fragment, int(bytes.size()) - pos});
    if (n > 0) std::memcpy(buffer, bytes.data() + pos, n);
    pos += n;
    if (count) *count = n;
    return readMode == 3 && n > 0 && n < size && pos == int(bytes.size()) ? kResultFalse : kResultOk;
  }
  int seek(int offset, IStreamSeekMode) { pos = offset; return kResultOk; }
};
}
constexpr int kHostWaveLab = 1;
int gFailPutAfter = -1; // injects an allocation failure into the chunk SetState() builds
struct IByteChunk {
  std::vector<unsigned char> data;
  int failPutAfter = gFailPutAfter, puts = 0;
  void* GetData() { return data.data(); }
  const void* GetData() const { return data.data(); }
  int Size() const { return int(data.size()); }
  int PutBytes(const void* bytes, int size) {
    if (failPutAfter >= 0 && puts++ >= failPutAfter) return -1;
    auto p = (const unsigned char*) bytes; data.insert(data.end(), p, p + size); return Size();
  }
  template <class T> int Get(T* dst, int pos) const {
    if (pos < 0 || pos + int(sizeof(T)) > Size()) return -1;
    std::memcpy(dst, data.data() + pos, sizeof(T)); return pos + int(sizeof(T));
  }
};
struct IPlugVST3ControllerBase { virtual ~IPlugVST3ControllerBase() = default; template <class T> void UpdateParams(T*, int) {} };
struct Plug {
  bool serialize = true, transactional = false, parsed = false, restored = false;
  int host = 0;
  virtual ~Plug() = default;
  int GetHost() { return host; }
  bool RestoresStateTransactionally() const { return transactional; }
  // The state is 8 bytes; the int32 bypass flag follows it.
  int ValidateState(const IByteChunk& chunk, int) const { return chunk.Size() >= 8 ? 8 : -1; }
  int UnserializeState(const IByteChunk& chunk, int) { parsed = true; return chunk.Size() >= 8 ? 8 : -1; }
  void RestoreHostBypass(bool) {}
  void OnRestoreState() { restored = true; }
  bool SerializeState(IByteChunk& chunk) { chunk.data = {1, 2, 3, 4, 5, 6, 7, 8}; return serialize; }
  bool GetBypassed() { return true; }
};
''' + methods + r'''
Steinberg::IBStream Saved(int fragment = 1000) {
  Steinberg::IBStream s; s.bytes = {1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0}; s.fragment = fragment; return s;
}
int main() {
  // Null streams fail instead of dereferencing.
  { Plug plug; assert(!IPlugVST3State::GetState(&plug, nullptr)); }
  for (bool transactional : {false, true}) {
    Plug dest; dest.transactional = transactional;
    assert(!IPlugVST3State::SetState(&dest, nullptr)); assert(!dest.parsed);
  }

  // Well-formed state: unchanged results on both restore paths.
  {
    Plug plug; Steinberg::IBStream out;
    assert(IPlugVST3State::GetState(&plug, &out));
    assert(out.bytes.size() == 12);
    for (int i = 0; i < 8; ++i) assert(out.bytes[i] == i + 1);
    int32_t bypass = 0; std::memcpy(&bypass, out.bytes.data() + 8, 4); assert(bypass == 1);
  }
  { // A host that does not report numBytesWritten still saves (write checking is not applied).
    Plug plug; Steinberg::IBStream out; out.reportWritten = false;
    assert(IPlugVST3State::GetState(&plug, &out)); assert(out.bytes.size() == 12);
  }
  { Plug plug; plug.serialize = false; Steinberg::IBStream out;
    assert(!IPlugVST3State::GetState(&plug, &out)); assert(out.calls == 0); }
  for (bool transactional : {false, true})
    for (int fragment : {1, 3, 8, 128}) {
      Plug dest; dest.transactional = transactional; auto in = Saved(fragment);
      assert(IPlugVST3State::SetState(&dest, &in)); assert(dest.parsed && dest.restored);
    }

  // A count above the request is rejected before anything is copied.
  for (bool transactional : {false, true}) {
    Plug dest; dest.transactional = transactional; auto in = Saved(); in.readMode = 2;
    assert(!IPlugVST3State::SetState(&dest, &in)); assert(!dest.parsed); assert(in.reads == 1);
  }

  // An allocation failure while collecting the state is rejected; the
  // incomplete chunk is not parsed. (Without the check, the legacy path
  // would parse the first 8 bytes and restore.)
  for (bool transactional : {false, true}) {
    gFailPutAfter = 1;
    Plug dest; dest.transactional = transactional; auto in = Saved(8);
    assert(!IPlugVST3State::SetState(&dest, &in)); assert(!dest.parsed);
    gFailPutAfter = -1;
  }

  // Behavior kept as on master (the stricter checks are not applied):
  { // WaveLab returns kResultFalse with data and keeps reading.
    Plug dest; dest.host = kHostWaveLab; auto in = Saved(8); in.readMode = 3;
    assert(IPlugVST3State::SetState(&dest, &in)); }
  { // Other hosts: kResultFalse with the last block ends reading; the
    // legacy path still restores and re-reads the bypass flag.
    Plug dest; auto in = Saved(8); in.readMode = 3;
    assert(IPlugVST3State::SetState(&dest, &in)); assert(dest.restored); }
  { // A negative count ends reading, as before.
    Plug dest; auto in = Saved(); in.readMode = 4;
    assert(!IPlugVST3State::SetState(&dest, &in)); assert(dest.parsed); }
}
'''
        compile_and_run(code)

    def test_put_bytes(self):
        source = (ROOT / 'IPlug/IPlugStructs.h').read_text()
        start = source.index('  inline int PutBytes(')
        end = source.index('\n  /**', start)
        code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
struct Buffer {
  uint8_t bytes[16] {1, 2, 3, 4}; int size = 4; bool fail = false;
  int GetSize() { return size; }
  uint8_t* Get() { return bytes; }
  // Like WDL_HeapBuf::Resize(): on failure the size is left unchanged.
  uint8_t* Resize(int n) { if (fail) return bytes; assert(n <= 16); size = n; return bytes; }
};
struct Chunk { Buffer mBytes;
''' + source[start:end] + r'''
};
int main() {
  uint8_t incoming[4] {5, 6, 7, 8};
  Chunk chunk;
  chunk.mBytes.fail = true;
  assert(chunk.PutBytes(incoming, 4) == -1);
  assert(chunk.mBytes.size == 4 && chunk.mBytes.bytes[0] == 1 && chunk.mBytes.bytes[4] == 0);
  chunk.mBytes.fail = false;
  assert(chunk.PutBytes(nullptr, 1) == -1);
  assert(chunk.PutBytes(incoming, -1) == -1);
  assert(chunk.mBytes.size == 4);
  assert(chunk.PutBytes(nullptr, 0) == 4);
  assert(chunk.PutBytes(incoming, 0) == 4);
  assert(chunk.PutBytes(incoming, 4) == 8);
  assert(chunk.mBytes.bytes[4] == 5 && chunk.mBytes.bytes[7] == 8);
  chunk.mBytes.size = std::numeric_limits<int>::max();
  assert(chunk.PutBytes(incoming, 1) == -1);
}
'''
        compile_and_run(code)


if __name__ == '__main__':
    unittest.main()
