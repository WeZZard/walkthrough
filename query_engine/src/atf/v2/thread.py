"""
ATF V2 Thread Reader - Combined index and detail readers
User Story: M1_E5_I2 - ATF V2 Thread Reader
Tech Spec: M1_E5_I2_TECH_DESIGN.md - Bidirectional navigation
"""

from pathlib import Path
from typing import Optional

from .detail import DetailReader
from .index import IndexReader
from .types import ATF_NO_DETAIL_SEQ, DetailEvent, IndexEvent


class ThreadReader:
    """Combined reader for thread index + detail files"""

    def __init__(self, thread_dir: Path):
        """Open thread directory and load index + optional detail files"""
        thread_dir = Path(thread_dir)

        # Load index file
        index_path = thread_dir / "index.atf"
        self.index = IndexReader(index_path)

        # Try to load detail file if it exists
        detail_path = thread_dir / "detail.atf"
        self.detail: Optional[DetailReader] = None
        if detail_path.exists():
            self.detail = DetailReader(detail_path)

    def get_detail_for(self, index_event: IndexEvent) -> Optional[DetailEvent]:
        """Forward lookup: index event → paired detail event (O(1))"""
        if index_event.detail_seq == ATF_NO_DETAIL_SEQ:
            return None

        if not self.detail:
            return None

        return self.detail.get(index_event.detail_seq)

    def get_index_for(self, detail_event: DetailEvent) -> IndexEvent:
        """Backward lookup: detail event → paired index event (O(1))"""
        return self.index[detail_event.header.index_seq]

    @property
    def thread_id(self) -> int:
        """Get thread ID"""
        return self.index.thread_id

    def time_range(self) -> tuple[int, int]:
        """Get time range"""
        return self.index.time_range()

    def close(self):
        """Close all readers"""
        self.index.close()
        if self.detail:
            self.detail.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False
