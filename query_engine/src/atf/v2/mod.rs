// User Story: M1_E5_I2 - ATF V2 Reader module
// Tech Spec: M1_E5_I2_TECH_DESIGN.md - Binary format readers with memory-mapped access

pub mod detail;
pub mod error;
pub mod index;
pub mod session;
pub mod thread;
pub mod types;

// Re-export main types
pub use detail::{DetailEventIter, DetailReader};
pub use error::{AtfV2Error, Result};
pub use index::{IndexEventIter, IndexReader};
pub use session::{Manifest, MergedEventIter, SessionReader, ThreadInfo};
pub use thread::ThreadReader;
pub use types::{
    AtfDetailFooter, AtfDetailHeader, AtfIndexFooter, AtfIndexHeader, DetailEvent,
    DetailEventHeader, IndexEvent, ATF_DETAIL_EVENT_FUNCTION_CALL,
    ATF_DETAIL_EVENT_FUNCTION_RETURN, ATF_EVENT_KIND_CALL, ATF_EVENT_KIND_EXCEPTION,
    ATF_EVENT_KIND_RETURN, ATF_INDEX_FLAG_HAS_DETAIL_FILE, ATF_NO_DETAIL_SEQ,
};
