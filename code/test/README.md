# Phoenix SDR Unit Tests

This directory contains the comprehensive unit test suite for the Phoenix SDR radio project. The tests use Google Test (gtest) framework with extensive mocking of Arduino and hardware interfaces to enable testing without physical hardware.

## Overview

The test framework enables:
- Testing embedded C/C++ code in a desktop environment
- Comprehensive unit testing of all major modules
- State machine verification
- DSP algorithm validation
- Hardware interface simulation through mocking

## Test Organization

### Test Suites

The test suite is organized into separate executables for different modules:

| Test Executable | Test File | Modules Tested |
|----------------|-----------|----------------|
| `all_RFboard_tests` | `RFBoard_test.cpp` | RF hardware control, VFO management |
| `all_ModeSm_tests` | `ModeSm_test.cpp` | Mode state machine (SSB/CW modes) |
| `all_UISm_tests` | `UISm_test.cpp` | User interface state machine |
| `all_Loop_tests` | `Loop_test.cpp` | Main program loop |
| `all_SigProc_tests` | `SignalProcessing_test.cpp` | Digital signal processing |
| `all_TransmitChain_tests` | `TransmitChain_test.cpp` | Transmit signal path |
| `all_FrontPanel_tests` | `FrontPanel_test.cpp` | Front panel control interface |
| `all_CAT_tests` | `CAT_test.cpp` | Computer Aided Transceiver protocol |
| `all_LPFBoard_tests` | `LPFBoard_test.cpp` | Low-pass filter board control |
| `all_BPFBoard_tests` | `BPFBoard_test.cpp` | Band-pass filter board control |
| `all_RFhardwareSM_tests` | `RFHardwareSM_test.cpp` | RF hardware state machine |
| `all_Radio_tests` | `Radio_test.cpp` | Overall radio functionality |
| `all_Display_tests` | `Display_test.cpp` | Display rendering and updates |
| `all_Micros_tests` | `micros_test.cpp` | Microsecond timer functionality |

### Mock Objects

The test framework provides mock implementations of Arduino and hardware libraries:

| Mock File | Purpose |
|-----------|---------|
| `Arduino_mock.cpp` | Arduino core functions (digitalWrite, pinMode, millis, etc.) |
| `si5351_mock.cpp` | Si5351 VFO clock generator |
| `OpenAudio_ArduinoLibrary_mock.cpp` | Audio processing library |
| `Adafruit_I2CDevice_mock.cpp` | I2C communication |
| `RA8875_mock.cpp` | Display controller |
| `LittleFS_mock.cpp` | Flash filesystem |
| `FrontPanel_mock.cpp` | Front panel hardware |

### Supporting Files

- `Arduino.h` - Arduino framework header for testing
- `arm_math.h`, `arm_functions.c` - ARM CMSIS-DSP math functions
- `Audio.h`, `SPI.h`, `Wire.h` - Library interface headers
- `*_spy.cpp` - Spy objects for state machine testing
- `*.ipynb` - Jupyter notebooks for signal analysis and test data generation
- `mock_*_data_*.c` - Pre-generated test waveform data

## Building the Tests

### Prerequisites

- CMake 3.14 or higher
- C++17 compliant compiler (GCC or Clang)
- Internet connection (first build only, to download Google Test)

### Build Steps

1. Create a build directory:
```bash
mkdir -p code/test/build
cd code/test/build
```

2. Run CMake to configure the build:
```bash
cmake ../
```

3. Build all test executables:
```bash
make
```

This will:
- Download Google Test framework (first run only)
- Compile all test executables
- Link test executables with production code and mocks

### Build Output

After building, you'll have multiple test executables in the `build/` directory:
- `all_RFboard_tests`
- `all_ModeSm_tests`
- `all_UISm_tests`
- And so on...

## Running Tests

### Run All Tests

From the `code/test/build` directory:

```bash
ctest --output-on-failure
```

This runs all test suites and displays output only for failing tests.

### Run with Verbose Output

To see detailed output from all tests:

```bash
ctest -V
```

### Run Specific Test Suite

To run a specific test suite:

```bash
ctest -R RFBoard --output-on-failure
```

Or run the executable directly:

```bash
./all_RFboard_tests
```

### Run Individual Test Case

To run a specific test case within a suite:

```bash
./all_RFboard_tests --gtest_filter=RFBoard.RXAttenuatorCreate_InitializesI2C
```

### List Available Tests

To see all tests in a suite:

```bash
./all_RFboard_tests --gtest_list_tests
```

## Writing New Tests

### Test File Structure

Each test file follows this pattern:

```cpp
#include "gtest/gtest.h"
#include "../src/PhoenixSketch/ModuleToTest.h"

// Test fixture (optional, for shared setup)
class ModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code
    }

    void TearDown() override {
        // Cleanup code
    }
};

// Simple test case
TEST(ModuleName, TestDescription) {
    // Arrange
    int expected = 42;

    // Act
    int actual = FunctionUnderTest();

    // Assert
    EXPECT_EQ(actual, expected);
}

// Test with fixture
TEST_F(ModuleTest, AnotherTest) {
    ASSERT_TRUE(SomeCondition());
}
```

### Adding a New Test Suite

1. Create a new test file: `MyModule_test.cpp`

2. Add the executable to `CMakeLists.txt`:
```cmake
add_executable(all_MyModule_tests MyModule_test.cpp
    ../src/PhoenixSketch/MyModule.cpp
    # Add other dependencies...
    Arduino_mock.cpp)
target_link_libraries(all_MyModule_tests GTest::gtest_main)
```

3. Register with CTest:
```cmake
gtest_discover_tests(all_MyModule_tests)
```

4. Rebuild and run:
```bash
cd code/test/build
cmake ../ && make && ctest -R MyModule --output-on-failure
```

## Test Patterns and Conventions

### Naming Conventions

- Test files: `ModuleName_test.cpp`
- Test suites: `TEST(ModuleName, TestDescription)`
- Test executables: `all_ModuleName_tests`
- Mock files: `Library_mock.cpp`

### Common Assertions

- `EXPECT_EQ(actual, expected)` - Check equality (continues on failure)
- `ASSERT_EQ(actual, expected)` - Check equality (stops on failure)
- `EXPECT_TRUE(condition)` - Check boolean condition
- `EXPECT_NEAR(val1, val2, tolerance)` - Compare floating point values
- `EXPECT_STREQ(str1, str2)` - Compare C strings

### Testing State Machines

State machine tests verify state transitions and outputs:

```cpp
TEST(ModeSm, TransitionToTransmit) {
    // Setup initial state
    ModeSm_instance.state = ModeSm_state_SSB_RECEIVE;

    // Trigger event
    ModeSm_dispatch(&ModeSm_instance, ModeSm_event_PTT_PRESSED);

    // Verify transition
    EXPECT_EQ(ModeSm_instance.state, ModeSm_state_SSB_TRANSMIT);
}
```

### Using Mocks

Mock objects simulate hardware behavior:

```cpp
TEST(RFBoard, SetFrequency) {
    // Mock will record VFO commands
    SetFrequency_dHz(145000000);  // 14.500 MHz

    // Verify mock received correct commands
    EXPECT_EQ(GetLastVFOFrequency(), 145000000);
}
```

## Debugging Tests

### Run with GDB

```bash
gdb ./all_RFboard_tests
(gdb) run --gtest_filter=RFBoard.SpecificTest
```

### Enable Verbose Logging

Many modules support debug output to Serial. Tests can capture this:

```cpp
Serial.createFile("test_output.txt");
FunctionUnderTest();
Serial.closeFile();
// Examine test_output.txt for debug info
```

### Check Build Dependencies

If tests fail to compile after changes, rebuild from scratch:

```bash
cd code/test/build
rm -rf *
cmake ../ && make
```

## DSP Test Visualization with Jupyter Notebooks

Many DSP tests in `SignalProcessing_test.cpp` and `TransmitChain_test.cpp` write intermediate signal processing data to files. These data files can be analyzed and visualized using Jupyter notebooks to verify algorithm correctness and diagnose issues.

### How It Works

1. **Tests Generate Data**: DSP tests write signal data to `.txt` files in the `build/` directory
2. **Notebooks Read Data**: Jupyter notebooks load these files using NumPy
3. **Visualization**: Notebooks plot time-domain waveforms, frequency spectra, and filter responses
4. **Analysis**: Visual inspection validates DSP algorithm behavior

### Data File Format

Tests write data in CSV format with comma-separated values:

```
0,0.5234,-0.1234
1,0.5123,-0.1345
2,0.4987,-0.1456
...
```

Typical columns:
- Column 0: Sample index or time
- Column 1: I (in-phase) or left channel data
- Column 2: Q (quadrature) or right channel data

### Available Analysis Notebooks

| Notebook | Purpose | Data Files Used |
|----------|---------|----------------|
| `analyze_filter_chain.ipynb` | Analyze receive filter chain behavior | `ConvolutionFilter_*.txt`, `DecimateBy*_*.txt`, `zoomFFT*.txt` |
| `analyze_transmit_filter_chain.ipynb` | Analyze transmit signal processing | Transmit chain filter outputs |
| `analyze_front_panel.ipynb` | Analyze front panel encoder behavior | Front panel test outputs |

### Example: Analyzing Filter Performance

#### Step 1: Run the Signal Processing Tests

```bash
cd code/test/build
./all_SigProc_tests
```

This generates data files like:

- `ConvolutionFilter_original_IQ.txt` - Input signal
- `ConvolutionFilter_pass1_filtered_IQ.txt` - After first filter pass
- `ConvolutionFilter_filtered_PSD.txt` - Power spectral density
- `zoomFFT1_psd.txt` - Zoom FFT results
- `DecimateBy4_decimated_IQ.txt` - Decimated signals
- `AGC1_Imagnitudes.txt` - AGC response curves
- And many more...

#### Step 2: Open Analysis Notebook

```bash
cd code/test
jupyter notebook analyze_filter_chain.ipynb
```

#### Step 3: Run Notebook Cells

The notebook will:

- Load data files from `build/` directory
- Compute power spectral densities using Welch's method
- Plot time-domain waveforms
- Plot frequency-domain spectra
- Compare input vs output signals
- Validate filter characteristics

### Example Analyses

**Convolution Filter Analysis**:
```python
# From analyze_filter_chain.ipynb
po = np.genfromtxt('build/ConvolutionFilter_original_IQ.txt', delimiter=",")
p3out = np.genfromtxt('build/ConvolutionFilter_pass3_filtered_IQ.txt', delimiter=",")

fa, Pa = welch(po[:,1]+1j*po[:,2], fs=192000/8, return_onesided=False)
f, P = welch(p3out[:,1]+1j*p3out[:,2], fs=192000/8, return_onesided=False)

plt.plot(fa, 10*np.log10(Pa), 'k-', label='Original')
plt.plot(f, 10*np.log10(P), 'r-', label='Filtered')
plt.legend()
```

**Zoom FFT Analysis**:
```python
# Analyzes spectrum zoom functionality
e = np.genfromtxt('build/zoomFFT1_psd.txt', delimiter=",")
plt.plot(e[:,0], 10*e[:,1], 'k-')
plt.title("Zoom FFT Power Spectral Density")
```

**AGC Response Analysis**:
```python
# Plots AGC recovery time for different settings
labels = ["Off", "Long", "Slow", "Med", "Fast"]
for i in range(1, 5):
    a = np.genfromtxt('build/AGC%d_Imagnitudes.txt'%i, delimiter=",")
    t_s = a[:,0]*256/(192000/8)
    plt.plot(t_s, a[:,1], label=labels[i])
plt.xlabel("Time [s]")
plt.ylabel("I magnitude")
plt.title("AGC recovery time")
```

### DSP Tests That Generate Data

**Filters**:

- `TEST(SignalProcessing, ZoomIIR_DecimateData)` → `data_after_IIR_zoom_*.txt`
- `TEST(SignalProcessing, ConvolutionFilter)` → `ConvolutionFilter_*.txt`
- `TEST(SignalProcessing, ReceiveEQ)` → `ReceiveEQ_band_*.txt`
- `TEST(SignalProcessing, CWFilter)` → `CWFilter_band_*.txt`

**Frequency Translation**:

- `TEST(SignalProcessing, FineTuneTranslate)` → `fineTuneTranslate_*.txt`
- `TEST(SignalProcessing, FrequencyTranslate)` → `frequencyTranslate_*.txt`

**Decimation**:

- `TEST(SignalProcessing, DecimateBy4)` → `DecimateBy4_*.txt`
- `TEST(SignalProcessing, DecimateBy8)` → `DecimateBy8_*.txt`

**Noise Reduction**:

- `TEST(SignalProcessing, KimNoiseReduction)` → `KimNR_*.txt`
- `TEST(SignalProcessing, SpectralNR)` → `SpectralNR_*.txt`

**AGC & Demodulation**:

- `TEST(SignalProcessing, AGC)` → `AGC*_Imagnitudes.txt`
- `TEST(SignalProcessing, DemodAM)` → `DemodAM_*.txt`

### Writing Tests That Generate Visualization Data

To write data from a test for visualization:

```cpp
TEST(SignalProcessing, MyNewFilter) {
    // Setup test data
    float32_t I[512], Q[512];
    GenerateTestSignal(I, Q, 512);

    // Write input data
    WriteIQFile(I, Q, "MyFilter_input_IQ.txt", 512);

    // Apply filter
    MyNewFilter(I, Q, 512);

    // Write output data
    WriteIQFile(I, Q, "MyFilter_output_IQ.txt", 512);

    // Compute and write PSD
    float32_t psd[256];
    ComputePSD(I, Q, psd, 256);
    WriteFile(psd, "MyFilter_psd.txt", 256);
}
```

Helper functions available:

- `WriteIQFile(I, Q, filename, length)` - Write I/Q data pairs
- `WriteFile(data, filename, length)` - Write single channel data
- `Serial.createFile(filename)` - Redirect Serial output to file

### Analyzing New Test Data

Create analysis code in a notebook:

```python
import numpy as np
import matplotlib.pyplot as plt
from scipy.signal import welch

# Load test data
input_data = np.genfromtxt('build/MyFilter_input_IQ.txt', delimiter=",")
output_data = np.genfromtxt('build/MyFilter_output_IQ.txt', delimiter=",")

# Plot time domain
plt.figure(figsize=(14, 5))
plt.subplot(1, 2, 1)
plt.plot(input_data[:,0], input_data[:,1], 'k-', label='I')
plt.plot(input_data[:,0], input_data[:,2], 'r-', label='Q')
plt.title("Input Signal")
plt.legend()

plt.subplot(1, 2, 2)
plt.plot(output_data[:,0], output_data[:,1], 'k-', label='I')
plt.plot(output_data[:,0], output_data[:,2], 'r-', label='Q')
plt.title("Filtered Signal")
plt.legend()

# Plot frequency domain
f_in, P_in = welch(input_data[:,1] + 1j*input_data[:,2],
                   fs=192000, return_onesided=False)
f_out, P_out = welch(output_data[:,1] + 1j*output_data[:,2],
                     fs=192000, return_onesided=False)

plt.figure()
plt.plot(f_in/1000, 10*np.log10(P_in), 'k-', label='Input')
plt.plot(f_out/1000, 10*np.log10(P_out), 'r-', label='Output')
plt.xlabel("Frequency [kHz]")
plt.ylabel("Power [dB]")
plt.title("Power Spectral Density")
plt.legend()
plt.grid()
```

### Prerequisites for Notebook Analysis

Install required Python packages:

```bash
pip install numpy matplotlib scipy jupyter
```

Or using conda:

```bash
conda install numpy matplotlib scipy jupyter
```

### Best Practices

1. **Run Tests First**: Always run tests before opening notebooks to ensure fresh data
2. **Check Build Directory**: Notebooks expect data in `code/test/build/`
3. **Version Control**: Don't commit generated `.txt` data files (add to `.gitignore`)
4. **Document Expectations**: In notebooks, document what "good" results look like
5. **Automated Checks**: Consider adding quantitative checks to test code (don't rely solely on visual inspection)

### When to Use Visualization

Visual analysis is particularly useful for:

- ✓ Verifying filter frequency response matches specifications
- ✓ Checking for phase discontinuities in frequency translation
- ✓ Validating AGC attack and decay time constants
- ✓ Ensuring decimation doesn't introduce aliasing
- ✓ Debugging unexpected signal artifacts
- ✓ Comparing different noise reduction algorithms
- ✓ Validating spectral purity of generated signals

For production testing, quantitative assertions in the test code are preferred, but visualization is invaluable during development and debugging.

## Continuous Integration

The test suite is designed for automated testing:

```bash
# Single command to build and run all tests
cd code/test/build && cmake ../ && make && ctest --output-on-failure
```

Expected result: All tests should pass with exit code 0.

## Troubleshooting

### Common Issues

**Problem**: CMake can't find Google Test

- **Solution**: Ensure internet connection for first build

**Problem**: Linker errors about undefined references

- **Solution**: Add missing source files to the executable definition in `CMakeLists.txt`

**Problem**: Tests pass in isolation but fail when run together

- **Solution**: Check for global state pollution; ensure proper test isolation

**Problem**: Mock behavior doesn't match hardware

- **Solution**: Update mock implementation to match hardware behavior

### Getting Help

- Check existing tests for patterns and examples
- Review Google Test documentation: https://google.github.io/googletest/
- Examine CMakeLists.txt to understand dependencies

## Best Practices

1. **Test Isolation**: Each test should be independent and not rely on execution order
2. **Fast Tests**: Keep tests fast; use mocks instead of real hardware delays
3. **Clear Names**: Use descriptive test names that explain what is being tested
4. **Arrange-Act-Assert**: Structure tests with clear setup, execution, and verification phases
5. **Test One Thing**: Each test should verify a single behavior
6. **Mock External Dependencies**: Don't test Arduino library code, mock it
7. **State Machine Coverage**: Test all state transitions and edge cases
8. **Add Tests for Bugs**: When fixing a bug, add a test that would have caught it

## Test Coverage

The test suite provides comprehensive coverage of:

- ✓ RF hardware control and VFO management
- ✓ State machine transitions (Mode and UI)
- ✓ Digital signal processing algorithms
- ✓ CAT protocol command handling
- ✓ Front panel input processing
- ✓ Display rendering
- ✓ Filter board control (BPF and LPF)
- ✓ Main program loop timing and sequencing

When adding new features, ensure corresponding tests are added to maintain coverage.
