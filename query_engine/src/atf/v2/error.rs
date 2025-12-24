// User Story: M1_E5_I2 - ATF V2 Reader error handling
// Tech Spec: M1_E5_I2_TECH_DESIGN.md - Error types for reader operations

use thiserror::Error;

#[derive(Debug, Error)]
pub enum AtfV2Error {
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),

    #[error("Invalid magic bytes: expected {expected:?}, got {got:?}")]
    InvalidMagic {
        expected: Vec<u8>,
        got: Vec<u8>,
    },

    #[error("Unsupported version: {0}")]
    UnsupportedVersion(u8),

    #[error("Unsupported endianness: {0}")]
    UnsupportedEndian(u8),

    #[error("Invalid event size: expected 32, got {0}")]
    InvalidEventSize(u32),

    #[error("Sequence out of bounds: {seq} >= {max}")]
    SeqOutOfBounds { seq: u32, max: u32 },

    #[error("Corrupted footer: invalid magic bytes")]
    CorruptedFooter,

    #[error("Missing detail file")]
    MissingDetail,

    #[error("File too small: expected at least {expected} bytes, got {actual}")]
    FileTooSmall { expected: usize, actual: usize },

    #[error("Invalid offset: {offset} out of bounds (file size: {file_size})")]
    InvalidOffset { offset: usize, file_size: usize },
}

pub type Result<T> = std::result::Result<T, AtfV2Error>;
