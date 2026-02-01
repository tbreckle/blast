use anyhow::{Context, Result};
use serialport::SerialPort;
use std::io::{Read, Write};
use std::time::Duration;

use crate::slip;
use crate::types::{ButtonMapping, FirmwareVersion};

// Command codes matching firmware SLIP protocol
pub const CMD_GET_VERSION: u8 = 0x01;
#[allow(dead_code)]
pub const CMD_GET_SETTINGS: u8 = 0x02;
#[allow(dead_code)]
pub const CMD_SET_SETTINGS: u8 = 0x03;
pub const CMD_GET_PROFILE: u8 = 0x04;
pub const CMD_SET_PROFILE: u8 = 0x05;
#[allow(dead_code)]
pub const CMD_GET_PROFILE_COUNT: u8 = 0x06;
pub const CMD_SAVE_PROFILE: u8 = 0x07;
pub const CMD_GET_MAX_PROFILES: u8 = 0x08;
pub const CMD_REBOOT_FLASH: u8 = 0x09;
pub const CMD_RESPONSE_OK: u8 = 0x10;
pub const CMD_RESPONSE_ERROR: u8 = 0x11;

/// Calculate CRC-16-CCITT
fn calculate_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &byte in data {
        crc ^= (byte as u16) << 8;
        for _ in 0..8 {
            if crc & 0x8000 != 0 {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    crc
}

/// Protocol frame structure
pub struct Frame {
    pub command: u8,
    pub payload: Vec<u8>,
}

impl Frame {
    pub fn new(command: u8, payload: Vec<u8>) -> Self {
        Self { command, payload }
    }

    /// Encode frame to bytes (command + length + payload + CRC16)
    pub fn encode(&self) -> Vec<u8> {
        let mut data = Vec::new();
        data.push(self.command);

        let len = self.payload.len() as u16;
        data.extend_from_slice(&len.to_le_bytes());

        data.extend_from_slice(&self.payload);

        // CRC-16-CCITT calculation
        let crc = calculate_crc16(&data);
        data.extend_from_slice(&crc.to_le_bytes());

        data
    }

    /// Decode frame from bytes
    pub fn decode(data: &[u8]) -> Result<Self> {
        if data.len() < 5 {
            anyhow::bail!("Frame too short");
        }

        let command = data[0];
        let length = u16::from_le_bytes([data[1], data[2]]) as usize;

        if data.len() < 3 + length + 2 {
            anyhow::bail!("Incomplete frame");
        }

        let payload = data[3..3 + length].to_vec();
        let received_crc = u16::from_le_bytes([data[3 + length], data[3 + length + 1]]);

        // Verify CRC
        let calculated_crc = calculate_crc16(&data[..3 + length]);
        if received_crc != calculated_crc {
            anyhow::bail!(
                "CRC mismatch: received 0x{:04X}, calculated 0x{:04X}",
                received_crc,
                calculated_crc
            );
        }

        Ok(Self { command, payload })
    }
}

pub struct FirmwareConnection {
    port: Box<dyn SerialPort>,
    recv_buffer: Vec<u8>,
}

impl FirmwareConnection {
    pub fn new(port: Box<dyn SerialPort>) -> Self {
        Self {
            port,
            recv_buffer: Vec::new(),
        }
    }

    /// Send a frame using SLIP encoding
    pub fn send_frame(&mut self, frame: &Frame) -> Result<()> {
        let frame_data = frame.encode();
        println!("[TX] Frame data: {:02X?}", frame_data);
        println!(
            "[TX] Command: 0x{:02X}, Payload len: {}",
            frame.command,
            frame.payload.len()
        );
        if !frame.payload.is_empty() {
            println!("[TX] Payload: {:02X?}", frame.payload);
        }

        let mut slip_encoded = Vec::new();
        slip::encode(&frame_data, &mut slip_encoded);
        println!(
            "[TX] SLIP encoded ({} bytes): {:02X?}",
            slip_encoded.len(),
            slip_encoded
        );

        self.port
            .write_all(&slip_encoded)
            .context("Failed to write to serial port")?;
        self.port.flush().context("Failed to flush serial port")?;
        println!("[TX] Sent successfully\n");

        Ok(())
    }

    /// Receive a frame using SLIP decoding
    pub fn receive_frame(&mut self, timeout: Duration) -> Result<Frame> {
        let start = std::time::Instant::now();
        println!("[RX] Waiting for frame (timeout: {:?})...", timeout);

        loop {
            // Try to decode existing buffer
            if !self.recv_buffer.is_empty() {
                match slip::decode(&self.recv_buffer) {
                    Ok((decoded, consumed)) => {
                        println!(
                            "[RX] SLIP decoded ({} bytes consumed): {:02X?}",
                            consumed, decoded
                        );
                        self.recv_buffer.drain(..consumed);
                        let frame = Frame::decode(&decoded)?;
                        println!(
                            "[RX] Command: 0x{:02X}, Payload len: {}",
                            frame.command,
                            frame.payload.len()
                        );
                        if !frame.payload.is_empty() {
                            println!("[RX] Payload: {:02X?}", frame.payload);
                        }
                        println!("[RX] Frame received successfully\n");
                        return Ok(frame);
                    }
                    Err(slip::SlipError::IncompleteFrame) => {
                        // Need more data, continue reading
                    }
                    Err(e) => {
                        anyhow::bail!("SLIP decode error: {}", e);
                    }
                }
            }

            // Check timeout
            if start.elapsed() >= timeout {
                anyhow::bail!("Timeout waiting for response");
            }

            // Read more data
            let mut buf = [0u8; 256];
            match self.port.read(&mut buf) {
                Ok(n) if n > 0 => {
                    println!("[RX] Read {} bytes from serial: {:02X?}", n, &buf[..n]);
                    self.recv_buffer.extend_from_slice(&buf[..n]);
                    println!("[RX] Buffer now {} bytes", self.recv_buffer.len());
                }
                Ok(_) => {
                    std::thread::sleep(Duration::from_millis(10));
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => {
                    std::thread::sleep(Duration::from_millis(10));
                }
                Err(e) => {
                    return Err(e).context("Failed to read from serial port");
                }
            }
        }
    }

    /// Get firmware version
    pub fn get_version(&mut self) -> Result<FirmwareVersion> {
        println!("[CMD] Getting firmware version...");
        let frame = Frame::new(CMD_GET_VERSION, vec![]);
        self.send_frame(&frame)?;

        let response = self.receive_frame(Duration::from_secs(2))?;

        if response.command == CMD_RESPONSE_ERROR {
            let error_code = response.payload.first().copied().unwrap_or(0);
            anyhow::bail!("Firmware returned error: 0x{:02X}", error_code);
        }

        if response.command != CMD_RESPONSE_OK {
            anyhow::bail!("Unexpected response command: 0x{:02X}", response.command);
        }

        if response.payload.len() < 3 {
            anyhow::bail!("Invalid version response");
        }

        let version = FirmwareVersion {
            major: response.payload[0],
            minor: response.payload[1],
            patch: response.payload[2],
        };
        println!("[CMD] Version received: {}\n", version);
        Ok(version)
    }

    /// Get a specific profile by index
    pub fn get_profile(&mut self, index: u8) -> Result<ButtonMapping> {
        println!("[CMD] Getting profile {}...", index);
        let frame = Frame::new(CMD_GET_PROFILE, vec![index]);
        self.send_frame(&frame)?;

        let response = self.receive_frame(Duration::from_secs(2))?;

        if response.command == CMD_RESPONSE_ERROR {
            let error_code = response.payload.first().copied().unwrap_or(0);
            anyhow::bail!("Firmware returned error: 0x{:02X}", error_code);
        }

        if response.command != CMD_RESPONSE_OK {
            anyhow::bail!("Unexpected response command: 0x{:02X}", response.command);
        }

        if response.payload.len() < std::mem::size_of::<ButtonMapping>() {
            anyhow::bail!("Invalid profile response size");
        }

        // SAFETY: ButtonMapping is repr(C, packed) and matches firmware layout
        let profile =
            unsafe { std::ptr::read_unaligned(response.payload.as_ptr() as *const ButtonMapping) };
        println!("[CMD] Profile received: {}\n", profile.name_str());

        Ok(profile)
    }

    /// Get maximum number of profile slots
    pub fn get_max_profiles(&mut self) -> Result<u8> {
        println!("[CMD] Getting max profiles...");
        let frame = Frame::new(CMD_GET_MAX_PROFILES, vec![]);
        self.send_frame(&frame)?;

        let response = self.receive_frame(Duration::from_secs(2))?;

        if response.command == CMD_RESPONSE_ERROR {
            let error_code = response.payload.first().copied().unwrap_or(0);
            anyhow::bail!("Firmware returned error: 0x{:02X}", error_code);
        }

        if response.command != CMD_RESPONSE_OK {
            anyhow::bail!("Unexpected response command: 0x{:02X}", response.command);
        }

        if response.payload.is_empty() {
            anyhow::bail!("Invalid max profiles response");
        }

        let max_profiles = response.payload[0];
        println!("[CMD] Max profiles: {}\n", max_profiles);
        Ok(max_profiles)
    }

    /// Get all profiles
    pub fn get_all_profiles(&mut self) -> Result<Vec<ButtonMapping>> {
        // First query max profiles from firmware
        let max_profiles = self.get_max_profiles()?;
        let mut profiles = Vec::new();

        for i in 0..max_profiles {
            match self.get_profile(i) {
                Ok(profile) => {
                    // Check if profile name is empty (unused slot)
                    if profile.name[0] != 0 {
                        profiles.push(profile);
                    }
                }
                Err(e) => {
                    eprintln!("Warning: Failed to read profile {}: {}", i, e);
                    break;
                }
            }
        }

        Ok(profiles)
    }

    /// Set a profile
    #[allow(dead_code)]
    pub fn set_profile(&mut self, index: u8, profile: &ButtonMapping) -> Result<()> {
        println!(
            "[CMD] Setting profile {} to '{}'...",
            index,
            profile.name_str()
        );
        let mut payload = vec![index];

        // SAFETY: ButtonMapping is repr(C, packed) and can be transmuted to bytes
        let profile_bytes = unsafe {
            std::slice::from_raw_parts(
                profile as *const ButtonMapping as *const u8,
                std::mem::size_of::<ButtonMapping>(),
            )
        };
        payload.extend_from_slice(profile_bytes);
        println!(
            "[CMD] Profile data ({} bytes): {:02X?}",
            profile_bytes.len(),
            profile_bytes
        );

        let frame = Frame::new(CMD_SET_PROFILE, payload);
        self.send_frame(&frame)?;

        let response = self.receive_frame(Duration::from_secs(2))?;

        if response.command == CMD_RESPONSE_ERROR {
            let error_code = response.payload.first().copied().unwrap_or(0);
            anyhow::bail!("Firmware returned error: 0x{:02X}", error_code);
        }

        if response.command != CMD_RESPONSE_OK {
            anyhow::bail!("Unexpected response command: 0x{:02X}", response.command);
        }
        println!("[CMD] Profile set successfully\n");

        Ok(())
    }

    /// Save a profile to persistent storage (EEPROM)
    pub fn save_profile(&mut self, index: u8, profile: &ButtonMapping) -> Result<()> {
        println!("[CMD] Saving profile {} to persistent storage...", index);
        let mut payload = vec![index];

        // SAFETY: ButtonMapping is repr(C, packed) and can be transmuted to bytes
        let profile_bytes = unsafe {
            std::slice::from_raw_parts(
                profile as *const ButtonMapping as *const u8,
                std::mem::size_of::<ButtonMapping>(),
            )
        };
        payload.extend_from_slice(profile_bytes);

        let frame = Frame::new(CMD_SAVE_PROFILE, payload);
        self.send_frame(&frame)?;

        let response = self.receive_frame(Duration::from_secs(2))?;

        if response.command == CMD_RESPONSE_ERROR {
            let error_code = response.payload.first().copied().unwrap_or(0);
            anyhow::bail!("Firmware returned error: 0x{:02X}", error_code);
        }

        if response.command != CMD_RESPONSE_OK {
            anyhow::bail!("Unexpected response command: 0x{:02X}", response.command);
        }
        println!("[CMD] Profile saved successfully\n");

        Ok(())
    }

    /// Set a profile
    pub fn reboot_flash(&mut self) -> Result<()> {
        println!("[CMD] Reboot to flash mode.");

        let frame = Frame::new(CMD_REBOOT_FLASH, vec![]);
        self.send_frame(&frame)?;

        let response = self.receive_frame(Duration::from_secs(2))?;

        if response.command == CMD_RESPONSE_ERROR {
            let error_code = response.payload.first().copied().unwrap_or(0);
            anyhow::bail!("Firmware returned error: 0x{:02X}", error_code);
        }

        if response.command != CMD_RESPONSE_OK {
            anyhow::bail!("Unexpected response command: 0x{:02X}", response.command);
        }
        println!("[CMD] Reboot to flash mode successful.\n");

        Ok(())
    }
}
