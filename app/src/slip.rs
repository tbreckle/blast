use thiserror::Error;

#[derive(Error, Debug)]
pub enum SlipError {
    #[error("Invalid frame")]
    InvalidFrame,
    #[error("Incomplete frame")]
    IncompleteFrame,
}

const FRAME_END: u8 = 0xC0;
const FRAME_ESC: u8 = 0xDB;
const FRAME_ESC_END: u8 = 0xDC;
const FRAME_ESC_ESC: u8 = 0xDD;

/// Encode data using SLIP protocol
pub fn encode(data: &[u8], output: &mut Vec<u8>) {
    output.push(FRAME_END);

    for &byte in data {
        match byte {
            FRAME_END => {
                output.push(FRAME_ESC);
                output.push(FRAME_ESC_END);
            }
            FRAME_ESC => {
                output.push(FRAME_ESC);
                output.push(FRAME_ESC_ESC);
            }
            _ => output.push(byte),
        }
    }

    output.push(FRAME_END);
}

/// Decode SLIP-encoded data
/// Returns the decoded data and the number of bytes consumed from input
pub fn decode(input: &[u8]) -> Result<(Vec<u8>, usize), SlipError> {
    let mut output = Vec::new();
    let mut i = 0;
    let mut in_frame = false;
    let mut escape_next = false;

    while i < input.len() {
        let byte = input[i];
        i += 1;

        match byte {
            FRAME_END => {
                if in_frame && !output.is_empty() {
                    // End of frame
                    return Ok((output, i));
                }
                // Start of new frame
                in_frame = true;
                output.clear();
                escape_next = false;
            }
            FRAME_ESC => {
                if !in_frame {
                    return Err(SlipError::InvalidFrame);
                }
                escape_next = true;
            }
            _ => {
                if !in_frame {
                    return Err(SlipError::InvalidFrame);
                }

                if escape_next {
                    match byte {
                        FRAME_ESC_END => output.push(FRAME_END),
                        FRAME_ESC_ESC => output.push(FRAME_ESC),
                        _ => return Err(SlipError::InvalidFrame),
                    }
                    escape_next = false;
                } else {
                    output.push(byte);
                }
            }
        }
    }

    Err(SlipError::IncompleteFrame)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_encode_simple() {
        let data = vec![0x01, 0x02, 0x03];
        let mut output = Vec::new();
        encode(&data, &mut output);
        assert_eq!(output, vec![0xC0, 0x01, 0x02, 0x03, 0xC0]);
    }

    #[test]
    fn test_encode_with_escape() {
        let data = vec![0xC0, 0xDB];
        let mut output = Vec::new();
        encode(&data, &mut output);
        assert_eq!(output, vec![0xC0, 0xDB, 0xDC, 0xDB, 0xDD, 0xC0]);
    }

    #[test]
    fn test_decode_simple() {
        let input = vec![0xC0, 0x01, 0x02, 0x03, 0xC0];
        let (decoded, consumed) = decode(&input).unwrap();
        assert_eq!(decoded, vec![0x01, 0x02, 0x03]);
        assert_eq!(consumed, 5);
    }

    #[test]
    fn test_decode_with_escape() {
        let input = vec![0xC0, 0xDB, 0xDC, 0xDB, 0xDD, 0xC0];
        let (decoded, consumed) = decode(&input).unwrap();
        assert_eq!(decoded, vec![0xC0, 0xDB]);
        assert_eq!(consumed, 6);
    }
}
