"""Exercise IByteGetter bounds and the preset bank parser with malformed input under sanitizers.

Run: python3 Tests/test_preset_bank.py   (needs a C++17 compiler with ASan/UBSan; CXX overrides c++)
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if not depth:
            break
    return source[start:end + 1].replace('IPluginBase::', 'Probe::')


def compile_and_run(code):
    with tempfile.TemporaryDirectory(prefix='iplug-bank-bounds-') as temp:
        cpp = Path(temp) / 'probe.cpp'
        exe = Path(temp) / 'probe'
        cpp.write_text(code)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-fno-omit-frame-pointer',
                        '-I' + str(ROOT / 'IPlug'), '-I' + str(ROOT / 'WDL'),
                        str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


class PresetBankTests(unittest.TestCase):
    def test_byte_getter_bounds(self):
        code = r'''
#include "IPlugStructs.h"
#include <cassert>
#include <climits>
#include <string>
using namespace iplug;
int main() {
  uint8_t source[4] {1, 2, 3, 4}, destination[4] {};
  // Well-formed reads are unchanged.
  assert(IByteGetter::GetBytes(source, 4, destination, 4, 0) == 4 && destination[3] == 4);
  assert(IByteGetter::GetBytes(source, 4, destination, 2, 2) == 4 && destination[0] == 3);
  assert(IByteGetter::GetBytes(source, 4, destination, 0, 4) == 4);
  assert(IByteGetter::GetBytes(nullptr, 0, nullptr, 0, 0) == 0);
  // Out of range, including sums that would overflow int.
  assert(IByteGetter::GetBytes(source, 4, destination, 1, 4) == -1);
  assert(IByteGetter::GetBytes(source, 4, destination, INT_MAX, 1) == -1);
  assert(IByteGetter::GetBytes(source, 4, destination, 1, INT_MAX) == -1);
  assert(IByteGetter::GetBytes(source, 4, destination, -1, 0) == -1);
  assert(IByteGetter::GetBytes(source, 4, destination, 1, -1) == -1);
  assert(IByteGetter::GetBytes(source, 4, nullptr, 1, 0) == -1);

  { // Well-formed strings are unchanged.
    IByteChunk chunk; chunk.PutStr("valid"); chunk.PutStr("");
    WDL_String a, b;
    const int next = chunk.GetStr(a, 0);
    assert(next == 9 && std::string(a.Get()) == "valid");
    assert(chunk.GetStr(b, next) == chunk.Size() && std::string(b.Get()).empty());
  }
  // A negative, overflowing or truncated length fails and leaves the string alone.
  for (int length : {-1, INT_MAX, 256}) {
    IByteChunk chunk; chunk.Put(&length);
    WDL_String name("sentinel");
    assert(chunk.GetStr(name, 0) == -1);
    assert(std::string(name.Get()) == "sentinel");
  }
  { IByteChunk chunk; chunk.PutStr("valid"); chunk.Resize(chunk.Size() - 1);
    WDL_String name("sentinel");
    assert(chunk.GetStr(name, 0) == -1 && std::string(name.Get()) == "sentinel"); }
}
'''
        compile_and_run(code)

    def test_bank_parser_bounds(self):
        source = (ROOT / 'IPlug/IPlugPluginBase.cpp').read_text()
        methods = '\n'.join(method(source, s) for s in (
            'static void MakeDefaultUserPresetName(',
            'int IPluginBase::ValidateState(',
            'int IPluginBase::ValidatePresets(',
            'int IPluginBase::UnserializePresets('))
        # The real RestorePreset(), renamed so the probe can count calls to it.
        methods += '\n' + method(source, 'bool IPluginBase::RestorePreset(int idx)').replace(
            'Probe::RestorePreset(int idx)', 'Probe::RestorePresetImpl(int idx)')
        code = r'''
#include "IPlugStructs.h"
#include <cassert>
#include <climits>
#include <cmath>
#include <cstring>
#include <string>
#define TRACE
#define Trace(...)
using namespace iplug;
// One preset of one double parameter, parsed and restored by the plug-in
// base's own code. Like an opted-in plug-in, UnserializeState() accepts exactly
// what the default ValidateState() accepts and publishes nothing on rejection.
struct Probe {
  IPreset preset;
  WDL_PtrList<IPreset> mPresets;
  int mCurrentPresetIdx = 0, restoreCalls = 0;
  bool transactional = false;
  double value = 0.0;
  Probe() { mPresets.Add(&preset); }
  int NParams() const { return 1; }
  bool RestoresStateTransactionally() const { return transactional; }
  int UnserializeState(const IByteChunk& chunk, int pos) {
    double v = 0.0;
    const int end = chunk.Get(&v, pos);
    if (end < 0 || !std::isfinite(v)) return -1;
    value = v;
    return end;
  }
  bool SerializeState(IByteChunk& chunk) const { return chunk.Put(&value) > 0; }
  void OnPresetsModified() {}
  void OnRestoreState() {}
  bool RestorePreset(int idx) { ++restoreCalls; return RestorePresetImpl(idx); }
  bool RestorePresetImpl(int idx);
  int ValidateState(const IByteChunk& chunk, int startPos) const;
  int ValidatePresets(const IByteChunk& chunk, int startPos) const;
  int UnserializePresets(const IByteChunk&, int);
};
''' + methods + r'''
IByteChunk Bank(const std::string& name, bool initialized, double value) {
  IByteChunk bank; bank.PutStr(name.c_str()); bank.Put(&initialized);
  if (initialized) bank.Put(&value);
  return bank;
}
int main() {
  for (bool transactional : {false, true}) {
    // Well-formed banks load as before. Preset 0 is current, so the bank's
    // values reach the live state through RestorePreset() on both paths.
    {
      Probe p; p.transactional = transactional;
      IByteChunk bank = Bank(std::string(MAX_PRESET_NAME_LEN - 1, 'N'), true, 0.5);
      assert(p.UnserializePresets(bank, 0) == bank.Size());
      assert(std::strlen(p.preset.mName) == MAX_PRESET_NAME_LEN - 1);
      assert(p.preset.mInitialized && p.restoreCalls == 1);
      double stored = 0.0;
      assert(p.preset.mChunk.Get(&stored, 0) == 8 && stored == 0.5);
      assert(p.value == 0.5);
    }
    { // An uninitialized current preset is initialized from the live state.
      Probe p; p.transactional = transactional;
      IByteChunk bank = Bank(std::string(MAX_PRESET_NAME_LEN - 1, 'N'), false, 0.5);
      assert(p.UnserializePresets(bank, 0) == bank.Size());
      assert(p.preset.mInitialized && p.restoreCalls == 1);
      assert(std::string(p.preset.mName) == DEFAULT_USER_PRESET_NAME " 1");
      double stored = -1.0;
      assert(p.preset.mChunk.Get(&stored, 0) == 8 && stored == 0.0);
      assert(p.value == 0.0);
    }
    // Malformed name lengths fail without reading or computing out of range.
    for (int length : {-1, INT_MAX, INT_MAX - 3, 64}) {
      Probe p; p.transactional = transactional;
      IByteChunk bank; bank.Put(&length);
      assert(p.UnserializePresets(bank, 0) == -1);
    }
    // Truncated banks fail.
    for (int keep : {0, 1, 3, 5, 9, 10}) {
      Probe p; p.transactional = transactional;
      IByteChunk bank = Bank("valid", true, 0.5); bank.Resize(keep);
      assert(p.UnserializePresets(bank, 0) == -1);
    }
  }
}
'''
        compile_and_run(code)


if __name__ == '__main__':
    unittest.main()
