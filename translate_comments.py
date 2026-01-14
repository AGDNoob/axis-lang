#!/usr/bin/env python3
"""
Script to translate German comments to English in AXIS Python files.
"""

import re
from pathlib import Path

# Translation dictionary for common German programming terms
translations = {
    # Verbs
    r'\bPrüft\b': 'Checks',
    r'\bGibt\b.*\bzurück\b': lambda m: m.group(0).replace('Gibt', 'Returns').replace('zurück', ''),
    r'\bErzeugt\b': 'Generates',
    r'\bAnalysiert\b': 'Analyzes',
    r'\bKompiliert\b': 'Compiles',
    r'\bBaut\b': 'Builds',
    r'\bGeneriert\b': 'Generates',
    r'\bFügt\b.*\bhinzu\b': lambda m: m.group(0).replace('Fügt', 'Adds').replace('hinzu', '').replace(' hinzu', ''),
    r'\bBewegt\b': 'Moves',
    r'\bSucht\b': 'Searches',
    r'\bErstellt\b': 'Creates',
    r'\bDefiniert\b': 'Defines',
    r'\bLiest\b': 'Reads',
    r'\bParst\b': 'Parses',
    r'\bSchaut voraus\b': 'Looks ahead',
    r'\bÜberspringt\b': 'Skips',
    r'\bValidiert\b': 'Validates',
    r'\bWirft\b': 'Throws',
    r'\bVerlässt\b': 'Exits',
    r'\bBerechnet\b': 'Calculates',
    r'\bSpeichert\b': 'Stores',
    r'\bEncodiert\b': 'Encodes',
    
    # Nouns and adjectives  
    r'\bTypgrößen\b': 'type sizes',
    r'\bGröße\b': 'size',
    r'\bSprunginstruktionen\b': 'jump instructions',
    r'\bMaschinen-Code\b': 'machine code',
    r'\bMaschinencode\b': 'machine code',
    r'\bZeilenende\b': 'end of line',
    r'\bvorangestellte\b': 'prepended',
    r'\bausführbare\b': 'executable',
    r'\blauffähige\b': 'executable',
    r'\bVorzeichen\b': 'sign',
    r'\bNegatives\b': 'Negative',
    r'\bÜbersprunge\b': 'Skip',
    r'\bKommentare\b': 'comments',
    r'\bSchlüsselwort\b': 'keyword',
    r'\bBinär\b': 'Binary',
    r'\bDezimal\b': 'Decimal',
    r'\bein Zeichen weiter\b': 'one character forward',
    r'\bein Token weiter\b': 'one token forward',
    r'\bunbekanntes Zeichen\b': 'unknown character',
    r'\bUnbekanntes Zeichen\b': 'Unknown character',
    r'\bWert\b': 'value',
    r'\bgültig\b': 'valid',
    r'\bungültig\b': 'invalid',
    r'\bLeerer\b': 'Empty',
    r'\bUngültiger\b': 'Invalid',
    r'\bspezifischen\b': 'specific',
    r'\baktuellen\b': 'current',
    r'\bersten\b': 'first',
    r'\bweiteren\b': 'additional',
    r'\bverschachtelte\b': 'nested',
    r'\bSemantischer Fehler\b': 'Semantic error',
    r'\bTyp-Checking\b': 'Type checking',
    r'\bSymboltabelle\b': 'Symbol table',
    r'\bStack-Layout-Berechnung\b': 'Stack layout calculation',
    r'\bBehandeln\b': 'handle',
    r'\bLexikalischer Scope\b': 'Lexical scope',
    r'\bVerkettung\b': 'chaining',
    r'\bSemantic Analyzer\b': 'Semantic Analyzer',
    r'\bausschließlich\b': 'only',
    r'\bidentisch\b': 'identical',
    r'\bVerschiedene\b': 'Different',
    r'\bZahlformate\b': 'Number formats',
    r'\bAbschluss\b': 'completion',
    r'\bFehler-Erkennung\b': 'Error detection',
    r'\bFehler-Tests\b': 'Error tests',
    r'\bSollte Exception werfen\b': 'Should throw exception',
    r'\bUnerwartete Exception\b': 'Unexpected exception',
    r'\babgeschlossen\b': 'completed',
    
    # Phrases
    r'ohne Position zu ändern': 'without changing position',
    r'ohne Backtracking': 'without backtracking',
    r'ohne Linker': 'without linker',
    r'ohne Typ': 'without type',
    r'Das nur': 'That only',
    r'das nur': 'that only',
    r'aus Expression besteht': 'consists only of expression',
    r'für jetzt': 'for now',
    r'nicht implementiert': 'not implemented',
    r'noch nicht': 'not yet',
    r'Falls vorhanden': 'If present',
    r'falls vorhanden': 'if present',
    r'im aktuellen Scope': 'in current scope',
    r'im aktuellen oder Parent-Scope': 'in current or parent scope',
    r'bis Konvergenz': 'until convergence',
    r'direkt': 'directly',
    r'Maximum': 'maximum',
    r'Zähler für': 'Counter for',
    r'Stack wächst nach unten': 'Stack grows downward',
    r'bis Zeilenende': 'until end of line',
    
    # Tech terms
    r'Deterministischer Scanner': 'Deterministic scanner',
    r'rekursiver Descent Parser': 'recursive descent parser',
    r'Basis-Klasse': 'Base class',
    r'Entry Point': 'Entry Point',
    r'Complete token list': 'Complete token list',
    r'Test-Funktion': 'Test function',
    r'Einfache Funktion': 'Simple function',
    r'Mit Operatoren': 'With operators',
}

def translate_line(line):
    """Translate a single line of German comments to English."""
    result = line
    
    # Apply translations
    for pattern, replacement in translations.items():
        if callable(replacement):
            result = re.sub(pattern, replacement, result, flags=re.IGNORECASE)
        else:
            result = re.sub(pattern, replacement, result, flags=re.IGNORECASE)
    
    return result

def translate_file(filepath):
    """Translate German comments in a Python file."""
    print(f"Translating {filepath.name}...")
    
    with open(filepath, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    
    translated_lines = []
    in_docstring = False
    docstring_char = None
    
    for line in lines:
        # Check for docstring markers
        if '"""' in line or "'''" in line:
            if '"""' in line:
                count = line.count('"""')
                if count == 2:
                    # Single-line docstring
                    translated_lines.append(translate_line(line))
                    continue
                elif count == 1:
                    in_docstring = not in_docstring
                    docstring_char = '"""'
            elif "'''" in line:
                count = line.count("'''")
                if count == 2:
                    translated_lines.append(translate_line(line))
                    continue
                elif count == 1:
                    in_docstring = not in_docstring
                    docstring_char = "'''"
        
        # Translate if in docstring or comment
        if in_docstring or line.strip().startswith('#'):
            translated_lines.append(translate_line(line))
        else:
            translated_lines.append(line)
    
    # Write back
    with open(filepath, 'w', encoding='utf-8') as f:
        f.writelines(translated_lines)
    
    print(f"  ✓ {filepath.name} translated")

def main():
    """Main function."""
    axis_root = Path(__file__).parent
    
    py_files = [
        'tokenization_engine.py',
        'syntactic_analyzer.py',
        'semantic_analyzer.py',
        'code_generator.py',
        'compilation_pipeline.py',
        'executable_format_generator.py',
        'tets.py'
    ]
    
    print("=" * 70)
    print("AXIS Comment Translation: German → English")
    print("=" * 70)
    print()
    
    for filename in py_files:
        filepath = axis_root / filename
        if filepath.exists():
            translate_file(filepath)
        else:
            print(f"  ⚠ {filename} not found")
    
    print()
    print("=" * 70)
    print("Translation complete!")
    print("=" * 70)

if __name__ == '__main__':
    main()
