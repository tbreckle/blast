#pragma once
#include <Adafruit_SSD1306.h>

#include "_debug.h"
#include "storage.h"

// Menu states.
enum MenuState { STATE_NONE, STATE_SPLASH, STATE_SELECT, STATE_PROFILE, STATE_SERVICEMENU };

// Profile menu entries.
enum ProfileMenuItem { PROFILE_MENU_BACK = 0, PROFILE_MENU_EXIT = 1, PROFILE_MENU_SERVICE = 2, PROFILE_MENU_COUNT = 3 };

// External references to objects and variables defined in main file.
extern Adafruit_SSD1306 display;
extern const uint8_t SCREEN_WIDTH;
extern const uint8_t SCREEN_HEIGHT;
extern const uint8_t MAX_VISIBLE_PROFILES;
extern MenuState currentMenuState;
extern uint8_t selectedProfileIndex;
extern uint8_t selectedProfileMenuItem;
extern uint8_t currentProfileIndex;
extern ButtonMapping currentProfile;
extern bool redrawDisplay;
extern uint32_t splashStartTime;

// Forward declaration for handleButtonPress (defined in main file).
// channel: MCP channel (0-15) for hold tracking; 255 = no channel (fixed minimum-duration press).
void handleButtonPress(KeyCombo* key, uint8_t channel = 255);

// Function declarations.
void displayProfileSelect();
void displayProfileMenu();
void displayServiceMenu();
void requestRedraw();
void setMenuState(MenuState newState);
void updateDisplay();
void handleMenuNavigation(bool up, bool right, bool down, bool left, bool enter);
