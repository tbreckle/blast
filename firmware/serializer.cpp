#include "serializer.h"

#include "storage.h"

// RP2040 bootloader reboot flag.
#ifndef REBOOT_BOOTSEL
#define REBOOT_BOOTSEL 0x00000007
#endif

// CRC-16-CCITT calculation.
uint16_t calculateCRC16(const uint8_t* data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// SLIP encoding: escape special bytes.
uint16_t slipEncode(const uint8_t* input, uint16_t inputLen, uint8_t* output, uint16_t outputMaxLen) {
    uint16_t outIdx = 0;

    // Start with END byte.
    if (outputMaxLen < outIdx + 1) return 0;
    output[outIdx++] = SLIP_END;

    // Encode each byte.
    for (uint16_t i = 0; i < inputLen; i++) {
        if (outIdx >= outputMaxLen) return 0;

        if (input[i] == SLIP_END) {
            if (outIdx + 1 >= outputMaxLen) return 0;
            output[outIdx++] = SLIP_ESC;
            output[outIdx++] = SLIP_ESC_END;
        } else if (input[i] == SLIP_ESC) {
            if (outIdx + 1 >= outputMaxLen) return 0;
            output[outIdx++] = SLIP_ESC;
            output[outIdx++] = SLIP_ESC_ESC;
        } else {
            output[outIdx++] = input[i];
        }
    }

    // End with END byte.
    if (outIdx >= outputMaxLen) return 0;
    output[outIdx++] = SLIP_END;

    return outIdx;
}

// SLIP decoding: unescape special bytes.
uint16_t slipDecode(const uint8_t* input, uint16_t inputLen, uint8_t* output, uint16_t outputMaxLen) {
    uint16_t outIdx = 0;
    bool escapeNext = false;

    for (uint16_t i = 0; i < inputLen; i++) {
        if (input[i] == SLIP_END) {
            continue;  // Skip frame delimiters.
        } else if (input[i] == SLIP_ESC) {
            escapeNext = true;
        } else if (escapeNext) {
            if (outIdx >= outputMaxLen) return 0;
            if (input[i] == SLIP_ESC_END) {
                output[outIdx++] = SLIP_END;
            } else if (input[i] == SLIP_ESC_ESC) {
                output[outIdx++] = SLIP_ESC;
            }
            escapeNext = false;
        } else {
            if (outIdx >= outputMaxLen) return 0;
            output[outIdx++] = input[i];
        }
    }

    return outIdx;
}

// Send packet with SLIP framing.
bool sendPacket(const Packet* packet) {
    uint8_t rawBuffer[MAX_PACKET_SIZE];
    uint16_t rawIdx = 0;

#ifdef DEBUG_SLIP
    Serial2.print("[SLIP] Sending command: 0x");
    Serial2.print(packet->command, HEX);
    Serial2.print(", payload length: ");
    Serial2.println(packet->length);
#endif

    // Build raw packet: [CMD][LENGTH_LOW][LENGTH_HIGH][PAYLOAD][CRC_LOW][CRC_HIGH]
    rawBuffer[rawIdx++] = packet->command;
    rawBuffer[rawIdx++] = packet->length & 0xFF;
    rawBuffer[rawIdx++] = (packet->length >> 8) & 0xFF;

    for (uint16_t i = 0; i < packet->length; i++) {
        rawBuffer[rawIdx++] = packet->payload[i];
    }

    // Calculate CRC over command + length + payload.
    uint16_t crc = calculateCRC16(rawBuffer, rawIdx);
    rawBuffer[rawIdx++] = crc & 0xFF;
    rawBuffer[rawIdx++] = (crc >> 8) & 0xFF;

    // SLIP encode
    uint8_t encodedBuffer[MAX_PACKET_SIZE * 2];
    uint16_t encodedLen = slipEncode(rawBuffer, rawIdx, encodedBuffer, sizeof(encodedBuffer));

    if (encodedLen == 0) return false;

    // Send over serial
    Serial.write(encodedBuffer, encodedLen);
    Serial.flush();

    return true;
}

// Receive packet with SLIP framing
bool receivePacket(Packet* packet, uint32_t timeoutMs) {
    uint8_t encodedBuffer[MAX_PACKET_SIZE * 2];
    uint16_t encodedIdx = 0;
    uint32_t startTime = millis();
    bool inFrame = false;

#ifdef DEBUG_SLIP
    Serial2.println("[SLIP] receivePacket: waiting for data...");
#endif

    // Read until we get a complete SLIP frame
    while (millis() - startTime < timeoutMs) {
        if (Serial.available()) {
            uint8_t b = Serial.read();

#ifdef DEBUG_SLIP
            Serial2.print("[SLIP] RX: 0x");
            Serial2.println(b, HEX);
#endif

            if (b == SLIP_END) {
                if (inFrame && encodedIdx > 0) {
                    // Frame complete
                    encodedBuffer[encodedIdx++] = b;
                    break;
                } else {
                    // Start of new frame
                    inFrame = true;
                    encodedIdx = 0;
                    encodedBuffer[encodedIdx++] = b;
                }
            } else if (inFrame) {
                if (encodedIdx < sizeof(encodedBuffer)) {
                    encodedBuffer[encodedIdx++] = b;
                } else {
                    // Buffer overflow.
                    return false;
                }
            }
        }
    }

    if (encodedIdx == 0) {
#ifdef DEBUG_SLIP
        Serial2.println("[SLIP] Timeout: no data received");
#endif
        return false;
    }

#ifdef DEBUG_SLIP
    Serial2.print("[SLIP] Frame complete, encoded length: ");
    Serial2.println(encodedIdx);
#endif

    // SLIP decode.
    uint8_t decodedBuffer[MAX_PACKET_SIZE];
    uint16_t decodedLen = slipDecode(encodedBuffer, encodedIdx, decodedBuffer, sizeof(decodedBuffer));

#ifdef DEBUG_SLIP
    Serial2.print("[SLIP] Decoded length: ");
    Serial2.println(decodedLen);
#endif

    // Minimum valid frame: CMD(1) + LEN(2) + CRC(2) = 5 bytes (zero-length payload allowed)
    if (decodedLen < 5) {
#ifdef DEBUG_SLIP
        Serial1.println("[SLIP] Frame too short");
#endif
        return false;
    }

    // Parse packet.
    uint16_t idx = 0;
    packet->command = decodedBuffer[idx++];
    packet->length = decodedBuffer[idx++];
    packet->length |= (uint16_t)decodedBuffer[idx++] << 8;

    if (packet->length > MAX_PAYLOAD_SIZE) return false;

    for (uint16_t i = 0; i < packet->length; i++) {
        packet->payload[i] = decodedBuffer[idx++];
    }

    uint16_t receivedCRC = decodedBuffer[idx++];
    receivedCRC |= (uint16_t)decodedBuffer[idx++] << 8;

    // Verify CRC.
    uint16_t calculatedCRC = calculateCRC16(decodedBuffer, idx - 2);
    if (calculatedCRC != receivedCRC) {
#ifdef DEBUG_SLIP
        Serial2.print("[SLIP] CRC mismatch! Received: 0x");
        Serial2.print(receivedCRC, HEX);
        Serial2.print(", Calculated: 0x");
        Serial2.println(calculatedCRC, HEX);
#endif
        sendError(ERR_INVALID_CRC);
        return false;
    }

#ifdef DEBUG_SLIP
    Serial2.println("[SLIP] Packet valid!");
#endif

    packet->crc = receivedCRC;
    return true;
}

// Send response packet.
void sendResponse(uint8_t command, const uint8_t* data, uint16_t dataLen) {
    Packet response;
    response.command = command;
    response.length = dataLen;

    if (dataLen > 0 && data != nullptr) {
        memcpy(response.payload, data, dataLen);
    }

    sendPacket(&response);
}

// Send error response.
void sendError(uint8_t errorCode) {
    const uint8_t errorData[1] = {errorCode};
    sendResponse(CMD_RESPONSE_ERROR, errorData, 1);
}

// Command handler: Get firmware version.
void handleGetVersion() {
    const uint8_t versionData[3] = {globalSettings.versionMajor, globalSettings.versionMinor,
                                    globalSettings.versionPatch};
    sendResponse(CMD_RESPONSE_OK, versionData, 3);
}

// Command handler: Get settings.
void handleGetSettings() {
    sendResponse(CMD_RESPONSE_OK, reinterpret_cast<uint8_t*>(&globalSettings), sizeof(FirmwareSettings));
}

// Command handler: Set settings.
void handleSetSettings(const uint8_t* payload, uint16_t length) {
    if (length != sizeof(FirmwareSettings)) {
        sendError(ERR_INVALID_LENGTH);
        return;
    }

    memcpy(&globalSettings, payload, sizeof(FirmwareSettings));
    sendResponse(CMD_RESPONSE_OK, nullptr, 0);
}

// Command handler: Get profile.
void handleGetProfile(const uint8_t* payload, uint16_t length) {
    if (length != 1) {
        sendError(ERR_INVALID_LENGTH);
        return;
    }

    uint8_t profileIndex = payload[0];
    if (profileIndex >= MAX_PROFILES) {
        sendError(ERR_INVALID_INDEX);
        return;
    }

    ButtonMapping profile;
    loadProfile(profileIndex, &profile);
    sendResponse(CMD_RESPONSE_OK, reinterpret_cast<uint8_t*>(&profile), sizeof(ButtonMapping));
}

// Command handler: Set profile.
void handleSetProfile(const uint8_t* payload, uint16_t length) {
    if (length != sizeof(ButtonMapping) + 1) {
        sendError(ERR_INVALID_LENGTH);
        return;
    }

    uint8_t profileIndex = payload[0];
    if (profileIndex >= MAX_PROFILES) {
        sendError(ERR_INVALID_INDEX);
        return;
    }

    ButtonMapping profile;
    memcpy(&profile, payload + 1, sizeof(ButtonMapping));

    // Store in RAM but don't save to EEPROM yet.
    // (Use SAVE_PROFILE command to persist.)
    sendResponse(CMD_RESPONSE_OK, nullptr, 0);
}

// Command handler: Save profile to EEPROM.
void handleSaveProfile(const uint8_t* payload, uint16_t length) {
    if (length != sizeof(ButtonMapping) + 1) {
        sendError(ERR_INVALID_LENGTH);
        return;
    }

    uint8_t profileIndex = payload[0];
    if (profileIndex >= MAX_PROFILES) {
        sendError(ERR_INVALID_INDEX);
        return;
    }

    ButtonMapping profile;
    memcpy(&profile, payload + 1, sizeof(ButtonMapping));
    saveProfile(profileIndex, &profile);

    // Reload profiles to update menu display.
    reloadAllProfiles();

    sendResponse(CMD_RESPONSE_OK, nullptr, 0);
}

// Command handler: Get profile count.
void handleGetProfileCount() {
    uint8_t count = getNumberOfProfiles();
    sendResponse(CMD_RESPONSE_OK, &count, 1);
}

// Command handler: Get max profiles.
void handleGetMaxProfiles() {
    uint8_t maxProfiles = MAX_PROFILES;
    sendResponse(CMD_RESPONSE_OK, &maxProfiles, 1);
}

// Main command processor.
void processSerialCommand() {
    if (!Serial.available()) return;

#ifdef DEBUG_SLIP
    Serial2.print("[SLIP] Serial available: ");
    Serial2.println(Serial.available());
#endif

    Packet packet;
    if (!receivePacket(&packet, 1000)) {
#ifdef DEBUG_SLIP
        Serial2.println("[SLIP] receivePacket failed or timeout.");
#endif
        return;  // Timeout or error (error already sent if CRC failed).
    }

#ifdef DEBUG_SLIP
    Serial2.print("[SLIP] Command received: 0x");
    Serial2.print(packet.command, HEX);
    Serial2.print(", Length: ");
    Serial2.println(packet.length);
#endif

    // Dispatch command.
    switch (packet.command) {
        case CMD_GET_VERSION:
            handleGetVersion();
            break;

        case CMD_GET_SETTINGS:
            handleGetSettings();
            break;

        case CMD_SET_SETTINGS:
            handleSetSettings(packet.payload, packet.length);
            break;

        case CMD_GET_PROFILE:
            handleGetProfile(packet.payload, packet.length);
            break;

        case CMD_SET_PROFILE:
            handleSetProfile(packet.payload, packet.length);
            break;

        case CMD_SAVE_PROFILE:
            handleSaveProfile(packet.payload, packet.length);
            break;

        case CMD_GET_PROFILE_COUNT:
            handleGetProfileCount();
            break;

        case CMD_GET_MAX_PROFILES:
            handleGetMaxProfiles();
            break;

        case CMD_REBOOT_FLASH:
            handleRebootFlash();
            break;

        default:
            sendError(ERR_INVALID_COMMAND);
            break;
    }
}

void handleRebootFlash() {
#ifdef DEBUG_SLIP
    Serial2.println("[SLIP] Rebooting device...");
#endif
    sendResponse(CMD_RESPONSE_OK, nullptr, 0);
    delay(200);  // Allow time for response to be sent.

    // Simply reboot. User must hold BOOTSEL button during reboot to enter bootloader mode.
    rp2040.reboot();
}
