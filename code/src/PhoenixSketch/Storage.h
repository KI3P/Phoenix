/* 
Copyright (C) 2026 T41 EP Software Contributors
See Contributors.txt for list of known authors.

This file is part of Phoenix.

Phoenix is free software: you can redistribute it and/or modify it under the 
terms of the GNU General Public License as published by the Free Software 
Foundation, either version 3 of the License, or (at your option) any later version.

Phoenix is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Phoenix. 
If not, see <https://www.gnu.org/licenses/>.
*/

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
 * @brief Restore radio configuration from SD card storage
 * @note Attempts to load config from SD card if available
 * @note Deserializes JSON configuration file to populate ED (EEPROMData) structure
 * @note Saves the new data to internal flash file system
 */
void RestoreDataFromSDCard(void);

/**
 * @brief Save radio configuration to persistent storage
 * @note Serializes ED (EEPROMData) structure to JSON format
 * @note Saves to both LittleFS program flash (1MB) and SD card if present
 * @note Configuration includes VFO settings, band data, calibration, and all user preferences
 */
void SaveDataToStorage(bool savetosd);

/**
 * @brief Initialize persistent storage subsystems
 * @note Initializes LittleFS program flash storage (1MB minimum)
 * @note Initializes SD card storage if card is present
 * @note Automatically calls RestoreDataFromStorage() after initialization
 */
void InitializeStorage(void);

/**
 * @brief Print ED structure over Serial link
 */
void PrintEDToSerial(void);

#endif // STORAGE_H