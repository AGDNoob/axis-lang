# ============================================================
# AXIS Compiler (axcc) – Makefile
# ============================================================
# Bootstrap with:  make          (uses cc / gcc / mingw)
# Clean with:      make clean
# ============================================================

CC       ?= gcc
CFLAGS   := -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude
LDFLAGS  :=
# Windows: link kernel32 for console I/O in generated PE
ifeq ($(OS),Windows_NT)
    EXE := axis.exe
else
    EXE := axis
endif

SRCDIR   := src
INCDIR   := include
OBJDIR   := build

SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

.PHONY: all clean

all: $(EXE)

$(EXE): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
ifeq ($(OS),Windows_NT)
	@echo @"%%~dp0axis.exe" %%* > $(subst axis.exe,ax.bat,$@)
else
	@ln -sf axis $(subst axis,ax,$@)
endif

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
ifeq ($(OS),Windows_NT)
	@if not exist $(OBJDIR) mkdir $(OBJDIR)
else
	@mkdir -p $(OBJDIR)
endif

clean:
	rm -rf $(OBJDIR) $(EXE)
