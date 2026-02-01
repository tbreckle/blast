#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <string.h>

#include "_debug.h"
#include "keycodes.h"

// Firmware settings structure.
typedef struct {
        uint8_t versionMajor;
        uint8_t versionMinor;
        uint8_t versionPatch;
        uint16_t keyPressDurationMs;  // Duration for key press simulation in milliseconds.
} FirmwareSettings;

/*
 * SLIP Protocol Configuration Interface
 * =====================================
 *
 * This module implements a SLIP-framed protocol for configuration exchange
 * between the firmware and a host PC application.
 *
 * Packet Format: [SLIP_END] [CMD] [LEN_L] [LEN_H] [PAYLOAD] [CRC_L] [CRC_H] [SLIP_END]
 *
 * Usage in firmware:
 *   In your main loop, call processSerialCommand() to handle incoming commands.
 *
 * Supported Operations:
 *   - Read firmware version (GET_VERSION)
 *   - Read/Write firmware settings (GET_SETTINGS, SET_SETTINGS)
 *   - Read/Write profiles (GET_PROFILE, SET_PROFILE, SAVE_PROFILE)
 *   - Query profile count (GET_PROFILE_COUNT)
 */

// SLIP Protocol constants.
#define SLIP_END 0xC0      // Frame delimiter..
#define SLIP_ESC 0xDB      // Escape byte.
#define SLIP_ESC_END 0xDC  // Escaped END.
#define SLIP_ESC_ESC 0xDD  // Escaped ESC.

// Protocol commands.
#define CMD_GET_VERSION 0x01
#define CMD_GET_SETTINGS 0x02
#define CMD_SET_SETTINGS 0x03
#define CMD_GET_PROFILE 0x04
#define CMD_SET_PROFILE 0x05
#define CMD_GET_PROFILE_COUNT 0x06
#define CMD_SAVE_PROFILE 0x07
#define CMD_GET_MAX_PROFILES 0x08
#define CMD_REBOOT_FLASH 0x09
#define CMD_RESPONSE_OK 0x10
#define CMD_RESPONSE_ERROR 0x11

// Error codes.
#define ERR_NONE 0x00
#define ERR_INVALID_COMMAND 0x01
#define ERR_INVALID_LENGTH 0x02
#define ERR_INVALID_CRC 0x03
#define ERR_INVALID_INDEX 0x04
#define ERR_TIMEOUT 0x05

// Buffer sizes.
#define MAX_PACKET_SIZE 256
#define MAX_PAYLOAD_SIZE 200

// Packet structure: [CMD(1)] [LENGTH(2)] [PAYLOAD(N)] [CRC16(2)].
typedef struct {
        uint8_t command;
        uint16_t length;
        uint8_t payload[MAX_PAYLOAD_SIZE];
        uint16_t crc;
} Packet;

// External settings instance (defined in firmware.ino).
extern FirmwareSettings globalSettings;

// SLIP Protocol functions.
uint16_t calculateCRC16(const uint8_t* data, uint16_t length);
uint16_t slipEncode(const uint8_t* input, uint16_t inputLen, uint8_t* output, uint16_t outputMaxLen);
uint16_t slipDecode(const uint8_t* input, uint16_t inputLen, uint8_t* output, uint16_t outputMaxLen);
bool sendPacket(const Packet* packet);
bool receivePacket(Packet* packet, uint32_t timeoutMs);
void sendResponse(uint8_t command, const uint8_t* data, uint16_t dataLen);
void sendError(uint8_t errorCode);

// Command handlers.
void handleGetVersion();
void handleGetSettings();
void handleSetSettings(const uint8_t* payload, uint16_t length);
void handleGetProfile(const uint8_t* payload, uint16_t length);
void handleSetProfile(const uint8_t* payload, uint16_t length);
void handleGetProfileCount();
void handleSaveProfile(const uint8_t* payload, uint16_t length);
void handleRebootFlash();

// Main command processor.
void processSerialCommand();
