GCC_SRC    := $(CURDIR)/src/gcc
BUILD_DIR  := $(CURDIR)/build
LOG_DIR    := $(BUILD_DIR)/Logs
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

# ------------------------------------------------------------------
# Generic host detection: compilers, headers and libc roots.
# Everything below is overridable (make VAR=...). Probing order is
# always: system/FHS locations first, /nix/store only as a last resort
# (NixOS has no /usr/include and no FHS libdirs).
# ------------------------------------------------------------------
CC      ?= cc
MUSL_CC ?= musl-gcc

# glibc headers for --with-native-system-header-dir. /usr/include counts
# only when it really is glibc (marker: gnu/stubs.h); otherwise ask the
# compiler where its stdlib.h lives (covers NixOS).
_GLIBC_INC_PROBE := $(shell printf '#include <stdlib.h>\n' | $(CC) -E - 2>/dev/null | grep -m1 'stdlib.h' | sed -n 's/.*"\(.*\)\/stdlib.h".*/\1/p')
GLIBC_INC ?= $(firstword \
	$(if $(wildcard /usr/include/gnu/stubs.h),/usr/include) \
	$(if $(wildcard $(_GLIBC_INC_PROBE)/gnu/stubs.h),$(_GLIBC_INC_PROBE)) \
	$(GLIBC_NIX_DEV))
NATIVE_INC_FLAG := $(if $(GLIBC_INC),--with-native-system-header-dir='$(GLIBC_INC)')

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
MULTIARCH   := x86_64-linux-gnu
MUSL_DEB    := x86_64-linux-musl
FILE_MATCH  := x86-64
LD_LINUX    := ld-linux-x86-64.so.2
else ifeq ($(ARCH_NORM),i686)
MUSL_TRIPLE := i686-unknown-linux-musl
GCC_TRIPLE  := i686-pc-linux-gnu
MULTIARCH   := i386-linux-gnu
MUSL_DEB    := i386-linux-musl
FILE_MATCH  := Intel 80386
LD_LINUX    := ld-linux.so.2
else ifeq ($(ARCH_NORM),aarch64)
MUSL_TRIPLE := aarch64-unknown-linux-musl
GCC_TRIPLE  := aarch64-unknown-linux-gnu
MULTIARCH   := aarch64-linux-gnu
MUSL_DEB    := aarch64-linux-musl
FILE_MATCH  := aarch64
LD_LINUX    := ld-linux-aarch64.so.1
else
$(error Unsupported ARCH=$(ARCH) (expected x86_64, i686/i386 or aarch64))
endif
# libgcc dir: prefer exact triple, fall back to any toolchain triple
# (exclude blob-root, which is output not input).
LIBGCC_DIR ?= $(firstword $(wildcard $(BUILD_DIR)/$(GCC_TRIPLE)/libgcc) $(wildcard $(BUILD_DIR)/*-pc-linux-gnu/libgcc $(BUILD_DIR)/*-unknown-linux-gnu/libgcc))

# --- glibc shared lib dir: crt files + shared objects (marker: libc.so.6,
# so a musl dir is never picked by accident) ---
_CC_CRT1_DIR := $(patsubst %/,%,$(dir $(filter /%,$(shell $(CC) -print-file-name=crt1.o 2>/dev/null))))
GLIBC_LIB ?= $(firstword \
	$(foreach D,$(_CC_CRT1_DIR) /usr/lib/$(MULTIARCH) /usr/lib64 /usr/lib /lib/$(MULTIARCH) /lib64 /lib,\
		$(if $(wildcard $(D)/crt1.o),$(if $(wildcard $(D)/libc.so.6),$(D)))) \
	$(GLIBC_NIX_LIB))

# --- glibc static dir: libc.a with a glibc marker beside it ---
_CC_LIBC_A_DIR := $(patsubst %/,%,$(dir $(filter /%,$(shell $(CC) -print-file-name=libc.a 2>/dev/null))))
GLIBC_STATIC ?= $(firstword \
	$(foreach D,$(GLIBC_LIB) $(_CC_LIBC_A_DIR) /usr/lib/$(MULTIARCH) /usr/lib64 /usr/lib,\
		$(if $(wildcard $(D)/libc.a),$(if $(or $(wildcard $(D)/libc.so.6),$(wildcard $(D)/libc_nonshared.a)),$(D)))) \
	$(GLIBC_NIX_STATIC))

# --- musl lib dir: crt files + libc.a, and NOT a glibc dir ---
# On musl-native hosts (Alpine) the system cc already targets musl.
_MUSL_HOST := $(or $(wildcard /lib/ld-musl-*),$(wildcard /usr/lib/ld-musl-*),$(wildcard /lib/libc.musl-*),$(wildcard /usr/lib/libc.musl-*))
_MUSL_CC_CRT1_DIR := $(patsubst %/,%,$(dir $(filter /%,$(shell $(MUSL_CC) -print-file-name=crt1.o 2>/dev/null))))
_MUSL_HOST_LIBDIR := $(if $(_MUSL_HOST),$(_CC_CRT1_DIR))
MUSL_LIB ?= $(firstword \
	$(foreach D,$(_MUSL_CC_CRT1_DIR) /usr/lib/$(MUSL_DEB) /lib/$(MUSL_DEB) /usr/lib/musl/lib /usr/local/musl/lib /opt/musl/lib $(_MUSL_HOST_LIBDIR),\
		$(if $(wildcard $(D)/crt1.o),$(if $(wildcard $(D)/libc.a),$(if $(wildcard $(D)/libc.so.6),,$(D))))) \
	$(MUSL_NIX_LIB))

# --- musl headers: musl-gcc knows best; never plain /usr/include of a glibc host ---
_MUSL_INC_PROBE := $(shell printf '#include <stdlib.h>\n' | $(MUSL_CC) -E - 2>/dev/null | grep -m1 'stdlib.h' | sed -n 's/.*"\(.*\)\/stdlib.h".*/\1/p')
MUSL_INC ?= $(firstword \
	$(if $(wildcard $(_MUSL_INC_PROBE)/gnu/stubs.h),,$(_MUSL_INC_PROBE)) \
	/usr/include/$(MUSL_DEB) /usr/lib/musl/include /usr/local/musl/include /opt/musl/include \
	$(if $(_MUSL_HOST),/usr/include) \
	$(MUSL_NIX_INC))

# --- musl dynamic libc.so (optional): store layout nests it under lib/ ---
MUSL_DYN ?= $(firstword $(MUSL_NIX_DYN) $(MUSL_LIB))

# --- NixOS store fallback (only used when generic probing finds nothing) ---
# The -dev include pins the version; among same-version lib dirs keep the
# one matching the target arch (32/64-bit variants may coexist).
# Note: filter/filter-out with an inner % is broken in make 4.4.1, hence
# firstword/wildcard selections with a literal suffix.
_GLIBC_STORE_VER   := $(shell printf '%s' '$(_GLIBC_INC_PROBE)' | sed -E 's#.*-glibc-([0-9][0-9.]*-[0-9]+).*#\1#')
# Keep the version only if sed really extracted it (else it echoes input back).
GLIBC_VER          ?= $(if $(filter-out $(_GLIBC_INC_PROBE),$(_GLIBC_STORE_VER)),$(_GLIBC_STORE_VER))
_GLIBC_STORE_CANDS := $(wildcard /nix/store/*-glibc-$(GLIBC_VER)/lib)
GLIBC_NIX_LIB      := $(firstword $(foreach L,$(_GLIBC_STORE_CANDS),$(if $(findstring $(FILE_MATCH),$(shell file -b $(L)/crt1.o 2>/dev/null)),$(L))))
GLIBC_NIX_STATIC   := $(firstword $(wildcard /nix/store/*-glibc-$(GLIBC_VER)-static/lib))
GLIBC_NIX_DEV      := $(firstword $(wildcard /nix/store/*-glibc-$(GLIBC_VER)-dev/include))
_MUSL_NIX_CANDS    := $(filter-out %-dev/ %-bin/,$(wildcard /nix/store/*-musl-static-$(MUSL_TRIPLE)-*/ /nix/store/*-musl-*/))
MUSL_NIX_STATIC    := $(patsubst %/,%,$(firstword $(foreach D,$(_MUSL_NIX_CANDS),$(if $(wildcard $(D)/lib/libc.a),$(D)))))
MUSL_NIX_LIB       := $(if $(MUSL_NIX_STATIC),$(MUSL_NIX_STATIC)/lib)
MUSL_VER           ?= $(lastword $(subst -, ,$(notdir $(MUSL_NIX_STATIC))))
MUSL_NIX_INC       := $(firstword $(wildcard /nix/store/*-musl-$(MUSL_VER)-dev/include))
MUSL_NIX_DYN       := $(patsubst %/,%,$(firstword $(filter-out %musl-static% %-dev/ %-bin/,$(wildcard /nix/store/*-musl-$(MUSL_VER)/))))

.PHONY: all build configure logs clean czet blob embed check-env

# Verify the host provides everything the blob needs, with install hints.
check-env:
	@test -n "$(GLIBC_INC)" || { echo "missing: glibc headers (GLIBC_INC empty). Debian/Ubuntu: apt install libc6-dev | Fedora: dnf install glibc-devel"; exit 1; }
	@test -n "$(GLIBC_LIB)" || { echo "missing: glibc lib dir (GLIBC_LIB empty). Debian/Ubuntu: apt install libc6-dev | Fedora: dnf install glibc-devel"; exit 1; }
	@test -n "$(GLIBC_STATIC)" || { echo "missing: static glibc (libc.a). Debian/Ubuntu: apt install libc6-dev | Fedora: dnf install glibc-static | NixOS: realize glibc.static"; exit 1; }
	@test -n "$(MUSL_LIB)" || { echo "missing: musl lib dir (MUSL_LIB empty). Debian/Ubuntu: apt install musl-tools | Fedora: dnf install musl-gcc musl-libc-static | Arch: pacman -S musl | Alpine: apk add musl-dev"; exit 1; }
	@test -n "$(MUSL_INC)" || { echo "missing: musl headers (MUSL_INC empty). Install musl dev files (see above) or override: make MUSL_INC=... MUSL_LIB=..."; exit 1; }
	@test -n "$(LIBGCC_DIR)" || echo "note: LIBGCC_DIR empty (normal before 'make build')"
	@echo "env OK:"
	@echo "  GLIBC_INC=$(GLIBC_INC) GLIBC_LIB=$(GLIBC_LIB) GLIBC_STATIC=$(GLIBC_STATIC)"
	@echo "  MUSL_LIB=$(MUSL_LIB) MUSL_INC=$(MUSL_INC)"

print-vars:
	@echo "ARCH         = $(ARCH) (norm: $(ARCH_NORM))"
	@echo "CC           = $(CC)"
	@echo "MUSL_CC      = $(MUSL_CC)"
	@echo "MUSL_TRIPLE  = $(MUSL_TRIPLE)"
	@echo "GCC_TRIPLE   = $(GCC_TRIPLE)"
	@echo "MULTIARCH    = $(MULTIARCH)"
	@echo "FILE_MATCH   = $(FILE_MATCH)"
	@echo "LD_LINUX     = $(LD_LINUX)"
	@echo "LIBGCC_DIR   = $(LIBGCC_DIR)"
	@echo "GLIBC_INC    = $(GLIBC_INC)"
	@echo "GLIBC_VER    = $(GLIBC_VER)"
	@echo "GLIBC_LIB    = $(GLIBC_LIB)"
	@echo "GLIBC_STATIC = $(GLIBC_STATIC)"
	@echo "MUSL_LIB     = $(MUSL_LIB)"
	@echo "MUSL_DYN     = $(MUSL_DYN)"
	@echo "MUSL_VER     = $(MUSL_VER)"
	@echo "MUSL_INC     = $(MUSL_INC)"

all:
	$(MAKE) build
	$(MAKE) czet
	@echo "=== done: self-contained binary at $(CZET) ==="

# Only build what the blob needs: the frontend (cc1/xgcc/collect2) and the
# target libgcc.  Skip the remaining target libraries (zlib, etc.): not
# embedded, and their configure fails on NixOS (link test).
build: $(BUILD_DIR)/Makefile
	@mkdir -p $(LOG_DIR) $(BUILD_DIR)/gcc/d $(BUILD_DIR)/gcc/jit $(BUILD_DIR)/gcc/rust
	@bash -o pipefail -c 'make -C $(BUILD_DIR) -j$(JOBS) all-gcc all-target-libgcc 2>&1 | tee -a $(LOG_DIR)/build.log'
	@echo "=== build done (see $(LOG_DIR)) ==="

configure:
	@mkdir -p $(BUILD_DIR) $(LOG_DIR)
	@echo "GLIBC_INC = $(GLIBC_INC)"
	@cd $(BUILD_DIR) && $(abspath $(GCC_SRC))/configure $(CONFIGURE_ARGS) $(NATIVE_INC_FLAG) >> $(LOG_DIR)/configure.log 2>&1; echo CONFIGURE_EXIT=$$? | tee -a $(LOG_DIR)/configure.log

$(BUILD_DIR)/Makefile: configure

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
	$(CC) -std=c11 -O2 -Wall -Wextra -static -s \
		$(if $(GLIBC_INC),-isystem $(GLIBC_INC)) \
		$(if $(GLIBC_LIB),-B$(GLIBC_LIB) -L$(GLIBC_LIB)) \
		$(if $(GLIBC_STATIC),-L$(GLIBC_STATIC)) \
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
	@test -n "$(MUSL_INC)" || { echo "missing musl headers (MUSL_INC empty; run 'make check-env')"; exit 1; }
	@mkdir -p $(TP_BUILD)/obj-musl
	@$(CC) -O2 -nostdinc -isystem $(MUSL_INC) -isystem $(BUILD_DIR)/gcc/include \
		-c $(TP_BUILD)/libaco/aco.c -o $(TP_BUILD)/obj-musl/aco.o
	@$(CC) -O2 -nostdinc -isystem $(MUSL_INC) -isystem $(BUILD_DIR)/gcc/include \
		-c $(TP_BUILD)/libaco/acosw.S -o $(TP_BUILD)/obj-musl/acosw.o
	@$(CC) -O2 -nostdinc -isystem $(MUSL_INC) -isystem $(BUILD_DIR)/gcc/include \
		-c $(TP_BUILD)/critbit/critbit.c -o $(TP_BUILD)/obj-musl/critbit.o
	@$(CC) -O2 -nostdinc -isystem $(MUSL_INC) -isystem $(BUILD_DIR)/gcc/include \
		-c $(TP_BUILD)/btree/btree.c -o $(TP_BUILD)/obj-musl/btree.o
	@ar rcs $@ $(TP_BUILD)/obj-musl/aco.o $(TP_BUILD)/obj-musl/acosw.o \
		$(TP_BUILD)/obj-musl/critbit.o $(TP_BUILD)/obj-musl/btree.o

$(TP_GLIBC_A): $(TP_STAMP)
	@echo ">> compilando libczet_utils (glibc)"
	@mkdir -p $(TP_BUILD)/obj-glibc
	@$(CC) -O2 -c $(TP_BUILD)/libaco/aco.c -o $(TP_BUILD)/obj-glibc/aco.o
	@$(CC) -O2 -c $(TP_BUILD)/libaco/acosw.S -o $(TP_BUILD)/obj-glibc/acosw.o
	@$(CC) -O2 -c $(TP_BUILD)/critbit/critbit.c -o $(TP_BUILD)/obj-glibc/critbit.o
	@$(CC) -O2 -c $(TP_BUILD)/btree/btree.c -o $(TP_BUILD)/obj-glibc/btree.o
	@ar rcs $@ $(TP_BUILD)/obj-glibc/aco.o $(TP_BUILD)/obj-glibc/acosw.o \
		$(TP_BUILD)/obj-glibc/critbit.o $(TP_BUILD)/obj-glibc/btree.o

$(BLOB_TAR): $(BUILD_DIR)/gcc/cc1 $(TP_MUSL_A) $(TP_GLIBC_A) \
		$(STDLIB_UTILS) $(wildcard $(STDLIB_UTILS)/*.h)
	@echo ">> ensamblando rootfs del blob"
	@test -f $(BUILD_DIR)/gcc/xgcc || { echo "missing gcc build (run 'make build')"; exit 1; }
	@test -n "$(MUSL_LIB)" || { echo "missing musl lib dir (MUSL_LIB empty; run 'make check-env')"; exit 1; }
	@test -n "$(MUSL_INC)" || { echo "missing musl headers (MUSL_INC empty; run 'make check-env')"; exit 1; }
	@test -n "$(GLIBC_LIB)" || { echo "missing glibc lib dir (GLIBC_LIB empty; run 'make check-env')"; exit 1; }
	@if [ -z "$(GLIBC_STATIC)" ]; then echo "warning: no static glibc found; blob will lack static glibc libs"; fi
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
	@cp $(MUSL_DYN)/lib/libc.so $(BLOB_ROOT)/sysroot/musl/lib/ 2>/dev/null || \
	 cp $(MUSL_DYN)/libc.so $(BLOB_ROOT)/sysroot/musl/lib/ 2>/dev/null || true
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
