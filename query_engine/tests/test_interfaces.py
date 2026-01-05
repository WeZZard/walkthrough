"""
Test that all interfaces compile and are properly defined.
"""

import pytest
import sys
from pathlib import Path

# Add src to path
sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

def test_interfaces_compile():
    """Test that all interface modules can be imported."""
    # This will fail if interfaces have syntax errors
    from interfaces import (
        EventType, LaneType, TimeRange, TokenBudget,
        IndexEvent, DetailEvent, QueryRequest, QueryResult,
        ATFReader, ATFWriter, QueryEngine, TraceAnalyzer,
        TokenEstimator, TraceIndex
    )
    
    # Verify enums
    assert EventType.FUNCTION_ENTER
    assert LaneType.INDEX
    
    # Verify dataclasses
    time_range = TimeRange(0, 1000000)
    assert time_range.duration_ms() == 1.0
    
    budget = TokenBudget(4000)
    assert budget.remaining() == 4000
    assert budget.can_fit(100)
    
    # Verify factory functions exist
    from interfaces import (
        create_atf_reader,
        create_atf_writer,
        create_query_engine,
        create_token_estimator,
        create_trace_analyzer
    )
    
    # Just check they're callable
    assert callable(create_atf_reader)
    assert callable(create_atf_writer)
    assert callable(create_query_engine)

if __name__ == "__main__":
    test_interfaces_compile()
    print("✅ Query Engine interfaces compile successfully!")