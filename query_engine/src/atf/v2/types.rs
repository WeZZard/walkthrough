// User Story: M1_E5_I2 - ATF V2 Reader types
// Tech Spec: docs/specs/TRACE_SCHEMA.md and tracer_backend/include/tracer_backend/atf/atf_v2_types.h
//
// Type definitions for ATF v2 binary format (zero-copy parsing)

use std::fmt;

/// ATF V2 Index File Header - 64 bytes
#[repr(C, packed)]
#[derive(Debug, Copy, Clone)]
pub struct AtfIndexHeader {
    // Identity (16 bytes)
    pub magic: [u8; 4],           // "ATI2"
    pub endian: u8,                // 0x01 = little-endian
    pub version: u8,               // 1
    pub arch: u8,                  // 1=x86_64, 2=arm64
    pub os: u8,                    // 1=iOS, 2=Android, 3=macOS, 4=Linux, 5=Windows
    pub flags: u32,                // Bit 0: has_detail_file
    pub thread_id: u32,            // Thread ID for this file

    // Timing metadata (8 bytes)
    pub clock_type: u8,            // 1=mach_continuous, 2=qpc, 3=boottime
    pub _reserved1: [u8; 3],
    pub _reserved2: u32,

    // Event layout (8 bytes)
    pub event_size: u32,           // 32 bytes per event
    pub event_count: u32,          // Total number of events

    // Offsets (16 bytes)
    pub events_offset: u64,        // Offset to first event
    pub footer_offset: u64,        // Offset to footer (for recovery)

    // Time range (16 bytes)
    pub time_start_ns: u64,        // First event timestamp
    pub time_end_ns: u64,          // Last event timestamp
}

// Compile-time size check
const _: () = assert!(std::mem::size_of::<AtfIndexHeader>() == 64);

/// ATF V2 Index Event - 32 bytes (fixed size)
#[repr(C, packed)]
#[derive(Debug, Copy, Clone)]
pub struct IndexEvent {
    pub timestamp_ns: u64,         // Platform continuous clock
    pub function_id: u64,          // (moduleId << 32) | symbolIndex
    pub thread_id: u32,            // OS thread identifier
    pub event_kind: u32,           // CALL=1, RETURN=2, EXCEPTION=3
    pub call_depth: u32,           // Call stack depth
    pub detail_seq: u32,           // Forward link to detail event (u32::MAX = none)
}

// Compile-time size check
const _: () = assert!(std::mem::size_of::<IndexEvent>() == 32);

/// ATF V2 Index File Footer - 64 bytes
#[repr(C, packed)]
#[derive(Debug, Copy, Clone)]
pub struct AtfIndexFooter {
    pub magic: [u8; 4],            // "2ITA" (reversed)
    pub checksum: u32,             // CRC32 of events section
    pub event_count: u64,          // Actual event count (authoritative)
    pub time_start_ns: u64,        // First event timestamp
    pub time_end_ns: u64,          // Last event timestamp
    pub bytes_written: u64,        // Total bytes in events section
    pub reserved: [u8; 24],
}

// Compile-time size check
const _: () = assert!(std::mem::size_of::<AtfIndexFooter>() == 64);

/// ATF V2 Detail File Header - 64 bytes
#[repr(C, packed)]
#[derive(Debug, Copy, Clone)]
pub struct AtfDetailHeader {
    pub magic: [u8; 4],            // "ATD2"
    pub endian: u8,                // 0x01 = little-endian
    pub version: u8,               // 1
    pub arch: u8,                  // 1=x86_64, 2=arm64
    pub os: u8,                    // 1=iOS, 2=Android, 3=macOS, 4=Linux
    pub flags: u32,                // Reserved
    pub thread_id: u32,            // Thread ID for this file
    pub _reserved1: u32,           // Padding
    pub events_offset: u64,        // Offset to first event (typically 64)
    pub event_count: u64,          // Number of detail events
    pub bytes_length: u64,         // Total bytes in events section
    pub index_seq_start: u64,      // First index sequence number covered
    pub index_seq_end: u64,        // Last index sequence number covered
    pub _reserved2: [u8; 4],       // Padding to 64 bytes
}

// Compile-time size check
const _: () = assert!(std::mem::size_of::<AtfDetailHeader>() == 64);

/// ATF V2 Detail Event Header - 24 bytes
#[repr(C, packed)]
#[derive(Debug, Copy, Clone)]
pub struct DetailEventHeader {
    pub total_length: u32,         // Including this header and payload
    pub event_type: u16,           // FUNCTION_CALL=3, FUNCTION_RETURN=4, etc.
    pub flags: u16,                // Event-specific flags
    pub index_seq: u32,            // Backward link to index event position
    pub thread_id: u32,            // Thread that generated event
    pub timestamp: u64,            // Monotonic nanoseconds (same as index)
}

// Compile-time size check
const _: () = assert!(std::mem::size_of::<DetailEventHeader>() == 24);

/// ATF V2 Detail File Footer - 64 bytes
#[repr(C, packed)]
#[derive(Debug, Copy, Clone)]
pub struct AtfDetailFooter {
    pub magic: [u8; 4],            // "2DTA" (reversed)
    pub checksum: u32,             // CRC32 of events section
    pub event_count: u64,          // Actual event count
    pub bytes_length: u64,         // Actual bytes in events section
    pub time_start_ns: u64,        // First event timestamp
    pub time_end_ns: u64,          // Last event timestamp
    pub reserved: [u8; 24],
}

// Compile-time size check
const _: () = assert!(std::mem::size_of::<AtfDetailFooter>() == 64);

/// Detail event payload (variable length)
pub struct DetailEvent<'a> {
    header: DetailEventHeader,
    payload: &'a [u8],
}

impl<'a> DetailEvent<'a> {
    // Return header by value to avoid packed struct alignment issues
    pub fn header(&self) -> DetailEventHeader {
        self.header
    }

    pub fn payload(&self) -> &[u8] {
        self.payload
    }

    pub fn from_bytes(data: &'a [u8]) -> Option<Self> {
        if data.len() < 24 {
            return None;
        }

        // SAFETY: We checked the length, and DetailEventHeader is repr(C, packed)
        let header = unsafe {
            std::ptr::read_unaligned(data.as_ptr() as *const DetailEventHeader)
        };

        if header.total_length < 24 || header.total_length as usize > data.len() {
            return None;
        }

        let payload = &data[24..header.total_length as usize];

        Some(DetailEvent { header, payload })
    }
}

impl<'a> fmt::Debug for DetailEvent<'a> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("DetailEvent")
            .field("header", &self.header)
            .field("payload_len", &self.payload.len())
            .finish()
    }
}

// Constants
pub const ATF_NO_DETAIL_SEQ: u32 = u32::MAX;
pub const ATF_INDEX_FLAG_HAS_DETAIL_FILE: u32 = 1 << 0;

// Event kinds
pub const ATF_EVENT_KIND_CALL: u32 = 1;
pub const ATF_EVENT_KIND_RETURN: u32 = 2;
pub const ATF_EVENT_KIND_EXCEPTION: u32 = 3;

// Detail event types
pub const ATF_DETAIL_EVENT_FUNCTION_CALL: u16 = 3;
pub const ATF_DETAIL_EVENT_FUNCTION_RETURN: u16 = 4;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_types__struct_sizes__then_correct() {
        // User Story: M1_E5_I2 - Verify struct sizes match C definitions
        assert_eq!(std::mem::size_of::<AtfIndexHeader>(), 64);
        assert_eq!(std::mem::size_of::<IndexEvent>(), 32);
        assert_eq!(std::mem::size_of::<AtfIndexFooter>(), 64);
        assert_eq!(std::mem::size_of::<AtfDetailHeader>(), 64);
        assert_eq!(std::mem::size_of::<DetailEventHeader>(), 24);
    }

    #[test]
    fn test_detail_event__from_bytes__then_parsed() {
        // User Story: M1_E5_I2 - Parse detail event from binary data
        let mut data = vec![0u8; 100];

        // total_length = 100
        data[0..4].copy_from_slice(&100u32.to_le_bytes());
        // event_type = 3
        data[4..6].copy_from_slice(&3u16.to_le_bytes());
        // flags = 0
        data[6..8].copy_from_slice(&0u16.to_le_bytes());
        // index_seq = 42
        data[8..12].copy_from_slice(&42u32.to_le_bytes());
        // thread_id = 123
        data[12..16].copy_from_slice(&123u32.to_le_bytes());
        // timestamp = 1000
        data[16..24].copy_from_slice(&1000u64.to_le_bytes());

        let event = DetailEvent::from_bytes(&data).unwrap();
        let header = event.header();
        // Extract values to avoid packed field alignment issues
        let total_length = header.total_length;
        let event_type = header.event_type;
        let index_seq = header.index_seq;
        let thread_id = header.thread_id;
        let timestamp = header.timestamp;

        assert_eq!(total_length, 100);
        assert_eq!(event_type, 3);
        assert_eq!(index_seq, 42);
        assert_eq!(thread_id, 123);
        assert_eq!(timestamp, 1000);
        assert_eq!(event.payload().len(), 76); // 100 - 24
    }

    #[test]
    fn test_detail_event__too_short__then_none() {
        // User Story: M1_E5_I2 - Handle truncated data
        let data = vec![0u8; 20]; // Less than 24 bytes
        assert!(DetailEvent::from_bytes(&data).is_none());
    }

    #[test]
    fn test_detail_event__invalid_length__then_none() {
        // User Story: M1_E5_I2 - Handle invalid total_length
        let mut data = vec![0u8; 50];
        data[0..4].copy_from_slice(&100u32.to_le_bytes()); // Claims 100 bytes but only 50
        assert!(DetailEvent::from_bytes(&data).is_none());
    }

    #[test]
    fn test_detail_event__debug_format__then_formatted() {
        // User Story: M1_E5_I2 - Test Debug trait on DetailEvent
        // Test Plan: Unit Tests - Types
        let mut data = vec![0u8; 100];

        // total_length = 100
        data[0..4].copy_from_slice(&100u32.to_le_bytes());
        // event_type = 3
        data[4..6].copy_from_slice(&3u16.to_le_bytes());
        // flags = 0
        data[6..8].copy_from_slice(&0u16.to_le_bytes());
        // index_seq = 42
        data[8..12].copy_from_slice(&42u32.to_le_bytes());
        // thread_id = 123
        data[12..16].copy_from_slice(&123u32.to_le_bytes());
        // timestamp = 1000
        data[16..24].copy_from_slice(&1000u64.to_le_bytes());

        let event = DetailEvent::from_bytes(&data).unwrap();
        let debug_str = format!("{:?}", event);

        // Verify Debug output contains expected fields
        assert!(debug_str.contains("DetailEvent"));
        assert!(debug_str.contains("header"));
        assert!(debug_str.contains("payload_len"));
    }
}
