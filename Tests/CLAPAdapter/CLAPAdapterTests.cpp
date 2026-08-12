#include <clap/clap.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

extern const clap_plugin_entry_t clap_entry;
extern "C" void TriggerCLAPAdapterParamChange();
#if defined OS_LINUX
extern "C" bool CLAPAdapterGuiApiSupported(const char* api, bool isFloating);
extern "C" bool CLAPAdapterSetX11Parent(uint64_t parent);
extern "C" uintptr_t CLAPAdapterLastParent();
#endif

namespace
{
int gFailures = 0;
int gFlushRequests = 0;
int gCallbackRequests = 0;
int gRescanRequests = 0;

#define CHECK(condition) \
  do \
  { \
    if (!(condition)) \
    { \
      std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #condition "\n"; \
      ++gFailures; \
    } \
  } while (false)

const void* HostGetExtension(const clap_host_t*, const char* extensionId);
void HostRequestRestart(const clap_host_t*) {}
void HostRequestProcess(const clap_host_t*) {}
void HostRequestCallback(const clap_host_t*) { ++gCallbackRequests; }
void HostParamsRescan(const clap_host_t*, clap_param_rescan_flags flags)
{
  if (flags & CLAP_PARAM_RESCAN_VALUES)
    ++gRescanRequests;
}
void HostParamsClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void HostParamsRequestFlush(const clap_host_t*) { ++gFlushRequests; }

const clap_host_params_t kHostParams {
  HostParamsRescan,
  HostParamsClear,
  HostParamsRequestFlush,
};

const void* HostGetExtension(const clap_host_t*, const char* extensionId)
{
  return extensionId && std::strcmp(extensionId, CLAP_EXT_PARAMS) == 0 ? &kHostParams : nullptr;
}

clap_host_t MakeHost()
{
  return {
    CLAP_VERSION,
    nullptr,
    "IPlug CLAP Adapter Tests",
    "Stillwell Audio",
    "https://www.stillwellaudio.com",
    "1.0.0",
    HostGetExtension,
    HostRequestRestart,
    HostRequestProcess,
    HostRequestCallback,
  };
}

struct MemoryOutput
{
  clap_ostream_t stream;
  std::vector<uint8_t> bytes;
  int64_t maxWrite = INT64_MAX;

  static int64_t Write(const clap_ostream_t* stream, const void* buffer, uint64_t size)
  {
    auto& self = *static_cast<MemoryOutput*>(stream->ctx);
    const auto count = static_cast<size_t>(std::min<uint64_t>(size, static_cast<uint64_t>(self.maxWrite)));
    const auto* first = static_cast<const uint8_t*>(buffer);
    self.bytes.insert(self.bytes.end(), first, first + count);
    return static_cast<int64_t>(count);
  }

  explicit MemoryOutput(int64_t writeLimit = INT64_MAX)
    : stream {this, Write}
    , maxWrite(writeLimit)
  {}
};

struct MemoryInput
{
  clap_istream_t stream;
  const std::vector<uint8_t>& bytes;
  size_t offset = 0;
  size_t maxRead = SIZE_MAX;

  static int64_t Read(const clap_istream_t* stream, void* buffer, uint64_t size)
  {
    auto& self = *static_cast<MemoryInput*>(stream->ctx);
    const size_t remaining = self.bytes.size() - self.offset;
    const size_t count = std::min({remaining, static_cast<size_t>(size), self.maxRead});
    std::memcpy(buffer, self.bytes.data() + self.offset, count);
    self.offset += count;
    return static_cast<int64_t>(count);
  }

  MemoryInput(const std::vector<uint8_t>& source, size_t readLimit)
    : stream {this, Read}
    , bytes(source)
    , maxRead(readLimit)
  {}
};

struct EventList
{
  clap_input_events_t input;
  std::vector<const clap_event_header_t*> events;

  static uint32_t Size(const clap_input_events_t* list)
  {
    return static_cast<uint32_t>(static_cast<const EventList*>(list->ctx)->events.size());
  }

  static const clap_event_header_t* Get(const clap_input_events_t* list, uint32_t index)
  {
    const auto& self = *static_cast<const EventList*>(list->ctx);
    return index < self.events.size() ? self.events[index] : nullptr;
  }

  EventList()
    : input {this, Size, Get}
  {}
};

struct OutputEvents
{
  clap_output_events_t output;
  std::vector<uint16_t> eventTypes;

  static bool Push(const clap_output_events_t* list, const clap_event_header_t* event)
  {
    auto& self = *static_cast<OutputEvents*>(list->ctx);
    if (!event)
      return false;
    self.eventTypes.push_back(event->type);
    return true;
  }

  OutputEvents()
    : output {this, Push}
  {}
};

void TestEntryAndFactoryLifetime()
{
  CHECK(clap_entry.init("/tmp/clapadaptertest.clap"));
  CHECK(clap_entry.init("/tmp/clapadaptertest.clap"));
  CHECK(!clap_entry.init("/tmp/different.clap"));

  const auto* factory = static_cast<const clap_plugin_factory_t*>(clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
  CHECK(factory != nullptr);
  CHECK(factory && factory->get_plugin_count(factory) == 1);

  const clap_plugin_descriptor_t* descriptor = factory ? factory->get_plugin_descriptor(factory, 0) : nullptr;
  CHECK(descriptor != nullptr);
  CHECK(descriptor && std::strcmp(descriptor->id, "com.stillwellaudio.clapadaptertest") == 0);
  CHECK(factory && factory->get_plugin_descriptor(factory, 1) == nullptr);

  clap_entry.deinit();
  CHECK(clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID) != nullptr);
  clap_entry.deinit();
  CHECK(clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID) == nullptr);
  CHECK(!clap_entry.init(nullptr));
  CHECK(!clap_entry.init(""));
}

void TestAudioPortsStateAndFactoryValidation()
{
  CHECK(clap_entry.init("/tmp/clapadaptertest.clap"));
  const auto* factory = static_cast<const clap_plugin_factory_t*>(clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
  const auto* descriptor = factory->get_plugin_descriptor(factory, 0);
  clap_host_t host = MakeHost();

  clap_host_t incompatibleHost = host;
  incompatibleHost.clap_version = {0, 99, 0};

  CHECK(factory->create_plugin(factory, &host, nullptr) == nullptr);
  CHECK(factory->create_plugin(factory, &host, "com.stillwellaudio.wrong") == nullptr);
  CHECK(factory->create_plugin(factory, &incompatibleHost, descriptor->id) == nullptr);

  const clap_plugin_t* plugin = factory->create_plugin(factory, &host, descriptor->id);
  CHECK(plugin != nullptr);
  CHECK(plugin && plugin->init(plugin));

#if defined OS_LINUX
  CHECK(CLAPAdapterGuiApiSupported(CLAP_WINDOW_API_X11, false));
  CHECK(!CLAPAdapterGuiApiSupported(CLAP_WINDOW_API_X11, true));
  CHECK(!CLAPAdapterGuiApiSupported("wayland", false));
  CHECK(!CLAPAdapterGuiApiSupported(nullptr, false));

  constexpr uint64_t kParent = UINT64_C(0x12345678abcdef01);
  CHECK(CLAPAdapterSetX11Parent(kParent));
  CHECK(CLAPAdapterLastParent() == static_cast<uintptr_t>(kParent));
#endif

  const auto* params = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
  CHECK(params != nullptr);
  clap_param_info_t paramInfo {};
  CHECK(params && params->get_info(plugin, 0, &paramInfo));
  CHECK(params && params->get_info(plugin, 1, &paramInfo));
  CHECK(params && !params->get_info(plugin, 2, &paramInfo));
  double paramValue = 0.0;
  CHECK(params && !params->get_value(plugin, 2, &paramValue));

  char initialModeText[CLAP_NAME_SIZE] {};
  char roundTripModeText[CLAP_NAME_SIZE] {};
  double parsedModeValue = 0.0;
  CHECK(params && params->value_to_text(plugin, 1, 0.020202020202020204,
                                        initialModeText, sizeof(initialModeText)));
  CHECK(params && params->text_to_value(plugin, 1, initialModeText, &parsedModeValue));
  CHECK(params && params->value_to_text(plugin, 1, parsedModeValue,
                                        roundTripModeText, sizeof(roundTripModeText)));
  CHECK(std::strcmp(initialModeText, roundTripModeText) == 0);

  TriggerCLAPAdapterParamChange();
  CHECK(gCallbackRequests > 0);
  plugin->on_main_thread(plugin);
  CHECK(gFlushRequests > 0);

  clap_event_param_value_t validParam {{
    sizeof(clap_event_param_value_t), 0, CLAP_CORE_EVENT_SPACE_ID,
    CLAP_EVENT_PARAM_VALUE, 0}, 0, nullptr, -1, -1, -1, -1, 0.25};
  clap_event_param_value_t invalidParam = validParam;
  invalidParam.header.time = 4;
  invalidParam.param_id = CLAP_INVALID_ID;
  EventList inputEvents;
  inputEvents.events = {&invalidParam.header, &validParam.header};
  OutputEvents outputEvents;
  params->flush(plugin, &inputEvents.input, &outputEvents.output);
  CHECK(params->get_value(plugin, 0, &paramValue));
  CHECK(paramValue == 0.25);

  const auto* configs = static_cast<const clap_plugin_audio_ports_config_t*>(
    plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS_CONFIG));
  CHECK(configs != nullptr);
  CHECK(configs && configs->count(plugin) == 2);

  clap_audio_ports_config_t effect {};
  CHECK(configs && configs->get(plugin, 0, &effect));
  CHECK(effect.input_port_count == 1);
  CHECK(effect.output_port_count == 1);
  CHECK(effect.has_main_input);
  CHECK(effect.has_main_output);
  CHECK(effect.main_input_channel_count == 1);
  CHECK(effect.main_output_channel_count == 1);

  clap_audio_ports_config_t instrument {};
  CHECK(configs && configs->get(plugin, 1, &instrument));
  CHECK(instrument.input_port_count == 0);
  CHECK(instrument.output_port_count == 1);
  CHECK(!instrument.has_main_input);
  CHECK(instrument.has_main_output);
  CHECK(instrument.main_input_channel_count == 0);
  CHECK(instrument.main_output_channel_count == 2);
  CHECK(configs && !configs->get(plugin, 2, &instrument));
  CHECK(configs && !configs->select(plugin, 2));

  const auto* latency = static_cast<const clap_plugin_latency_t*>(plugin->get_extension(plugin, CLAP_EXT_LATENCY));
  const auto* tail = static_cast<const clap_plugin_tail_t*>(plugin->get_extension(plugin, CLAP_EXT_TAIL));
  CHECK(latency && latency->get(plugin) == 64);
  CHECK(tail && tail->get(plugin) == 0);

  CHECK(plugin->activate(plugin, 48000.0, 0, 64));
  CHECK(plugin->start_processing(plugin));
  clap_process_t zeroFrames {};
  zeroFrames.frames_count = 0;
  zeroFrames.in_events = &inputEvents.input;
  zeroFrames.out_events = &outputEvents.output;
  CHECK(plugin->process(plugin, &zeroFrames) != CLAP_PROCESS_ERROR);
  plugin->stop_processing(plugin);
  plugin->deactivate(plugin);

  const auto* state = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
  MemoryOutput saved;
  CHECK(state && state->save(plugin, &saved.stream));
  CHECK(!saved.bytes.empty());

  MemoryInput shortReads(saved.bytes, 3);
  CHECK(state && state->load(plugin, &shortReads.stream));
  CHECK(gRescanRequests > 0);

  auto stateWithTrailingBytes = saved.bytes;
  stateWithTrailingBytes.push_back(0xff);
  MemoryInput trailingBytes(stateWithTrailingBytes, SIZE_MAX);
  CHECK(state && !state->load(plugin, &trailingBytes.stream));

  MemoryOutput shortWrite(1);
  CHECK(state && state->save(plugin, &shortWrite.stream));
  CHECK(shortWrite.bytes == saved.bytes);

  plugin->destroy(plugin);
  clap_entry.deinit();
}
}

int main()
{
  TestEntryAndFactoryLifetime();
  TestAudioPortsStateAndFactoryValidation();
  return gFailures == 0 ? 0 : 1;
}
