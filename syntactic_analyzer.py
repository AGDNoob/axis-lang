"""
AXIS Parser - syntactic analyzer
LL(1) recursive descent, ein lookahead reicht
AST raus, keine types oder codegen hier
"""

from dataclasses import dataclass
from typing import Optional, List
from tokenization_engine import Lexer, Token, TokenType

@dataclass
class ASTNode:
    """Base class für alle AST-Knoten"""
    pass

@dataclass
class Program(ASTNode):
    mode: str  # "script" or "compile"
    functions: List['Function']
    statements: List['Statement']  # Top-level statements (script mode only)
    field_defs: List['FieldDef'] = None  # Field type definitions
    enum_defs: List['EnumDef'] = None  # Enum type definitions

    def __post_init__(self):
        if self.field_defs is None:
            self.field_defs = []
        if self.enum_defs is None:
            self.enum_defs = []


@dataclass
class Parameter(ASTNode):
    """Function parameter with optional modifier"""
    name: str
    type: str  # scalar type or 'array'
    modifier: Optional[str]  # 'update', 'copy', or None
    array_type: Optional['ArrayType'] = None  # set if this is an array parameter
    stack_offset: int = 0  # set by semantic analyzer


@dataclass
class Function(ASTNode):
    name: str
    params: List['Parameter']
    return_type: Optional[str]
    body: 'Block'


@dataclass
class Block(ASTNode):
    statements: List['Statement']


@dataclass
class Statement(ASTNode):
    pass


@dataclass
class VarDecl(Statement):
    name: str
    type: str  # scalar type like 'i32' OR 'array' for arrays
    mutable: bool
    init: Optional['Expression']
    array_type: Optional['ArrayType'] = None  # set if this is an array declaration


@dataclass
class Assignment(Statement):
    target: 'Expression'
    value: 'Expression'


@dataclass
class Return(Statement):
    value: Optional['Expression']


@dataclass
class If(Statement):
    condition: 'Expression'
    then_block: Block
    else_block: Optional[Block]


@dataclass
class While(Statement):
    condition: 'Expression'
    body: Block


@dataclass
class Break(Statement):
    pass


@dataclass
class Continue(Statement):
    pass


@dataclass
class Write(Statement):
    value: 'Expression'  # was ausgegeben wird
    newline: bool        # True für writeln


@dataclass
class ExprStatement(Statement):
    expression: 'Expression'


@dataclass
class Expression(ASTNode):
    pass


@dataclass
class BinaryOp(Expression):
    left: Expression
    op: str
    right: Expression


@dataclass
class UnaryOp(Expression):
    op: str
    operand: Expression


@dataclass
class Literal(Expression):
    value: str
    type: str


@dataclass
class StringLiteral(Expression):
    value: str  # der string content ohne quotes


@dataclass
class Read(Expression):
    """read() - read until EOF"""
    pass


@dataclass
class Readln(Expression):
    """readln() - read one line until \n"""
    pass


@dataclass
class Readchar(Expression):
    """readchar() - read single byte, returns -1 for EOF"""
    pass


@dataclass
class ReadFailed(Expression):
    """read_failed() - returns bool indicating if last read failed"""
    pass


@dataclass
class Identifier(Expression):
    name: str


@dataclass
class Call(Expression):
    name: str
    args: List[Expression]


# Pointer-related AST nodes removed in v1.1.0
# AXIS uses value-oriented semantics instead


@dataclass
class CopyExpr(Expression):
    """
    copy <expr> - explicit value/array copy
    copy.runtime <expr> - optimized for runtime performance (default)
    copy.compile <expr> - optimized for faster compilation
    """
    operand: Expression
    mode: str = 'runtime'  # 'runtime' or 'compile'


@dataclass
class ArrayType:
    """Array type: (i32; 5) or [i32] etc."""
    element_type: str
    size: Optional[int]  # None for dynamic/inferred size


@dataclass
class ArrayLiteral(Expression):
    """Array literal: [1, 2, 3, 4]"""
    elements: List[Expression]


@dataclass
class IndexAccess(Expression):
    """Array index access: arr[0]"""
    array: Expression
    index: Expression


# =============================================================================
# Field-related AST nodes (v1.1.0)
# =============================================================================

@dataclass
class FieldMember:
    """A member of a field type"""
    name: str
    type: str  # scalar type like 'i32', or 'array', or 'field' for nested
    default_value: Optional[Expression] = None
    array_type: Optional['ArrayType'] = None  # set if this is an array member
    inline_field: Optional['FieldDef'] = None  # set if this is an inline nested field


@dataclass
class FieldDef(ASTNode):
    """
    Field type definition:
    Vec2: field:
        x: i32 = 0
        y: i32 = 0
    """
    name: Optional[str]  # None for anonymous inline fields
    members: List['FieldMember']


@dataclass
class FieldAccess(Expression):
    """Field member access: player.position.x"""
    object: Expression
    member: str


@dataclass
class FieldInstance(Expression):
    """
    Instantiation of a field type, auto-generated from VarDecl with field type.
    Tracks which field type it is.
    """
    field_type: str  # name of the field type


# =============================================================================
# Enum-related AST nodes (v1.2.0)
# =============================================================================

@dataclass
class EnumVariant:
    """A variant of an enum type"""
    name: str
    value: Optional[int] = None  # explicit value, or auto-assigned


@dataclass
class EnumDef(ASTNode):
    """
    Enum type definition:
    Color: enum:           # defaults to i32
        Red
        Green
        Blue = 10
    
    Status: enum u8:       # explicitly u8
        Active
        Inactive
    """
    name: str
    variants: List['EnumVariant']
    underlying_type: str = 'i32'  # default to i32, can be i8-i64 or u8-u64


@dataclass
class EnumAccess(Expression):
    """Access to an enum variant: Color.Red"""
    enum_name: str
    variant_name: str


# =============================================================================
# Match-related AST nodes (v1.2.0)
# =============================================================================

@dataclass
class MatchArm:
    """A single arm of a match statement/expression"""
    pattern: 'Expression'  # The pattern to match (or None for wildcard _)
    is_wildcard: bool      # True if this is the _ (default) case
    body: 'Block'          # The block to execute if matched


@dataclass
class Match(Statement):
    """
    Match statement:
    match value:
        1:
            do_something()
        2:
            do_other()
        _:
            default_action()
    """
    value: Expression
    arms: List['MatchArm']


class Parser:
    # recursive descent parser - LL(1) mit einem lookahead
    def __init__(self, tokens: List[Token]):
        self.tokens = tokens
        self.pos = 0
        self.current = self.tokens[0] if tokens else None
    
    def error(self, msg: str):
        if self.current:
            raise SyntaxError(f"Parse Error at {self.current.line}:{self.current.column}: {msg}")
        else:
            raise SyntaxError(f"Parse Error: {msg}")
    
    def advance(self):
        self.pos += 1
        if self.pos < len(self.tokens):
            self.current = self.tokens[self.pos]
        else:
            self.current = None
    
    def expect(self, token_type: TokenType) -> Token:
        if not self.current or self.current.type != token_type:
            self.error(f"Expected {token_type.name}, got {self.current.type.name if self.current else 'EOF'}")
        token = self.current
        self.advance()
        return token
    
    def match(self, *token_types: TokenType) -> bool:
        if not self.current:
            return False
        return self.current.type in token_types
    
    def peek(self, offset: int = 1) -> Optional[Token]:
        """Look ahead in the token stream"""
        peek_pos = self.pos + offset
        if peek_pos >= len(self.tokens):
            return None
        return self.tokens[peek_pos]
    
    def skip_newlines(self):
        # newlines überspringen - wichtig vor/nach blocks
        while self.match(TokenType.NEWLINE):
            self.advance()
    
    def parse(self) -> Program:
        functions = []
        statements = []
        field_defs = []
        enum_defs = []
        self.skip_newlines()  # skip initial newlines
        
        # Parse mode declaration (optional, default = compile)
        mode = "compile"
        if self.match(TokenType.MODE):
            self.advance()
            if self.match(TokenType.SCRIPT):
                mode = "script"
                self.advance()
            elif self.match(TokenType.COMPILE):
                mode = "compile"
                self.advance()
            else:
                raise SyntaxError(f"Expected 'script' or 'compile' after 'mode', got {self.current}")
            self.skip_newlines()
        
        # Parse content based on mode
        while self.current and self.current.type != TokenType.EOF:
            if self.match(TokenType.FUNC):
                functions.append(self.parse_function())
            # Check for field definition: Name: field:
            elif self.match(TokenType.IDENTIFIER):
                next_tok = self.peek(1)
                next_next_tok = self.peek(2)
                if (next_tok and next_tok.type == TokenType.COLON and
                    next_next_tok and next_next_tok.type == TokenType.FIELD):
                    field_defs.append(self.parse_field_def())
                # Check for enum definition: Name: enum:
                elif (next_tok and next_tok.type == TokenType.COLON and
                      next_next_tok and next_next_tok.type == TokenType.ENUM):
                    enum_defs.append(self.parse_enum_def())
                elif mode == "script":
                    statements.append(self.parse_statement())
                else:
                    raise SyntaxError(f"Unexpected token in compile mode (only functions, fields, and enums allowed): {self.current}")
            elif mode == "script":
                # Script mode: allow top-level statements
                statements.append(self.parse_statement())
            else:
                # Compile mode: only functions, fields, and enums allowed
                raise SyntaxError(f"Unexpected token in compile mode (only functions, fields, and enums allowed): {self.current}")
            self.skip_newlines()
        
        # Validate: compile mode requires main()
        if mode == "compile":
            has_main = any(f.name == "main" for f in functions)
            if not has_main:
                raise SyntaxError("Compile mode requires a 'func main()' definition")
        
        return Program(mode, functions, statements, field_defs, enum_defs)
    
    def parse_function(self) -> Function:
        self.expect(TokenType.FUNC)
        name = self.expect(TokenType.IDENTIFIER).value
        
        self.expect(TokenType.LPAREN)
        params = self.parse_params()
        self.expect(TokenType.RPAREN)
        
        return_type = None
        # Support both old syntax (-> type) and new syntax (type directly)
        if self.match(TokenType.ARROW):
            self.advance()
            return_type = self.parse_type()
        elif not self.match(TokenType.COLON):
            # New syntax: func add(a: i32, b: i32) i32:
            return_type = self.parse_type()
        
        self.expect(TokenType.COLON)
        self.skip_newlines()
        body = self.parse_block()
        
        return Function(name, params, return_type, body)
    
    def parse_field_def(self, anonymous: bool = False) -> 'FieldDef':
        """
        Parse a field definition:
        Name: field:
            x: i32 = 0
            y: i32 = 0
        
        Or anonymous (inline):
        field:
            x: i32 = 0
        """
        if anonymous:
            name = None
        else:
            name = self.expect(TokenType.IDENTIFIER).value
            self.expect(TokenType.COLON)
        
        self.expect(TokenType.FIELD)
        self.expect(TokenType.COLON)
        self.skip_newlines()
        
        # Parse field body (indented block of members)
        members = self.parse_field_members()
        
        return FieldDef(name, members)
    
    def parse_enum_def(self) -> 'EnumDef':
        """
        Parse an enum definition:
        Color: enum:           # defaults to i32
            Red
            Green = 1
            Blue
        
        Status: enum u8:       # explicitly u8
            Active
            Inactive
        """
        name = self.expect(TokenType.IDENTIFIER).value
        self.expect(TokenType.COLON)
        self.expect(TokenType.ENUM)
        
        # Check for optional underlying type (e.g., enum u8:)
        underlying_type = 'i32'  # default
        type_tokens = {
            TokenType.I8: 'i8', TokenType.I16: 'i16', TokenType.I32: 'i32', TokenType.I64: 'i64',
            TokenType.U8: 'u8', TokenType.U16: 'u16', TokenType.U32: 'u32', TokenType.U64: 'u64'
        }
        if self.current and self.current.type in type_tokens:
            underlying_type = type_tokens[self.current.type]
            self.advance()
        
        self.expect(TokenType.COLON)
        self.skip_newlines()
        
        # Parse enum body (indented block of variants)
        variants = self.parse_enum_variants()
        
        return EnumDef(name, variants, underlying_type)
    
    def parse_enum_variants(self) -> List['EnumVariant']:
        """Parse the variants of an enum definition"""
        self.expect(TokenType.INDENT)
        variants = []
        next_value = 0  # auto-assign values starting from 0
        
        while not self.match(TokenType.DEDENT):
            if not self.current:
                self.error("Unexpected EOF in enum definition")
            if self.match(TokenType.NEWLINE):
                self.advance()
                continue
            
            variant = self.parse_enum_variant(next_value)
            variants.append(variant)
            
            # Update next_value for auto-assignment
            if variant.value is not None:
                next_value = variant.value + 1
            else:
                variant.value = next_value
                next_value += 1
        
        self.expect(TokenType.DEDENT)
        return variants
    
    def parse_enum_variant(self, default_value: int) -> 'EnumVariant':
        """
        Parse a single enum variant:
        VariantName
        VariantName = 10
        """
        name = self.expect(TokenType.IDENTIFIER).value
        
        value = None
        if self.match(TokenType.ASSIGN):
            self.advance()
            # Expect an integer literal
            if not self.match(TokenType.INT_LITERAL):
                self.error("Expected integer literal for enum variant value")
            value = int(self.current.value)
            self.advance()
        
        self.skip_newlines()
        return EnumVariant(name, value)
    
    def parse_field_members(self) -> List['FieldMember']:
        """Parse the members of a field definition"""
        self.expect(TokenType.INDENT)
        members = []
        
        while not self.match(TokenType.DEDENT):
            if not self.current:
                self.error("Unexpected EOF in field definition")
            if self.match(TokenType.NEWLINE):
                self.advance()
                continue
            
            member = self.parse_field_member()
            members.append(member)
        
        self.expect(TokenType.DEDENT)
        return members
    
    def parse_field_member(self) -> 'FieldMember':
        """
        Parse a single field member:
        name: type = default_value
        name: field:  # inline nested field
            ...
        name: (type; size) = [...]  # array member
        name: (field; size): [...]  # array of inline fields
        """
        name = self.expect(TokenType.IDENTIFIER).value
        self.expect(TokenType.COLON)
        
        # Check for inline field: name: field:
        if self.match(TokenType.FIELD):
            # Inline nested field
            inline_field = self.parse_field_def(anonymous=True)
            self.skip_newlines()
            return FieldMember(name, 'field', None, None, inline_field)
        
        # Check for array type
        if self.match(TokenType.LPAREN):
            self.advance()  # consume (
            
            # Could be (type; size) or (field; size)
            if self.match(TokenType.FIELD):
                # Array of inline fields: (field; size): [...]
                self.advance()  # consume field
                self.expect(TokenType.SEMICOLON)
                size = int(self.expect(TokenType.INT_LITERAL).value)
                self.expect(TokenType.RPAREN)
                self.expect(TokenType.COLON)
                
                # Parse inline field body in [...]
                inline_field = self.parse_inline_array_field()
                array_type = ArrayType('field', size)
                self.skip_newlines()
                return FieldMember(name, 'array', None, array_type, inline_field)
            else:
                # Regular array type: (type; size)
                element_type = self.parse_type()
                size = None
                if self.match(TokenType.SEMICOLON):
                    self.advance()
                    size = int(self.expect(TokenType.INT_LITERAL).value)
                self.expect(TokenType.RPAREN)
                
                array_type = ArrayType(element_type, size)
                
                # Check for default value
                default_value = None
                if self.match(TokenType.ASSIGN):
                    self.advance()
                    default_value = self.parse_expression()
                
                self.skip_newlines()
                return FieldMember(name, 'array', default_value, array_type, None)
        
        # Scalar type with optional default value
        type_name = self.parse_type()
        
        default_value = None
        if self.match(TokenType.ASSIGN):
            self.advance()
            default_value = self.parse_expression()
        
        self.skip_newlines()
        return FieldMember(name, type_name, default_value, None, None)
    
    def parse_inline_array_field(self) -> 'FieldDef':
        """
        Parse inline field definition in square brackets for arrays of fields:
        (field; 11): [
            name: str = ""
            number: i32 = 0
        ]
        """
        self.expect(TokenType.LBRACKET)
        self.skip_newlines()
        
        members = []
        
        # Parse members until ]
        while not self.match(TokenType.RBRACKET):
            if not self.current:
                self.error("Unexpected EOF in inline field array")
            if self.match(TokenType.NEWLINE):
                self.advance()
                continue
            if self.match(TokenType.INDENT):
                self.advance()  # Skip indent tokens inside [...]
                continue
            if self.match(TokenType.DEDENT):
                self.advance()  # Skip dedent tokens inside [...]
                continue
            
            member = self.parse_field_member()
            members.append(member)
        
        self.expect(TokenType.RBRACKET)
        return FieldDef(None, members)  # Anonymous field definition

    def parse_params(self) -> List['Parameter']:
        # parameter list parsen - [update/copy] name: type, ...
        params = []
        
        if not self.match(TokenType.RPAREN):
            param = self._parse_single_param()
            params.append(param)
            
            while self.match(TokenType.COMMA):
                self.advance()
                param = self._parse_single_param()
                params.append(param)
        
        return params
    
    def _parse_single_param(self) -> 'Parameter':
        """Parse a single parameter with optional modifier"""
        # Check for modifier (update or copy)
        modifier = None
        if self.match(TokenType.UPDATE):
            modifier = 'update'
            self.advance()
        elif self.match(TokenType.COPY):
            modifier = 'copy'
            self.advance()
        
        name = self.expect(TokenType.IDENTIFIER).value
        self.expect(TokenType.COLON)
        
        # Parse type (could be scalar or array)
        type_name, array_type = self.parse_type_or_array_type()
        
        return Parameter(name, type_name, modifier, array_type)
    
    def parse_type(self) -> str:
        # type names - i8, i32, i64, u32, str, etc (no ptr in v1.1.0)
        # Also handles field type names (any identifier)
        type_tokens = [
            TokenType.I8, TokenType.I16, TokenType.I32, TokenType.I64,
            TokenType.U8, TokenType.U16, TokenType.U32, TokenType.U64,
            TokenType.BOOL, TokenType.STR
        ]
        
        if self.match(*type_tokens):
            type_token = self.current
            self.advance()
            return type_token.value
        elif self.match(TokenType.IDENTIFIER):
            # This could be a field type name (validated in semantic analysis)
            type_name = self.current.value
            self.advance()
            return type_name
        else:
            self.error(f"Expected type, got {self.current.type.name if self.current else 'EOF'}")
    
    def parse_type_or_array_type(self) -> tuple[str, Optional['ArrayType']]:
        """
        Parse a type, which can be:
        - Scalar: i32, str, bool, etc.
        - Field type: Vec2, Player (any identifier)
        - Array with (): (i32) or (i32; 5) or (Vec2; 10)
        - Array with []: [i32] or [i32; 5]
        
        Returns (type_name, array_type) where array_type is None for scalars.
        """
        # Check for array type with ( or [
        if self.match(TokenType.LPAREN, TokenType.LBRACKET):
            open_bracket = self.current.type
            close_bracket = TokenType.RPAREN if open_bracket == TokenType.LPAREN else TokenType.RBRACKET
            self.advance()  # consume ( or [
            
            # Parse element type
            element_type = self.parse_type()
            
            # Check for optional size after semicolon
            size = None
            if self.match(TokenType.SEMICOLON):
                self.advance()  # consume ;
                if self.match(TokenType.INT_LITERAL):
                    size = int(self.current.value)
                    self.advance()
                else:
                    self.error(f"Expected array size, got {self.current.type.name if self.current else 'EOF'}")
            
            self.expect(close_bracket)
            
            return ('array', ArrayType(element_type, size))
        else:
            # Scalar or field type
            return (self.parse_type(), None)
    
    def parse_block(self) -> Block:
        self.expect(TokenType.INDENT)
        statements = []
        
        while not self.match(TokenType.DEDENT):
            if not self.current:
                self.error("Unexpected EOF in block")
            # newlines zwischen statements erlauben
            if self.match(TokenType.NEWLINE):
                self.advance()
                continue
            statements.append(self.parse_statement())
        
        self.expect(TokenType.DEDENT)
        return Block(statements)
    
    def parse_statement(self) -> Statement:
        # keine variable declaration mehr - direkt assignment
        # check for identifier mit colon (type annotation)
        if self.match(TokenType.IDENTIFIER):
            # lookahead für assignment vs declaration
            if self.pos + 1 < len(self.tokens) and self.tokens[self.pos + 1].type == TokenType.COLON:
                return self.parse_var_decl()
        
        if self.match(TokenType.GIVE, TokenType.RETURN):
            return self.parse_return()
        
        if self.match(TokenType.WHEN):
            return self.parse_if()
        
        if self.match(TokenType.WHILE):
            return self.parse_while()
        
        if self.match(TokenType.LOOP, TokenType.REPEAT):
            return self.parse_loop()
        
        if self.match(TokenType.MATCH):
            return self.parse_match()
        
        if self.match(TokenType.BREAK):
            self.advance()
            self.skip_newlines()
            return Break()
        
        if self.match(TokenType.CONTINUE):
            self.advance()
            self.skip_newlines()
            return Continue()
        
        if self.match(TokenType.WRITE, TokenType.WRITELN):
            return self.parse_write()
        
        expr = self.parse_expression()
        
        if self.match(TokenType.ASSIGN):
            self.advance()
            value = self.parse_expression()
            self.skip_newlines()
            return Assignment(expr, value)
        
        self.skip_newlines()
        return ExprStatement(expr)
    
    def parse_var_decl(self) -> VarDecl:
        # Python-style: name: type = value
        # Array style: name: (i32; 5) = [1, 2, 3, 4, 5]
        # alles ist mutable by default
        name = self.expect(TokenType.IDENTIFIER).value
        self.expect(TokenType.COLON)
        type_name, array_type = self.parse_type_or_array_type()
        
        init = None
        if self.match(TokenType.ASSIGN):
            self.advance()
            init = self.parse_expression()
        
        self.skip_newlines()
        return VarDecl(name, type_name, True, init, array_type)
    
    def parse_return(self) -> Return:
        # Accept both 'give' and 'return'
        if self.match(TokenType.GIVE):
            self.advance()
        elif self.match(TokenType.RETURN):
            self.advance()
        else:
            self.error(f"Expected 'give' or 'return', got {self.current.type.name}")
        
        value = None
        if not self.match(TokenType.NEWLINE, TokenType.DEDENT):
            value = self.parse_expression()
        
        self.skip_newlines()
        return Return(value)
    
    def parse_if(self) -> If:
        self.expect(TokenType.WHEN)
        condition = self.parse_expression()
        self.expect(TokenType.COLON)
        self.skip_newlines()
        then_block = self.parse_block()
        
        else_block = None
        if self.match(TokenType.ELSE):
            self.advance()
            if self.match(TokenType.WHEN):
                # 'else when' handling
                else_if = self.parse_if()
                else_block = Block([else_if])
            else:
                self.expect(TokenType.COLON)
                self.skip_newlines()
                else_block = self.parse_block()
        
        return If(condition, then_block, else_block)
    
    def parse_while(self) -> While:
        self.expect(TokenType.WHILE)
        condition = self.parse_expression()
        self.expect(TokenType.COLON)
        self.skip_newlines()
        body = self.parse_block()
        return While(condition, body)
    
    def parse_loop(self) -> While:
        # infinite loop: loop: oder repeat:
        # wird als 'while True' behandelt
        self.advance()  # LOOP oder REPEAT
        self.expect(TokenType.COLON)
        self.skip_newlines()
        body = self.parse_block()
        # True literal erzeugen für infinite loop
        true_literal = Literal('1', 'bool')
        return While(true_literal, body)
    
    def parse_match(self) -> Match:
        """
        Parse a match statement:
        match value:
            pattern1:
                statements...
            pattern2:
                statements...
            _:
                default statements...
        """
        self.advance()  # consume 'match'
        value = self.parse_expression()
        self.expect(TokenType.COLON)
        self.skip_newlines()
        
        # Parse match arms (indented block)
        self.expect(TokenType.INDENT)
        arms = []
        
        while not self.match(TokenType.DEDENT):
            if not self.current:
                self.error("Unexpected EOF in match statement")
            if self.match(TokenType.NEWLINE):
                self.advance()
                continue
            
            arm = self.parse_match_arm()
            arms.append(arm)
        
        self.expect(TokenType.DEDENT)
        return Match(value, arms)
    
    def parse_match_arm(self) -> MatchArm:
        """
        Parse a single match arm:
        pattern:
            statements...
        Or:
        _:
            statements...
        """
        # Check for wildcard pattern
        is_wildcard = False
        pattern = None
        
        # Check if this is a wildcard _ pattern
        # The underscore is parsed as an identifier with name '_'
        if self.match(TokenType.IDENTIFIER) and self.current.value == '_':
            is_wildcard = True
            self.advance()
        else:
            # Parse pattern expression (could be literal, enum access, etc.)
            pattern = self.parse_expression()
        
        self.expect(TokenType.COLON)
        self.skip_newlines()
        
        # Parse body block
        body = self.parse_block()
        
        return MatchArm(pattern, is_wildcard, body)
    
    def parse_write(self) -> Write:
        # write(expr) oder writeln(expr)
        newline = self.current.type == TokenType.WRITELN
        self.advance()  # WRITE oder WRITELN
        self.expect(TokenType.LPAREN)
        value = self.parse_expression()
        self.expect(TokenType.RPAREN)
        self.skip_newlines()
        return Write(value, newline)
    
    def parse_expression(self) -> Expression:
        return self.parse_bitwise_or()
    
    def parse_bitwise_or(self) -> Expression:
        expr = self.parse_bitwise_xor()
        
        while self.match(TokenType.PIPE):
            op = self.current.value
            self.advance()
            right = self.parse_bitwise_xor()
            expr = BinaryOp(expr, op, right)
        
        return expr
    
    def parse_bitwise_xor(self) -> Expression:
        expr = self.parse_bitwise_and()
        
        while self.match(TokenType.CARET):
            op = self.current.value
            self.advance()
            right = self.parse_bitwise_and()
            expr = BinaryOp(expr, op, right)
        
        return expr
    
    def parse_bitwise_and(self) -> Expression:
        expr = self.parse_comparison()
        
        while self.match(TokenType.AMPERSAND):
            op = self.current.value
            self.advance()
            right = self.parse_comparison()
            expr = BinaryOp(expr, op, right)
        
        return expr
    
    def parse_comparison(self) -> Expression:
        expr = self.parse_shift()
        
        while self.match(TokenType.EQ, TokenType.NE, TokenType.LT, TokenType.LE, TokenType.GT, TokenType.GE):
            op = self.current.value
            self.advance()
            right = self.parse_shift()
            expr = BinaryOp(expr, op, right)
        
        return expr
    
    def parse_shift(self) -> Expression:
        expr = self.parse_additive()
        
        while self.match(TokenType.LSHIFT, TokenType.RSHIFT):
            op = self.current.value
            self.advance()
            right = self.parse_additive()
            expr = BinaryOp(expr, op, right)
        
        return expr
    
    def parse_additive(self) -> Expression:
        expr = self.parse_multiplicative()
        
        while self.match(TokenType.PLUS, TokenType.MINUS):
            op = self.current.value
            self.advance()
            right = self.parse_multiplicative()
            expr = BinaryOp(expr, op, right)
        
        return expr
    
    def parse_multiplicative(self) -> Expression:
        expr = self.parse_unary()
        
        while self.match(TokenType.STAR, TokenType.SLASH, TokenType.PERCENT):
            op = self.current.value
            self.advance()
            right = self.parse_unary()
            expr = BinaryOp(expr, op, right)
        
        return expr
    
    def parse_unary(self) -> Expression:
        if self.match(TokenType.MINUS):
            op = self.current.value
            self.advance()
            operand = self.parse_unary()
            return UnaryOp(op, operand)
        
        if self.match(TokenType.BANG):
            # Boolean NOT
            op = self.current.value
            self.advance()
            operand = self.parse_unary()
            return UnaryOp(op, operand)
        
        # Pointer operators removed in v1.1.0 - AXIS uses value semantics
        
        return self.parse_postfix()
    
    def parse_postfix(self) -> Expression:
        """Handle postfix operators like array indexing: arr[0], field access: obj.field, and method calls: obj.method()"""
        expr = self.parse_primary()
        
        # Handle chained postfix: arr[0][1], player.position.x, arr[0].x
        while self.match(TokenType.LBRACKET, TokenType.DOT):
            if self.match(TokenType.LBRACKET):
                self.advance()  # consume [
                index = self.parse_expression()
                self.expect(TokenType.RBRACKET)
                expr = IndexAccess(expr, index)
            elif self.match(TokenType.DOT):
                self.advance()  # consume .
                member = self.expect(TokenType.IDENTIFIER).value
                expr = FieldAccess(expr, member)
        
        return expr
    
    def parse_primary(self) -> Expression:
        """
        primary := INT_LITERAL | STRING_LITERAL | TRUE | FALSE | IDENTIFIER | call | '(' expression ')' | array_literal | copy
        """
        if self.match(TokenType.INT_LITERAL):
            value = self.current.value
            self.advance()
            return Literal(value, 'int')
        
        if self.match(TokenType.STRING_LITERAL):
            value = self.current.value
            self.advance()
            return StringLiteral(value)
        
        if self.match(TokenType.TRUE):
            self.advance()
            return Literal('1', 'bool')
        
        if self.match(TokenType.FALSE):
            self.advance()
            return Literal('0', 'bool')
        
        # null and sizeof removed in v1.1.0 - AXIS uses value semantics
        
        # copy <expr> - explicit value/array copy
        # copy.runtime <expr> - optimized for runtime (default)
        # copy.compile <expr> - optimized for compile time
        if self.match(TokenType.COPY):
            self.advance()
            mode = 'runtime'  # default
            
            # Check for .runtime or .compile modifier
            if self.match(TokenType.DOT):
                self.advance()
                # Handle 'runtime' as identifier and 'compile' as keyword
                if self.current:
                    if self.current.type == TokenType.IDENTIFIER and self.current.value == 'runtime':
                        mode = 'runtime'
                        self.advance()
                    elif self.current.type == TokenType.COMPILE:
                        mode = 'compile'
                        self.advance()
                    elif self.current.type == TokenType.IDENTIFIER:
                        self.error(f"Unknown copy mode: {self.current.value}. Use 'runtime' or 'compile'")
                    else:
                        self.error("Expected 'runtime' or 'compile' after 'copy.'")
                else:
                    self.error("Expected 'runtime' or 'compile' after 'copy.'")
            
            operand = self.parse_unary()  # parse what follows
            return CopyExpr(operand, mode)
        
        # Array literal: [1, 2, 3, 4]
        if self.match(TokenType.LBRACKET):
            self.advance()  # consume [
            elements = []
            
            if not self.match(TokenType.RBRACKET):
                elements.append(self.parse_expression())
                
                while self.match(TokenType.COMMA):
                    self.advance()
                    elements.append(self.parse_expression())
            
            self.expect(TokenType.RBRACKET)
            return ArrayLiteral(elements)
        
        # read() - read until EOF
        if self.match(TokenType.READ):
            self.advance()
            self.expect(TokenType.LPAREN)
            self.expect(TokenType.RPAREN)
            return Read()
        
        # readln() - read one line until \n
        if self.match(TokenType.READLN):
            self.advance()
            self.expect(TokenType.LPAREN)
            self.expect(TokenType.RPAREN)
            return Readln()
        
        # readchar() - read single byte
        if self.match(TokenType.READCHAR):
            self.advance()
            self.expect(TokenType.LPAREN)
            self.expect(TokenType.RPAREN)
            return Readchar()
        
        # read_failed() - check if last read failed
        if self.match(TokenType.READ_FAILED):
            self.advance()
            self.expect(TokenType.LPAREN)
            self.expect(TokenType.RPAREN)
            return ReadFailed()
        
        if self.match(TokenType.IDENTIFIER):
            name = self.current.value
            self.advance()
            
            if self.match(TokenType.LPAREN):
                self.advance()
                args = self.parse_args()
                self.expect(TokenType.RPAREN)
                return Call(name, args)
            
            return Identifier(name)
        
        if self.match(TokenType.LPAREN):
            self.advance()
            expr = self.parse_expression()
            self.expect(TokenType.RPAREN)
            return expr
        
        self.error(f"Unexpected token in expression: {self.current.type.name if self.current else 'EOF'}")
    
    def parse_args(self) -> List[Expression]:
        args = []
        
        if not self.match(TokenType.RPAREN):
            args.append(self.parse_expression())
            
            while self.match(TokenType.COMMA):
                self.advance()
                args.append(self.parse_expression())
        
        return args


def print_ast(node: ASTNode, indent: int = 0):
    prefix = "  " * indent
    
    if isinstance(node, Program):
        print(f"{prefix}Program:")
        for func in node.functions:
            print_ast(func, indent + 1)
    
    elif isinstance(node, Function):
        params_str = ", ".join(f"{name}: {type_}" for name, type_ in node.params)
        ret_str = f" -> {node.return_type}" if node.return_type else ""
        print(f"{prefix}Function: {node.name}({params_str}){ret_str}")
        print_ast(node.body, indent + 1)
    
    elif isinstance(node, Block):
        print(f"{prefix}Block:")
        for stmt in node.statements:
            print_ast(stmt, indent + 1)
    
    elif isinstance(node, VarDecl):
        mut_str = "mut " if node.mutable else ""
        init_str = f" = ..." if node.init else ""
        print(f"{prefix}VarDecl: {mut_str}{node.name}: {node.type}{init_str}")
        if node.init:
            print_ast(node.init, indent + 1)
    
    elif isinstance(node, Assignment):
        print(f"{prefix}Assignment:")
        print(f"{prefix}  target:")
        print_ast(node.target, indent + 2)
        print(f"{prefix}  value:")
        print_ast(node.value, indent + 2)
    
    elif isinstance(node, Return):
        print(f"{prefix}Return:")
        if node.value:
            print_ast(node.value, indent + 1)
    
    elif isinstance(node, If):
        print(f"{prefix}If:")
        print(f"{prefix}  condition:")
        print_ast(node.condition, indent + 2)
        print(f"{prefix}  then:")
        print_ast(node.then_block, indent + 2)
        if node.else_block:
            print(f"{prefix}  else:")
            print_ast(node.else_block, indent + 2)
    
    elif isinstance(node, While):
        print(f"{prefix}While:")
        print(f"{prefix}  condition:")
        print_ast(node.condition, indent + 2)
        print(f"{prefix}  body:")
        print_ast(node.body, indent + 2)
    
    elif isinstance(node, Break):
        print(f"{prefix}Break")
    
    elif isinstance(node, Continue):
        print(f"{prefix}Continue")
    
    elif isinstance(node, ExprStatement):
        print(f"{prefix}ExprStatement:")
        print_ast(node.expression, indent + 1)
    
    elif isinstance(node, BinaryOp):
        print(f"{prefix}BinaryOp: {node.op}")
        print_ast(node.left, indent + 1)
        print_ast(node.right, indent + 1)
    
    elif isinstance(node, UnaryOp):
        print(f"{prefix}UnaryOp: {node.op}")
        print_ast(node.operand, indent + 1)
    
    elif isinstance(node, Literal):
        print(f"{prefix}Literal: {node.value}")
    
    elif isinstance(node, Identifier):
        print(f"{prefix}Identifier: {node.name}")
    
    elif isinstance(node, Call):
        args_str = f"({len(node.args)} args)" if node.args else "()"
        print(f"{prefix}Call: {node.name}{args_str}")
        for arg in node.args:
            print_ast(arg, indent + 1)
    
    elif isinstance(node, CopyExpr):
        print(f"{prefix}CopyExpr:")
        print_ast(node.operand, indent + 1)
    
    elif isinstance(node, FieldDef):
        name_str = node.name if node.name else "(anonymous)"
        print(f"{prefix}FieldDef: {name_str}")
        for member in node.members:
            print_ast(member, indent + 1)
    
    elif isinstance(node, FieldMember):
        type_str = node.type
        if node.array_type:
            type_str = f"({node.array_type.element_type}; {node.array_type.size})"
        default_str = " = ..." if node.default_value else ""
        print(f"{prefix}FieldMember: {node.name}: {type_str}{default_str}")
        if node.inline_field:
            print_ast(node.inline_field, indent + 1)
    
    elif isinstance(node, FieldAccess):
        print(f"{prefix}FieldAccess: .{node.member}")
        print_ast(node.object, indent + 1)
    
    elif isinstance(node, IndexAccess):
        print(f"{prefix}IndexAccess:")
        print(f"{prefix}  array:")
        print_ast(node.array, indent + 2)
        print(f"{prefix}  index:")
        print_ast(node.index, indent + 2)

if __name__ == '__main__':
    from tokenization_engine import Lexer
    
    # Test 1: Ziel-Testfall
    print("Test 1: Ziel-Testfall")
    print("=" * 60)
    source1 = """
    fn main() -> i32 {
        let x: i32 = 10;
        return x;
    }
    """
    
    lexer1 = Lexer(source1)
    tokens1 = lexer1.tokenize()
    parser1 = Parser(tokens1)
    ast1 = parser1.parse()
    print_ast(ast1)
    print("\n")
    
    # Test 2: Mit Arithmetik
    print("Test 2: Mit Arithmetik")
    print("=" * 60)
    source2 = """
    fn add(a: i32, b: i32) -> i32 {
        return a + b;
    }
    
    fn main() -> i32 {
        let x: i32 = 10;
        let y: i32 = 20;
        let z: i32 = add(x, y);
        return z;
    }
    """
    
    lexer2 = Lexer(source2)
    tokens2 = lexer2.tokenize()
    parser2 = Parser(tokens2)
    ast2 = parser2.parse()
    print_ast(ast2)
    print("\n")
    
    # Test 3: Control Flow
    print("Test 3: Control Flow")
    print("=" * 60)
    source3 = """
    fn abs(x: i32) -> i32 {
        if x < 0 {
            return -x;
        }
        return x;
    }
    
    fn count() {
        let mut i: i32 = 0;
        while i < 10 {
            i = i + 1;
        }
    }
    """
    
    lexer3 = Lexer(source3)
    tokens3 = lexer3.tokenize()
    parser3 = Parser(tokens3)
    ast3 = parser3.parse()
    print_ast(ast3)
