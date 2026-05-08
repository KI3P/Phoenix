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
#ifndef PARAM_SAVE_H
#define PARAM_SAVE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Maximum storage capacities
#define MAX_SAVED_ARRAYS 3
#define MAX_SAVED_VARS 5
#define MAX_ARRAY_SIZE 256  // Maximum bytes per array

// Save a variable from ED (copies data to backup storage)
// slot: 0-4, ptr: pointer to the variable, size: sizeof(variable)
// Returns true on success
bool SaveVariable(uint8_t slot, void* ptr, size_t size);

// Restore a variable to ED (copies from backup storage)
// slot: 0-4, ptr: pointer to the variable, size: sizeof(variable)
// Returns true on success
bool RestoreVariable(uint8_t slot, void* ptr, size_t size);

// Save an array from ED (copies data to backup storage)
// slot: 0-2, ptr: pointer to the array, size: total size in bytes
// Returns true on success
bool SaveArray(uint8_t slot, void* ptr, size_t size);

// Restore an array to ED (copies from backup storage)
// slot: 0-2, ptr: pointer to the array, size: total size in bytes
// Returns true on success
bool RestoreArray(uint8_t slot, void* ptr, size_t size);

// Clear all saved data
void ClearSavedParams(void);

// Check if a variable slot has saved data
bool IsVariableSaved(uint8_t slot);

// Check if an array slot has saved data
bool IsArraySaved(uint8_t slot);

#endif // PARAM_SAVE_H
