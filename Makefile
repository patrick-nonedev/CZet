GCC_SRC    := $(CURDIR)/src/gcc
BUILD_DIR  := $(CURDIR)/build
LOG_DIR    := $(BUILD_DIR)/Logs
SHELL_NIX  := shell.nix
JOBS       ?= 16
CZET     := $(CURDIR)/czet
CZET_SRC := $(CURDIR)/src/czet.c
EMBED_PY   := $(CURDIR)/tools/embed.py
# SPDX-License-Identifier: GPL-3.0-or-later
# CZet's own stdlib (<utils/...> headers): source of truth.
STDLIB_UTILS := $(CURDIR)/stdlib/utils
# Pinned third-party originals (tarballs in third_party/src/).
THIRD_SRC := $(CURDIR)/third_party/src
THIRD_PATCH_CRITBIT := $(CURDIR)/third_party/critbit-cplusplus-guard.patch
TP_BUILD  := $(BUILD_DIR)/third_party
TP_STAMP  := $(TP_BUILD)/.done
TP_MUSL_A  := $(TP_BUILD)/libczet_utils_musl.a
TP_GLIBC_A := $(TP_BUILD)/libczet_utils_glibc.a

CONFIGURE_ARGS = \
	--enable-languages=c \
	--disable-nls \
	--disable-bootstrap \
	--disable-multilib \
	--disable-werror \
	--disable-checking \
	--disable-shared

# No /usr/include on NixOS; point at the real glibc headers.
# NOTE: grep glibc explicitly: shell.nix also provides musl, whose stdlib.h
# would otherwise win the -m1 race.
GLIBC_INC = $(shell nix-shell $(SHELL_NIX) --run 'printf "#include <stdlib.h>\n" | gcc -E - 2>/dev/null | grep glibc | grep -m1 stdlib.h' | sed -n 's/.*"\(.*\)\/stdlib.h".*/\1/p')

# Libc sysroot paths for the embedded blob (probed on NixOS).
# Derived from the same glibc used by configure (consistent version).
# Note: filter/filter-out with an inner % is broken in make 4.4.1, hence
# firstword/wildcard selections with a literal suffix.
#
# Multi-arch: native build per arch. ARCH defaults to host (uname -m),
# override with `make ARCH=i686` / `ARCH=aarch64`. i386 is an alias of i686.
ARCH_RAW ?= $(shell uname -m)
ARCH ?= $(ARCH_RAW)
ifeq ($(ARCH),i386)
ARCH_NORM := i686
else
ARCH_NORM := $(ARCH)
endif
ifeq ($(ARCH_NORM),x86_64)
MUSL_TRIPLE := x86_64-unknown-linux-musl
GCC_TRIPLE  := x86_64-pc-linux-gnu
FILE_MATCH  := x86-64
LD_LINUX    := ld-linux-x86-64.so.2
else ifeq ($(ARCH_NORM),i686)
MUSL_TRIPLE := i686-unknown-linux-musl
GCC_TRIPLE  := i686-pc-linux-gnu
FILE_MATCH  := Intel 80386
LD_LINUX    := ld-linux.so.2
else ifeq ($(ARCH_NORM),aarch64)
MUSL_TRIPLE := aarch64-unknown-linux-musl
GCC_TRIPLE  := aarch64-unknown-linux-gnu
FILE_MATCH  := aarch64
LD_LINUX    := ld-linux-aarch64.so.1
else
$(error Unsupported ARCH=$(ARCH) (expected x86_64, i686/i386 or aarch64))
endif
# libgcc dir: prefer exact triple, fall back to any toolchain triple
# (exclude blob-root, which is output not input).
LIBGCC_DIR ?= $(firstword $(wildcard $(BUILD_DIR)/$(GCC_TRIPLE)/libgcc) $(wildcard $(BUILD_DIR)/*-pc-linux-gnu/libgcc $(BUILD_DIR)/*-unknown-linux-gnu/libgcc))
GLIBC_VER     := $(shell printf '%s' '$(GLIBC_INC)' | sed -E 's#.*-glibc-([0-9][0-9.]*-[0-9]+).*#\1#')
# The -dev (include) pins the version. Among the lib/static dirs of that
# version, pick the ones matching FILE_MATCH (both 32 and 64-bit variants
# may be present on multilib hosts).
GLIBC_CANDS   := $(wildcard /nix/store/*-glibc-$(GLIBC_VER)/lib)
GLIBC_64      := $(foreach L,$(GLIBC_CANDS),$(if $(findstring $(FILE_MATCH),$(shell file -b $(L)/crt1.o 2>/dev/null)),$(L)))
GLIBC_LIB     ?= $(firstword $(GLIBC_64))
GLIBC_STATIC  ?= $(firstword $(wildcard /nix/store/*-glibc-$(GLIBC_VER)-static/lib))
# musl static: prefer the -musl-static-<triple>- triple dir, fall back to any
# realized musl output containing lib/libc.a (plain *-musl-<ver>/ after GC).
MUSL_STATIC_CANDS := $(filter-out %-dev/ %-bin/,$(wildcard /nix/store/*-musl-static-$(MUSL_TRIPLE)-*/ /nix/store/*-musl-*/))
MUSL_STATIC   := $(patsubst %/,%,$(firstword $(foreach D,$(MUSL_STATIC_CANDS),$(if $(wildcard $(D)/lib/libc.a),$(D)))))
MUSL_LIB      ?= $(MUSL_STATIC)/lib
MUSL_VER      := $(lastword $(subst -, ,$(notdir $(MUSL_STATIC))))
MUSL_INC      ?= $(firstword $(wildcard /nix/store/*-musl-$(MUSL_VER)-dev/include))
# Dynamic musl package (non -static): provides libc.so for dynamic links.
MUSL_DYN      := $(patsubst %/,%,$(firstword $(filter-out %musl-static% %-dev/ %-bin/,\
	$(wildcard /nix/store/*-musl-$(MUSL_VER)/))))

.PHONY: all build configure shell logs clean czet blob embed prefetch

# Realize the static libcs into /nix/store (fresh machines/CI only have the
# dev/dynamic outputs; the blob needs libc.a + musl headers).
prefetch:
	nix-build --no-out-link "<nixpkgs>" -A glibc.static
	nix-build --no-out-link "<nixpkgs>" -A musl

print-vars:
	@echo "ARCH         = $(ARCH) (norm: $(ARCH_NORM))"
	@echo "MUSL_TRIPLE  = $(MUSL_TRIPLE)"
	@echo "GCC_TRIPLE   = $(GCC_TRIPLE)"
	@echo "FILE_MATCH   = $(FILE_MATCH)"
	@echo "LD_LINUX     = $(LD_LINUX)"
	@echo "LIBGCC_DIR   = $(LIBGCC_DIR)"
	@echo "GLIBC_VER    = $(GLIBC_VER)"
	@echo "GLIBC_LIB    = $(GLIBC_LIB)"
	@echo "GLIBC_STATIC = $(GLIBC_STATIC)"
	@echo "MUSL_STATIC  = $(MUSL_STATIC)"
	@echo "MUSL_LIB     = $(MUSL_LIB)"
	@echo "MUSL_DYN     = $(MUSL_DYN)"
	@echo "MUSL_VER     = $(MUSL_VER)"
	@echo "MUSL_INC     = $(MUSL_INC)"

all:
	$(MAKE) build
	$(MAKE) czet
	@echo "=== done: self-contained binary at $(CZET) ==="

# Enter the nix shell (applies NIX_HARDENING_ENABLE="") and run make.
# Only build what the blob needs: the frontend (cc1/xgcc/collect2) and the
# target libgcc.  Skip the remaining target libraries (zlib, etc.): not
# embedded, and their configure fails on NixOS (link test).
build: $(BUILD_DIR)/Makefile
	@mkdir -p $(LOG_DIR) $(BUILD_DIR)/gcc/d $(BUILD_DIR)/gcc/jit $(BUILD_DIR)/gcc/rust
	@nix-shell $(SHELL_NIX) --run 'set -o pipefail; make -C $(BUILD_DIR) -j$(JOBS) all-gcc all-target-libgcc 2>&1 | tee -a $(LOG_DIR)/build.log'
	@echo "=== build done (see $(LOG_DIR)) ==="

configure:
	@mkdir -p $(BUILD_DIR) $(LOG_DIR)
	@echo "GLIBC_INC = $(GLIBC_INC)"
	nix-shell $(SHELL_NIX) --run "cd $(BUILD_DIR) && $(abspath $(GCC_SRC))/configure $(CONFIGURE_ARGS) --with-native-system-header-dir='$(GLIBC_INC)' >> $(LOG_DIR)/configure.log 2>&1; echo CONFIGURE_EXIT=$$? | tee -a $(LOG_DIR)/configure.log"

$(BUILD_DIR)/Makefile: configure

shell:
	nix-shell $(SHELL_NIX)

logs:
	@tail -n 50 $(LOG_DIR)/build.log

clean:
	@chmod -R u+w $(BUILD_DIR) 2>/dev/null || true
	rm -rf $(BUILD_DIR)

# ------------------------------------------------------------------
# Self-contained "czet" binary: static base + blob(toolchain+libc).
# ------------------------------------------------------------------

# Root of the embedded tree (minimal C toolchain + musl/glibc sysroots).
BLOB_ROOT := $(BUILD_DIR)/blob-root
BLOB_TAR  := $(BUILD_DIR)/czet.blob.tar
CZET0   := $(BUILD_DIR)/czet.base

$(CZET0): $(CZET_SRC)
	@echo ">> compilando $(CZET0)"
	@mkdir -p $(BUILD_DIR)
	cc -std=c11 -O2 -Wall -Wextra -static -s \
		-isystem $(GLIBC_INC) \
		-B$(GLIBC_LIB) -L$(GLIBC_LIB) \
		-L$(GLIBC_STATIC) \
		-o $@ $(CZET_SRC)

blob: $(BLOB_TAR)

# Don't list build drivers (xgcc/collect2/crt) as make prerequisites: it
# retriggers the collect2/crt relink in gcc's Makefile.  Just test for them
# and copy as-is (the build was already validated by 'make build').  The
# only part tracking frontend sources is cc1, so the blob keys off it: any
# frontend change rebuilds the final binary.
# Extract pinned third-party originals (once; the stamp tracks the tarballs
# and the patch) to stable build paths.
$(TP_STAMP): $(THIRD_SRC)/libaco.tar.gz $(THIRD_SRC)/critbit.tar.gz \
		$(THIRD_SRC)/btree.c.tar.gz $(THIRD_PATCH_CRITBIT)
	@echo ">> extrayendo third-party originales"
	@rm -rf $(TP_BUILD)
	@mkdir -p $(TP_BUILD)/libaco $(TP_BUILD)/critbit $(TP_BUILD)/btree
	@tar xzf $(THIRD_SRC)/libaco.tar.gz -C $(TP_BUILD)/libaco --strip-components=1
	@tar xzf $(THIRD_SRC)/critbit.tar.gz -C $(TP_BUILD)/critbit --strip-components=1
	@tar xzf $(THIRD_SRC)/btree.c.tar.gz -C $(TP_BUILD)/btree --strip-components=1
	@patch -N -p1 -d $(TP_BUILD)/critbit < $(THIRD_PATCH_CRITBIT)
	@touch $@

# libczet_utils.a: aco.c + acosw.S (libaco) + critbit.c + btree.c.
# musl flavor: built against the same headers the user will see
# (-nostdinc + blob isystems).  glibc flavor: plain system cc.
$(TP_MUSL_A): $(TP_STAMP)
	@echo ">> compilando libczet_utils (musl)"
	@mkdir -p $(TP_BUILD)/obj-musl
	@cc -O2 -nostdinc -isystem $(MUSL_INC) -isystem $(BUILD_DIR)/gcc/include \
		-c $(TP_BUILD)/libaco/aco.c -o $(TP_BUILD)/obj-musl/aco.o
	@cc -O2 -nostdinc -isystem $(MUSL_INC) -isystem $(BUILD_DIR)/gcc/include \
		-c $(TP_BUILD)/libaco/acosw.S -o $(TP_BUILD)/obj-musl/acosw.o
	@cc -O2 -nostdinc -isystem $(MUSL_INC) -isystem $(BUILD_DIR)/gcc/include \
		-c $(TP_BUILD)/critbit/critbit.c -o $(TP_BUILD)/obj-musl/critbit.o
	@cc -O2 -nostdinc -isystem $(MUSL_INC) -isystem $(BUILD_DIR)/gcc/include \
		-c $(TP_BUILD)/btree/btree.c -o $(TP_BUILD)/obj-musl/btree.o
	@ar rcs $@ $(TP_BUILD)/obj-musl/aco.o $(TP_BUILD)/obj-musl/acosw.o \
		$(TP_BUILD)/obj-musl/critbit.o $(TP_BUILD)/obj-musl/btree.o

$(TP_GLIBC_A): $(TP_STAMP)
	@echo ">> compilando libczet_utils (glibc)"
	@mkdir -p $(TP_BUILD)/obj-glibc
	@cc -O2 -c $(TP_BUILD)/libaco/aco.c -o $(TP_BUILD)/obj-glibc/aco.o
	@cc -O2 -c $(TP_BUILD)/libaco/acosw.S -o $(TP_BUILD)/obj-glibc/acosw.o
	@cc -O2 -c $(TP_BUILD)/critbit/critbit.c -o $(TP_BUILD)/obj-glibc/critbit.o
	@cc -O2 -c $(TP_BUILD)/btree/btree.c -o $(TP_BUILD)/obj-glibc/btree.o
	@ar rcs $@ $(TP_BUILD)/obj-glibc/aco.o $(TP_BUILD)/obj-glibc/acosw.o \
		$(TP_BUILD)/obj-glibc/critbit.o $(TP_BUILD)/obj-glibc/btree.o

$(BLOB_TAR): $(BUILD_DIR)/gcc/cc1 $(TP_MUSL_A) $(TP_GLIBC_A) \
		$(STDLIB_UTILS) $(wildcard $(STDLIB_UTILS)/*.h)
	@echo ">> ensamblando rootfs del blob"
	@test -f $(BUILD_DIR)/gcc/xgcc || { echo "missing gcc build (run 'make build')"; exit 1; }
	@chmod -R u+w $(BLOB_ROOT) 2>/dev/null || true
	@rm -rf $(BLOB_ROOT)
	@mkdir -p $(BLOB_ROOT)/gcc $(BLOB_ROOT)/libgcc \
		$(BLOB_ROOT)/sysroot/musl/include $(BLOB_ROOT)/sysroot/musl/lib \
		$(BLOB_ROOT)/sysroot/glibc/include $(BLOB_ROOT)/sysroot/glibc/lib
	@cp $(BUILD_DIR)/gcc/xgcc $(BUILD_DIR)/gcc/cc1 $(BUILD_DIR)/gcc/collect2 \
		$(BUILD_DIR)/gcc/liblto_plugin.so $(BLOB_ROOT)/gcc/
	@cp $(BUILD_DIR)/gcc/crtbegin.o $(BUILD_DIR)/gcc/crtbeginS.o \
		$(BUILD_DIR)/gcc/crtbeginT.o $(BUILD_DIR)/gcc/crtend.o \
		$(BUILD_DIR)/gcc/crtendS.o $(BLOB_ROOT)/gcc/
	@cp -r $(BUILD_DIR)/gcc/include $(BLOB_ROOT)/gcc/include
	@test -n "$(LIBGCC_DIR)" || { echo "missing libgcc dir for $(GCC_TRIPLE) (run 'make build')"; exit 1; }
	@cp $(LIBGCC_DIR)/libgcc.a \
		$(BLOB_ROOT)/libgcc/
	@cp -r $(MUSL_INC)/. $(BLOB_ROOT)/sysroot/musl/include/
	@mkdir -p $(BLOB_ROOT)/sysroot/musl/include/utils \
		$(BLOB_ROOT)/sysroot/glibc/include/utils
	@cp -r $(STDLIB_UTILS)/. $(BLOB_ROOT)/sysroot/musl/include/utils/
	@cp -r $(STDLIB_UTILS)/. $(BLOB_ROOT)/sysroot/glibc/include/utils/
	@cp $(TP_BUILD)/libaco/aco.h $(BLOB_ROOT)/sysroot/musl/include/utils/libaco.h
	@cp $(TP_BUILD)/libaco/aco.h $(BLOB_ROOT)/sysroot/glibc/include/utils/libaco.h
	@cp $(TP_BUILD)/critbit/critbit.h $(BLOB_ROOT)/sysroot/musl/include/utils/critbit.h
	@cp $(TP_BUILD)/critbit/critbit.h $(BLOB_ROOT)/sysroot/glibc/include/utils/critbit.h
	@cp $(TP_BUILD)/btree/btree.h $(BLOB_ROOT)/sysroot/musl/include/utils/btree.h
	@cp $(TP_BUILD)/btree/btree.h $(BLOB_ROOT)/sysroot/glibc/include/utils/btree.h
	@cp $(TP_MUSL_A) $(BLOB_ROOT)/sysroot/musl/lib/libczet_utils.a
	@cp $(TP_GLIBC_A) $(BLOB_ROOT)/sysroot/glibc/lib/libczet_utils.a
	@cp $(MUSL_LIB)/crt1.o $(MUSL_LIB)/crti.o $(MUSL_LIB)/crtn.o \
		$(MUSL_LIB)/libc.a $(BLOB_ROOT)/sysroot/musl/lib/
	@cp $(MUSL_DYN)/lib/libc.so $(BLOB_ROOT)/sysroot/musl/lib/ 2>/dev/null || true
	@cp $(GLIBC_LIB)/crt1.o $(GLIBC_LIB)/crti.o $(GLIBC_LIB)/crtn.o \
		$(BLOB_ROOT)/sysroot/glibc/lib/
	@cp $(GLIBC_STATIC)/libc.a $(GLIBC_STATIC)/libc_nonshared.a \
		$(GLIBC_STATIC)/libm.a $(GLIBC_STATIC)/libpthread.a \
		$(GLIBC_STATIC)/libdl.a $(GLIBC_STATIC)/librt.a \
		$(GLIBC_STATIC)/libutil.a $(GLIBC_STATIC)/libresolv.a \
		$(GLIBC_STATIC)/libmvec.a $(GLIBC_STATIC)/libg.a \
		$(BLOB_ROOT)/sysroot/glibc/lib/ 2>/dev/null || true
	@cp $(GLIBC_LIB)/libc.so.6 $(GLIBC_LIB)/libm.so.6 \
		$(GLIBC_LIB)/$(LD_LINUX) \
		$(GLIBC_LIB)/libc_nonshared.a $(BLOB_ROOT)/sysroot/glibc/lib/ \
		2>/dev/null || true
	@echo ">> generando tar ustar"
	@cd $(BLOB_ROOT) && tar --format=ustar -cf $(BLOB_TAR) \
		gcc libgcc sysroot
	@du -h $(BLOB_TAR)

$(CZET): $(CZET0) $(BLOB_TAR)
	@echo ">> ensamblando y cociendo binario autocontenido"
	@$(MAKE) --no-print-directory $(BLOB_TAR)
	@python3 $(EMBED_PY) $(CZET0) $(BLOB_TAR) $@
	@chmod +x $@

czet: $(CZET)

embed: $(CZET)
