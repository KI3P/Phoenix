# Storage: Preserving Radio Configuration Across Power Cycles

## Introduction

The T41 SDR needs to remember dozens of user preferences: VFO frequencies, power levels, equalizer settings, calibration values, and more. When you power off the radio, all these settings must be preserved. When you power it back on, they should be restored exactly as you left them.

This post explores the storage system in the Phoenix firmware. We'll examine the configuration structure, the dual-storage architecture using program flash and SD card, the JSON serialization mechanism, and the timing of save/restore operations.

## The Configuration Structure: config_t

All user-configurable settings are stored in a single global struct called `ED` (a contraction of "EEPROM Data" from earlier designs). This struct is defined in `SDT.h:229-278`:

```cpp
extern struct config_t {
    // AGC and audio settings
    AGCMode agc = AGCOff;
    int32_t audioVolume = 30;
    float32_t rfGainAllBands_dB = 0;

    // Display and spectrum settings
    int32_t spectrumScale = 1;
    int16_t spectrumNoiseFloor[NUMBER_OF_BANDS] = {50,50,...};
    uint32_t spectrum_zoom = 1;

    // CW settings
    int32_t CWFilterIndex = 5;
    int32_t CWToneIndex = 3;
    KeyTypeId keyType = KEYER_TYPE;
    int32_t currentWPM = DEFAULT_KEYER_WPM;
    float32_t sidetoneVolume = 20.0;
    bool keyerFlip = KEYER_FLIP;

    // VFO settings (per VFO)
    uint8_t activeVFO = 0;
    ModulationType modulation[2] = {LSB, LSB};
    int32_t currentBand[2] = {STARTUP_BAND, STARTUP_BAND};
    int64_t centerFreq_Hz[2] = {CURRENT_FREQ_A, CURRENT_FREQ_B};
    int64_t fineTuneFreq_Hz[2] = {0, 0};

    // Equalizer settings
    int32_t equalizerRec[EQUALIZER_CELL_COUNT] = {100,100,...};
    int32_t equalizerXmt[EQUALIZER_CELL_COUNT] = {100,100,...};

    // Transmit settings
    int32_t currentMicGain = -10;
    float32_t powerOutCW[NUMBER_OF_BANDS] = {...};
    float32_t powerOutSSB[NUMBER_OF_BANDS] = {...};

    // Calibration values (per band)
    float32_t IQAmpCorrectionFactor[NUMBER_OF_BANDS] = {1,1,...};
    float32_t IQPhaseCorrectionFactor[NUMBER_OF_BANDS] = {0,0,...};
    float32_t XAttenCW[NUMBER_OF_BANDS] = {0,0,...};
    float32_t XAttenSSB[NUMBER_OF_BANDS] = {0,0,...};
    float32_t RAtten[NUMBER_OF_BANDS] = {0,0,...};
    float32_t SWR_F_SlopeAdj[NUMBER_OF_BANDS] = {0,0,...};
    float32_t SWR_R_SlopeAdj[NUMBER_OF_BANDS] = {0,0,...};
    float32_t SWR_F_Offset[NUMBER_OF_BANDS] = {0,0,...};
    float32_t SWR_R_Offset[NUMBER_OF_BANDS] = {0,0,...};

    // Antenna and frequency memory
    int32_t antennaSelection[NUMBER_OF_BANDS] = {0,0,...};
    int64_t lastFrequencies[NUMBER_OF_BANDS][3] = {{...},...};

    // Other settings
    int64_t stepFineTune = FAST_TUNE_INCREMENT;
    int32_t freqIncrement = DEFAULTFREQINCREMENT;
    float32_t freqCorrectionFactor = 0;
    NoiseReductionType nrOptionSelect = NROff;
    uint8_t ANR_notchOn = 0;
    int32_t decoderFlag = 0;
    float32_t dbm_calibration = 17.5;
} ED;
```

The struct uses default initializers so all settings have sensible defaults if no stored configuration is found.

## Dual Storage Architecture

The Phoenix SDR uses a two-tier storage system implemented in `Storage.cpp`:

### Primary Storage: LittleFS Program Flash

The primary storage uses **LittleFS** (Little Filesystem), a fail-safe filesystem designed for microcontrollers. It's stored in the Teensy 4.1's program flash memory:

```cpp
const char *filename = "config.txt";
LittleFS_Program myfs;
```

LittleFS is initialized with a 1MB partition (`Storage.cpp:15-18`):

```cpp
void InitializeStorage(void){
    if (!myfs.begin(1024 * 1024)) { // minimum size is 64 KB
        Serial.printf("Error starting %s\n", "Program flash DISK");
    }
    // ...
}
```

Using LittleFS rather than directly reading and writing bytes from a section of flash storage allows us to save the configuration parameters as a file. This avoids the problem in previous firmware where changing any part of the `config_t` struct required a flash erase and loss of all saved parameters. Using LittleFS has several other advantages:

- **Power-fail safe**: Designed to survive unexpected power loss during writes
- **Wear leveling**: Distributes writes across flash to extend lifetime
- **Built-in with Teensyduino**: No external dependencies

### Secondary Storage: SD Card

The secondary storage uses an optional SD card via the Teensy 4.1's built-in SD card slot:

```cpp
static bool SDpresent = false;

void InitializeStorage(void){
    // ... after LittleFS initialization ...
    if (SD.begin(BUILTIN_SDCARD)) {
        Serial.println("SD card initialized.");
        SDpresent = true;
    } else {
        Serial.println("SD card not initialized!");
        SDpresent = false;
    }
    RestoreDataFromStorage();
}
```

The SD card is completely optional — the radio works fine without it. However, it provides:

- **Backup protection**: If program flash becomes corrupted, settings can be restored from SD
- **Easy configuration transfer**: Users can copy `config.txt` between radios
- **Human-readable format**: SD card uses pretty-printed JSON for easy inspection and editing
- **Development convenience**: Developers can examine and modify settings without special tools

## Storage Format: JSON Serialization

The configuration is stored as JSON (JavaScript Object Notation) using the **ArduinoJson** library. This provides a structured, human-readable format that's easy to parse and debug.

### Saving Data: JSON Encoding

The `SaveDataToStorage()` function (`Storage.cpp:33-135`) serializes the `ED` struct to JSON:

```cpp
void SaveDataToStorage(void){
    JsonDocument doc; 

    // Scalar values - simple assignment
    doc["agc"] = ED.agc;
    doc["audioVolume"] = ED.audioVolume;
    doc["rfGainAllBands_dB"] = ED.rfGainAllBands_dB;
    // ... many more scalar values ...

    // Simple arrays - indexed assignment
    doc["modulation"][0] = ED.modulation[0];
    doc["modulation"][1] = ED.modulation[1];

    // Per-band arrays - loop through all bands
    for(int i = 0; i < NUMBER_OF_BANDS; i++) {
        doc["powerOutCW"][i] = ED.powerOutCW[i];
        doc["powerOutSSB"][i] = ED.powerOutSSB[i];
        doc["IQAmpCorrectionFactor"][i] = ED.IQAmpCorrectionFactor[i];
        // ... more per-band values ...
    }

    // Multi-dimensional arrays
    for(int i = 0; i < NUMBER_OF_BANDS; i++) {
        for(int j = 0; j < 3; j++) {
            doc["lastFrequencies"][i][j] = ED.lastFrequencies[i][j];
        }
    }

    // ... write to storage (see below) ...
}
```

The ArduinoJson library handles type conversion automatically. Integer values become JSON numbers, enums are stored as integers, and arrays become JSON arrays.

### Writing to LittleFS

After building the JSON document, it's written to program flash (`Storage.cpp:100-114`):

```cpp
// Delete existing file, otherwise data is appended
myfs.remove(filename);

// Open file for writing
File file = myfs.open(filename, FILE_WRITE);
if (file) {
    if (serializeJson(doc, file) == 0){
        Serial.println(F("Failed to write to LittleFS"));
    } else {
        Serial.println("Config saved to LittleFS");
    }
    file.flush();  // Ensure data is written to flash
    file.close();
} else {
    Serial.println("Failed to open LittleFS file for writing");
}
```

The compact JSON format on LittleFS looks like:

```json
{"agc":0,"audioVolume":30,"rfGainAllBands_dB":0,"stepFineTune":500,...}
```

### Writing to SD Card

If an SD card is present, the configuration is also saved there (`Storage.cpp:116-134`):

```cpp
if (SDpresent) {
    SD.remove(filename);
    File fileSD = SD.open(filename, FILE_WRITE);
    if (fileSD) {
        if (serializeJsonPretty(doc, fileSD) == 0) {
            Serial.println(F("Failed to write to SD card file"));
        } else {
            Serial.println("Config saved to SD card");
        }
        fileSD.flush();
        fileSD.close();
    } else {
        Serial.println(F("Failed to create file on SD card"));
    }
}
```

Differences from LittleFS save:

- Uses `serializeJsonPretty()` instead of `serializeJson()` for human-readable formatting
- Only attempted if `SDpresent == true`
- Failure doesn't affect operation (SD card is optional)

The pretty-printed format on SD card is more readable:

```json
{
  "agc": 0,
  "audioVolume": 30,
  "rfGainAllBands_dB": 0,
  "stepFineTune": 500,
  "nrOptionSelect": 0,
  "spectrumScale": 1,
  "spectrumNoiseFloor": [50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50],
  ...
}
```

This pretty format makes it easy to inspect and manually edit configuration values for debugging or experimentation.

## Loading Data: JSON Decoding

The `RestoreDataFromStorage()` function (`Storage.cpp:179-359`) loads the configuration at startup.

### Load Priority: LittleFS First, SD Card Fallback

The restore operation tries multiple sources in priority order (`Storage.cpp:185-223`):

```cpp
void RestoreDataFromStorage(void){
    JsonDocument doc;
    bool dataLoaded = false;

    // Try to load from LittleFS first
    File file = myfs.open(filename, FILE_READ);
    if (file) {
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        if (error) {
            Serial.print("Failed to parse config from LittleFS: ");
            Serial.println(error.c_str());
        } else {
            Serial.println("Config loaded from LittleFS");
            dataLoaded = true;
        }
    } else {
        Serial.println("Config file not found on LittleFS");
    }

    // If LittleFS failed and SD card is present, try SD card
    if (!dataLoaded && SDpresent) {
        File fileSD = SD.open(filename, FILE_READ);
        if (fileSD) {
            DeserializationError error = deserializeJson(doc, fileSD);
            fileSD.close();
            if (error) {
                Serial.print("Failed to parse config from SD card: ");
                Serial.println(error.c_str());
            } else {
                Serial.println("Config loaded from SD card");
                dataLoaded = true;
            }
        } else {
            Serial.println("Config file not found on SD card");
        }
    }

    // If no data was loaded, keep default values
    if (!dataLoaded) {
        Serial.println("No config file found, using default values");
        return;
    }

    // ... restore values from doc (see below) ...
}
```

This tries LittleFS first, and falls back to the SD card (if it is present). If both fail, use compiled-in defaults from struct initializers.

### Deserializing with Default Fallbacks

Each value is restored with a default fallback using the `|` (OR) operator (`Storage.cpp:225-356`):

```cpp
// Restore scalar values with defaults if not present
ED.agc = (AGCMode)(doc["agc"] | (int)ED.agc);
ED.audioVolume = doc["audioVolume"] | ED.audioVolume;
ED.rfGainAllBands_dB = doc["rfGainAllBands_dB"] | ED.rfGainAllBands_dB;
```

How the `|` operator works with ArduinoJson:

- If `doc["key"]` exists and is valid, use that value
- If `doc["key"]` is missing or invalid, use the value after `|` (the default)

This provides graceful handling of:

- **Incomplete configuration files**: Missing fields use defaults
- **Format changes**: New fields added to `config_t` work even with old config files
- **Corrupted values**: Invalid JSON entries fall back to defaults

## When Storage Operations Occur

### Initialization: RestoreDataFromStorage()

Storage initialization happens early in the boot sequence (`PhoenixSketch.ino:203-204`):

```cpp
void setup() {
    // ... hardware initialization ...

    Serial.println("...Initializing storage");
    InitializeStorage();

    // ... continue with radio setup ...
}
```

The `InitializeStorage()` function:

1. Initializes LittleFS filesystem (1MB in program flash)
2. Attempts to initialize SD card (sets `SDpresent` flag)
3. Calls `RestoreDataFromStorage()` to load configuration
4. Lists files on internal storage for debugging

Occurs once at startup, before any radio operations begin. This ensures all modules see the correct configuration when they initialize.

### Shutdown: SaveDataToStorage()

Configuration is saved during graceful shutdown (`Loop.cpp:817-824`):

```cpp
void ShutdownTeensy(void){
    // Save all configuration to storage
    SaveDataToStorage();

    // Signal ATTiny that shutdown is complete
    digitalWrite(SHUTDOWN_COMPLETE, 1);
    MyDelay(1000); // Wait for power to be cut
}
```

The shutdown sequence:

1. User presses the front panel power button
2. ATTiny85 power management controller signals the Teensy via `BEGIN_TEENSY_SHUTDOWN` pin
3. Main loop detects the signal: `if (digitalRead(BEGIN_TEENSY_SHUTDOWN)) ShutdownTeensy();`
4. `ShutdownTeensy()` saves configuration to both LittleFS and SD card
5. Teensy signals completion via `SHUTDOWN_COMPLETE` pin
6. ATTiny85 cuts power to the Teensy

Occurs once per power-off. The 1-second delay ensures power is cut before the main loop execution continues.

### Alternative Trigger: CAT Command

The shutdown (and thus storage save) can also be triggered remotely via the CAT control interface (`CAT.cpp:561-565`):

```cpp
/**
 * CAT command PS - Turn radio power off
 *
 * @return Response "PS0;" (requests shutdown from ATtiny)
 */
String PS_write(String data) {
    ShutdownTeensy();
    return "PS0;";
}
```

Computer control software can send the `PS0;` command to power off the radio, which triggers the same graceful shutdown and storage save sequence.

## Storage During Operation: Live Updates

An important characteristic of the Phoenix storage system: **configuration is NOT saved during normal operation**. Changes made during operation (frequency adjustments, power level changes, equalizer tweaks, etc.) are only written to the `ED` struct in RAM. They persist to storage only when the radio is powered off.

This design choice has several implications:

**Advantages:**

- **No wear on flash memory**: Flash has limited write cycles (typically 10,000-100,000)
- **No performance impact**: Storage writes would take time and could cause audio glitches
- **No power-fail corruption risk**: No storage writes during operation means no risk of corrupted config

**Considerations:**

- **Unexpected power loss**: If power is lost without pressing the power button, recent changes are lost
- **Recovery from SD card**: If flash is corrupted but SD card is intact, last-shutdown configuration is restored

For amateur radio use, I feel like this trade-off is reasonable. Users typically make configuration changes, operate for a while, then power off normally. The risk of unexpected power loss is low compared to the benefits of avoiding flash wear and runtime delays.

## Storage Size Considerations

The JSON configuration file is typically around 1.5-2 KB depending on the number of bands and settings. The LittleFS filesystem uses 1MB of program flash, providing ample space for:

- The configuration file itself (~2 KB)
- LittleFS metadata and wear leveling structures
- Future expansion (additional configuration files, logs, etc.)

The Teensy 4.1 has 8MB of program flash total. Allocating 1MB for LittleFS (12.5% of total) leaves plenty of room for the firmware code and provides substantial space for filesystem operations and wear leveling.

## Code References

- Configuration structure definition: `code/src/PhoenixSketch/SDT.h:229-278`
- Storage implementation: `code/src/PhoenixSketch/Storage.cpp:1-360`
- Storage interface: `code/src/PhoenixSketch/Storage.h:1-8`
- Initialization during boot: `code/src/PhoenixSketch/PhoenixSketch.ino:203-204`
- Save during shutdown: `code/src/PhoenixSketch/Loop.cpp:817-824`
- CAT-triggered shutdown: `code/src/PhoenixSketch/CAT.cpp:561-565`
- Shutdown signal monitoring: `code/src/PhoenixSketch/Loop.cpp:852`