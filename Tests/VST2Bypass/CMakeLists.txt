cmake_minimum_required(VERSION 3.20)

project(IPlugVST2BypassTests LANGUAGES C CXX)

if(WIN32)
  message(FATAL_ERROR "The VST2 bypass test loads the fixture with dlopen and runs on macOS and Linux only.")
endif()

if(APPLE)
  enable_language(OBJCXX)
  set(IPLUG_PATHS_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../IPlug/IPlugPaths.mm")
else()
  set(IPLUG_PATHS_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../IPlug/IPlugPaths.cpp")
endif()

set(IPLUG2_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(VST2_SDK_ROOT "${IPLUG2_ROOT}/../VST2_SDK" CACHE PATH "VST2 SDK root")
set(VST2_SDK_INCLUDE "${VST2_SDK_ROOT}/pluginterfaces/vst2.x")

# The fixture is a loadable module, as a host sees it: the VST2 entry point
# also exports main(), so it cannot link into the test executable.
add_library(iplug_vst2_bypass_fixture MODULE
  VST2BypassPlugin.cpp
  ${IPLUG2_ROOT}/IPlug/VST2/IPlugVST2.cpp
  ${IPLUG2_ROOT}/IPlug/IPlugAPIBase.cpp
  ${IPLUG2_ROOT}/IPlug/IPlugParameter.cpp
  ${IPLUG2_ROOT}/IPlug/IPlugPluginBase.cpp
  ${IPLUG2_ROOT}/IPlug/IPlugProcessor.cpp
  ${IPLUG2_ROOT}/IPlug/IPlugTimer.cpp
  ${IPLUG_PATHS_SOURCE}
)
target_compile_features(iplug_vst2_bypass_fixture PRIVATE cxx_std_17)
target_compile_definitions(iplug_vst2_bypass_fixture PRIVATE
  VST2_API
  IPLUG_DSP=1
  IPLUG_EDITOR=0
  NO_IGRAPHICS
)
target_include_directories(iplug_vst2_bypass_fixture PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${IPLUG2_ROOT}/IPlug
  ${IPLUG2_ROOT}/IPlug/VST2
  ${IPLUG2_ROOT}/IPlug/Extras
  ${IPLUG2_ROOT}/WDL
  ${VST2_SDK_INCLUDE}
)
if(APPLE)
  target_link_libraries(iplug_vst2_bypass_fixture PRIVATE
    "-framework CoreFoundation"
    "-framework Foundation"
  )
else()
  find_package(Threads REQUIRED)
  target_link_libraries(iplug_vst2_bypass_fixture PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()

add_executable(iplug_vst2_bypass_tests VST2BypassTests.cpp)
target_compile_features(iplug_vst2_bypass_tests PRIVATE cxx_std_17)
target_include_directories(iplug_vst2_bypass_tests PRIVATE ${VST2_SDK_INCLUDE})
target_compile_definitions(iplug_vst2_bypass_tests PRIVATE
  VST2_BYPASS_FIXTURE_PATH="$<TARGET_FILE:iplug_vst2_bypass_fixture>"
)
target_link_libraries(iplug_vst2_bypass_tests PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(iplug_vst2_bypass_tests iplug_vst2_bypass_fixture)

enable_testing()
add_test(NAME iplug_vst2_bypass_tests COMMAND iplug_vst2_bypass_tests)
