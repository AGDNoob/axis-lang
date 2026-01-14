"""
AXIS Lexer
Deterministic scanner without backtracking.
"""

from enum import Enum, auto
from dataclasses import dataclass
from typing import Optional


class TokenType(Enum):
    # Keywords
    FN = auto()
    LET = auto()
    MUT = auto()
    RETURN = auto()
    IF = auto()
    ELSE = auto()
    WHILE = auto()
    BREAK = auto()
    CONTINUE = auto()
    SYSCALL = auto()
    
    # Types
    I8 = auto()
    I16 = auto()
    I32 = auto()
    I64 = auto()
    U8 = auto()
    U16 = auto()
    U32 = auto()
    U64 = auto()
    PTR = auto()
    BOOL = auto()
    
    # Operators
    PLUS = auto()       # +
    MINUS = auto()      # -
    STAR = auto()       # *
    SLASH = auto()      # /
    EQ = auto()         # ==
    NE = auto()         # !=
    LT = auto()         # <
    LE = auto()         # <=
    GT = auto()         # >
    GE = auto()         # >=
    ASSIGN = auto()     # =
    
    # Delimiters
    LPAREN = auto()     # (
    RPAREN = auto()     # )
    LBRACE = auto()     # {
    RBRACE = auto()     # }
    COLON = auto()      # :
    SEMICOLON = auto()  # ;
    COMMA = auto()      # ,
    ARROW = auto()      # ->
    
    # Literals
    INT_LITERAL = auto()
    IDENTIFIER = auto()
    
    # Special
    EOF = auto()


@dataclass
class Token:
    type: TokenType
    value: str
    line: int
    column: int
    
    def __repr__(self):
        return f"Token({self.type.name}, '{self.value}', {self.line}:{self.column})"


class Lexer:
    KEYWORDS = {
        'fn': TokenType.FN,
        'let': TokenType.LET,
        'mut': TokenType.MUT,
        'return': TokenType.RETURN,
        'if': TokenType.IF,
        'else': TokenType.ELSE,
        'while': TokenType.WHILE,
        'break': TokenType.BREAK,
        'continue': TokenType.CONTINUE,
        'syscall': TokenType.SYSCALL,
        # Types
        'i8': TokenType.I8,
        'i16': TokenType.I16,
        'i32': TokenType.I32,
        'i64': TokenType.I64,
        'u8': TokenType.U8,
        'u16': TokenType.U16,
        'u32': TokenType.U32,
        'u64': TokenType.U64,
        'ptr': TokenType.PTR,
        'bool': TokenType.BOOL,
    }
    
    def __init__(self, source: str):
        self.source = source
        self.pos = 0
        self.line = 1
        self.column = 1
        self.current_char = self.source[0] if source else None
    
    def error(self, msg: str):
        raise SyntaxError(f"Lexer Error at {self.line}:{self.column}: {msg}")
    
    def advance(self):
        """Advances position by one character"""
        if self.current_char == '\n':
            self.line += 1
            self.column = 1
        else:
            self.column += 1
        
        self.pos += 1
        if self.pos >= len(self.source):
            self.current_char = None
        else:
            self.current_char = self.source[self.pos]
    
    def peek(self, offset: int = 1) -> Optional[str]:
        """Looks ahead without changing position"""
        peek_pos = self.pos + offset
        if peek_pos >= len(self.source):
            return None
        return self.source[peek_pos]
    
    def skip_whitespace(self):
        """Skips whitespace"""
        while self.current_char and self.current_char.isspace():
            self.advance()
    
    def skip_comment(self):
        """Skips comments // until end of line"""
        if self.current_char == '/' and self.peek() == '/':
            while self.current_char and self.current_char != '\n':
                self.advance()
            if self.current_char == '\n':
                self.advance()
    
    def read_number(self) -> Token:
        """Reads integer literal (decimal, hex, binary)"""
        start_line = self.line
        start_column = self.column
        num_str = ''
        
        # Negative sign
        if self.current_char == '-':
            num_str += '-'
            self.advance()
        
        # Hex: 0x...
        if self.current_char == '0' and self.peek() in ['x', 'X']:
            num_str += self.current_char
            self.advance()
            num_str += self.current_char
            self.advance()
            
            if not (self.current_char and self.current_char in '0123456789abcdefABCDEF'):
                self.error("Invalid hex literal")
            
            while self.current_char and self.current_char in '0123456789abcdefABCDEF_':
                if self.current_char != '_':
                    num_str += self.current_char
                self.advance()
        
        # Binary: 0b...
        elif self.current_char == '0' and self.peek() in ['b', 'B']:
            num_str += self.current_char
            self.advance()
            num_str += self.current_char
            self.advance()
            
            if not (self.current_char and self.current_char in '01'):
                self.error("Invalid binary literal")
            
            while self.current_char and self.current_char in '01_':
                if self.current_char != '_':
                    num_str += self.current_char
                self.advance()
        
        # Decimal
        else:
            while self.current_char and (self.current_char.isdigit() or self.current_char == '_'):
                if self.current_char != '_':
                    num_str += self.current_char
                self.advance()
        
        return Token(TokenType.INT_LITERAL, num_str, start_line, start_column)
    
    def read_identifier(self) -> Token:
        """Reads identifier or keyword"""
        start_line = self.line
        start_column = self.column
        ident = ''
        
        # Identifier: [a-zA-Z_][a-zA-Z0-9_]*
        while self.current_char and (self.current_char.isalnum() or self.current_char == '_'):
            ident += self.current_char
            self.advance()
        
        # Check if keyword
        token_type = self.KEYWORDS.get(ident, TokenType.IDENTIFIER)
        return Token(token_type, ident, start_line, start_column)
    
    def next_token(self) -> Token:
        """Returns next token"""
        while self.current_char:
            # Whitespace
            if self.current_char.isspace():
                self.skip_whitespace()
                continue
            
            # Comments
            if self.current_char == '/' and self.peek() == '/':
                self.skip_comment()
                continue
            
            # Numbers
            if self.current_char.isdigit() or (self.current_char == '-' and self.peek() and self.peek().isdigit()):
                return self.read_number()
            
            # Identifiers / Keywords
            if self.current_char.isalpha() or self.current_char == '_':
                return self.read_identifier()
            
            # Operators and delimiters
            start_line = self.line
            start_column = self.column
            char = self.current_char
            
            # Two-character operators
            if char == '=' and self.peek() == '=':
                self.advance()
                self.advance()
                return Token(TokenType.EQ, '==', start_line, start_column)
            
            if char == '!' and self.peek() == '=':
                self.advance()
                self.advance()
                return Token(TokenType.NE, '!=', start_line, start_column)
            
            if char == '<' and self.peek() == '=':
                self.advance()
                self.advance()
                return Token(TokenType.LE, '<=', start_line, start_column)
            
            if char == '>' and self.peek() == '=':
                self.advance()
                self.advance()
                return Token(TokenType.GE, '>=', start_line, start_column)
            
            if char == '-' and self.peek() == '>':
                self.advance()
                self.advance()
                return Token(TokenType.ARROW, '->', start_line, start_column)
            
            # Single-character operators
            single_char_tokens = {
                '+': TokenType.PLUS,
                '-': TokenType.MINUS,
                '*': TokenType.STAR,
                '/': TokenType.SLASH,
                '=': TokenType.ASSIGN,
                '<': TokenType.LT,
                '>': TokenType.GT,
                '(': TokenType.LPAREN,
                ')': TokenType.RPAREN,
                '{': TokenType.LBRACE,
                '}': TokenType.RBRACE,
                ':': TokenType.COLON,
                ';': TokenType.SEMICOLON,
                ',': TokenType.COMMA,
            }
            
            if char in single_char_tokens:
                token_type = single_char_tokens[char]
                self.advance()
                return Token(token_type, char, start_line, start_column)
            
            # Unknown character
            self.error(f"Unexpected character: '{char}'")
        
        # EOF
        return Token(TokenType.EOF, '', self.line, self.column)
    
    def tokenize(self) -> list[Token]:
        """Generates Complete token list"""
        tokens = []
        while True:
            token = self.next_token()
            tokens.append(token)
            if token.type == TokenType.EOF:
                break
        return tokens


# Test function
if __name__ == '__main__':
    # Test 1: Simple function
    source1 = """
    fn main() -> i32 {
        let x: i32 = 10;
        return x;
    }
    """
    
    lexer = Lexer(source1)
    tokens = lexer.tokenize()
    
    print("Test 1: Simple Function")
    print("=" * 60)
    for token in tokens:
        print(token)
    print()
    
    # Test 2: With operators
    source2 = """
    fn add(a: i32, b: i32) -> i32 {
        return a + b;
    }
    """
    
    lexer2 = Lexer(source2)
    tokens2 = lexer2.tokenize()
    
    print("Test 2: With Operators")
    print("=" * 60)
    for token in tokens2:
        print(token)
    print()
    
    # Test 3: Hex and Binary Literals
    source3 = """
    let x: i32 = 0xFF;
    let y: i32 = 0b1010;
    let z: i32 = 42;
    """
    
    lexer3 = Lexer(source3)
    tokens3 = lexer3.tokenize()
    
    print("Test 3: Different Number Formats")
    print("=" * 60)
    for token in tokens3:
        print(token)
