#include "IPlug/IPlugLinuxPathUtils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace iplug::linux_paths;

namespace {

int gFailures = 0;

void ExpectEqual(const std::string& actual, const std::string& expected, const char* label)
{
  if (actual != expected)
  {
    std::cerr << label << ": expected '" << expected << "', got '" << actual << "'\n";
    ++gFailures;
  }
}

void ExpectEmpty(const std::string& actual, const char* label)
{
  ExpectEqual(actual, {}, label);
}

void TestVST3ResourcePath()
{
  ExpectEqual(
    VST3ResourcePathFromModule(
      "/opt/Stillwell/Event Horizon.vst3/Contents/x86_64-linux/eventhorizon.so"),
    "/opt/Stillwell/Event Horizon.vst3/Contents/Resources/",
    "VST3 resource path");

  ExpectEmpty(
    VST3ResourcePathFromModule("/opt/Stillwell/eventhorizon.so"),
    "non-bundle module path");
  ExpectEmpty(
    VST3ResourcePathFromModule(
      "/opt/Stillwell/Event Horizon.vst3/Other/x86_64-linux/eventhorizon.so"),
    "non-Contents bundle path");
  ExpectEmpty(
    VST3ResourcePathFromModule(
      "/opt/Stillwell/Event Horizon.vst3/Contents/x86_64-win/eventhorizon.so"),
    "non-Linux architecture directory");
}

void TestXDGPaths()
{
  ExpectEqual(
    ResolveXDGPath("/tmp/xdg-data", "/home/tester", ".local/share"),
    "/tmp/xdg-data",
    "absolute XDG path");
  ExpectEqual(
    ResolveXDGPath("relative-data", "/home/tester", ".local/share"),
    "/home/tester/.local/share",
    "relative XDG path falls back");
  ExpectEqual(
    ResolveXDGPath(nullptr, "/home/tester/", ".config"),
    "/home/tester/.config",
    "HOME fallback path");
  ExpectEmpty(
    ResolveXDGPath(nullptr, nullptr, ".cache"),
    "missing XDG and HOME");
}

void TestResourceLookup()
{
  const fs::path root = fs::temp_directory_path() / "iplug-linux-paths-test";
  std::error_code error;
  fs::remove_all(root, error);
  fs::create_directories(root / "Resources" / "img");
  std::ofstream(root / "Resources" / "img" / "background.png") << "fixture";

  ExpectEqual(
    FindResource(root / "Resources", "img/background.png"),
    (root / "Resources" / "img" / "background.png").string(),
    "existing bundle resource");
  ExpectEmpty(
    FindResource(root / "Resources", "img/missing.png"),
    "missing bundle resource");
  ExpectEmpty(
    FindResource(root / "Resources", "../outside.png"),
    "resource traversal");
  ExpectEmpty(
    FindResource(root / "missing", "img/background.png"),
    "missing resource directory");

  fs::remove_all(root, error);
}

} // namespace

int main()
{
  TestVST3ResourcePath();
  TestXDGPaths();
  TestResourceLookup();
  return gFailures == 0 ? 0 : 1;
}
