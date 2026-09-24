/// Mapping of captured key presses (Slint `KeyEvent.text`) to firmware key combos.
use crate::types::{KeyCombo, MOD_ESC, MOD_F};
use slint::platform::Key;

/// Result of feeding a key press into a binding that is being captured.
#[derive(Debug, PartialEq, Eq)]
pub enum CaptureResult {
    /// Modifier-only key press (e.g. Shift): keep waiting for the actual key.
    Ignored,
    /// Capture finished, the binding may have changed.
    Done,
}

/// Keys that only act as modifiers and never end a capture.
const MODIFIER_KEYS: [Key; 9] = [
    Key::Shift,
    Key::ShiftR,
    Key::Control,
    Key::ControlR,
    Key::Alt,
    Key::AltGr,
    Key::Meta,
    Key::MetaR,
    Key::CapsLock,
];

/// Function keys F1-F24, in order.
const FUNCTION_KEYS: [Key; 24] = [
    Key::F1,
    Key::F2,
    Key::F3,
    Key::F4,
    Key::F5,
    Key::F6,
    Key::F7,
    Key::F8,
    Key::F9,
    Key::F10,
    Key::F11,
    Key::F12,
    Key::F13,
    Key::F14,
    Key::F15,
    Key::F16,
    Key::F17,
    Key::F18,
    Key::F19,
    Key::F20,
    Key::F21,
    Key::F22,
    Key::F23,
    Key::F24,
];

/// Apply a captured key press to a binding.
///
/// - ESC clears the binding.
/// - F1-F24 set the F-key flag and the function key number.
/// - Letters (stored lowercase), digits, Space and Enter replace the key. The F-key and ESC
///   flags are cleared so the new key takes effect; Ctrl/Alt/Shift are kept.
/// - Any other key ends the capture without changing the binding.
pub fn apply_captured_key(combo: &mut KeyCombo, text: &str) -> CaptureResult {
    let Some(c) = text.chars().next() else {
        return CaptureResult::Ignored;
    };

    if MODIFIER_KEYS.iter().any(|&key| char::from(key) == c) {
        return CaptureResult::Ignored;
    }

    if c == char::from(Key::Escape) {
        *combo = KeyCombo::default();
        return CaptureResult::Done;
    }

    if let Some(index) = FUNCTION_KEYS.iter().position(|&key| char::from(key) == c) {
        combo.modifiers = (combo.modifiers & !MOD_ESC) | MOD_F;
        combo.key = index as u8 + 1;
        return CaptureResult::Done;
    }

    let key = match c {
        'a'..='z' | '0'..='9' | ' ' => Some(c as u8),
        'A'..='Z' => Some(c.to_ascii_lowercase() as u8),
        '\n' | '\r' => Some(13),
        _ => None,
    };
    if let Some(key) = key {
        combo.modifiers &= !(MOD_F | MOD_ESC);
        combo.key = key;
    }
    CaptureResult::Done
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::types::{MOD_ALT, MOD_CTRL, MOD_SHIFT};

    fn capture(start: KeyCombo, text: &str) -> (CaptureResult, u8, u8) {
        let mut combo = start;
        let result = apply_captured_key(&mut combo, text);
        (result, combo.modifiers, combo.key)
    }

    fn key_text(key: Key) -> String {
        char::from(key).to_string()
    }

    #[test]
    fn letters_digits_space_enter() {
        let empty = KeyCombo::default();
        assert_eq!(capture(empty, "a"), (CaptureResult::Done, 0, b'a'));
        assert_eq!(capture(empty, "Z"), (CaptureResult::Done, 0, b'z'));
        assert_eq!(capture(empty, "5"), (CaptureResult::Done, 0, b'5'));
        assert_eq!(capture(empty, " "), (CaptureResult::Done, 0, b' '));
        assert_eq!(
            capture(empty, &key_text(Key::Return)),
            (CaptureResult::Done, 0, 13)
        );
    }

    #[test]
    fn function_keys() {
        let empty = KeyCombo::default();
        assert_eq!(
            capture(empty, &key_text(Key::F1)),
            (CaptureResult::Done, MOD_F, 1)
        );
        assert_eq!(
            capture(empty, &key_text(Key::F12)),
            (CaptureResult::Done, MOD_F, 12)
        );
        assert_eq!(
            capture(empty, &key_text(Key::F24)),
            (CaptureResult::Done, MOD_F, 24)
        );
        // Ctrl is kept, ESC is cleared.
        assert_eq!(
            capture(KeyCombo::new(MOD_CTRL | MOD_ESC, 0), &key_text(Key::F5)),
            (CaptureResult::Done, MOD_CTRL | MOD_F, 5)
        );
    }

    #[test]
    fn regular_key_clears_function_and_esc_flags() {
        assert_eq!(
            capture(KeyCombo::new(MOD_F | MOD_ALT, 5), "p"),
            (CaptureResult::Done, MOD_ALT, b'p')
        );
        assert_eq!(
            capture(KeyCombo::new(MOD_ESC, 0), "q"),
            (CaptureResult::Done, 0, b'q')
        );
    }

    #[test]
    fn modifier_keys_are_ignored() {
        let start = KeyCombo::new(MOD_SHIFT, b'x');
        for key in MODIFIER_KEYS {
            assert_eq!(
                capture(start, &key_text(key)),
                (CaptureResult::Ignored, MOD_SHIFT, b'x')
            );
        }
        assert_eq!(
            capture(start, ""),
            (CaptureResult::Ignored, MOD_SHIFT, b'x')
        );
    }

    #[test]
    fn escape_clears_binding() {
        assert_eq!(
            capture(KeyCombo::new(MOD_CTRL | MOD_F, 7), &key_text(Key::Escape)),
            (CaptureResult::Done, 0, 0)
        );
    }

    #[test]
    fn unmapped_keys_end_capture_unchanged() {
        let start = KeyCombo::new(MOD_CTRL, b'c');
        assert_eq!(capture(start, "!"), (CaptureResult::Done, MOD_CTRL, b'c'));
        assert_eq!(
            capture(start, &key_text(Key::Tab)),
            (CaptureResult::Done, MOD_CTRL, b'c')
        );
        assert_eq!(
            capture(start, &key_text(Key::UpArrow)),
            (CaptureResult::Done, MOD_CTRL, b'c')
        );
    }
}
