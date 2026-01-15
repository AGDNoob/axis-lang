"""
AXIS Parser
LL(1) recursive descent parser.
Generates AST without typee sizes oder Codegen.
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
    functions: List['Function']


@dataclass
class Function(ASTNode):
    name: str
    params: List[tuple[str, str]]
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
    type: str
    mutable: bool
    init: Optional['Expression']


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
class Identifier(Expression):
    name: str


@dataclass
class Call(Expression):
    name: str
    args: List[Expression]


@dataclass
class Deref(Expression):
    operand: Expression


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
    
    def skip_newlines(self):
        # newlines überspringen - wichtig vor/nach blocks
        while self.match(TokenType.NEWLINE):
            self.advance()
    
    def parse(self) -> Program:
        functions = []
        self.skip_newlines()  # skip initial newlines
        while self.current and self.current.type != TokenType.EOF:
            functions.append(self.parse_function())
            self.skip_newlines()  # skip newlines between functions
        return Program(functions)
    
    def parse_function(self) -> Function:
        self.expect(TokenType.FUNC)
        name = self.expect(TokenType.IDENTIFIER).value
        
        self.expect(TokenType.LPAREN)
        params = self.parse_params()
        self.expect(TokenType.RPAREN)
        
        return_type = None
        if self.match(TokenType.ARROW):
            self.advance()
            return_type = self.parse_type()
        
        self.expect(TokenType.COLON)
        self.skip_newlines()
        body = self.parse_block()
        
        return Function(name, params, return_type, body)
    
    def parse_params(self) -> List[tuple[str, str]]:
        # parameter list parsen - name: type, name: type, ...
        params = []
        
        if not self.match(TokenType.RPAREN):
            name = self.expect(TokenType.IDENTIFIER).value
            self.expect(TokenType.COLON)
            type_name = self.parse_type()
            params.append((name, type_name))
            
            while self.match(TokenType.COMMA):
                self.advance()
                name = self.expect(TokenType.IDENTIFIER).value
                self.expect(TokenType.COLON)
                type_name = self.parse_type()
                params.append((name, type_name))
        
        return params
    
    def parse_type(self) -> str:
        # type names - i8, i32, i64, u32, etc
        type_tokens = [
            TokenType.I8, TokenType.I16, TokenType.I32, TokenType.I64,
            TokenType.U8, TokenType.U16, TokenType.U32, TokenType.U64,
            TokenType.PTR, TokenType.BOOL
        ]
        
        if not self.match(*type_tokens):
            self.error(f"Expected type, got {self.current.type.name if self.current else 'EOF'}")
        
        type_token = self.current
        self.advance()
        return type_token.value
    
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
        
        if self.match(TokenType.GIVE):
            return self.parse_return()
        
        if self.match(TokenType.WHEN):
            return self.parse_if()
        
        if self.match(TokenType.WHILE):
            return self.parse_while()
        
        if self.match(TokenType.LOOP, TokenType.REPEAT):
            return self.parse_loop()
        
        if self.match(TokenType.BREAK):
            self.advance()
            self.skip_newlines()
            return Break()
        
        if self.match(TokenType.CONTINUE):
            self.advance()
            self.skip_newlines()
            return Continue()
        
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
        # alles ist mutable by default
        name = self.expect(TokenType.IDENTIFIER).value
        self.expect(TokenType.COLON)
        type_name = self.parse_type()
        
        init = None
        if self.match(TokenType.ASSIGN):
            self.advance()
            init = self.parse_expression()
        
        self.skip_newlines()
        return VarDecl(name, type_name, True, init)  # always mutable
    
    def parse_return(self) -> Return:
        self.expect(TokenType.GIVE)
        
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
        
        if self.match(TokenType.STAR):
            # Pointer-Dereferenzierung
            self.advance()
            operand = self.parse_unary()
            return Deref(operand)
        
        return self.parse_primary()
    
    def parse_primary(self) -> Expression:
        """
        primary := INT_LITERAL | TRUE | FALSE | IDENTIFIER | call | '(' expression ')'
        """
        if self.match(TokenType.INT_LITERAL):
            value = self.current.value
            self.advance()
            return Literal(value, 'int')
        
        if self.match(TokenType.TRUE):
            self.advance()
            return Literal('1', 'bool')
        
        if self.match(TokenType.FALSE):
            self.advance()
            return Literal('0', 'bool')
        
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
    
    elif isinstance(node, Deref):
        print(f"{prefix}Deref:")
        print_ast(node.operand, indent + 1)

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
