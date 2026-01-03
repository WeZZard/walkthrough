"""
ATF V2 Types - Binary format structures
User Story: M1_E5_I2 - ATF V2 Reader types
Tech Spec: M1_E5_I2_TECH_DESIGN.md and tracer_backend/include/tracer_backend/atf/atf_v2_types.h
"""

import struct
from typing import NamedTuple

# Constants
ATF_NO_DETAIL_SEQ = 0xFFFFFFFF
ATF_INDEX_FLAG_HAS_DETAIL_FILE = 1 << 0

# Event kinds
ATF_EVENT_KIND_CALL = 1
ATF_EVENT_KIND_RETURN = 2
ATF_EVENT_KIND_EXCEPTION = 3

# Detail event types
ATF_DETAIL_EVENT_FUNCTION_CALL = 3
ATF_DETAIL_EVENT_FUNCTION_RETURN = 4


class IndexHeader(NamedTuple):
    """ATF V2 Index File Header - 64 bytes"""
    magic: bytes  # "ATI2"
    endian: int  # 0x01 = little-endian
    version: int  # 1
    arch: int  # 1=x86_64, 2=arm64
    os: int  # 1=iOS, 2=Android, 3=macOS, 4=Linux, 5=Windows
    flags: int  # Bit 0: has_detail_file
    thread_id: int
    clock_type: int
    event_size: int  # 32 bytes per event
    event_count: int
    events_offset: int
    footer_offset: int
    time_start_ns: int
    time_end_ns: int

    @classmethod
    def from_bytes(cls, data: bytes) -> 'IndexHeader':
        """Parse index header from bytes"""
        if len(data) < 64:
            raise ValueError(f"Header too small: {len(data)} < 64")

        # Format: 4s B B B B I I B 3x 4x I I Q Q Q Q
        # magic, endian, version, arch, os, flags, thread_id, clock_type,
        # (3 reserved1 - skipped), (4 reserved2 - skipped),
        # event_size, event_count, events_offset, footer_offset,
        # time_start_ns, time_end_ns
        values = struct.unpack('<4sBBBBII B3x 4x II QQ QQ', data[:64])

        return cls(
            magic=values[0],
            endian=values[1],
            version=values[2],
            arch=values[3],
            os=values[4],
            flags=values[5],
            thread_id=values[6],
            clock_type=values[7],
            event_size=values[8],
            event_count=values[9],
            events_offset=values[10],
            footer_offset=values[11],
            time_start_ns=values[12],
            time_end_ns=values[13],
        )


class IndexEvent(NamedTuple):
    """ATF V2 Index Event - 32 bytes (fixed size)"""
    timestamp_ns: int
    function_id: int
    thread_id: int
    event_kind: int
    call_depth: int
    detail_seq: int

    @classmethod
    def from_bytes(cls, data: bytes) -> 'IndexEvent':
        """Parse index event from bytes"""
        if len(data) < 32:
            raise ValueError(f"Event too small: {len(data)} < 32")

        # Format: Q Q I I I I
        values = struct.unpack('<QQIIII', data[:32])

        return cls(
            timestamp_ns=values[0],
            function_id=values[1],
            thread_id=values[2],
            event_kind=values[3],
            call_depth=values[4],
            detail_seq=values[5],
        )


class IndexFooter(NamedTuple):
    """ATF V2 Index File Footer - 64 bytes"""
    magic: bytes  # "2ITA" (reversed)
    checksum: int
    event_count: int
    time_start_ns: int
    time_end_ns: int
    bytes_written: int

    @classmethod
    def from_bytes(cls, data: bytes) -> 'IndexFooter':
        """Parse index footer from bytes"""
        if len(data) < 64:
            raise ValueError(f"Footer too small: {len(data)} < 64")

        # Format: 4s I Q Q Q Q 24x
        values = struct.unpack('<4sIQQQQ24x', data[:64])

        return cls(
            magic=values[0],
            checksum=values[1],
            event_count=values[2],
            time_start_ns=values[3],
            time_end_ns=values[4],
            bytes_written=values[5],
        )


class DetailHeader(NamedTuple):
    """ATF V2 Detail File Header - 64 bytes"""
    magic: bytes  # "ATD2"
    endian: int
    version: int
    arch: int
    os: int
    flags: int
    thread_id: int
    events_offset: int
    event_count: int
    bytes_length: int
    index_seq_start: int
    index_seq_end: int

    @classmethod
    def from_bytes(cls, data: bytes) -> 'DetailHeader':
        """Parse detail header from bytes"""
        if len(data) < 64:
            raise ValueError(f"Header too small: {len(data)} < 64")

        # Format: 4s B B B B I I I Q Q Q Q Q 4x
        values = struct.unpack('<4sBBBBII I QQQQQ 4x', data[:64])

        return cls(
            magic=values[0],
            endian=values[1],
            version=values[2],
            arch=values[3],
            os=values[4],
            flags=values[5],
            thread_id=values[6],
            events_offset=values[8],
            event_count=values[9],
            bytes_length=values[10],
            index_seq_start=values[11],
            index_seq_end=values[12],
        )


class DetailEventHeader(NamedTuple):
    """ATF V2 Detail Event Header - 24 bytes"""
    total_length: int
    event_type: int
    flags: int
    index_seq: int
    thread_id: int
    timestamp: int

    @classmethod
    def from_bytes(cls, data: bytes) -> 'DetailEventHeader':
        """Parse detail event header from bytes"""
        if len(data) < 24:
            raise ValueError(f"Header too small: {len(data)} < 24")

        # Format: I H H I I Q
        values = struct.unpack('<IHHIIQ', data[:24])

        return cls(
            total_length=values[0],
            event_type=values[1],
            flags=values[2],
            index_seq=values[3],
            thread_id=values[4],
            timestamp=values[5],
        )


class DetailEvent:
    """Detail event with header and payload"""

    def __init__(self, header: DetailEventHeader, payload: bytes):
        self._header = header
        self._payload = payload

    @property
    def header(self) -> DetailEventHeader:
        return self._header

    @property
    def payload(self) -> bytes:
        return self._payload

    @classmethod
    def from_bytes(cls, data: bytes) -> 'DetailEvent':
        """Parse detail event from bytes"""
        header = DetailEventHeader.from_bytes(data)
        if header.total_length > len(data):
            raise ValueError(
                f"Event length {header.total_length} > available data {len(data)}"
            )

        payload = data[24:header.total_length]
        return cls(header, payload)

    def __repr__(self):
        return f"DetailEvent(header={self._header}, payload_len={len(self._payload)})"
