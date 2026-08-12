cmake_minimum_required(VERSION 3.20)

project(IPlugCLAPAdapterTests LANGUAGES C CXX)

if(APPLE)
  enable_language(OBJCXX)
  set(IPLUG_PATHS_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../IPlug/IPlugPaths.mm")
else()
  set(IPLUG_PATHS_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../IPlug/IPlugPaths.cpp")
endif()

set(IPLUG2_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(CLAP_SDK_ROOT "${IPLUG2_ROOT}/../../../CLAP_SDK" CACHE PATH "Pinned CLAP SDK root")
set(CLAP_HELPERS_ROOT "${IPLUG2_ROOT}/../../../CLAP_HELPERS" CACHE PATH "Pinned clap-helpers root")

add_executable(iplug_clap_adapter_tests
  CLAPAdapterPlugin.cpp
  CLAPAdapterTests.cpp
  ${IPLUG2_ROOT}/IPlug/CLAP/IPlugCLAP.cpp
  ${IPLUG2_ROOT}/IPlug/IPlugAPIBase.cpp
  ${IPLUG2_ROOT}/IPlug/IPlugParameter.cpp
  ${IPLUG2_ROOT}/IPlug/IPlugPluginBase.cpp
  ${IPLUG2_ROOT}/IPlug/IPlugProcessor.cpp
  ${IPLUG2_ROOT}/IPlug/IPlugTimer.cpp
  ${IPLUG_PATHS_SOURCE}
)

target_compile_features(iplug_clap_adapter_tests PRIVATE cxx_std_17)
target_compile_definitions(iplug_clap_adapter_tests PRIVATE
  CLAP_API
  IPLUG_DSP=1
  IPLUG_EDITOR=0
  NO_IGRAPHICS
)
target_include_directories(iplug_clap_adapter_tests PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${IPLUG2_ROOT}/IPlug
  ${IPLUG2_ROOT}/IPlug/CLAP
  ${IPLUG2_ROOT}/IPlug/Extras
  ${IPLUG2_ROOT}/WDL
  ${CLAP_SDK_ROOT}/include
  ${CLAP_HELPERS_ROOT}/include
  ${CLAP_HELPERS_ROOT}/include/clap/helpers
)

if(APPLE)
  target_link_libraries(iplug_clap_adapter_tests PRIVATE
    "-framework CoreFoundation"
    "-framework Foundation"
  )
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(iplug_clap_adapter_tests PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()

enable_testing()
add_test(NAME iplug_clap_adapter_tests COMMAND iplug_clap_adapter_tests)
