cmake_minimum_required(VERSION 3.20)

project(IPlugLinuxPathsTests LANGUAGES CXX)

enable_testing()

add_executable(iplug-linux-paths-test IPlugLinuxPathsTest.cpp)
target_compile_features(iplug-linux-paths-test PRIVATE cxx_std_17)
target_include_directories(iplug-linux-paths-test PRIVATE ../..)

add_test(NAME iplug-linux-paths COMMAND iplug-linux-paths-test)

add_executable(iplug-linux-timer-test IPlugLinuxTimerTest.cpp)
target_compile_features(iplug-linux-timer-test PRIVATE cxx_std_17)
target_include_directories(iplug-linux-timer-test PRIVATE ../..)

add_test(NAME iplug-linux-timer COMMAND iplug-linux-timer-test)

add_executable(iplug-linux-input-test IPlugLinuxInputTest.cpp)
target_compile_features(iplug-linux-input-test PRIVATE cxx_std_17)
target_include_directories(iplug-linux-input-test PRIVATE ../..)

add_test(NAME iplug-linux-input COMMAND iplug-linux-input-test)

if(UNIX AND NOT APPLE)
  find_package(X11 REQUIRED)

  set(IPLUG_LINUX_TEXT_ENTRY_REFERENCE_WIDTH 600 CACHE STRING
      "Logical width used to scale the text-entry test click")
  set(IPLUG_LINUX_TEXT_ENTRY_CLICK_X 120 CACHE STRING
      "Logical X coordinate used by the text-entry test")
  set(IPLUG_LINUX_TEXT_ENTRY_CLICK_Y 120 CACHE STRING
      "Logical Y coordinate used by the text-entry test")
  add_executable(iplug-linux-text-entry-host-test IPlugLinuxTextEntryHostTest.cpp)
  target_compile_features(iplug-linux-text-entry-host-test PRIVATE cxx_std_17)
  target_compile_definitions(iplug-linux-text-entry-host-test PRIVATE
    IPLUG_LINUX_TEXT_ENTRY_REFERENCE_WIDTH=${IPLUG_LINUX_TEXT_ENTRY_REFERENCE_WIDTH}
  )
  target_include_directories(iplug-linux-text-entry-host-test PRIVATE
    ../../../CLAP_SDK/include
  )
  target_link_libraries(iplug-linux-text-entry-host-test PRIVATE
    X11::X11
    ${CMAKE_DL_LIBS}
  )

  set(IPLUG_LINUX_CLAP_TEST_PLUGIN "" CACHE FILEPATH
      "Optional CLAP binary used by the Linux text-entry host regression")
  if(IPLUG_LINUX_CLAP_TEST_PLUGIN)
    add_test(NAME iplug-linux-text-entry-host
      COMMAND iplug-linux-text-entry-host-test
              ${IPLUG_LINUX_CLAP_TEST_PLUGIN}
              ${IPLUG_LINUX_TEXT_ENTRY_CLICK_X}
              ${IPLUG_LINUX_TEXT_ENTRY_CLICK_Y})
  endif()
endif()
