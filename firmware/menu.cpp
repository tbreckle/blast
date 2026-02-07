#include "menu.h"

void displayProfileSelect() {
    /**
     * Display the profile selection screen.
     *
     * Shows a scrollable list of available profiles with the selected profile highlighted.
     * Displays up to MAX_VISIBLE_PROFILES at a time with the selection centered when possible.
     */
#ifdef DEBUG
    Serial2.println("Displaying profile selection menu.");
#endif
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Top bar with title.
    display.fillRect(0, 0, SCREEN_WIDTH, 10, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(2, 2);
    display.println("SELECT PROFILE");
    display.setTextColor(SSD1306_WHITE);

    // Collect all used profiles.
    uint8_t usedProfiles[MAX_PROFILES];
    uint8_t usedProfileCount = 0;
    uint8_t selectedSlotIndex = 0;

    for (uint8_t i = 0; i < getNumberOfProfiles(); i++) {
        if (isProfileSlotUsed(i)) {
            usedProfiles[usedProfileCount] = i;
            if (i == selectedProfileIndex) {
                selectedSlotIndex = usedProfileCount;
            }
            usedProfileCount++;
        }
    }

    if (usedProfileCount == 0) {
        display.setCursor(4, 20);
        display.println("No profiles found.");
        display.display();
        return;
    }

    // Calculate which profiles to display.
    // selectedSlotIndex should always be in the middle if possible.
    uint8_t middleIndex = MAX_VISIBLE_PROFILES / 2;
    int8_t startIndex = selectedSlotIndex - middleIndex;
    int8_t endIndex = startIndex + MAX_VISIBLE_PROFILES;

    // Adjust for boundaries.
    if (startIndex < 0) {
        startIndex = 0;
        endIndex = min(MAX_VISIBLE_PROFILES, usedProfileCount);
    }
    if (endIndex > usedProfileCount) {
        endIndex = usedProfileCount;
        startIndex = max(0, endIndex - MAX_VISIBLE_PROFILES);
    }

    // Draw profiles.
    uint8_t yPos = 15;
    for (int8_t i = startIndex; i < endIndex; i++) {
        uint8_t profileIndex = usedProfiles[i];
        ButtonMapping profile;
        loadProfile(profileIndex, &profile);

        // Highlight the selected profile.
        if (profileIndex == selectedProfileIndex) {
            display.fillRect(0, yPos - 2, SCREEN_WIDTH, 9, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK);
        } else {
            display.setTextColor(SSD1306_WHITE);
        }

        display.setCursor(4, yPos);
        display.println(profile.name);
        display.setTextColor(SSD1306_WHITE);

        yPos += 12;
    }

    display.display();
}

void displayProfileMenu() {
    /**
     * Display the profile configuration menu.
     *
     * Shows menu options: BACK (return to selection), EXIT (exit to game), and SERVICE (test menu).
     * The currently selected menu item is highlighted.
     */
#ifdef DEBUG
    Serial2.println("Displaying profile menu.");
#endif
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Top bar with profile name.
    display.fillRect(0, 0, SCREEN_WIDTH, 10, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(2, 2);
    display.println(currentProfile.name);
    display.setTextColor(SSD1306_WHITE);

    // Menu items.
    const char* menuItems[] = {"< BACK", "EXIT", "SERVICE"};
    uint8_t yPos = 20;

    for (uint8_t i = 0; i < PROFILE_MENU_COUNT; i++) {
        if (i == selectedProfileMenuItem) {
            display.fillRect(0, yPos - 2, SCREEN_WIDTH, 9, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK);
        } else {
            display.setTextColor(SSD1306_WHITE);
        }

        display.setCursor(4, yPos);
        display.println(menuItems[i]);
        display.setTextColor(SSD1306_WHITE);

        yPos += 12;
    }

    display.display();
}

void displayServiceMenu() {
    /**
     * Display the service/test menu.
     *
     * Shows a directional layout for service and test buttons:
     * - North: Service P2
     * - East: Test P1
     * - South: Service P1
     * - West: Test P2
     */
#ifdef DEBUG
    Serial2.println("Displaying service menu.");
#endif
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Top bar with title.
    display.fillRect(0, 0, SCREEN_WIDTH, 10, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(2, 2);
    display.println("SERVICE MENU");
    display.setTextColor(SSD1306_WHITE);

    // Center point for the display.
    uint8_t centerX = SCREEN_WIDTH / 2;
    uint8_t centerY = SCREEN_HEIGHT / 2 + 5;

    // North - Service P2.
    display.setCursor(centerX - 30, 15);
    display.println("Service P2");

    // East - Test P1.
    display.setCursor(centerX + 20, centerY - 4);
    display.println("Test P1");

    // South - Service P1.
    display.setCursor(centerX - 30, SCREEN_HEIGHT - 10);
    display.println("Service P1");

    // West - Test P2.
    display.setCursor(5, centerY - 4);
    display.println("Test P2");

    display.display();
}

void requestRedraw() {
    /**
     * Request a display redraw on the next update cycle.
     *
     * Sets the redraw flag to trigger screen refresh in updateDisplay().
     */
#ifdef DEBUG
    Serial2.println("Redraw requested.");
#endif
    redrawDisplay = true;
}

void setMenuState(MenuState newState) {
    /**
     * Change the current menu state and request a redraw.
     *
     * @param newState The new menu state to transition to.
     */
#ifdef DEBUG
    Serial2.print("Changing menu state to ");
    Serial2.println(newState);
#endif
    currentMenuState = newState;
    requestRedraw();
}

void updateDisplay() {
    /**
     * Update the display based on the current menu state.
     *
     * Handles splash screen timeout and redraws the appropriate screen
     * when the redraw flag is set.
     */
    if (currentMenuState == STATE_SPLASH) {
        if (millis() - splashStartTime > 2000) {
            setMenuState(STATE_SELECT);
            selectedProfileIndex = 0;
        }
    } else {
        // Check if redraw is requested.
        if (redrawDisplay) {
            redrawDisplay = false;
            if (currentMenuState == STATE_SELECT) {
                displayProfileSelect();
            } else if (currentMenuState == STATE_PROFILE) {
                displayProfileMenu();
            } else if (currentMenuState == STATE_SERVICEMENU) {
                displayServiceMenu();
            }
        }
    }
}

void handleMenuNavigation(bool up, bool right, bool down, bool left, bool enter) {
    /**
     * Handle navigation inputs for the menu system.
     *
     * @param up Navigation up pressed.
     * @param right Navigation right pressed.
     * @param down Navigation down pressed.
     * @param left Navigation left pressed.
     * @param enter Enter/Select pressed.
     *
     * Processes navigation differently based on current menu state.
     */
    if (currentMenuState == STATE_SELECT) {
        if (up) {
            if (selectedProfileIndex > 0 && isProfileSlotUsed(selectedProfileIndex - 1)) {
                selectedProfileIndex--;
                requestRedraw();
            }
        } else if (down) {
            if (selectedProfileIndex < MAX_PROFILES - 1 && isProfileSlotUsed(selectedProfileIndex + 1)) {
                selectedProfileIndex++;
                requestRedraw();
            }
        } else if (enter) {
            loadProfile(selectedProfileIndex, &currentProfile);
            setMenuState(STATE_PROFILE);
            selectedProfileMenuItem = 0;
        }
    } else if (currentMenuState == STATE_PROFILE) {
        if (up) {
            if (selectedProfileMenuItem > 0) {
                selectedProfileMenuItem--;
                requestRedraw();
            } else {
                selectedProfileMenuItem = PROFILE_MENU_COUNT - 1;
                requestRedraw();
            }
        } else if (down) {
            if (selectedProfileMenuItem < PROFILE_MENU_COUNT - 1) {
                selectedProfileMenuItem++;
                requestRedraw();
            } else {
                selectedProfileMenuItem = 0;
                requestRedraw();
            }
        } else if (enter) {
            if (selectedProfileMenuItem == PROFILE_MENU_BACK) {
                setMenuState(STATE_SELECT);
                selectedProfileIndex = currentProfileIndex;
            } else if (selectedProfileMenuItem == PROFILE_MENU_SERVICE) {
                setMenuState(STATE_SERVICEMENU);
            } else if (selectedProfileMenuItem == PROFILE_MENU_EXIT) {
                handleButtonPress(&currentProfile.exit);
            }
        }
    } else if (currentMenuState == STATE_SERVICEMENU) {
        if (up) {
            handleButtonPress(&currentProfile.serviceP2);
        } else if (right) {
            handleButtonPress(&currentProfile.testP1);
        } else if (down) {
            handleButtonPress(&currentProfile.serviceP1);
        } else if (left) {
            handleButtonPress(&currentProfile.testP2);
        } else if (enter) {
            setMenuState(STATE_PROFILE);
        }
    }
}
