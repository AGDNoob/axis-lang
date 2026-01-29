"""
AXIS Error Handling
Provides pretty error messages with source context
"""

from dataclasses import dataclass
from typing import Optional, List


@dataclass
class SourceLocation:
    """Tracks a position in source code"""
    line: int
    column: int
    end_line: Optional[int] = None
    end_column: Optional[int] = None
    
    def __str__(self):
        if self.end_line and self.end_line != self.line:
            return f"{self.line}:{self.column}-{self.end_line}:{self.end_column}"
        return f"{self.line}:{self.column}"


class AxisError(Exception):
    """Base class for all AXIS errors with source location support"""
    
    def __init__(self, message: str, location: Optional[SourceLocation] = None, 
                 source_lines: Optional[List[str]] = None, filename: Optional[str] = None):
        self.message = message
        self.location = location
        self.source_lines = source_lines
        self.filename = filename or "<unknown>"
        super().__init__(self.format_message())
    
    def format_message(self) -> str:
        """Format error with source context"""
        parts = []
        
        # Header: filename:line:column: error: message
        if self.location:
            parts.append(f"{self.filename}:{self.location}: error: {self.message}")
        else:
            parts.append(f"{self.filename}: error: {self.message}")
        
        # Source context
        if self.location and self.source_lines:
            line_num = self.location.line
            if 1 <= line_num <= len(self.source_lines):
                source_line = self.source_lines[line_num - 1]
                
                # Show line number and source
                line_prefix = f"  {line_num} | "
                parts.append(line_prefix + source_line)
                
                # Show caret pointing to error column
                col = self.location.column
                # Account for tabs in the source line
                spaces = ""
                for i, ch in enumerate(source_line[:col-1]):
                    spaces += "\t" if ch == "\t" else " "
                
                caret_line = " " * len(line_prefix) + spaces + "^"
                
                # If we have an end column, extend the underline
                if self.location.end_column and self.location.end_line == self.location.line:
                    underline_len = self.location.end_column - col
                    caret_line = " " * len(line_prefix) + spaces + "^" + "~" * max(0, underline_len - 1)
                
                parts.append(caret_line)
        
        return "\n".join(parts)


class LexerError(AxisError):
    """Error during tokenization"""
    pass


class ParseError(AxisError):
    """Error during parsing"""
    pass


class SemanticError(AxisError):
    """Error during semantic analysis"""
    pass


class CodeGenError(AxisError):
    """Error during code generation"""
    pass


# Global source storage for error reporting
_source_lines: List[str] = []
_source_filename: str = "<unknown>"


def set_source(source: str, filename: str = "<unknown>"):
    """Set the source code for error reporting"""
    global _source_lines, _source_filename
    _source_lines = source.split('\n')
    _source_filename = filename


def get_source_lines() -> List[str]:
    """Get the current source lines"""
    return _source_lines


def get_source_filename() -> str:
    """Get the current source filename"""
    return _source_filename


def make_error(error_class, message: str, line: int = None, column: int = None,
               end_line: int = None, end_column: int = None) -> AxisError:
    """Create an error with current source context"""
    location = None
    if line is not None:
        location = SourceLocation(line, column or 1, end_line, end_column)
    
    return error_class(
        message=message,
        location=location,
        source_lines=_source_lines,
        filename=_source_filename
    )


def format_error(e: Exception, show_traceback: bool = False) -> str:
    """Format an error for display"""
    import traceback
    
    if isinstance(e, AxisError):
        result = str(e)
    else:
        result = f"error: {e}"
    
    if show_traceback:
        result += "\n\nTraceback:\n" + traceback.format_exc()
    
    return result
