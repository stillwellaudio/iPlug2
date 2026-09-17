cmake_minimum_required(VERSION 3.20)
project(IPlugVST3RunLoopTests LANGUAGES CXX)
enable_testing()
set(VST3_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../../VST3_SDK" CACHE PATH "VST3 SDK root")
add_executable(iplug-vst3-runloop-test IPlugVST3RunLoopTest.cpp
  ${VST3_SDK}/pluginterfaces/base/funknown.cpp
  ${VST3_SDK}/pluginterfaces/base/coreiids.cpp
  ${VST3_SDK}/public.sdk/source/common/commoniids.cpp)
target_compile_features(iplug-vst3-runloop-test PRIVATE cxx_std_17)
target_compile_definitions(iplug-vst3-runloop-test PRIVATE RELEASE)
target_compile_options(iplug-vst3-runloop-test PRIVATE -UNDEBUG)
target_include_directories(iplug-vst3-runloop-test PRIVATE ../.. ${VST3_SDK})
target_link_libraries(iplug-vst3-runloop-test PRIVATE pthread)
add_test(NAME iplug-vst3-runloop COMMAND iplug-vst3-runloop-test)

if(UNIX AND NOT APPLE)
  find_package(X11 REQUIRED)
  add_executable(iplug-vst3-menu-host-test IPlugVST3MenuHostTest.cpp
    ${VST3_SDK}/pluginterfaces/base/funknown.cpp
    ${VST3_SDK}/pluginterfaces/base/coreiids.cpp
    ${VST3_SDK}/public.sdk/source/common/commoniids.cpp
    ${VST3_SDK}/public.sdk/source/vst/vstinitiids.cpp)
  target_compile_features(iplug-vst3-menu-host-test PRIVATE cxx_std_17)
  target_compile_definitions(iplug-vst3-menu-host-test PRIVATE RELEASE)
  target_compile_options(iplug-vst3-menu-host-test PRIVATE -UNDEBUG)
  target_include_directories(iplug-vst3-menu-host-test PRIVATE ${VST3_SDK})
  target_link_libraries(iplug-vst3-menu-host-test PRIVATE X11::X11 ${CMAKE_DL_LIBS} pthread)
  set(IPLUG_VST3_MENU_TEST_BINARY "" CACHE FILEPATH "Olga VST3 binary for the native menu lifecycle regression (requires X11)")
  if(IPLUG_VST3_MENU_TEST_BINARY)
    add_test(NAME iplug-vst3-menu-host COMMAND iplug-vst3-menu-host-test ${IPLUG_VST3_MENU_TEST_BINARY})
  endif()
endif()
