"""
ADA Query Engine - Python interface for trace analysis
"""

# Import the Rust extension module
try:
    from .query_engine import __version__
except ImportError:
    __version__ = "0.1.0"

# Keep the ping function for backward compatibility
def ping() -> str:
    return "pong"

__all__ = [
    "__version__",
    "ping",
]