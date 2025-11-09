#ifndef STORAGE_H
#define STORAGE_H

/**
 * @brief Restore radio configuration from persistent storage
 * @note Attempts to load config from LittleFS first, falls back to SD card if available
 * @note Deserializes JSON configuration file to populate ED (EEPROMData) structure
 * @note Uses default values if no configuration file is found on either storage
 */
void RestoreDataFromStorage(void);

/**
 * @brief Save radio configuration to persistent storage
 * @note Serializes ED (EEPROMData) structure to JSON format
 * @note Saves to both LittleFS program flash (1MB) and SD card if present
 * @note Configuration includes VFO settings, band data, calibration, and all user preferences
 */
void SaveDataToStorage(void);

/**
 * @brief Initialize persistent storage subsystems
 * @note Initializes LittleFS program flash storage (1MB minimum)
 * @note Initializes SD card storage if card is present
 * @note Automatically calls RestoreDataFromStorage() after initialization
 */
void InitializeStorage(void);

#endif // STORAGE_H