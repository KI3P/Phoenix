#include "SDT.h"
#include <LittleFS.h> // comes bundled with Teensyduino
#include <SD.h>
#include <ArduinoJson.h>

const char *filename = "config.txt";
LittleFS_Program myfs;
static bool SDpresent = false;

void InitializeStorage(void){
    if (!myfs.begin(1024 * 1024)) { // minimum size is 64 KB
        Serial.printf("Error starting %s\n", "Program flash DISK");
    }
    if (SD.begin(BUILTIN_SDCARD)) {
        Serial.println("SD card initialized.");
        SDpresent = true;
    } else {
        Serial.println("SD card not initialized!");
        SDpresent = false;
    }
    RestoreDataFromStorage();
}

/**
 * Save config_t struct to flash memory as a file using the LittleFS.h
 * file system. This also saves the file to the SD card, if it is present.
 */
void SaveDataToStorage(void){
    JsonDocument doc;  // This uses the heap.

    // Assign the elements of ED to doc
    doc["agc"] = ED.agc;
    doc["audioVolume"] = ED.audioVolume;
    doc["rfGainAllBands_dB"] = ED.rfGainAllBands_dB;
    doc["stepFineTune"] = ED.stepFineTune;
    doc["nrOptionSelect"] = ED.nrOptionSelect;
    doc["ANR_notchOn"] = ED.ANR_notchOn;
    doc["spectrumScale"] = ED.spectrumScale;
    doc["spectrumNoiseFloor"] = ED.spectrumNoiseFloor;
    doc["spectrum_zoom"] = ED.spectrum_zoom;
    doc["CWFilterIndex"] = ED.CWFilterIndex;
    doc["CWToneIndex"] = ED.CWToneIndex;
    doc["decoderFlag"] = ED.decoderFlag;
    doc["keyType"] = ED.keyType;
    doc["currentWPM"] = ED.currentWPM;
    doc["sidetoneVolume"] = ED.sidetoneVolume;
    doc["freqIncrement"] = ED.freqIncrement;
    doc["freqCorrectionFactor"] = ED.freqCorrectionFactor;
    doc["activeVFO"] = ED.activeVFO;

    // Arrays
    doc["modulation"][0] = ED.modulation[0];
    doc["modulation"][1] = ED.modulation[1];
    doc["currentBand"][0] = ED.currentBand[0];
    doc["currentBand"][1] = ED.currentBand[1];
    doc["centerFreq_Hz"][0] = ED.centerFreq_Hz[0];
    doc["centerFreq_Hz"][1] = ED.centerFreq_Hz[1];
    doc["fineTuneFreq_Hz"][0] = ED.fineTuneFreq_Hz[0];
    doc["fineTuneFreq_Hz"][1] = ED.fineTuneFreq_Hz[1];

    // Equalizer arrays
    for(int i = 0; i < EQUALIZER_CELL_COUNT; i++) {
        doc["equalizerRec"][i] = ED.equalizerRec[i];
        doc["equalizerXmt"][i] = ED.equalizerXmt[i];
    }

    doc["currentMicGain"] = ED.currentMicGain;
    doc["dbm_calibration"] = ED.dbm_calibration;

    // Band-specific arrays
    for(int i = 0; i < NUMBER_OF_BANDS; i++) {
        doc["powerOutCW"][i] = ED.powerOutCW[i];
        doc["powerOutSSB"][i] = ED.powerOutSSB[i];
        doc["IQAmpCorrectionFactor"][i] = ED.IQAmpCorrectionFactor[i];
        doc["IQPhaseCorrectionFactor"][i] = ED.IQPhaseCorrectionFactor[i];
        doc["XAttenCW"][i] = ED.XAttenCW[i];
        doc["XAttenSSB"][i] = ED.XAttenSSB[i];
        doc["RAtten"][i] = ED.RAtten[i];
        doc["antennaSelection"][i] = ED.antennaSelection[i];
        doc["SWR_F_SlopeAdj"][i] = ED.SWR_F_SlopeAdj[i];
        doc["SWR_R_SlopeAdj"][i] = ED.SWR_R_SlopeAdj[i];
        doc["SWR_R_Offset"][i] = ED.SWR_R_Offset[i];
        doc["SWR_F_Offset"][i] = ED.SWR_F_Offset[i];

        // Last frequencies array (3 values per band)
        for(int j = 0; j < 3; j++) {
            doc["lastFrequencies"][i][j] = ED.lastFrequencies[i][j];
        }
    }

    doc["keyerFlip"] = ED.keyerFlip;

    // Write this JSON object to filename on the LittleFS
    // Delete existing file, otherwise data is appended to the file
    myfs.remove(filename);
    File file = myfs.open(filename, FILE_WRITE);
    if (file) {
        if (serializeJson(doc, file) == 0){
            Serial.println(F("Failed to write to LittleFS"));
        }else{
            Serial.println("Config saved to LittleFS");
        }
        file.flush();
        file.close();
    } else {
        Serial.println("Failed to open LittleFS file for writing");
    }

    // Save it to the SD card as well if it is present
    if (SDpresent) {
        // Delete existing file, otherwise data is appended to the file
        SD.remove(filename);
        // Open file for writing
        File fileSD = SD.open(filename, FILE_WRITE);
        if (fileSD) {
            // Serialize JSON to file
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
}

void listDir(LittleFS_Program &fs, const char *dirname) {
    //Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if (!root) {
        Serial.println("- failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.print("  DIR : ");
            Serial.println(file.name());
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}


/**
 * Restore the ED config_t struct from the data stored in the LittleFS.
 * If the LittleFS file is not present, attempt to restore the data from
 * the SD card if it is present.
 */
void RestoreDataFromStorage(void){
    JsonDocument doc;
    bool dataLoaded = false;
    Serial.println("Files on internal storage:");
    listDir(myfs, "/");

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

    // Restore scalar values with defaults if not present
    ED.agc = (AGCMode)(doc["agc"] | (int)ED.agc);
    ED.audioVolume = doc["audioVolume"] | ED.audioVolume;
    ED.rfGainAllBands_dB = doc["rfGainAllBands_dB"] | ED.rfGainAllBands_dB;
    ED.stepFineTune = doc["stepFineTune"] | ED.stepFineTune;
    ED.nrOptionSelect = (NoiseReductionType)(doc["nrOptionSelect"] | (int)ED.nrOptionSelect);
    ED.ANR_notchOn = doc["ANR_notchOn"] | ED.ANR_notchOn;
    ED.spectrumScale = doc["spectrumScale"] | ED.spectrumScale;
    ED.spectrumNoiseFloor = doc["spectrumNoiseFloor"] | ED.spectrumNoiseFloor;
    ED.spectrum_zoom = doc["spectrum_zoom"] | ED.spectrum_zoom;
    ED.CWFilterIndex = doc["CWFilterIndex"] | ED.CWFilterIndex;
    ED.CWToneIndex = doc["CWToneIndex"] | ED.CWToneIndex;
    ED.decoderFlag = doc["decoderFlag"] | ED.decoderFlag;
    ED.keyType = (KeyTypeId)(doc["keyType"] | (int)ED.keyType);
    ED.currentWPM = doc["currentWPM"] | ED.currentWPM;
    ED.sidetoneVolume = doc["sidetoneVolume"] | ED.sidetoneVolume;
    ED.freqIncrement = doc["freqIncrement"] | ED.freqIncrement;
    ED.freqCorrectionFactor = doc["freqCorrectionFactor"] | ED.freqCorrectionFactor;
    ED.activeVFO = doc["activeVFO"] | ED.activeVFO;

    // Restore small arrays
    if (doc["modulation"].is<JsonArray>()) {
        ED.modulation[0] = (ModulationType)(doc["modulation"][0] | (int)ED.modulation[0]);
        ED.modulation[1] = (ModulationType)(doc["modulation"][1] | (int)ED.modulation[1]);
    }
    if (doc["currentBand"].is<JsonArray>()) {
        ED.currentBand[0] = doc["currentBand"][0] | ED.currentBand[0];
        ED.currentBand[1] = doc["currentBand"][1] | ED.currentBand[1];
    }
    if (doc["centerFreq_Hz"].is<JsonArray>()) {
        Debug("restoring center freq from storage");
        ED.centerFreq_Hz[0] = doc["centerFreq_Hz"][0] | ED.centerFreq_Hz[0];
        ED.centerFreq_Hz[1] = doc["centerFreq_Hz"][1] | ED.centerFreq_Hz[1];
    }
    if (doc["fineTuneFreq_Hz"].is<JsonArray>()) {
        ED.fineTuneFreq_Hz[0] = doc["fineTuneFreq_Hz"][0] | ED.fineTuneFreq_Hz[0];
        ED.fineTuneFreq_Hz[1] = doc["fineTuneFreq_Hz"][1] | ED.fineTuneFreq_Hz[1];
    }

    // Restore equalizer arrays
    if (doc["equalizerRec"].is<JsonArray>()) {
        for(int i = 0; i < EQUALIZER_CELL_COUNT; i++) {
            ED.equalizerRec[i] = doc["equalizerRec"][i] | ED.equalizerRec[i];
        }
    }
    if (doc["equalizerXmt"].is<JsonArray>()) {
        for(int i = 0; i < EQUALIZER_CELL_COUNT; i++) {
            ED.equalizerXmt[i] = doc["equalizerXmt"][i] | ED.equalizerXmt[i];
        }
    }

    ED.currentMicGain = doc["currentMicGain"] | ED.currentMicGain;
    ED.dbm_calibration = doc["dbm_calibration"] | ED.dbm_calibration;

    // Restore band-specific arrays
    if (doc["powerOutCW"].is<JsonArray>()) {
        for(int i = 0; i < NUMBER_OF_BANDS; i++) {
            ED.powerOutCW[i] = doc["powerOutCW"][i] | ED.powerOutCW[i];
        }
    }
    if (doc["powerOutSSB"].is<JsonArray>()) {
        for(int i = 0; i < NUMBER_OF_BANDS; i++) {
            ED.powerOutSSB[i] = doc["powerOutSSB"][i] | ED.powerOutSSB[i];
        }
    }
    if (doc["IQAmpCorrectionFactor"].is<JsonArray>()) {
        for(int i = 0; i < NUMBER_OF_BANDS; i++) {
            ED.IQAmpCorrectionFactor[i] = doc["IQAmpCorrectionFactor"][i] | ED.IQAmpCorrectionFactor[i];
        }
    }
    if (doc["IQPhaseCorrectionFactor"].is<JsonArray>()) {
        for(int i = 0; i < NUMBER_OF_BANDS; i++) {
            ED.IQPhaseCorrectionFactor[i] = doc["IQPhaseCorrectionFactor"][i] | ED.IQPhaseCorrectionFactor[i];
        }
    }
    if (doc["XAttenCW"].is<JsonArray>()) {
        for(int i = 0; i < NUMBER_OF_BANDS; i++) {
            ED.XAttenCW[i] = doc["XAttenCW"][i] | ED.XAttenCW[i];
        }
    }
    if (doc["XAttenSSB"].is<JsonArray>()) {
        for(int i = 0; i < NUMBER_OF_BANDS; i++) {
            ED.XAttenSSB[i] = doc["XAttenSSB"][i] | ED.XAttenSSB[i];
        }
    }
    if (doc["RAtten"].is<JsonArray>()) {
        for(int i = 0; i < NUMBER_OF_BANDS; i++) {
            ED.RAtten[i] = doc["RAtten"][i] | ED.RAtten[i];
        }
    }
    if (doc["antennaSelection"].is<JsonArray>()) {
        for(int i = 0; i < NUMBER_OF_BANDS; i++) {
            ED.antennaSelection[i] = doc["antennaSelection"][i] | ED.antennaSelection[i];
        }
    }
    if (doc["SWR_F_SlopeAdj"].is<JsonArray>()) {
        for(int i = 0; i < NUMBER_OF_BANDS; i++) {
            ED.SWR_F_SlopeAdj[i] = doc["SWR_F_SlopeAdj"][i] | ED.SWR_F_SlopeAdj[i];
        }
    }
    if (doc["SWR_R_SlopeAdj"].is<JsonArray>()) {
        for(int i = 0; i < NUMBER_OF_BANDS; i++) {
            ED.SWR_R_SlopeAdj[i] = doc["SWR_R_SlopeAdj"][i] | ED.SWR_R_SlopeAdj[i];
        }
    }
    if (doc["SWR_R_Offset"].is<JsonArray>()) {
        for(int i = 0; i < NUMBER_OF_BANDS; i++) {
            ED.SWR_R_Offset[i] = doc["SWR_R_Offset"][i] | ED.SWR_R_Offset[i];
        }
    }
    if (doc["SWR_F_Offset"].is<JsonArray>()) {
        for(int i = 0; i < NUMBER_OF_BANDS; i++) {
            ED.SWR_F_Offset[i] = doc["SWR_F_Offset"][i] | ED.SWR_F_Offset[i];
        }
    }

    // Restore multi-dimensional lastFrequencies array
    if (doc["lastFrequencies"].is<JsonArray>()) {
        for(int i = 0; i < NUMBER_OF_BANDS; i++) {
            if (doc["lastFrequencies"][i].is<JsonArray>()) {
                for(int j = 0; j < 3; j++) {
                    ED.lastFrequencies[i][j] = doc["lastFrequencies"][i][j] | ED.lastFrequencies[i][j];
                }
            }
        }
    }

    ED.keyerFlip = doc["keyerFlip"] | ED.keyerFlip;

    Serial.println("Config data restored successfully");
}
