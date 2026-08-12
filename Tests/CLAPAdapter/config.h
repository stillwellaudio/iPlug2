#define PLUG_NAME "CLAPAdapterTest"
#define PLUG_MFR "Stillwell Audio"
#define PLUG_VERSION_HEX 0x00010000
#define PLUG_VERSION_STR "1.0.0"
#define PLUG_UNIQUE_ID 'ClAt'
#define PLUG_MFR_ID 'Stwl'
#define PLUG_URL_STR "https://www.stillwellaudio.com"
#define PLUG_EMAIL_STR "support@stillwellaudio.com"
#define PLUG_COPYRIGHT_STR "Copyright 2026 Stillwell Audio LLC"
#define PLUG_CLASS_NAME CLAPAdapterPlugin

#define BUNDLE_NAME "clapadaptertest"
#define BUNDLE_MFR "stillwellaudio"
#define BUNDLE_DOMAIN "com"
#define SHARED_RESOURCES_SUBPATH "CLAPAdapterTest"

#define PLUG_CHANNEL_IO "1-1 0-2"
#define PLUG_LATENCY 64
#define PLUG_TYPE 0
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 0
#define PLUG_HAS_UI 0
#define PLUG_WIDTH 0
#define PLUG_HEIGHT 0
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 0

#define CLAP_MANUAL_URL ""
#define CLAP_SUPPORT_URL PLUG_URL_STR
#define CLAP_DESCRIPTION "iPlug2 CLAP adapter contract test"
#define CLAP_FEATURES CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY
