/// Firmware communication types matching the C structures
use std::fmt;

/// KeyCombo structure matching firmware
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, Default)]
pub struct KeyCombo {
    pub modifiers: u8, // 4 bits modifiers, 4 bits unused
    pub key: u8,       // ASCII or keycode
}

impl KeyCombo {
    #[allow(dead_code)]
    pub const fn new(modifiers: u8, key: u8) -> Self {
        Self { modifiers, key }
    }

    /// Format modifiers as text (e.g., "Ctrl+Alt")
    pub fn modifiers_str(&self) -> String {
        if self.modifiers == MOD_NONE {
            return String::new();
        }

        let mut parts = Vec::new();

        if self.modifiers & MOD_CTRL != 0 {
            parts.push("Ctrl");
        }
        if self.modifiers & MOD_ALT != 0 {
            parts.push("Alt");
        }
        if self.modifiers & MOD_SHIFT != 0 {
            parts.push("Shift");
        }
        if self.modifiers & MOD_ESC != 0 {
            return "ESC".to_string();
        }
        // Don't include MOD_F here - it's handled in key_str() as "F5", etc.

        parts.join("+")
    }

    /// Format key as readable character or name
    pub fn key_str(&self) -> String {
        if self.modifiers & MOD_F != 0 {
            // Function key (F1-F24)
            return format!("F{}", self.key);
        }

        if self.modifiers & MOD_ESC != 0 {
            return String::new(); // ESC already shown in modifiers
        }

        // Printable ASCII characters
        if self.key >= 32 && self.key <= 126 {
            return (self.key as char).to_string();
        }

        // Special keys
        match self.key {
            0 => "(none)".to_string(),
            9 => "Tab".to_string(),
            10 => "Enter".to_string(),
            13 => "Enter".to_string(),
            27 => "ESC".to_string(),
            _ => format!("0x{:02X}", self.key),
        }
    }

    /// Format complete key combo as readable string
    pub fn display(&self) -> String {
        let mods = self.modifiers_str();
        let key = self.key_str();

        if mods.is_empty() {
            key
        } else if key.is_empty() {
            mods
        } else {
            format!("{}+{}", mods, key)
        }
    }
}

// Modifier constants matching firmware
pub const MOD_NONE: u8 = 0x0;
pub const MOD_CTRL: u8 = 0x1;
pub const MOD_ALT: u8 = 0x2;
pub const MOD_SHIFT: u8 = 0x4;
pub const MOD_F: u8 = 0x8;
pub const MOD_ESC: u8 = 0x10;

/// ButtonMapping structure matching firmware (41 bytes total)
#[repr(C, packed)]
#[derive(Clone, Copy, Default)]
pub struct ButtonMapping {
    pub name: [u8; 17], // Max 16 chars + null terminator
    pub start_p1: KeyCombo,
    pub start_p2: KeyCombo,
    pub coin_p1: KeyCombo,
    pub coin_p2: KeyCombo,
    pub action_p1_1: KeyCombo,
    pub action_p1_2: KeyCombo,
    pub action_p2_1: KeyCombo,
    pub action_p2_2: KeyCombo,
    pub service_p1: KeyCombo,
    pub service_p2: KeyCombo,
    pub test_p1: KeyCombo,
    pub test_p2: KeyCombo,
    pub pause: KeyCombo,
    pub save: KeyCombo,
    pub load: KeyCombo,
    pub exit: KeyCombo,
}

impl ButtonMapping {
    pub fn name_str(&self) -> String {
        let null_pos = self.name.iter().position(|&c| c == 0).unwrap_or(8);
        String::from_utf8_lossy(&self.name[..null_pos]).to_string()
    }
}

// pub struct ButtonMapping {
//     fn default() -> Self {
//         Self {
//             name: [0; 17],
//             start_p1: KeyCombo::default(),
//             start_p2: KeyCombo::default(),
//             coin_p1: KeyCombo::default(),
//             coin_p2: KeyCombo::default(),
//             action_p1_1: KeyCombo::default(),
//             action_p1_2: KeyCombo::default(),
//             action_p2_1: KeyCombo::default(),
//             action_p2_2: KeyCombo::default(),
//             service_p1: KeyCombo::default(),
//             service_p2: KeyCombo::default(),
//             test_p1: KeyCombo::default(),
//             test_p2: KeyCombo::default(),
//             pause: KeyCombo::default(),
//             save: KeyCombo::default(),
//             load: KeyCombo::default(),
//             exit: KeyCombo::default(),
//         }
//     }
// }

impl fmt::Debug for ButtonMapping {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ButtonMapping")
            .field("name", &self.name_str())
            .field("start_p1", &self.start_p1)
            .field("start_p2", &self.start_p2)
            .finish()
    }
}

/// Firmware version structure
#[derive(Debug, Clone)]
pub struct FirmwareVersion {
    pub major: u8,
    pub minor: u8,
    pub patch: u8,
}

impl fmt::Display for FirmwareVersion {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}.{}.{}", self.major, self.minor, self.patch)
    }
}
