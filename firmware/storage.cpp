#include "storage.h"

#include "menu.h"

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
    EEPROM.commit();
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
