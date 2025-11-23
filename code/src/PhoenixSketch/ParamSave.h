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
