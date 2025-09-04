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

def test_mcp_interfaces_compile():
    """Test that MCP interfaces compile."""
    import sys
    mcp_path = Path(__file__).parent.parent.parent / "mcp_server" / "src"
    
    # Remove existing paths that might conflict
    original_path = sys.path.copy()
    sys.path = [str(mcp_path)] + [p for p in sys.path if 'query_engine' not in p]
    
    # Reimport to get the MCP interfaces module
    import importlib
    if 'interfaces' in sys.modules:
        del sys.modules['interfaces']
    
    from interfaces import (
        MessageType, MethodName, MCPMessage, MCPRequest,
        MCPResponse, MCPError, MCPNotification, TraceSession,
        MCPServerConfig, RequestContext, AuthToken,
        MCPServer, TraceManager, MCPQueryHandler
    )
    
    # Verify enums
    assert MessageType.REQUEST
    assert MethodName.START_TRACE
    
    # Verify dataclasses  
    config = MCPServerConfig()
    assert config.port == 8765
    assert config.max_connections == 10
    
    # Verify factory functions
    from interfaces import (
        create_mcp_server,
        create_trace_manager,
        create_mcp_query_handler,
        create_authenticator
    )
    
    assert callable(create_mcp_server)
    assert callable(create_trace_manager)
    
    # Restore original path
    sys.path = original_path

if __name__ == "__main__":
    test_interfaces_compile()
    print("✅ Query Engine interfaces compile successfully!")
    test_mcp_interfaces_compile()
    print("✅ MCP Server interfaces compile successfully!")
    print("\n✅ All Python interfaces compile successfully!")