"""
AXIS Compilation Pipeline
End-to-End Compiler: Source → Machine Code

Usage:
    python compilation_pipeline.py <source.axis>
    python compilation_pipeline.py <source.axis> -o output.bin
"""

import sys
import argparse
from pathlib import Path

from tokenization_engine import Lexer
from syntactic_analyzer import Parser
from semantic_analyzer import SemanticAnalyzer
from code_generator import CodeGenerator
from executable_format_generator import ELF64Writer, write_elf_executable

# Import Assembler-Backend
sys.path.insert(0, str(Path(__file__).parent))
from tets import Assembler


class CompilationPipeline:
    """
    Vollständige Compilation Pipeline für AXIS
    """
    
    def __init__(self, verbose: bool = False):
        self.verbose = verbose
    
    def log(self, msg: str):
        """Logging wenn verbose"""
        if self.verbose:
            print(f"[PIPELINE] {msg}")
    
    def compile(self, source_code: str) -> bytes:
        """
        Compiles Source-Code zu Machine-Code
        
        Returns:
            bytes: Ausführbarer machine code
        """
        
        # Phase 1: Lexical Analysis
        self.log("Phase 1: Tokenization...")
        lexer = Lexer(source_code)
        tokens = lexer.tokenize()
        self.log(f"  Generated {len(tokens)} tokens")
        
        # Phase 2: Syntactic Analysis
        self.log("Phase 2: Parsing...")
        parser = Parser(tokens)
        ast = parser.parse()
        self.log(f"  Parsed {len(ast.functions)} functions")
        
        # Phase 3: Semantic Analysis
        self.log("Phase 3: Semantic Analysis...")
        analyzer = SemanticAnalyzer()
        analyzer.analyze(ast)
        self.log("  Type checking complete")
        
        # Phase 4: Code Generation
        self.log("Phase 4: Code Generation...")
        codegen = CodeGenerator()
        assembly = codegen.compile(ast)
        
        if self.verbose:
            print("\n--- Generated Assembly ---")
            print(assembly)
            print("--- End Assembly ---\n")
        
        # Phase 5: Assembly
        self.log("Phase 5: Assembling to machine code...")
        assembler = Assembler()
        machine_code = assembler.assemble_code(assembly)
        
        if not machine_code:
            raise RuntimeError("Assembly failed: No machine code generated")
        
        self.log(f"  Generated {len(machine_code)} bytes of machine code")
        
        return bytes(machine_code)
    
    def compile_file(self, input_path: str, output_path: str = None, dump_hex: bool = True, elf_format: bool = False):
        """
        Compiles Source-Datei
        
        Args:
            input_path: Pfad zur Source-Datei
            output_path: Pfad zur Output-Binary (optional)
            dump_hex: Hex-Dump ausgeben
            elf_format: ELF64 Executable generieren (Linux)
        """
        
        # Read source
        self.log(f"Reading source: {input_path}")
        with open(input_path, 'r', encoding='utf-8') as f:
            source_code = f.read()
        
        # Compile
        try:
            machine_code = self.compile(source_code)
        except Exception as e:
            print(f"Compilation failed: {e}", file=sys.stderr)
            import traceback
            traceback.print_exc()
            return False
        
        # Hex dump
        if dump_hex:
            print("\n=== Machine Code (Hex) ===")
            hex_str = ' '.join(f'{byte:02X}' for byte in machine_code)
            # Print in rows of 16 bytes
            for i in range(0, len(hex_str), 48):  # 16*3 = 48
                print(hex_str[i:i+48])
            print(f"\nTotal: {len(machine_code)} bytes\n")
        
        # Write binary
        if output_path:
            self.log(f"Writing binary: {output_path}")
            
            if elf_format:
                # Generate ELF64 Executable
                write_elf_executable(machine_code, output_path, verbose=self.verbose)
                print(f"ELF64 executable written to: {output_path}")
                if not self.verbose:
                    print(f"Run with: chmod +x {output_path} && ./{output_path}")
            else:
                # Raw machine code
                with open(output_path, 'wb') as f:
                    f.write(machine_code)
                print(f"Binary written to: {output_path}")
        
        return True


def main():
    parser = argparse.ArgumentParser(
        description='AXIS Compiler - System Programming Language',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python compilation_pipeline.py test.axis
  python compilation_pipeline.py test.axis -o test.bin
  python compilation_pipeline.py test.axis -v
        """
    )
    
    parser.add_argument('input', help='Input source file (.axis)')
    parser.add_argument('-o', '--output', help='Output binary file')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')
    parser.add_argument('--no-hex', action='store_true', help='Disable hex dump')
    parser.add_argument('--elf', action='store_true', help='Generate ELF64 executable (Linux)')
    
    args = parser.parse_args()
    
    # Check input file
    if not Path(args.input).exists():
        print(f"Error: Input file not found: {args.input}", file=sys.stderr)
        return 1
    
    # Create pipeline
    pipeline = CompilationPipeline(verbose=args.verbose)
    
    # Compile
    success = pipeline.compile_file(
        args.input,
        output_path=args.output,
        dump_hex=not args.no_hex,
        elf_format=args.elf
    )
    
    return 0 if success else 1


if __name__ == '__main__':
    # Quick test without args
    if len(sys.argv) == 1:
        print("=" * 70)
        print("AXIS Compilation Pipeline - Quick Test")
        print("=" * 70)
        print()
        
        # Test 1: Simple arithmetic
        print("Test 1: Simple arithmetic")
        print("-" * 70)
        test_source1 = """
fn main() -> i32 {
    let x: i32 = 10;
    let y: i32 = 20;
    return x + y;
}
        """
        
        pipeline = CompilationPipeline(verbose=True)
        try:
            machine_code = pipeline.compile(test_source1)
            print("\n✓ Compilation successful!")
            print(f"Machine code: {' '.join(f'{b:02X}' for b in machine_code[:32])}...")
        except Exception as e:
            print(f"\n✗ Compilation failed: {e}")
            import traceback
            traceback.print_exc()
        
        print("\n" + "=" * 70)
        print("For full usage, run:")
        print("  python compilation_pipeline.py <source.axis> [-o output.bin] [-v]")
        print("=" * 70)
    else:
        sys.exit(main())
