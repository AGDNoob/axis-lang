"""
AXIS Lexer - tokenization engine
deterministic scanner, kein backtracking nötig
"""

from enum import Enum, auto
from dataclasses import dataclass
from typing import Optional


class TokenType(Enum):
    FUNC = auto()
    GIVE = auto()
    WHEN = auto()
    ELSE = auto()
    WHILE = auto()
    LOOP = auto()
    REPEAT = auto()
    BREAK = auto()
    CONTINUE = auto()
    SYSCALL = auto()
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
    STR = auto()        # str type
    TRUE = auto()
    FALSE = auto()
    WRITE = auto()      # write()
    WRITELN = auto()    # writeln()
    READ = auto()       # read()
    READLN = auto()     # readln()
    READCHAR = auto()   # readchar()
    READ_FAILED = auto() # read_failed()
    MODE = auto()       # mode
    SCRIPT = auto()     # script
    COMPILE = auto()    # compile
    PLUS = auto()       # +
    MINUS = auto()      # -
    STAR = auto()       # *
    SLASH = auto()      # /
    PERCENT = auto()    # %
    AMPERSAND = auto()  # &
    PIPE = auto()       # |
    CARET = auto()      # ^
    LSHIFT = auto()     # <<
    RSHIFT = auto()     # >>
    EQ = auto()         # ==
    NE = auto()         # !=
    LT = auto()         # <
    LE = auto()         # <=
    GT = auto()         # >
    GE = auto()         # >=
    ASSIGN = auto()     # =
    BANG = auto()       # !
    LPAREN = auto()     # (
    RPAREN = auto()     # )
    LBRACE = auto()     # {
    RBRACE = auto()     # }
    COLON = auto()      # :
    SEMICOLON = auto()  # ;
    COMMA = auto()      # ,
    ARROW = auto()      # ->
    INT_LITERAL = auto()
    STRING_LITERAL = auto()  # "..."
    IDENTIFIER = auto()
    INDENT = auto()
    DEDENT = auto()
    NEWLINE = auto()
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
    # alle keywords hier, ziemlich straightforward
    KEYWORDS = {
        'func': TokenType.FUNC,
        'give': TokenType.GIVE,
        'when': TokenType.WHEN,
        'else': TokenType.ELSE,
        'while': TokenType.WHILE,
        'loop': TokenType.LOOP,
        'repeat': TokenType.REPEAT,
        'break': TokenType.BREAK,
        'stop': TokenType.BREAK,      # alias for break
        'continue': TokenType.CONTINUE,
        'skip': TokenType.CONTINUE,   # alias for continue
        'syscall': TokenType.SYSCALL,
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
        'str': TokenType.STR,
        'True': TokenType.TRUE,
        'False': TokenType.FALSE,
        'write': TokenType.WRITE,
        'writeln': TokenType.WRITELN,
        'read': TokenType.READ,
        'readln': TokenType.READLN,
        'readchar': TokenType.READCHAR,
        'read_failed': TokenType.READ_FAILED,
        'mode': TokenType.MODE,
        'script': TokenType.SCRIPT,
        'compile': TokenType.COMPILE,
    }

    def __init__(self, source: str):
        # lexer state - pos tracken und so
        self.source = source
        self.pos = 0
        self.line = 1
        self.column = 1
        self.current_char = self.source[0] if source else None
        # indentation tracking für Python-style blocks
        self.indent_stack = [0]
        self.at_line_start = True
        self.pending_tokens = []
    
    def error(self, msg: str):
        raise SyntaxError(f"Lexer Error at {self.line}:{self.column}: {msg}")
    
    def advance(self):
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
        peek_pos = self.pos + offset
        if peek_pos >= len(self.source):
            return None
        return self.source[peek_pos]
    
    def skip_whitespace_inline(self):
        # whitespace überspringen, aber nicht newlines (wichtig für indentation)
        while self.current_char and self.current_char in ' \t\r':
            self.advance()
    
    def handle_indentation(self):
        # indentation level am line start berechnen
        if not self.at_line_start:
            return None
        
        self.at_line_start = False
        indent_level = 0
        
        while self.current_char in ' \t':
            if self.current_char == ' ':
                indent_level += 1
            elif self.current_char == '\t':
                indent_level += 4  # tabs = 4 spaces
            self.advance()
        
        # leere zeilen und comments ignorieren
        if self.current_char in ('\n', None):
            return None
        if self.current_char == '/' and self.peek() == '/':
            self.skip_comment()
            return None
        if self.current_char == '#':
            self.skip_comment()
            return None
        
        # indentation mit stack vergleichen
        current_indent = self.indent_stack[-1]
        
        if indent_level > current_indent:
            self.indent_stack.append(indent_level)
            return Token(TokenType.INDENT, '', self.line, 1)
        elif indent_level < current_indent:
            # mehrere DEDENTs möglich
            dedent_tokens = []
            while self.indent_stack and self.indent_stack[-1] > indent_level:
                self.indent_stack.pop()
                dedent_tokens.append(Token(TokenType.DEDENT, '', self.line, 1))
            
            if self.indent_stack[-1] != indent_level:
                self.error(f"Indentation error: level {indent_level} doesn't match any outer level")
            
            # erstes DEDENT returnen, rest in pending queue
            if dedent_tokens:
                self.pending_tokens.extend(dedent_tokens[1:])
                return dedent_tokens[0]
        
        return None
    
    def skip_comment(self):
        # kommentare skippen - // und # beide ok
        if (self.current_char == '/' and self.peek() == '/') or self.current_char == '#':
            while self.current_char and self.current_char != '\n':
                self.advance()
            # newline nicht konsumieren, main loop macht das
    
    def read_number(self) -> Token:
        start_line = self.line
        start_column = self.column
        num_str = ''
        
        if self.current_char == '-':
            num_str += '-'
            self.advance()
        
        # hex literals mit 0x prefix
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
        
        # binary literals mit 0b prefix
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
        
        else:
            while self.current_char and (self.current_char.isdigit() or self.current_char == '_'):
                if self.current_char != '_':
                    num_str += self.current_char
                self.advance()
        
        return Token(TokenType.INT_LITERAL, num_str, start_line, start_column)
    
    def read_identifier(self) -> Token:
        # identifiers lesen, dann keyword check
        start_line = self.line
        start_column = self.column
        ident = ''
        
        while self.current_char and (self.current_char.isalnum() or self.current_char == '_'):
            ident += self.current_char
            self.advance()
        
        token_type = self.KEYWORDS.get(ident, TokenType.IDENTIFIER)
        return Token(token_type, ident, start_line, start_column)
    
    def read_string(self) -> Token:
        # string literal lesen mit escape sequences
        start_line = self.line
        start_column = self.column
        self.advance()  # opening "
        
        string_content = ''
        while self.current_char and self.current_char != '"':
            if self.current_char == '\\':
                self.advance()
                if not self.current_char:
                    self.error("Unterminated string escape")
                # escape sequences
                escape_map = {
                    'n': '\n',
                    't': '\t',
                    'r': '\r',
                    '\\': '\\',
                    '"': '"',
                    '0': '\0',
                }
                if self.current_char in escape_map:
                    string_content += escape_map[self.current_char]
                else:
                    self.error(f"Unknown escape sequence: \\{self.current_char}")
                self.advance()
            elif self.current_char == '\n':
                self.error("Unterminated string literal")
            else:
                string_content += self.current_char
                self.advance()
        
        if not self.current_char:
            self.error("Unterminated string literal")
        
        self.advance()  # closing "
        return Token(TokenType.STRING_LITERAL, string_content, start_line, start_column)
    
    def next_token(self) -> Token:
        # pending tokens von DEDENT handling returnen
        if self.pending_tokens:
            return self.pending_tokens.pop(0)
        
        while self.current_char:
            # handle indentation at line start
            if self.at_line_start:
                indent_token = self.handle_indentation()
                if indent_token:
                    return indent_token
                # falls keine indentation token, weitermachen
            
            # newline als token behandeln (wichtig für statement separation)
            if self.current_char == '\n':
                line_num = self.line
                col_num = self.column
                self.advance()
                self.at_line_start = True
                # NEWLINE nur returnen wenn nicht am file start
                if line_num > 1 or col_num > 1:
                    return Token(TokenType.NEWLINE, '\\n', line_num, col_num)
                continue
            
            # whitespace überspringen (aber nicht newlines)
            if self.current_char in ' \t\r':
                self.skip_whitespace_inline()
                continue
            
            if self.current_char == '/' and self.peek() == '/':
                self.skip_comment()
                continue
            
            if self.current_char == '#':
                self.skip_comment()
                continue
            
            # string literals mit "..."
            if self.current_char == '"':
                return self.read_string()
            
            if self.current_char.isdigit() or (self.current_char == '-' and self.peek() and self.peek().isdigit()):
                return self.read_number()
            
            if self.current_char.isalpha() or self.current_char == '_':
                return self.read_identifier()
            
            start_line = self.line
            start_column = self.column
            char = self.current_char
            if char == '=' and self.peek() == '=':
                self.advance()
                self.advance()
                return Token(TokenType.EQ, '==', start_line, start_column)
            
            if char == '!' and self.peek() == '=':
                self.advance()
                self.advance()
                return Token(TokenType.NE, '!=', start_line, start_column)
            
            # Single ! for boolean NOT
            if char == '!':
                self.advance()
                return Token(TokenType.BANG, '!', start_line, start_column)
            
            if char == '<' and self.peek() == '<':
                self.advance()
                self.advance()
                return Token(TokenType.LSHIFT, '<<', start_line, start_column)
            
            if char == '<' and self.peek() == '=':
                self.advance()
                self.advance()
                return Token(TokenType.LE, '<=', start_line, start_column)
            
            if char == '>' and self.peek() == '>':
                self.advance()
                self.advance()
                return Token(TokenType.RSHIFT, '>>', start_line, start_column)
            
            if char == '>' and self.peek() == '=':
                self.advance()
                self.advance()
                return Token(TokenType.GE, '>=', start_line, start_column)
            
            if char == '-' and self.peek() == '>':
                self.advance()
                self.advance()
                return Token(TokenType.ARROW, '->', start_line, start_column)
            
            single_char_tokens = {
                '+': TokenType.PLUS,
                '-': TokenType.MINUS,
                '*': TokenType.STAR,
                '/': TokenType.SLASH,
                '%': TokenType.PERCENT,
                '&': TokenType.AMPERSAND,
                '|': TokenType.PIPE,
                '^': TokenType.CARET,
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
            
            self.error(f"Unexpected character: '{char}'")
        
        # bei EOF alle übrigen DEDENTs emittieren
        dedents = []
        while len(self.indent_stack) > 1:
            self.indent_stack.pop()
            dedents.append(Token(TokenType.DEDENT, '', self.line, self.column))
        
        if dedents:
            self.pending_tokens.extend(dedents[1:])
            return dedents[0]
        
        return Token(TokenType.EOF, '', self.line, self.column)
    
    def tokenize(self) -> list[Token]:
        # convenience method - gibt alle tokens zurück
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
