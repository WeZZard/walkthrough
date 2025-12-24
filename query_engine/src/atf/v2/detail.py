"""
ATF V2 Detail Reader - Pure Python with mmap
User Story: M1_E5_I2 - ATF V2 Detail Reader
Tech Spec: M1_E5_I2_TECH_DESIGN.md - Variable-length event reader with O(1) access
"""

import mmap
import struct
from pathlib import Path
from typing import Iterator, Optional

from .types import DetailEvent, DetailHeader


class DetailReader:
    """Memory-mapped reader for ATF v2 detail files"""

    def __init__(self, path: Path):
        """Open and memory-map a detail file, building event index"""
        self._path = Path(path)
        self._file = open(self._path, 'rb')
        self._mmap = mmap.mmap(self._file.fileno(), 0, access=mmap.ACCESS_READ)

        # Parse header
        self._header = self._parse_header()

        # Validate header
        self._validate_header()

        # Build event index for O(1) lookup
        self._event_index = self._build_event_index()

    def _parse_header(self) -> DetailHeader:
        """Parse detail header"""
        if len(self._mmap) < 64:
            raise ValueError(f"File too small: {len(self._mmap)} < 64")

        return DetailHeader.from_bytes(self._mmap[:64])

    def _validate_header(self):
        """Validate header fields"""
        if self._header.magic != b'ATD2':
            raise ValueError(
                f"Invalid magic: expected b'ATD2', got {self._header.magic}"
            )

        if self._header.version != 1:
            raise ValueError(f"Unsupported version: {self._header.version}")

        if self._header.endian != 0x01:
            raise ValueError(f"Unsupported endian: {self._header.endian}")

    def _build_event_index(self) -> list[int]:
        """Build index of detail events for O(1) access"""
        index = []
        offset = self._header.events_offset
        end_offset = len(self._mmap) - 64  # Leave room for footer

        while offset + 24 <= end_offset:
            # Record this offset
            index.append(offset)

            # Read total_length
            if offset + 4 > len(self._mmap):
                break

            total_length = struct.unpack('<I', self._mmap[offset:offset + 4])[0]

            # Validate total_length
            if total_length < 24:
                break  # Invalid event

            # Advance to next event
            offset += total_length

        return index

    def __len__(self) -> int:
        """Get event count"""
        return len(self._event_index)

    def get(self, detail_seq: int) -> Optional[DetailEvent]:
        """Get detail event by sequence number (O(1))"""
        if detail_seq < 0 or detail_seq >= len(self._event_index):
            return None

        offset = self._event_index[detail_seq]

        if offset + 24 > len(self._mmap):
            return None

        return DetailEvent.from_bytes(self._mmap[offset:])

    def get_by_index_seq(self, index_seq: int) -> Optional[DetailEvent]:
        """Find detail event by its linked index sequence (O(n) scan)"""
        for detail_seq in range(len(self._event_index)):
            event = self.get(detail_seq)
            if event and event.header.index_seq == index_seq:
                return event
        return None

    def __iter__(self) -> Iterator[DetailEvent]:
        """Iterate all detail events"""
        for seq in range(len(self._event_index)):
            event = self.get(seq)
            if event:
                yield event

    @property
    def thread_id(self) -> int:
        """Get thread ID"""
        return self._header.thread_id

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
