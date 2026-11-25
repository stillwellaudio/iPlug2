"""Exercise the pinned bank parser and byte types with malformed input under sanitizers."""
from pathlib import Path
import os
import subprocess
import tempfile


def test_bank_parser_bounds():
    root = Path(__file__).resolve().parents[1]
    source = (root / "IPlug/IPlugPluginBase.cpp").read_text()
    start = source.index("int IPluginBase::UnserializePresets(")
    opening = source.index("{", start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if not depth:
            break
    method = source[start:end + 1].replace("IPluginBase::", "Probe::")
    code = r'''
#include "IPlugStructs.h"
#include <cassert>
#include <climits>
#include <cstring>
#include <string>
#define TRACE
#define Trace(...)
using namespace iplug;
struct Probe {
  IPreset preset;
  WDL_PtrList<IPreset> mPresets;
  int mCurrentPresetIdx = 0, applyCalls = 0, restoreCalls = 0;
  Probe() { mPresets.Add(&preset); }
  int UnserializeState(const IByteChunk& chunk, int pos) {
    ++applyCalls;
    return pos < chunk.Size() ? pos + 1 : -1;
  }
  bool SerializeState(IByteChunk& chunk) { const uint8_t value = 7; return chunk.Put(&value) > 0; }
  bool RestorePreset(int) { ++restoreCalls; return true; }
  int UnserializePresets(const IByteChunk&, int);
};
''' + method + r'''
void reject(const IByteChunk& bank) {
  Probe p;
  const std::string before = p.preset.mName;
  assert(p.UnserializePresets(bank, 0) == -1);
  assert(before == p.preset.mName);
  assert(!p.preset.mInitialized && p.preset.mChunk.Size() == 0);
  assert(p.restoreCalls == 0);
}
int main() {
  for (int length : {256, 4096}) {
    IByteChunk bank; bank.PutStr(std::string(length, 'X').c_str());
    const uint8_t initialized = 0; bank.Put(&initialized); reject(bank);
  }
  for (int length : {-1, INT_MAX, 256}) {
    IByteChunk bank; bank.Put(&length); reject(bank);
    WDL_String name("sentinel");
    assert(bank.GetStr(name, 0) == -1);
    assert(std::string(name.Get()) == "sentinel");
  }
  for (int size : {0, 1, 2, 3}) { IByteChunk bank; bank.Resize(size); reject(bank); }
  for (int flag : {-1, 2, 255}) {
    IByteChunk bank; bank.PutStr("valid"); const uint8_t initialized = uint8_t(flag);
    bank.Put(&initialized); reject(bank);
  }
  { IByteChunk bank; bank.PutStr("valid"); reject(bank); }
  { IByteChunk bank; bank.PutStr("valid"); const uint8_t initialized = 1;
    bank.Put(&initialized); reject(bank); }
  for (bool initialized : {false, true}) {
    Probe p; IByteChunk bank; bank.PutStr(std::string(255, 'N').c_str()); bank.Put(&initialized);
    if (initialized) { const uint8_t value = 7; bank.Put(&value); }
    assert(p.UnserializePresets(bank, 0) == bank.Size());
    assert(std::strlen(p.preset.mName) == 255 && p.preset.mInitialized == initialized);
    assert(p.applyCalls == int(initialized) && p.restoreCalls == 1);
  }
  uint8_t source = 0, destination = 0;
  assert(IByteGetter::GetBytes(&source, 1, &destination, INT_MAX, 1) == -1);
  assert(IByteGetter::GetBytes(&source, 1, &destination, -1, 0) == -1);
  assert(IByteGetter::GetBytes(&source, 1, &destination, 1, INT_MAX) == -1);
  assert(IByteGetter::GetBytes(nullptr, 0, nullptr, 0, 0) == 0);
}
'''
    with tempfile.TemporaryDirectory(prefix="iplug-bank-bounds-") as temp:
        cpp = Path(temp) / "probe.cpp"
        exe = Path(temp) / "probe"
        cpp.write_text(code)
        compiler = os.environ.get("CXX", "clang++")
        subprocess.run([compiler, "-std=c++17", "-O1", "-g",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I" + str(root / "IPlug"), "-I" + str(root / "WDL"),
                        str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
