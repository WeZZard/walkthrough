"""
ATF V2 Session Reader - Cross-thread merge-sort
User Story: M1_E5_I2 - ATF V2 Session Reader
Tech Spec: M1_E5_I2_TECH_DESIGN.md - Multi-thread reader with merge-sort iterator
"""

import heapq
import json
from pathlib import Path
from typing import Iterator, NamedTuple

from .thread import ThreadReader
from .types import IndexEvent


class ThreadInfo(NamedTuple):
    """Thread metadata from manifest"""
    id: int
    has_detail: bool = False


class Manifest(NamedTuple):
    """Session manifest"""
    threads: list[ThreadInfo]
    time_start_ns: int = 0
    time_end_ns: int = 0

    @classmethod
    def from_dict(cls, data: dict) -> 'Manifest':
        """Parse manifest from JSON dict"""
        threads = [
            ThreadInfo(
                id=t['id'],
                has_detail=t.get('has_detail', False)
            )
            for t in data.get('threads', [])
        ]

        return cls(
            threads=threads,
            time_start_ns=data.get('time_start_ns', 0),
            time_end_ns=data.get('time_end_ns', 0),
        )


class SessionReader:
    """Session reader with multi-thread support"""

    def __init__(self, session_dir: Path):
        """Open session directory and load all thread readers"""
        session_dir = Path(session_dir)

        # Read manifest.json
        manifest_path = session_dir / "manifest.json"
        with open(manifest_path, 'r') as f:
            manifest_data = json.load(f)

        self.manifest = Manifest.from_dict(manifest_data)

        # Load thread readers
        self.threads: list[ThreadReader] = []
        for thread_info in self.manifest.threads:
            thread_dir = session_dir / f"thread_{thread_info.id}"
            if thread_dir.exists():
                self.threads.append(ThreadReader(thread_dir))

    def time_range(self) -> tuple[int, int]:
        """Time range across all threads"""
        if not self.threads:
            return 0, 0

        min_time = float('inf')
        max_time = 0

        for thread in self.threads:
            start, end = thread.time_range()
            min_time = min(min_time, start)
            max_time = max(max_time, end)

        return int(min_time), int(max_time)

    def event_count(self) -> int:
        """Total event count across all threads"""
        return sum(len(thread.index) for thread in self.threads)

    def merged_iter(self) -> Iterator[tuple[int, IndexEvent]]:
        """Merge-sort iterator across all threads by timestamp_ns"""
        return MergedEventIter(self.threads)

    def close(self):
        """Close all thread readers"""
        for thread in self.threads:
            thread.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False


class MergedEventIter:
    """Merge-sort iterator using min-heap"""

    def __init__(self, threads: list[ThreadReader]):
        self.threads = threads
        self.heap = []

        # Seed heap with first event from each thread
        for idx, thread in enumerate(threads):
            if len(thread.index) > 0:
                event = thread.index[0]
                # (timestamp, thread_idx, seq)
                heapq.heappush(self.heap, (event.timestamp_ns, idx, 0))

    def __iter__(self):
        return self

    def __next__(self) -> tuple[int, IndexEvent]:
        """Get next event in global timestamp order"""
        if not self.heap:
            raise StopIteration

        # Pop smallest timestamp from heap
        timestamp, thread_idx, seq = heapq.heappop(self.heap)

        event = self.threads[thread_idx].index[seq]

        # Push next event from same thread if available
        next_seq = seq + 1
        if next_seq < len(self.threads[thread_idx].index):
            next_event = self.threads[thread_idx].index[next_seq]
            heapq.heappush(self.heap, (next_event.timestamp_ns, thread_idx, next_seq))

        return thread_idx, event
