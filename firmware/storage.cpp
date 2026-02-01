#include "storage.h"

#include "menu.h"

// Write profile to EEPROM
void saveProfile(uint8_t index, const ButtonMapping* profile) {
    uint16_t addr = EEPROM_BASE_ADDR + (index * PROFILE_SIZE);
    EEPROM.put(addr, *profile);
    EEPROM.commit();
}

// Read profile from EEPROM
void loadProfile(uint8_t index, ButtonMapping* profile) {
    uint16_t addr = EEPROM_BASE_ADDR + (index * PROFILE_SIZE);
    EEPROM.get(addr, *profile);
}

uint8_t getNumberOfProfiles() {
    // Cycle through all profiles and count non-empty ones.
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_PROFILES; i++) {
        if (isProfileSlotUsed(i)) {
            count++;
        }
    }
    return count;
}

bool isProfileSlotUsed(uint8_t index) {
    ButtonMapping temp;
    loadProfile(index, &temp);
    return (temp.name[0] != '\0');
}

void intializeStorage() {
    // Set all profiles to empty on first run.
    for (uint8_t i = 0; i < MAX_PROFILES; i++) {
        ButtonMapping emptyProfile = {};
        saveProfile(i, &emptyProfile);
    }
    EEPROM.commit();
}

void reloadAllProfiles() {
    // Signal the menu system to reload profiles from storage.
    // This will refresh the profile list display after profiles have been updated.
    requestRedraw();
}
