# Makefile for SyncTime - Amiga NTP Clock Synchronizer
# Usage: make / make clean / make archive
# Override: make PREFIX=/opt/amiga

PREFIX ?= /opt/amiga
CC      = $(PREFIX)/bin/m68k-amigaos-gcc

VERSION := $(shell cat version.txt | tr -d '\n')
HASH    := $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")
STAMP   := $(shell date '+%Y-%m-%d %H:%M')

CFLAGS  ?= -O2 -Wall -Wno-pointer-sign
CFLAGS  += '-DVERSION_STRING="$(VERSION)"' \
           '-DCOMMIT_HASH="$(HASH)"' \
           '-DBUILD_DATE="$(STAMP)"'
LDFLAGS  = -noixemul
INCLUDES = -Iinclude
LIBS     = -lamiga

# Timezone database
TZDB_VERSION = 2025c
TZDB_URL = https://data.iana.org/time-zones/releases/tzdata$(TZDB_VERSION).tar.gz
TZDB_DIR = tzdata

SRCDIR = src
SRCS   = $(SRCDIR)/main.c \
         $(SRCDIR)/config.c \
         $(SRCDIR)/network.c \
         $(SRCDIR)/sntp.c \
         $(SRCDIR)/clock.c \
         $(SRCDIR)/window.c \
         $(SRCDIR)/tz.c \
         $(SRCDIR)/tz_table.c

OBJS = $(SRCS:.c=.o)

# Output paths - build directly into dist/
DISTDIR = dist/SyncTime
OUT     = $(DISTDIR)/SyncTime
README  = $(DISTDIR)/SyncTime.readme
LICENSE_DEST = $(DISTDIR)/LICENSE

.PHONY: all clean clean-generated archive dist-setup

all: $(OUT) $(README) $(LICENSE_DEST)

# Setup dist folder from template (only if needed)
dist-setup:
	@if [ ! -d dist ]; then cp -r dist-src dist; fi

# Build binary directly into dist/SyncTime/
$(OUT): $(OBJS) | dist-setup
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

# Generate Aminet readme directly into dist/SyncTime/
$(README): README.md scripts/gen_readme.py version.txt | dist-setup
	@echo "Generating SyncTime.readme..."
	python3 scripts/gen_readme.py README.md $(VERSION) > $@

# Copy LICENSE into dist/SyncTime/
$(LICENSE_DEST): LICENSE | dist-setup
	cp LICENSE $@

# Create lha archive for Aminet
archive: all
	@echo "Creating SyncTime-$(VERSION).lha..."
	cd dist && lha -c ../SyncTime-$(VERSION).lha SyncTime.info SyncTime

# Download and extract tzdb
$(TZDB_DIR)/.downloaded:
	@echo "Downloading tzdata $(TZDB_VERSION)..."
	mkdir -p $(TZDB_DIR)
	curl -sfL $(TZDB_URL) | tar xz -C $(TZDB_DIR) && touch $@

# Generate timezone table
$(SRCDIR)/tz_table.c: $(TZDB_DIR)/.downloaded scripts/gen_tz_table.py
	@echo "Generating timezone table..."
	python3 scripts/gen_tz_table.py $(TZDB_DIR) 2>/dev/null > $@.tmp && mv $@.tmp $@

$(SRCDIR)/%.o: $(SRCDIR)/%.c include/synctime.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean-generated:
	rm -f $(SRCDIR)/tz_table.c
	rm -rf $(TZDB_DIR)

clean: clean-generated
	rm -f $(OBJS)
	rm -rf dist
	rm -f SyncTime-*.lha
