#include "storage.h"

#include <stddef.h>

#include "menu.h"

// Profile layout of storage version 1 (before gameName was added).
typedef struct {
        char name[17];
        KeyCombo keys[16];
} ButtonMappingV1;

static_assert(sizeof(ButtonMappingV1) == 49, "Unexpected storage version 1 profile size.");
static_assert(offsetof(ButtonMapping, gameName) == sizeof(ButtonMappingV1),
              "ButtonMapping must start with the storage version 1 layout.");

void saveProfile(uint8_t index, const ButtonMapping* profile) {
    /**
     * Save a button profile to EEPROM.
     *
     * @param index Profile slot index (0 to MAX_PROFILES-1).
     * @param profile Pointer to ButtonMapping structure to save.
     *
     * Writes the profile to EEPROM at the calculated address and commits changes.
     */
    uint16_t addr = EEPROM_BASE_ADDR + (index * PROFILE_SIZE);
    EEPROM.put(addr, *profile);
    EEPROM.commit();
}

void loadProfile(uint8_t index, ButtonMapping* profile) {
    /**
     * Load a button profile from EEPROM.
     *
     * @param index Profile slot index (0 to MAX_PROFILES-1).
     * @param profile Pointer to ButtonMapping structure to fill.
     *
     * Reads the profile from EEPROM at the calculated address.
     */
    uint16_t addr = EEPROM_BASE_ADDR + (index * PROFILE_SIZE);
    EEPROM.get(addr, *profile);
}

uint8_t getNumberOfProfiles() {
    /**
     * Get the number of used profile slots.
     *
     * @return Count of profiles with non-empty names.
     *
     * Cycles through all profile slots and counts non-empty ones.
     */
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_PROFILES; i++) {
        if (isProfileSlotUsed(i)) {
            count++;
        }
    }
    return count;
}

bool isProfileSlotUsed(uint8_t index) {
    /**
     * Check if a profile slot is used.
     *
     * @param index Profile slot index to check.
     * @return True if the profile has a non-empty name, false otherwise.
     */
    ButtonMapping temp;
    loadProfile(index, &temp);
    return (temp.name[0] != '\0');
}

void intializeStorage() {
    /**
     * Initialize EEPROM storage by clearing all profile slots.
     *
     * Sets all profiles to empty state. Should be called on first run
     * or when resetting the device to factory defaults.
     */
    for (uint8_t i = 0; i < MAX_PROFILES; i++) {
        ButtonMapping emptyProfile = {};
        saveProfile(i, &emptyProfile);
    }
    EEPROM.write(STORAGE_VERSION_ADDR, STORAGE_VERSION);
    EEPROM.commit();
}

void migrateStorage() {
    /**
     * Migrate stored profiles to the current layout if required.
     *
     * Storage version 1 had no version byte, so any value other than STORAGE_VERSION
     * is treated as version 1. Names and key combos are kept, gameName is cleared.
     * Slots starting with 0xFF (erased flash) are cleared as well.
     */
    if (EEPROM.read(STORAGE_VERSION_ADDR) == STORAGE_VERSION) {
        return;
    }
#ifdef DEBUG
    Serial2.println("Migrating profiles to storage version 2.");
#endif

    // New slots are larger than old ones, so convert from the last slot down: new slot i
    // starts at or after the end of old slot i, so unconverted old slots are never overwritten.
    for (int16_t i = MAX_PROFILES - 1; i >= 0; i--) {
        ButtonMappingV1 oldProfile;
        EEPROM.get(EEPROM_BASE_ADDR + (i * sizeof(ButtonMappingV1)), oldProfile);

        ButtonMapping profile = {};
        if (oldProfile.name[0] != '\0' && (uint8_t)oldProfile.name[0] != 0xFF) {
            memcpy(&profile, &oldProfile, sizeof(ButtonMappingV1));
            profile.name[sizeof(profile.name) - 1] = '\0';
        }
        EEPROM.put(EEPROM_BASE_ADDR + (i * PROFILE_SIZE), profile);
    }

    EEPROM.write(STORAGE_VERSION_ADDR, STORAGE_VERSION);
    EEPROM.commit();
}

int16_t findProfileByGameName(const char* gameName) {
    /**
     * Find the first used profile whose gameName equals the given name.
     *
     * @param gameName Null-terminated game name to search for.
     * @return Profile slot index, or -1 if no profile matches (or gameName is empty or too long).
     */
    size_t length = strlen(gameName);
    if (length == 0 || length >= sizeof(ButtonMapping::gameName)) {
        return -1;
    }

    for (uint8_t i = 0; i < MAX_PROFILES; i++) {
        ButtonMapping profile;
        loadProfile(i, &profile);
        if (profile.name[0] != '\0' && strncmp(profile.gameName, gameName, sizeof(profile.gameName)) == 0) {
            return i;
        }
    }
    return -1;
}

void reloadAllProfiles() {
    /**
     * Signal the menu system to reload profiles from storage.
     *
     * Requests a display redraw to refresh the profile list after
     * profiles have been updated via serial commands.
     */
    requestRedraw();
}
