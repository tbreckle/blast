#pragma once
#include <EEPROM.h>

#include "keycodes.h"

#define EEPROM_SIZE 4096                        // Size of the emulated EEPROM in bytes.
#define EEPROM_BASE_ADDR 0                      // Starting address for profiles in EEPROM.
#define MAX_PROFILES 50                         // Maximum number of profiles.
#define PROFILE_SIZE sizeof(ButtonMapping)      // Size of each profile in bytes.
#define STORAGE_VERSION_ADDR (EEPROM_SIZE - 1)  // Address of the profile layout version byte.
#define STORAGE_VERSION 2                       // Current profile layout version (2 = with gameName).

static_assert(EEPROM_BASE_ADDR + MAX_PROFILES * PROFILE_SIZE <= STORAGE_VERSION_ADDR,
              "Profiles do not fit into EEPROM.");

// Function declarations.
void saveProfile(uint8_t index, const ButtonMapping* profile);
void loadProfile(uint8_t index, ButtonMapping* profile);
uint8_t getNumberOfProfiles();
bool isProfileSlotUsed(uint8_t index);
void intializeStorage();
void migrateStorage();
int16_t findProfileByGameName(const char* gameName);
void reloadAllProfiles();
