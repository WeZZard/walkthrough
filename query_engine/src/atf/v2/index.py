"""
ATF V2 Index Reader - Pure Python with mmap
User Story: M1_E5_I2 - ATF V2 Index Reader
Tech Spec: M1_E5_I2_TECH_DESIGN.md - Memory-mapped reader for O(1) access
"""

import mmap
from pathlib import Path
from typing import Iterator, Optional

from .types import IndexEvent, IndexFooter, IndexHeader, ATF_INDEX_FLAG_HAS_DETAIL_FILE


class IndexReader:
    """Memory-mapped reader for ATF v2 index files"""

    def __init__(self, path: Path):
        """Open and memory-map an index file"""
        self._path = Path(path)
        self._file = open(self._path, 'rb')
        self._mmap = mmap.mmap(self._file.fileno(), 0, access=mmap.ACCESS_READ)

        # Parse header
        self._header = self._parse_header()

        # Validate header
        self._validate_header()

        # Parse footer (authoritative for event count)
        self._footer, self._event_count = self._parse_footer()

    def _parse_header(self) -> IndexHeader:
        """Parse and validate index header"""
        if len(self._mmap) < 64:
            raise ValueError(f"File too small: {len(self._mmap)} < 64")

        return IndexHeader.from_bytes(self._mmap[:64])

    def _validate_header(self):
        """Validate header fields"""
        if self._header.magic != b'ATI2':
            raise ValueError(
                f"Invalid magic: expected b'ATI2', got {self._header.magic}"
            )

        if self._header.version != 1:
            raise ValueError(f"Unsupported version: {self._header.version}")

        if self._header.endian != 0x01:
            raise ValueError(f"Unsupported endian: {self._header.endian}")

        if self._header.event_size != 32:
            raise ValueError(f"Invalid event size: {self._header.event_size}")

    def _parse_footer(self) -> tuple[Optional[IndexFooter], int]:
        """Read footer and determine authoritative event count"""
        footer_offset = self._header.footer_offset

        # Try to read footer
        if footer_offset + 64 <= len(self._mmap):
            footer_bytes = self._mmap[footer_offset:footer_offset + 64]
            footer = IndexFooter.from_bytes(footer_bytes)

            # Validate footer magic
            if footer.magic == b'2ITA':
                # Footer is valid, use its count (authoritative)
                return footer, footer.event_count

        # Footer invalid or missing, calculate from file size
        events_section_size = len(self._mmap) - self._header.events_offset
        calculated_count = events_section_size // 32

        return None, calculated_count

    def __len__(self) -> int:
        """Get event count"""
        return self._event_count

    def __getitem__(self, seq: int) -> IndexEvent:
        """Get event by sequence number (O(1))"""
        if seq < 0 or seq >= self._event_count:
            raise IndexError(f"Sequence {seq} out of bounds (0..{self._event_count})")

        offset = self._header.events_offset + seq * 32
        return IndexEvent.from_bytes(self._mmap[offset:offset + 32])

    def __iter__(self) -> Iterator[IndexEvent]:
        """Iterate all events"""
        for seq in range(self._event_count):
            yield self[seq]

    @property
    def has_detail(self) -> bool:
        """Check if detail file exists"""
        return bool(self._header.flags & ATF_INDEX_FLAG_HAS_DETAIL_FILE)

    @property
    def thread_id(self) -> int:
        """Get thread ID"""
        return self._header.thread_id

    def time_range(self) -> tuple[int, int]:
        """Get time range (use footer if available)"""
        if self._footer:
            return self._footer.time_start_ns, self._footer.time_end_ns
        return self._header.time_start_ns, self._header.time_end_ns

    def close(self):
        """Close the memory-mapped file"""
        if self._mmap:
            self._mmap.close()
        if self._file:
            self._file.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False
