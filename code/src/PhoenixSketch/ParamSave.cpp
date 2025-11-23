#include "SDT.h"

// Storage for saved variables (up to 5)
static uint8_t savedVarData[MAX_SAVED_VARS][64];  // 64 bytes max per variable
static size_t savedVarSizes[MAX_SAVED_VARS] = {0};
static bool varSaved[MAX_SAVED_VARS] = {false};

// Storage for saved arrays (up to 3)
static uint8_t savedArrayData[MAX_SAVED_ARRAYS][MAX_ARRAY_SIZE];
static size_t savedArraySizes[MAX_SAVED_ARRAYS] = {0};
static bool arraySaved[MAX_SAVED_ARRAYS] = {false};

/**
 * Save a variable to backup storage.
 * Copies the data from the specified pointer to an internal buffer.
 *
 * @param slot Storage slot index (0 to MAX_SAVED_VARS-1)
 * @param ptr Pointer to the variable to save
 * @param size Size of the variable in bytes (max 64)
 * @return true on success, false if slot invalid, ptr null, or size invalid
 */
bool SaveVariable(uint8_t slot, void* ptr, size_t size) {
    if (slot >= MAX_SAVED_VARS || ptr == nullptr || size == 0 || size > 64) {
        return false;
    }

    memcpy(savedVarData[slot], ptr, size);
    savedVarSizes[slot] = size;
    varSaved[slot] = true;
    return true;
}

/**
 * Restore a variable from backup storage.
 * Copies the saved data back to the specified pointer.
 *
 * @param slot Storage slot index (0 to MAX_SAVED_VARS-1)
 * @param ptr Pointer to the variable to restore to
 * @param size Size of the variable in bytes (must match saved size)
 * @return true on success, false if slot invalid, not saved, or size mismatch
 */
bool RestoreVariable(uint8_t slot, void* ptr, size_t size) {
    if (slot >= MAX_SAVED_VARS || ptr == nullptr || !varSaved[slot]) {
        return false;
    }

    if (size != savedVarSizes[slot]) {
        return false;  // Size mismatch
    }

    memcpy(ptr, savedVarData[slot], size);
    return true;
}

/**
 * Save an array to backup storage.
 * Copies the data from the specified pointer to an internal buffer.
 *
 * @param slot Storage slot index (0 to MAX_SAVED_ARRAYS-1)
 * @param ptr Pointer to the array to save
 * @param size Total size of the array in bytes (max MAX_ARRAY_SIZE)
 * @return true on success, false if slot invalid, ptr null, or size invalid
 */
bool SaveArray(uint8_t slot, void* ptr, size_t size) {
    if (slot >= MAX_SAVED_ARRAYS || ptr == nullptr || size == 0 || size > MAX_ARRAY_SIZE) {
        return false;
    }

    memcpy(savedArrayData[slot], ptr, size);
    savedArraySizes[slot] = size;
    arraySaved[slot] = true;
    return true;
}

/**
 * Restore an array from backup storage.
 * Copies the saved data back to the specified pointer.
 *
 * @param slot Storage slot index (0 to MAX_SAVED_ARRAYS-1)
 * @param ptr Pointer to the array to restore to
 * @param size Total size of the array in bytes (must match saved size)
 * @return true on success, false if slot invalid, not saved, or size mismatch
 */
bool RestoreArray(uint8_t slot, void* ptr, size_t size) {
    if (slot >= MAX_SAVED_ARRAYS || ptr == nullptr || !arraySaved[slot]) {
        return false;
    }

    if (size != savedArraySizes[slot]) {
        return false;  // Size mismatch
    }

    memcpy(ptr, savedArrayData[slot], size);
    return true;
}

/**
 * Clear all saved parameter data.
 * Resets all variable and array slots to empty state.
 */
void ClearSavedParams(void) {
    for (int i = 0; i < MAX_SAVED_VARS; i++) {
        varSaved[i] = false;
        savedVarSizes[i] = 0;
    }
    for (int i = 0; i < MAX_SAVED_ARRAYS; i++) {
        arraySaved[i] = false;
        savedArraySizes[i] = 0;
    }
}

/**
 * Check if a variable slot contains saved data.
 *
 * @param slot Storage slot index (0 to MAX_SAVED_VARS-1)
 * @return true if the slot contains saved data, false otherwise
 */
bool IsVariableSaved(uint8_t slot) {
    if (slot >= MAX_SAVED_VARS) {
        return false;
    }
    return varSaved[slot];
}

/**
 * Check if an array slot contains saved data.
 *
 * @param slot Storage slot index (0 to MAX_SAVED_ARRAYS-1)
 * @return true if the slot contains saved data, false otherwise
 */
bool IsArraySaved(uint8_t slot) {
    if (slot >= MAX_SAVED_ARRAYS) {
        return false;
    }
    return arraySaved[slot];
}
