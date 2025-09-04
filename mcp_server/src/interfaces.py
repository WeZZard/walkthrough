"""
ADA MCP Server Interface Definitions

This module defines the COMPLETE interface contracts for the Model Context Protocol server.
All implementations MUST comply with these protocols.

Design Principles:
- Protocol-first for extensibility
- Async streaming for large traces
- Clear separation of concerns
- Type-safe contracts
"""

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import (
    AsyncIterator, Optional, Protocol, List, Dict, Any, 
    Union, Callable, TypeVar, Generic
)
import json
from datetime import datetime

# ============================================================================
# MCP Protocol Types
# ============================================================================

class MessageType(Enum):
    """MCP message types"""
    REQUEST = "request"
    RESPONSE = "response"
    NOTIFICATION = "notification"
    ERROR = "error"

class MethodName(Enum):
    """MCP method names for ADA"""
    # Trace control
    START_TRACE = "trace/start"
    STOP_TRACE = "trace/stop"
    ATTACH_PROCESS = "trace/attach"
    DETACH_PROCESS = "trace/detach"
    
    # Query operations
    QUERY_TRACE = "query/execute"
    GET_SUMMARY = "query/summary"
    FIND_ANOMALIES = "query/anomalies"
    
    # Data operations
    LIST_TRACES = "data/list"
    LOAD_TRACE = "data/load"
    EXPORT_TRACE = "data/export"
    
    # System operations
    GET_STATUS = "system/status"
    GET_CAPABILITIES = "system/capabilities"

@dataclass
class MCPMessage:
    """Base MCP message structure"""
    type: MessageType
    id: Optional[str] = None
    timestamp: datetime = field(default_factory=datetime.now)

@dataclass  
class MCPRequest:
    """MCP request message"""
    method: MethodName
    params: Dict[str, Any]
    type: MessageType = MessageType.REQUEST
    id: Optional[str] = None
    timestamp: datetime = field(default_factory=datetime.now)

@dataclass
class MCPResponse:
    """MCP response message"""
    type: MessageType = MessageType.RESPONSE
    result: Optional[Any] = None
    error: Optional['MCPError'] = None
    id: Optional[str] = None
    timestamp: datetime = field(default_factory=datetime.now)

@dataclass
class MCPError:
    """MCP error structure"""
    code: int
    message: str
    data: Optional[Dict[str, Any]] = None

@dataclass
class MCPNotification:
    """MCP notification message"""
    method: str
    params: Dict[str, Any]
    type: MessageType = MessageType.NOTIFICATION
    id: Optional[str] = None
    timestamp: datetime = field(default_factory=datetime.now)

# ============================================================================
# Server Configuration
# ============================================================================

@dataclass
class MCPServerConfig:
    """MCP server configuration"""
    host: str = "localhost"
    port: int = 8765
    max_connections: int = 10
    auth_required: bool = False
    tls_enabled: bool = False
    trace_output_dir: Path = Path("/tmp/ada_traces")
    max_trace_size_mb: int = 1000
    token_budget_default: int = 4000

# ============================================================================
# Connection Interface
# ============================================================================

class MCPConnection(Protocol):
    """Protocol for MCP client connections"""
    
    @property
    def connection_id(self) -> str:
        """Unique connection identifier"""
        ...
    
    @property
    def is_connected(self) -> bool:
        """Check if connection is active"""
        ...
    
    async def send_message(self, message: MCPMessage) -> None:
        """Send message to client"""
        ...
    
    async def receive_message(self) -> MCPMessage:
        """Receive message from client"""
        ...
    
    async def close(self) -> None:
        """Close the connection"""
        ...

# ============================================================================
# Request Handler Interface
# ============================================================================

HandlerResult = TypeVar('HandlerResult')

class RequestHandler(Protocol, Generic[HandlerResult]):
    """Protocol for request handlers"""
    
    @property
    def method(self) -> MethodName:
        """Method this handler processes"""
        ...
    
    async def validate_params(self, params: Dict[str, Any]) -> bool:
        """Validate request parameters"""
        ...
    
    async def handle(self, 
                    params: Dict[str, Any],
                    context: 'RequestContext') -> HandlerResult:
        """Handle the request and return result"""
        ...

@dataclass
class RequestContext:
    """Context for request handling"""
    connection: MCPConnection
    server: 'MCPServer'
    trace_manager: 'TraceManager'
    auth_info: Optional[Dict[str, Any]] = None

# ============================================================================
# Trace Management Interface
# ============================================================================

@dataclass
class TraceSession:
    """Active trace session"""
    session_id: str
    process_id: Optional[int]
    process_name: str
    start_time: datetime
    output_path: Path
    is_active: bool = True
    events_captured: int = 0
    bytes_written: int = 0

class TraceManager(ABC):
    """Abstract base for trace session management"""
    
    @abstractmethod
    async def create_session(self, 
                           process_path: str,
                           args: List[str]) -> TraceSession:
        """Create new trace session"""
        ...
    
    @abstractmethod
    async def attach_session(self, process_id: int) -> TraceSession:
        """Attach to existing process"""
        ...
    
    @abstractmethod
    async def stop_session(self, session_id: str) -> None:
        """Stop trace session"""
        ...
    
    @abstractmethod
    async def get_session(self, session_id: str) -> Optional[TraceSession]:
        """Get session by ID"""
        ...
    
    @abstractmethod
    async def list_sessions(self) -> List[TraceSession]:
        """List all sessions"""
        ...
    
    @abstractmethod
    async def list_traces(self) -> List[Path]:
        """List available trace files"""
        ...

# ============================================================================
# MCP Server Interface
# ============================================================================

class MCPServer(ABC):
    """Abstract base for MCP server implementation"""
    
    @abstractmethod
    async def initialize(self, config: MCPServerConfig) -> None:
        """Initialize the server"""
        ...
    
    @abstractmethod
    async def start(self) -> None:
        """Start accepting connections"""
        ...
    
    @abstractmethod
    async def stop(self) -> None:
        """Stop the server"""
        ...
    
    @abstractmethod
    async def register_handler(self, 
                              method: MethodName,
                              handler: RequestHandler) -> None:
        """Register a request handler"""
        ...
    
    @abstractmethod
    async def handle_connection(self, connection: MCPConnection) -> None:
        """Handle a client connection"""
        ...
    
    @abstractmethod
    async def broadcast_notification(self, 
                                    notification: MCPNotification) -> None:
        """Broadcast notification to all clients"""
        ...
    
    @abstractmethod
    def get_status(self) -> Dict[str, Any]:
        """Get server status"""
        ...
    
    @abstractmethod
    def get_capabilities(self) -> Dict[str, Any]:
        """Get server capabilities"""
        ...

# ============================================================================
# Streaming Interface
# ============================================================================

class StreamingResponse(Protocol):
    """Protocol for streaming responses"""
    
    async def write_chunk(self, data: bytes) -> None:
        """Write a chunk of data"""
        ...
    
    async def write_json(self, obj: Any) -> None:
        """Write JSON object as chunk"""
        ...
    
    async def close(self) -> None:
        """Close the stream"""
        ...

class StreamingHandler(Protocol):
    """Protocol for streaming request handlers"""
    
    async def handle_stream(self,
                          params: Dict[str, Any],
                          context: RequestContext,
                          stream: StreamingResponse) -> None:
        """Handle request with streaming response"""
        ...

# ============================================================================
# Authentication Interface
# ============================================================================

@dataclass
class AuthToken:
    """Authentication token"""
    token: str
    expires_at: Optional[datetime] = None
    scopes: List[str] = field(default_factory=list)

class Authenticator(Protocol):
    """Protocol for authentication"""
    
    async def authenticate(self, token: str) -> Optional[AuthToken]:
        """Authenticate a token"""
        ...
    
    async def authorize(self, 
                       token: AuthToken, 
                       method: MethodName) -> bool:
        """Check if token authorizes method"""
        ...

# ============================================================================
# Query Interface Integration
# ============================================================================

class MCPQueryHandler(ABC):
    """Bridge between MCP and query engine"""
    
    @abstractmethod
    async def execute_query(self,
                          trace_path: Path,
                          query: str,
                          token_budget: int) -> Dict[str, Any]:
        """Execute query on trace"""
        ...
    
    @abstractmethod
    async def get_summary(self,
                         trace_path: Path,
                         token_budget: int) -> str:
        """Get trace summary"""
        ...
    
    @abstractmethod  
    async def find_anomalies(self,
                           trace_path: Path,
                           token_budget: int) -> List[Dict[str, Any]]:
        """Find anomalies in trace"""
        ...

# ============================================================================
# WebSocket Interface
# ============================================================================

class WebSocketConnection(MCPConnection):
    """WebSocket-based MCP connection"""
    
    @abstractmethod
    async def send_text(self, text: str) -> None:
        """Send text message"""
        ...
    
    @abstractmethod
    async def send_bytes(self, data: bytes) -> None:
        """Send binary message"""
        ...
    
    @abstractmethod
    async def receive_text(self) -> str:
        """Receive text message"""
        ...
    
    @abstractmethod
    async def receive_bytes(self) -> bytes:
        """Receive binary message"""
        ...

# ============================================================================
# Factory Functions
# ============================================================================

def create_mcp_server(config: MCPServerConfig) -> MCPServer:
    """Create default MCP server implementation"""
    from .mcp_server_impl import DefaultMCPServer
    return DefaultMCPServer(config)

def create_trace_manager() -> TraceManager:
    """Create default trace manager"""
    from .trace_manager_impl import DefaultTraceManager
    return DefaultTraceManager()

def create_mcp_query_handler() -> MCPQueryHandler:
    """Create default query handler"""
    from .mcp_query_handler_impl import DefaultMCPQueryHandler
    return DefaultMCPQueryHandler()

def create_authenticator() -> Authenticator:
    """Create default authenticator"""
    from .auth_impl import DefaultAuthenticator
    return DefaultAuthenticator()

# ============================================================================
# Standard Handlers
# ============================================================================

def register_standard_handlers(server: MCPServer) -> None:
    """Register all standard ADA handlers"""
    from .handlers import (
        StartTraceHandler,
        StopTraceHandler,
        QueryTraceHandler,
        GetSummaryHandler,
        ListTracesHandler,
        GetStatusHandler,
        GetCapabilitiesHandler
    )
    
    handlers = [
        StartTraceHandler(),
        StopTraceHandler(),
        QueryTraceHandler(),
        GetSummaryHandler(),
        ListTracesHandler(),
        GetStatusHandler(),
        GetCapabilitiesHandler(),
    ]
    
    for handler in handlers:
        server.register_handler(handler.method, handler)

# ============================================================================
# Interface Validation
# ============================================================================

def validate_interfaces():
    """Validate that all interfaces are properly defined"""
    required_protocols = [
        MCPConnection, RequestHandler, StreamingResponse,
        StreamingHandler, Authenticator
    ]
    
    required_classes = [
        MCPServer, TraceManager, MCPQueryHandler,
        MessageType, MethodName, MCPMessage, MCPRequest,
        MCPResponse, MCPError, MCPNotification, TraceSession,
        MCPServerConfig, RequestContext, AuthToken
    ]
    
    for protocol in required_protocols:
        assert hasattr(protocol, '__annotations__')
    
    for cls in required_classes:
        assert cls is not None
    
    return True

# Run validation at import
assert validate_interfaces()