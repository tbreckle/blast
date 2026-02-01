#pragma once
#include <EEPROM.h>

#include "keycodes.h"

#define EEPROM_BASE_ADDR 0
#define MAX_PROFILES 50
#define PROFILE_SIZE sizeof(ButtonMapping)

// Function declarations.
void saveProfile(uint8_t index, const ButtonMapping* profile);
void loadProfile(uint8_t index, ButtonMapping* profile);
uint8_t getNumberOfProfiles();
bool isProfileSlotUsed(uint8_t index);
void intializeStorage();
void reloadAllProfiles();
