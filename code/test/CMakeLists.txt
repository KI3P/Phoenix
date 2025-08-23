cmake_minimum_required(VERSION 3.14)
project(Phoenix C CXX)

# GoogleTest requires at least C++14
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/release-1.12.1.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(CMAKE_BUILD_TYPE Debug)

enable_testing()

# Try to get rid of this if you can
add_compile_options(-fpermissive)

include_directories(../src/PhoenixSketch)

add_executable(all_RFboard_tests RFBoard_test.cpp ../src/PhoenixSketch/RFBoard.cpp ../src/PhoenixSketch/DSP.cpp 
  ../src/PhoenixSketch/DSP_FFT.cpp ../src/PhoenixSketch/Loop.cpp ../src/PhoenixSketch/MainBoard_Display.cpp ../src/PhoenixSketch/Mode.cpp ../src/PhoenixSketch/ModeSm.cpp ../src/PhoenixSketch/UISm.cpp ../src/PhoenixSketch/UI.cpp 
  ../src/PhoenixSketch/RFBoard.cpp  ../src/PhoenixSketch/Globals.cpp arm_functions.c ../src/PhoenixSketch/DSP_FIR.cpp ../src/PhoenixSketch/DSP_Noise.cpp ../src/PhoenixSketch/DSP_CWProcessing.cpp 
  ../src/PhoenixSketch/MainBoard_AudioIO.cpp AudioIO_mock.cpp FrontPanel_mock.cpp SignalProcessing_mock.cpp)
target_link_libraries(all_RFboard_tests GTest::gtest_main)

add_executable(all_ModeSm_tests ModeSm_test.cpp ../src/PhoenixSketch/Mode.cpp ../src/PhoenixSketch/ModeSm.cpp
    ../src/PhoenixSketch/MainBoard_AudioIO.cpp AudioIO_mock.cpp ../src/PhoenixSketch/Globals.cpp ../src/PhoenixSketch/DSP_FIR.cpp arm_functions.c
    SignalProcessing_mock.cpp ../src/PhoenixSketch/DSP.cpp ../src/PhoenixSketch/DSP_FFT.cpp ../src/PhoenixSketch/Loop.cpp ../src/PhoenixSketch/MainBoard_Display.cpp ../src/PhoenixSketch/UISm.cpp ../src/PhoenixSketch/UI.cpp 
  ../src/PhoenixSketch/RFBoard.cpp  ../src/PhoenixSketch/Globals.cpp arm_functions.c ../src/PhoenixSketch/DSP_FIR.cpp ../src/PhoenixSketch/DSP_Noise.cpp ../src/PhoenixSketch/DSP_CWProcessing.cpp 
  ../src/PhoenixSketch/MainBoard_AudioIO.cpp AudioIO_mock.cpp FrontPanel_mock.cpp)
target_link_libraries(all_ModeSm_tests GTest::gtest_main)

add_executable(all_UISm_tests UISm_test.cpp ../src/PhoenixSketch/UISm.cpp ../src/PhoenixSketch/Mode.cpp ../src/PhoenixSketch/ModeSm.cpp ../src/PhoenixSketch/UI.cpp ../src/PhoenixSketch/RFBoard.cpp
  ../src/PhoenixSketch/DSP_FFT.cpp ../src/PhoenixSketch/Loop.cpp ../src/PhoenixSketch/MainBoard_Display.cpp ../src/PhoenixSketch/Mode.cpp ../src/PhoenixSketch/ModeSm.cpp ../src/PhoenixSketch/UISm.cpp ../src/PhoenixSketch/UI.cpp 
  ../src/PhoenixSketch/RFBoard.cpp  ../src/PhoenixSketch/Globals.cpp arm_functions.c ../src/PhoenixSketch/DSP_FIR.cpp ../src/PhoenixSketch/DSP_Noise.cpp ../src/PhoenixSketch/DSP_CWProcessing.cpp 
  ../src/PhoenixSketch/MainBoard_AudioIO.cpp AudioIO_mock.cpp SignalProcessing_mock.cpp ../src/PhoenixSketch/DSP.cpp FrontPanel_mock.cpp)
target_link_libraries(all_UISm_tests GTest::gtest_main)

add_executable(all_Loop_tests Loop_test.cpp ../src/PhoenixSketch/Loop.cpp ../src/PhoenixSketch/MainBoard_Display.cpp ../src/PhoenixSketch/Mode.cpp ../src/PhoenixSketch/ModeSm.cpp 
  ../src/PhoenixSketch/UISm.cpp ../src/PhoenixSketch/UI.cpp ../src/PhoenixSketch/RFBoard.cpp ../src/PhoenixSketch/DSP.cpp SignalProcessing_mock.cpp 
  ../src/PhoenixSketch/DSP_FFT.cpp ../src/PhoenixSketch/Globals.cpp arm_functions.c ../src/PhoenixSketch/DSP_FIR.cpp ../src/PhoenixSketch/DSP_Noise.cpp ../src/PhoenixSketch/DSP_CWProcessing.cpp 
  ../src/PhoenixSketch/MainBoard_AudioIO.cpp AudioIO_mock.cpp FrontPanel_mock.cpp)
target_link_libraries(all_Loop_tests GTest::gtest_main)

add_executable(all_SigProc_tests SignalProcessing_test.cpp SignalProcessing_mock.cpp ../src/PhoenixSketch/DSP.cpp 
  ../src/PhoenixSketch/DSP_FFT.cpp ../src/PhoenixSketch/Loop.cpp ../src/PhoenixSketch/MainBoard_Display.cpp ../src/PhoenixSketch/Mode.cpp ../src/PhoenixSketch/ModeSm.cpp ../src/PhoenixSketch/UISm.cpp ../src/PhoenixSketch/UI.cpp 
  ../src/PhoenixSketch/RFBoard.cpp  ../src/PhoenixSketch/Globals.cpp arm_functions.c ../src/PhoenixSketch/DSP_FIR.cpp ../src/PhoenixSketch/DSP_Noise.cpp ../src/PhoenixSketch/DSP_CWProcessing.cpp 
  ../src/PhoenixSketch/MainBoard_AudioIO.cpp AudioIO_mock.cpp FrontPanel_mock.cpp)
target_link_libraries(all_SigProc_tests GTest::gtest_main)

add_executable(all_TransmitChain_tests TransmitChain_test.cpp SignalProcessing_mock.cpp ../src/PhoenixSketch/DSP.cpp 
  ../src/PhoenixSketch/DSP_FFT.cpp ../src/PhoenixSketch/Loop.cpp ../src/PhoenixSketch/MainBoard_Display.cpp ../src/PhoenixSketch/Mode.cpp ../src/PhoenixSketch/ModeSm.cpp ../src/PhoenixSketch/UISm.cpp ../src/PhoenixSketch/UI.cpp 
  ../src/PhoenixSketch/RFBoard.cpp  ../src/PhoenixSketch/Globals.cpp arm_functions.c ../src/PhoenixSketch/DSP_FIR.cpp ../src/PhoenixSketch/DSP_Noise.cpp ../src/PhoenixSketch/DSP_CWProcessing.cpp 
  ../src/PhoenixSketch/MainBoard_AudioIO.cpp AudioIO_mock.cpp FrontPanel_mock.cpp)
target_link_libraries(all_TransmitChain_tests GTest::gtest_main)

add_executable(all_FrontPanel_tests FrontPanel_test.cpp SignalProcessing_mock.cpp ../src/PhoenixSketch/DSP.cpp 
  ../src/PhoenixSketch/DSP_FFT.cpp ../src/PhoenixSketch/Loop.cpp ../src/PhoenixSketch/MainBoard_Display.cpp ../src/PhoenixSketch/Mode.cpp ../src/PhoenixSketch/ModeSm.cpp ../src/PhoenixSketch/UISm.cpp ../src/PhoenixSketch/UI.cpp 
  ../src/PhoenixSketch/RFBoard.cpp  ../src/PhoenixSketch/Globals.cpp arm_functions.c ../src/PhoenixSketch/DSP_FIR.cpp ../src/PhoenixSketch/DSP_Noise.cpp ../src/PhoenixSketch/DSP_CWProcessing.cpp 
  ../src/PhoenixSketch/MainBoard_AudioIO.cpp AudioIO_mock.cpp FrontPanel_mock.cpp)
target_link_libraries(all_FrontPanel_tests GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(all_RFboard_tests)
gtest_discover_tests(all_ModeSm_tests)
gtest_discover_tests(all_UISm_tests)
gtest_discover_tests(all_Loop_tests)
gtest_discover_tests(all_SigProc_tests)
gtest_discover_tests(all_TransmitChain_tests)
gtest_discover_tests(all_FrontPanel_tests)
