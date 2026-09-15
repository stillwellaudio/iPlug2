/* Factory preset discovery for the CLAP entry point. */
#pragma once

#include "IPlugCLAP.h"
#include <clap/factory/preset-discovery.h>
#include <memory>
#include <string>
#include <vector>

BEGIN_IPLUG_NAMESPACE

// Each provider owns a snapshot of the factory names. Constructing the plug-in
// supplies its existing bank without duplicating product metadata. No editor,
// audio activation, host initialization, or idle timer is needed for discovery.
class IPlugCLAPPresetProvider
{
public:
  using MakePlugin = IPlugCLAP* (*)(const InstanceInfo&);

  IPlugCLAPPresetProvider(const clap_preset_discovery_provider_descriptor_t* descriptor,
                         const clap_preset_discovery_indexer_t* indexer,
                         const clap_plugin_descriptor_t* pluginDescriptor, MakePlugin makePlugin)
    : mIndexer(indexer), mPluginDescriptor(pluginDescriptor), mMakePlugin(makePlugin)
  {
    mProvider = {descriptor, this, Init, Destroy, GetMetadata, GetExtension};
  }

  const clap_preset_discovery_provider_t* Get() const { return &mProvider; }

private:
  struct Preset { std::string name, key; };
  clap_preset_discovery_provider_t mProvider {};
  const clap_preset_discovery_indexer_t* mIndexer;
  const clap_plugin_descriptor_t* mPluginDescriptor;
  MakePlugin mMakePlugin;
  std::vector<Preset> mPresets;
  bool mInitialized = false;

  static IPlugCLAPPresetProvider& Self(const clap_preset_discovery_provider_t* p)
  {
    return *static_cast<IPlugCLAPPresetProvider*>(p->provider_data);
  }

  static bool CLAP_ABI Init(const clap_preset_discovery_provider_t* provider)
  {
    auto& self = Self(provider);
    if (self.mInitialized)
      return true;
    try
    {
      // Deliberately do not forward any callbacks to the discovery indexer as a host.
      const clap_host_t host {CLAP_VERSION, nullptr, "Preset discovery", "", "", "1.0.0",
        [](const clap_host_t*, const char*) -> const void* { return nullptr; },
        [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {}};
      std::unique_ptr<IPlugCLAP> plugin(self.mMakePlugin({self.mPluginDescriptor, &host, true}));
      if (!plugin)
        return false;
      self.mPresets.clear();
      for (int i = 0; i < plugin->NPresets(); ++i)
      {
        if (plugin->GetPreset(i)->mInitialized)
          self.mPresets.push_back({plugin->GetPresetName(i), std::to_string(i)});
      }
      plugin.reset();
      const clap_preset_discovery_location_t location {CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT,
        "Factory Presets", CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr};
      if (!self.mPresets.empty() && !self.mIndexer->declare_location(self.mIndexer, &location))
        return false;
      self.mInitialized = true;
      return true;
    }
    catch (...)
    {
      return false;
    }
  }

  static void CLAP_ABI Destroy(const clap_preset_discovery_provider_t* provider)
  {
    delete &Self(provider);
  }

  static bool CLAP_ABI GetMetadata(const clap_preset_discovery_provider_t* provider,
                                  uint32_t kind, const char* location,
                                  const clap_preset_discovery_metadata_receiver_t* receiver)
  {
    auto& self = Self(provider);
    if (!self.mInitialized || kind != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || location ||
        !receiver || !receiver->begin_preset || !receiver->add_plugin_id)
      return false;
    const clap_universal_plugin_id_t id {"clap", self.mPluginDescriptor->id};
    for (const auto& preset : self.mPresets)
    {
      if (!receiver->begin_preset(receiver, preset.name.c_str(), preset.key.c_str()))
        return false;
      receiver->add_plugin_id(receiver, &id);
      if (receiver->set_flags)
        receiver->set_flags(receiver, CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
    }
    return true;
  }

  static const void* CLAP_ABI GetExtension(const clap_preset_discovery_provider_t*, const char*)
  {
    return nullptr;
  }
};

END_IPLUG_NAMESPACE
