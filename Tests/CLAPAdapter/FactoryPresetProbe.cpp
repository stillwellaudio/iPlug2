// Exercise discovery and recall against a built CLAP binary (without an editor).
#include <clap/clap.h>
#include <clap/factory/preset-discovery.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

struct Preset { std::string name, key; };
struct Capture
{
  std::vector<Preset> presets;
  std::string pluginID;
  int locations = 0, rescans = 0, loaded = 0;
};
static void Require(bool ok, const char* message)
{
  if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main(int argc, char** argv)
{
  Require(argc == 3, "usage: factory_preset_probe binary expected-count");
#ifdef _WIN32
  auto library = LoadLibraryA(argv[1]);
  auto entry = reinterpret_cast<const clap_plugin_entry_t*>(GetProcAddress(library, "clap_entry"));
#else
  auto library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!library) std::cerr << dlerror() << '\n';
  Require(library, "load binary");
  auto entry = static_cast<const clap_plugin_entry_t*>(dlsym(library, "clap_entry"));
#endif
  Require(entry && entry->init(argv[1]), "entry init");
  auto factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  auto discovery = static_cast<const clap_preset_discovery_factory_t*>(entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
  Require(factory && discovery, "plugin and preset factories");
  Capture capture;
  capture.pluginID = factory->get_plugin_descriptor(factory, 0)->id;
  clap_preset_discovery_indexer_t indexer {};
  indexer.clap_version = CLAP_VERSION;
  indexer.indexer_data = &capture;
  indexer.declare_location = [](const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_location_t* loc) {
    Require(loc->kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && !loc->location, "bundled location");
    ++static_cast<Capture*>(i->indexer_data)->locations;
    return true;
  };
  const auto* descriptor = discovery->get_descriptor(discovery, 0);
  Require(descriptor, "provider descriptor");
  Require(descriptor->id == capture.pluginID, "provider identity matches plugin identity");
  const auto* provider = discovery->create(discovery, &indexer, descriptor->id);
  Require(provider && provider->init(provider), "provider init");
  clap_preset_discovery_metadata_receiver_t receiver {};
  receiver.receiver_data = &capture;
  receiver.begin_preset = [](const clap_preset_discovery_metadata_receiver_t* r, const char* name, const char* key) {
    Require(name && *name && key && *key, "nonempty name/key");
    static_cast<Capture*>(r->receiver_data)->presets.push_back({name, key});
    return true;
  };
  receiver.add_plugin_id = [](const clap_preset_discovery_metadata_receiver_t* r, const clap_universal_plugin_id_t* id) {
    Require(std::strcmp(id->abi, "clap") == 0 && id->id == static_cast<Capture*>(r->receiver_data)->pluginID, "plugin identity");
  };
  Require(provider->get_metadata(provider, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, &receiver), "metadata");
  provider->destroy(provider);
  std::cout << "Discovered " << capture.presets.size() << " initialized presets" << std::endl;
  Require(capture.locations == (capture.presets.empty() ? 0 : 1) && capture.presets.size() == std::stoul(argv[2]), "expected bank size");
  std::set<std::string> keys;
  for (auto& p : capture.presets) Require(keys.insert(p.key).second, "unique load keys");

  const clap_host_params_t hostParams {
    [](const clap_host_t* h, clap_param_rescan_flags f) { if (f & CLAP_PARAM_RESCAN_VALUES) ++static_cast<Capture*>(h->host_data)->rescans; },
    [](const clap_host_t*, clap_id, clap_param_clear_flags) {}, [](const clap_host_t*) {}};
  const clap_host_preset_load_t hostPresets {
    [](const clap_host_t*, uint32_t, const char*, const char*, int32_t, const char*) {},
    [](const clap_host_t* h, uint32_t, const char*, const char*) { ++static_cast<Capture*>(h->host_data)->loaded; }};
  // Both extension callbacks use the capture stored in host_data.
  static const clap_host_params_t* paramsExtension;
  static const clap_host_preset_load_t* presetExtension;
  paramsExtension = &hostParams; presetExtension = &hostPresets;
  const clap_host_t host {CLAP_VERSION, &capture, "Factory preset probe", "Stillwell Audio", "", "1.0.0",
    [](const clap_host_t*, const char* id) -> const void* {
      if (!std::strcmp(id, CLAP_EXT_PARAMS)) return paramsExtension;
      if (!std::strcmp(id, CLAP_EXT_PRESET_LOAD)) return presetExtension;
      return nullptr;
    }, [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {}};
  const auto* plugin = factory->create_plugin(factory, &host, capture.pluginID.c_str());
  Require(plugin && plugin->init(plugin), "plugin init");
  const auto* load = static_cast<const clap_plugin_preset_load_t*>(plugin->get_extension(plugin, CLAP_EXT_PRESET_LOAD));
  const auto* params = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
  Require(load && params, "preset-load and params extensions");
  std::vector<std::vector<double>> snapshots;
  auto snapshot = [&]() {
    std::vector<double> values;
    for (uint32_t i=0; i<params->count(plugin); ++i)
    {
      clap_param_info_t info {}; double value;
      Require(params->get_info(plugin, i, &info) && params->get_value(plugin, info.id, &value), "parameter read");
      values.push_back(value);
    }
    return values;
  };
  for (const auto& preset : capture.presets)
  {
    Require(load->from_location(plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, preset.key.c_str()), "preset recall");
    snapshots.push_back(snapshot());
  }
  // Empty banks and banks containing only repeated defaults are legitimate.
  // Exact recall and notifications are still checked for every initialized slot.
  std::cout << "Distinct parameter sets: "
            << std::set<std::vector<double>>(snapshots.begin(), snapshots.end()).size() << std::endl;
  for (size_t i=capture.presets.size(); i-- > 0;)
  {
    Require(load->from_location(plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, capture.presets[i].key.c_str()), "reverse recall");
    Require(snapshot() == snapshots[i], "repeatable recall");
  }
  Require(capture.rescans == capture.presets.size()*2 && capture.loaded == capture.rescans, "host notifications");
  const auto before = snapshot();
  Require(!load->from_location(plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "99999999999999"), "reject invalid key");
  Require(snapshot() == before, "invalid key preserves state");
  plugin->destroy(plugin);
  entry->deinit();
  std::cout << "PASS: " << capture.presets.size() << " presets, " << capture.loaded
            << " recalls with parameter notifications, exact reverse recall, invalid-key preservation\n";
  return 0;
}
