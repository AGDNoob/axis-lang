# Release Roadmap — AXIS v1.2.1

> **Goal:** Prepare everything for a clean push + GitHub Release.  
> **Status:** NO PUSH until all items are checked off.

---

## Overview: What's New in v1.2.1?

- 32-bit native arithmetic (all integer ops encoded as 32-bit x86 instructions)
- 6 new optimizer passes (peephole, copy propagation, function inlining, LICM, loop unrolling, redundant instruction elimination)
- Total: 14-pass optimization pipeline
- Benchmarks: GCC `-O0` parity on Prime Count, beats GCC on GCD Stress
- Fibonacci improved from 1.3× to 1.11× slower than GCC `-O0`
- Binaries up to 20% smaller

---

## Checklist

```text
[x] 32-bit native arithmetic implemented and tested
[x] New optimizer passes implemented (peephole, copy prop, inlining, LICM, loop unrolling, RIE)
[x] Benchmarks updated (docs/Benchmarks.md)
[x] Release Notes created (RELEASE_NOTES_v1.2.1.md)
[x] CHANGELOG updated (v1.2.1 = 2026-03-16)
[x] GitHub Release text created (GITHUB_RELEASE_v1.2.1.md)
[x] Feature-List updated to v1.3.0 scope
[x] Move RELEASE_NOTES_v1.2.0.md to Old_Release-Notes/
[ ] Commits erstellen (siehe Plan unten)
[ ] Push
[ ] Create GitHub Release + Tag
```

---

## Commit-Plan (8 Commits)

> **Reihenfolge:** Bugfixes → Features → Infrastruktur → Docs → Release  
> **Konvention:** Alle Messages im Format `type: kurze Beschreibung`

---

### Commit 1 — Bugfixes (Lexer, Semantic, IRGen, Parser)

```text
fix: resolve lexer tab handling, copy mutability, and div-by-zero issues
```

| Datei | Änderung |
| --- | --- |
| `axcc/src/lexer.c` | L8: mixed tabs/spaces + L9: tab column tracking (+24 / −5) |
| `axcc/src/semantic.c` | S8: copy mutability check (+57 / −3) |
| `axcc/src/irgen.c` | XU-04: division-by-zero guard (+44 / −12) |
| `axcc/src/parser.c` | Parser improvements related to above (+17 / −2) |

Gesamt: +127 / −24

---

### Commit 2 — 32-Bit Native Arithmetic + x64 Codegen

```text
feat: encode all integer ops as 32-bit x86 instructions
```

| Datei | Änderung |
| --- | --- |
| `axcc/src/x64.c` | 32-bit MOV/ADD/SUB/IMUL/CMP encoding, register helpers (+384 / −126) |
| `axcc/src/pe.c` | PE stub adjustments for 32-bit code (+25 / −1) |
| `axcc/src/elf.c` | ELF adjustments for 32-bit code (+32 / −1) |
| `axcc/include/axis_x64.h` | Neue x64-Helfer Deklarationen (+4) |
| `axcc/include/axis_ast.h` | AST-Node Ergänzung (+1) |

Gesamt: +320 / −126

---

### Commit 3 — Neue Optimizer-Passes

```text
feat: add 6 optimizer passes (peephole, copy-prop, inlining, LICM, loop-unroll, RIE)
```

| Datei | Änderung |
| --- | --- |
| `axcc/src/opt.c` | 6 vollständige neue Passes (+903) |
| `axcc/include/axis_opt.h` | Optimizer-Deklarationen & Enums (+76) |

Gesamt: +979

---

### Commit 4 — Version Bump + Main

```text
chore: bump version to 1.2.1
```

| Datei | Änderung |
| --- | --- |
| `axcc/include/axis_common.h` | `AXIS_VERSION_PATCH` 0→1, `AXIS_VERSION_STR` "1.2.1" |
| `axcc/src/main.c` | Check-Command + Version-Output Anpassungen (+9 / −1) |

Gesamt: ~+10 / −2

---

### Commit 5 — User Guide Updates

```text
docs: update user guide for v1.2.1 features
```

| Datei | Änderung |
| --- | --- |
| `docs/guide/03-control-flow.md` | Neue Beispiele / Ergänzungen (+17) |
| `docs/guide/05-arrays.md` | Kleine Ergänzung (+2) |
| `docs/guide/08-operators.md` | Operator-Dokumentation erweitert (+20) |
| `docs/guide/09-io.md` | IO-Dokumentation erweitert (+16) |
| `docs/guide/11-compile-mode.md` | Navigation „Next" Link hinzugefügt (+4) |

Gesamt: ~+59 / −5

---

### Commit 6 — Technical Docs

```text
docs: update technical documentation for 14-pass pipeline
```

| Datei | Änderung |
| --- | --- |
| `docs/technical/01-architecture.md` | Pipeline-Diagramm, Line Counts, Datei-Tabelle (+28 / −8) |
| `docs/technical/06-x64-codegen.md` | Version-Ref v1.2.0→v1.2.1 (+1 / −1) |
| `docs/technical/07-binary-formats.md` | Navigation „Next" Link hinzugefügt (+4) |
| `docs/technical/08-optimizations.md` | 32-Bit Arithmetic Sektion, RIE-Fix, Binary-Size Tabelle (+74 / −8) |

Gesamt: ~+107 / −17

---

### Commit 7 — Release-Dokumentation

```text
docs: add v1.2.1 release notes, changelog, and benchmarks
```

| Datei | Änderung |
| --- | --- |
| `docs/CHANGELOG.md` | v1.2.1 Einträge (+42) |
| `docs/Benchmarks.md` | Aktualisierte Benchmark-Ergebnisse (+109 / −71) |
| `RELEASE_NOTES_v1.2.1.md` | Neue Release Notes (neu) |
| `RELEASE_NOTES_v1.2.0.md` | Gelöscht (nach Old verschoben) |
| `docs/Old_Release-Notes/RELEASE_NOTES_v1.2.0.md` | Archivierte v1.2.0 Notes (neu) |

Gesamt: ~+310 / −71 + Datei-Verschiebung

---

### Commit 8 — Meta, Installer, Beispiel

```text
chore: update gitignore, readme, installer, and example
```

| Datei | Änderung |
| --- | --- |
| `.gitignore` | Neue Ignore-Regeln (COMMIT_HISTORY, GITHUB_RELEASE_*, etc.) |
| `README.md` | Release-Notes-Link → v1.2.1 |
| `installer/axis-installer.nsi` | NSIS-Installer Version auf v1.2.1 |
| `code/examples/18_gcd_lcm.axis` | Überarbeitetes GCD/LCM Beispiel |

Gesamt: ~+50 / −20

---

### Zusammenfassung

| # | Typ | Beschreibung | Dateien | Zeilen |
| --- | --- | --- | --- | --- |
| 1 | `fix` | Bugfixes (Lexer, Semantic, IRGen, Parser) | 4 | +127 / −24 |
| 2 | `feat` | 32-Bit Native Arithmetic | 5 | +320 / −126 |
| 3 | `feat` | 6 neue Optimizer-Passes | 2 | +979 |
| 4 | `chore` | Version Bump 1.2.1 | 2 | ~+10 |
| 5 | `docs` | User Guide Updates | 5 | ~+59 |
| 6 | `docs` | Technical Docs Update | 4 | ~+107 |
| 7 | `docs` | Release Notes + Changelog + Benchmarks | 5 | ~+310 |
| 8 | `chore` | .gitignore, README, Installer, Beispiel | 4 | ~+50 |
| | | **Gesamt** | **31** | **~+1962 / −268** |

---

*Commit-Plan erstellt am: 2026-03-18*  
*Created on: 2026-03-16 — Based on v1.2.1 changes.*
