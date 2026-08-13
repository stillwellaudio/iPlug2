#include <clap/clap.h>

#include <X11/Xlib.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <thread>

#ifndef IPLUG_LINUX_TEXT_ENTRY_REFERENCE_WIDTH
#define IPLUG_LINUX_TEXT_ENTRY_REFERENCE_WIDTH 600
#endif

namespace {

void HostRequestRestart(const clap_host_t*) {}
void HostRequestProcess(const clap_host_t*) {}
void HostRequestCallback(const clap_host_t*) {}
void HostResizeHintsChanged(const clap_host_t*) {}
bool HostRequestResize(const clap_host_t*, uint32_t, uint32_t) { return true; }
bool HostRequestShow(const clap_host_t*) { return true; }
bool HostRequestHide(const clap_host_t*) { return true; }
void HostClosed(const clap_host_t*, bool) {}

const clap_host_gui_t kHostGui {
  HostResizeHintsChanged,
  HostRequestResize,
  HostRequestShow,
  HostRequestHide,
  HostClosed,
};

const void* HostGetExtension(const clap_host_t*, const char* extensionId)
{
  if (extensionId && std::strcmp(extensionId, CLAP_EXT_GUI) == 0)
    return &kHostGui;
  return nullptr;
}

clap_host_t MakeHost()
{
  return {
    CLAP_VERSION,
    nullptr,
    "IPlug Linux text-entry host test",
    "iPlug2",
    "https://iplug2.github.io",
    "1.0.0",
    HostGetExtension,
    HostRequestRestart,
    HostRequestProcess,
    HostRequestCallback,
  };
}

Window WaitForChildWindow(Display* display, Window parent)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline)
  {
    Window root = 0;
    Window actualParent = 0;
    Window* children = nullptr;
    unsigned int childCount = 0;
    if (XQueryTree(display, parent, &root, &actualParent, &children, &childCount))
    {
      const Window child = childCount ? children[childCount - 1] : 0;
      if (children)
        XFree(children);
      if (child)
        return child;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return 0;
}

bool SendModifiedClick(Display* display, Window window, int x, int y)
{
  XEvent event {};
  event.xbutton.type = ButtonPress;
  event.xbutton.display = display;
  event.xbutton.window = window;
  event.xbutton.root = DefaultRootWindow(display);
  event.xbutton.time = CurrentTime;
  event.xbutton.x = x;
  event.xbutton.y = y;
  event.xbutton.x_root = x;
  event.xbutton.y_root = y;
  // Event Horizon currently prompts from its Shift branch. Including ControlMask
  // also exercises the user-reported Ctrl-modified interaction.
  event.xbutton.state = ShiftMask | ControlMask;
  event.xbutton.button = Button1;
  event.xbutton.same_screen = True;

  const Status sent = XSendEvent(display, window, False, ButtonPressMask, &event);
  XFlush(display);
  return sent != 0;
}

} // namespace

int main(int argc, char** argv)
{
  if (argc < 2 || argc > 4)
  {
    std::cerr << "usage: " << argv[0] << " <plugin.clap> [click-x click-y]\n";
    return 2;
  }

  const char* pluginPath = argv[1];
  const int clickX = argc == 4 ? std::atoi(argv[2]) : 120;
  const int clickY = argc == 4 ? std::atoi(argv[3]) : 120;
  void* library = dlopen(pluginPath, RTLD_NOW | RTLD_LOCAL);
  if (!library)
  {
    std::cerr << "dlopen failed: " << dlerror() << '\n';
    return 1;
  }

  const auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(library, "clap_entry"));
  if (!entry || !entry->init(pluginPath))
  {
    std::cerr << "CLAP entry initialization failed\n";
    dlclose(library);
    return 1;
  }

  const auto* factory = static_cast<const clap_plugin_factory_t*>(
    entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  const clap_plugin_descriptor_t* descriptor =
    factory && factory->get_plugin_count(factory) ? factory->get_plugin_descriptor(factory, 0) : nullptr;
  clap_host_t host = MakeHost();
  const clap_plugin_t* plugin = descriptor ? factory->create_plugin(factory, &host, descriptor->id) : nullptr;
  if (!plugin || !plugin->init(plugin))
  {
    std::cerr << "CLAP plugin initialization failed\n";
    if (plugin)
      plugin->destroy(plugin);
    entry->deinit();
    dlclose(library);
    return 1;
  }

  Display* display = XOpenDisplay(nullptr);
  const auto* gui = static_cast<const clap_plugin_gui_t*>(plugin->get_extension(plugin, CLAP_EXT_GUI));
  if (!display || !gui || !gui->is_api_supported(plugin, CLAP_WINDOW_API_X11, false)
      || !gui->create(plugin, CLAP_WINDOW_API_X11, false))
  {
    std::cerr << "X11 CLAP GUI initialization failed\n";
    if (display)
      XCloseDisplay(display);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(library);
    return 1;
  }

  const int screen = DefaultScreen(display);
  const Window parent = XCreateSimpleWindow(
    display, RootWindow(display, screen), 0, 0, 420, 360, 0,
    BlackPixel(display, screen), WhitePixel(display, screen));
  XMapWindow(display, parent);
  XFlush(display);

  clap_window_t clapParent {};
  clapParent.api = CLAP_WINDOW_API_X11;
  clapParent.x11 = parent;
  if (!gui->set_parent(plugin, &clapParent))
  {
    std::cerr << "CLAP GUI parent attachment failed\n";
    gui->destroy(plugin);
    XDestroyWindow(display, parent);
    XCloseDisplay(display);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(library);
    return 1;
  }

  const Window child = WaitForChildWindow(display, parent);
  XWindowAttributes attributes {};
  if (!child || !XGetWindowAttributes(display, child, &attributes))
  {
    std::cerr << "failed to inspect the plugin child window\n";
    gui->destroy(plugin);
    XDestroyWindow(display, parent);
    XCloseDisplay(display);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(library);
    return 1;
  }

  constexpr float kReferenceWidth = static_cast<float>(IPLUG_LINUX_TEXT_ENTRY_REFERENCE_WIDTH);
  const float scale = static_cast<float>(attributes.width) / kReferenceWidth;
  const int deviceClickX = static_cast<int>(clickX * scale);
  const int deviceClickY = static_cast<int>(clickY * scale);
  std::cout << "plugin child " << attributes.width << 'x' << attributes.height
            << ", click " << deviceClickX << ',' << deviceClickY << '\n';
  if (!SendModifiedClick(display, child, deviceClickX, deviceClickY))
  {
    std::cerr << "failed to deliver the text-entry click\n";
    gui->destroy(plugin);
    XDestroyWindow(display, parent);
    XCloseDisplay(display);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(library);
    return 1;
  }

  // The original failure is synchronous when the Linux display timer consumes
  // the click. Surviving this interval proves that text-entry creation did not
  // crash the hosting process.
  std::this_thread::sleep_for(std::chrono::milliseconds(750));
  std::cout << "text-entry click survived\n";

  gui->destroy(plugin);
  XDestroyWindow(display, parent);
  XCloseDisplay(display);
  plugin->destroy(plugin);
  entry->deinit();
  dlclose(library);
  return 0;
}
