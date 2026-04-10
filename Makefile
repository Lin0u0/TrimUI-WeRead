PLATFORM ?= tg5040

APP_NAME := WeRead
APP_VERSION := $(strip $(shell cat VERSION 2>/dev/null || printf '0.1.0'))
TARGET := weread
DIST_DIR := dist
BUILD_DIR := build/$(PLATFORM)
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin
HOST_BUILD_DIR := build/host
HOST_OBJ_DIR := $(HOST_BUILD_DIR)/obj
HOST_BIN_DIR := $(HOST_BUILD_DIR)/bin
HOST_CC ?= cc
STAGE_ROOT := $(BUILD_DIR)/stage
TARGET_PATH := $(BIN_DIR)/$(TARGET)
HOST_APP_PATH := $(HOST_BIN_DIR)/$(TARGET)

SRC_DIRS := src vendor
SRCS := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS))
HOST_APP_SRCS := $(SRCS)
HOST_TEST_APP_SRCS := $(filter-out src/main.c,$(SRCS))
HOST_APP_OBJS := $(patsubst %.c,$(HOST_OBJ_DIR)/%.o,$(HOST_APP_SRCS))
HOST_TEST_APP_OBJS := $(patsubst %.c,$(HOST_OBJ_DIR)/%.o,$(HOST_TEST_APP_SRCS))
HOST_TEST_SUPPORT_OBJ := $(HOST_OBJ_DIR)/tests/host/test_support.o
HOST_TEST_SRCS := $(filter-out tests/host/test_support.c,$(wildcard tests/host/test_*.c))
HOST_TEST_BINS := $(patsubst tests/host/%.c,$(HOST_BIN_DIR)/%,$(HOST_TEST_SRCS))

ASSET_ICON := assets/icons/weread.png
ASSET_ICONTOP := assets/icons/weread-icontop.png
ASSET_CACERT := assets/cacert.pem

TG5040_DEPS_PREFIX ?= $(abspath third_party/tg5040)
TG5040_CURL_PREFIX ?= $(TG5040_DEPS_PREFIX)/curl
TG5040_SDK_ROOT ?= $(abspath build/tg5040-sdk)
TG5040_SDK_USR ?= $(TG5040_SDK_ROOT)/sdk_usr/usr
TG5040_TOOLCHAIN_ROOT ?= $(abspath build/tg5040-toolchain)
TG5040_GCC_PATH ?=
TG5040_SDK_SHA256 := b6b615d03204e9d9bb1a91c31de0c3402434f6b8fc780743fa88a5f1da6f3c79
TG5040_TOOLCHAIN_SHA256 := 1e33d53dea59c8de823bbdfe0798280bdcd138636c7060da9d77a97ded095a84
TG5040_CC := $(or $(strip $(TG5040_GCC_PATH)),$(shell command -v aarch64-none-linux-gnu-gcc 2>/dev/null),$(shell command -v aarch64-linux-gnu-gcc 2>/dev/null),aarch64-linux-gnu-gcc)
TG5040_CROSS_PREFIX := $(or $(strip $(patsubst %gcc,%,$(notdir $(TG5040_CC)))),$(shell $(TG5040_CC) -dumpmachine 2>/dev/null | sed 's|$$|-|' ),aarch64-linux-gnu-)
TG5040_TARGET := $(or $(strip $(patsubst %-,%,$(TG5040_CROSS_PREFIX))),aarch64-linux-gnu)

COMMON_CFLAGS := -Wall -Wextra -Wno-unused-parameter -Ivendor
COMMON_LDFLAGS := -lm
HOST_SDKROOT := $(strip $(shell xcrun --show-sdk-path 2>/dev/null))
HOST_CURL_CFLAGS := $(strip $(shell pkg-config --cflags libcurl 2>/dev/null || curl-config --cflags 2>/dev/null))
HOST_CURL_LIBS := $(strip $(shell pkg-config --libs libcurl 2>/dev/null || curl-config --libs 2>/dev/null || printf '%s' '-lcurl'))
ifeq ($(HOST_CURL_CFLAGS),)
  ifneq ($(HOST_SDKROOT),)
    HOST_CURL_CFLAGS := -I$(HOST_SDKROOT)/usr/include
  endif
endif
HOST_CFLAGS := -O0 -g -Wall -Wextra -Wno-unused-parameter -DHAVE_SDL=0 -Isrc -Ivendor -Itests/host $(HOST_CURL_CFLAGS)
HOST_LDFLAGS := $(HOST_CURL_LIBS) $(COMMON_LDFLAGS) -lpthread

TG5040_RUNTIME_LIBS := \
	libSDL2.so \
	libSDL2-2.0.so.0 \
	libSDL2-2.0.so.0.2600.1 \
	libSDL2_ttf.so \
	libSDL2_ttf-2.0.so.0 \
	libSDL2_ttf-2.0.so.0.10.3 \
	libSDL2_image.so \
	libSDL2_image-2.0.so.0 \
	libSDL2_image-2.0.so.0.0.1 \
	libfreetype.so \
	libfreetype.so.6 \
	libfreetype.so.6.12.1 \
	libbz2.so \
	libbz2.so.1.0 \
	libbz2.so.1.0.6 \
	libssl.so \
	libssl.so.1.1 \
	libcrypto.so \
	libcrypto.so.1.1 \
	libz.so \
	libz.so.1

ifeq ($(PLATFORM),tg5040)
  CROSS ?= aarch64-linux-gnu-
  CC := $(TG5040_CC)
  TG5040_RESOLVED_SYSROOT := $(strip $(shell $(CC) -print-sysroot 2>/dev/null))
  TG5040_LIBGCC_S_PATH := $(strip $(shell $(CC) -print-file-name=libgcc_s.so.1 2>/dev/null))
  SDL_CFLAGS ?= -I$(TG5040_SDK_USR)/include/SDL2 -D_REENTRANT
  TG5040_RPATH_LINKS :=
  ifneq ($(TG5040_RESOLVED_SYSROOT),)
    TG5040_RPATH_LINKS += \
      -Wl,-rpath-link,$(TG5040_RESOLVED_SYSROOT)/lib \
      -Wl,-rpath-link,$(TG5040_RESOLVED_SYSROOT)/usr/lib \
      -Wl,-rpath-link,$(TG5040_RESOLVED_SYSROOT)/lib64 \
      -Wl,-rpath-link,$(TG5040_RESOLVED_SYSROOT)/usr/lib64
  endif
  SDL_LIBS ?= $(TG5040_RPATH_LINKS) \
    $(TG5040_SDK_USR)/lib/libSDL2.so \
    $(TG5040_SDK_USR)/lib/libSDL2_ttf.so \
    $(TG5040_SDK_USR)/lib/libSDL2_image.so \
    $(TG5040_SDK_USR)/lib/libfreetype.so \
    $(TG5040_SDK_USR)/lib/libbz2.so \
    $(TG5040_SDK_USR)/lib/libz.so \
    -ldl -lpthread
  CURL_CFLAGS ?= -I$(TG5040_CURL_PREFIX)/include
  CURL_LIBS ?= \
    $(TG5040_CURL_PREFIX)/lib/libcurl.a \
    $(TG5040_SDK_USR)/lib/libssl.so.1.1 \
    $(TG5040_SDK_USR)/lib/libcrypto.so.1.1 \
    $(TG5040_SDK_USR)/lib/libz.so \
    -ldl -lpthread
else
  $(error Unsupported PLATFORM '$(PLATFORM)')
endif

ifneq ($(strip $(SDL_CFLAGS)$(SDL_LIBS)),)
  COMMON_CFLAGS += -DHAVE_SDL=1
else
  COMMON_CFLAGS += -DHAVE_SDL=0
endif

CFLAGS := -mcpu=cortex-a53 -O2 $(COMMON_CFLAGS) $(SDL_CFLAGS) $(CURL_CFLAGS)
LDFLAGS := $(SDL_LIBS) $(CURL_LIBS) $(COMMON_LDFLAGS) -lpthread

.PHONY: all help clean clean-tg5040 clean-dist \
	dirs tg5040 tg5040-sdk tg5040-toolchain tg5040-libcurl tg5040-bootstrap \
	doctor-release doctor-nextui doctor-stock doctor-crossmix \
	package package-tg5040 package-nextui package-stock package-crossmix package-all \
	package-audit-nextui package-audit-stock package-audit-crossmix package-audit-all \
	nextui-release stock-release crossmix-release print-config test-host test-smoke test-package-audit-smoke

all: $(TARGET_PATH)

help:
	@printf '%s\n' 'Available targets:'
	@printf '%s\n' '  make                    Build the tg5040 binary'
	@printf '%s\n' '  make package            Build the default NextUI / MinUI package'
	@printf '%s\n' '  make nextui-release     Build the NextUI / MinUI archive'
	@printf '%s\n' '  make stock-release      Build the TrimUI stock OS app archive'
	@printf '%s\n' '  make crossmix-release   Build the CrossMix-OS app archive'
	@printf '%s\n' '  make package-all        Build all distributable archives'
	@printf '%s\n' '  make package-audit-nextui     Audit the built NextUI tarball and pakz contents'
	@printf '%s\n' '  make package-audit-stock      Audit the built Stock OS package archive'
	@printf '%s\n' '  make package-audit-crossmix   Audit the built CrossMix-OS package archive'
	@printf '%s\n' '  make package-audit-all        Build and audit all distributable archives'
	@printf '%s\n' ''
	@printf '%s\n' 'TG5040 dependency targets:'
	@printf '%s\n' '  make tg5040-sdk         Download and extract the official TrimUI SDK userland'
	@printf '%s\n' '  make tg5040-libcurl     Build static libcurl against the TrimUI SDK'
	@printf '%s\n' '  make tg5040-bootstrap   Prepare both SDK and tg5040 libcurl'
	@printf '%s\n' ''
	@printf '%s\n' 'Build / debug targets:'
	@printf '%s\n' '  make tg5040             Build the tg5040 binary only'
	@printf '%s\n' '  make test-host          Build and run host-native C verification binaries'
	@printf '%s\n' '  make test-smoke         Run thin host-native CLI smoke checks'
	@printf '%s\n' '  make test-package-audit-smoke Run deterministic smoke coverage for the package audit helper'
	@printf '%s\n' '  make doctor-release     Verify tg5040 release prerequisites before packaging'
	@printf '%s\n' '  make print-config       Print resolved build configuration'
	@printf '%s\n' ''
	@printf '%s\n' 'Cleanup targets:'
	@printf '%s\n' '  make clean              Remove all build artifacts'
	@printf '%s\n' '  make clean-tg5040       Remove tg5040 build artifacts, SDK cache, and cached libcurl'
	@printf '%s\n' '  make clean-dist         Remove packaged archives'

dirs:
	@mkdir -p $(OBJ_DIR)/src $(OBJ_DIR)/vendor $(BIN_DIR) $(DIST_DIR)

$(TARGET_PATH): dirs $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(HOST_OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<

$(HOST_APP_PATH): $(HOST_APP_OBJS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -o $@ $^ $(HOST_LDFLAGS)

$(HOST_BIN_DIR)/test_%: $(HOST_OBJ_DIR)/tests/host/test_%.o $(HOST_TEST_SUPPORT_OBJ) $(HOST_TEST_APP_OBJS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -o $@ $^ $(HOST_LDFLAGS)

tg5040:
	$(MAKE) PLATFORM=tg5040 all

test-host: $(HOST_TEST_BINS)
	@set -e; \
	for test_bin in $(HOST_TEST_BINS); do \
		printf '%s\n' "[test-host] $$test_bin"; \
		"$$test_bin"; \
	done

test-smoke: $(HOST_APP_PATH)
	@sh tests/smoke/cli_smoke.sh "$(HOST_APP_PATH)"

test-package-audit-smoke:
	@sh tests/smoke/package_audit_smoke.sh

tg5040-sdk:
	SDK_SHA256="$(TG5040_SDK_SHA256)" ./scripts/bootstrap_tg5040_sdk.sh "$(TG5040_SDK_ROOT)"

tg5040-toolchain:
	TOOLCHAIN_SHA256="$(TG5040_TOOLCHAIN_SHA256)" ./scripts/setup_tg5040_toolchain.sh "$(TG5040_TOOLCHAIN_ROOT)"

tg5040-libcurl:
	CC="$(TG5040_CC)" TARGET="$(TG5040_TARGET)" CROSS_PREFIX="$(TG5040_CROSS_PREFIX)" ./scripts/build_tg5040_libcurl.sh "$(TG5040_DEPS_PREFIX)"

tg5040-bootstrap: tg5040-sdk tg5040-libcurl

doctor-release:
	@test -x "$(TG5040_CC)" || { echo "missing tg5040 compiler: $(TG5040_CC)" >&2; echo "install aarch64-none-linux-gnu-gcc or aarch64-linux-gnu-gcc" >&2; exit 1; }
	@test -f "$(TG5040_SDK_USR)/include/SDL2/SDL.h" && test -f "$(TG5040_SDK_USR)/lib/libSDL2.so" || { echo "missing TrimUI SDK userland under $(TG5040_SDK_USR)" >&2; echo "run: make tg5040-sdk" >&2; exit 1; }
	@test -f "$(TG5040_CURL_PREFIX)/lib/libcurl.a" && test -f "$(TG5040_CURL_PREFIX)/include/curl/curl.h" || { echo "missing tg5040 static libcurl under $(TG5040_CURL_PREFIX)" >&2; echo "run: make tg5040-libcurl" >&2; exit 1; }
	@command -v zip >/dev/null 2>&1 || { echo "missing zip command" >&2; exit 1; }
	@test -n "$(TG5040_LIBGCC_S_PATH)" && test "$(TG5040_LIBGCC_S_PATH)" != "libgcc_s.so.1" && test -f "$(TG5040_LIBGCC_S_PATH)" || { echo "missing libgcc_s.so.1 for $(TG5040_CC)" >&2; echo "run: make tg5040-bootstrap" >&2; exit 1; }

doctor-nextui:
	@test -f "$(ASSET_ICON)" || { echo "missing icon asset: $(ASSET_ICON)" >&2; exit 1; }
	@test -f "$(ASSET_CACERT)" || { echo "missing CA bundle asset: $(ASSET_CACERT)" >&2; exit 1; }

doctor-stock:
	@test -f "$(ASSET_ICON)" || { echo "missing icon asset: $(ASSET_ICON)" >&2; exit 1; }
	@test -f "$(ASSET_ICONTOP)" || { echo "missing icontop asset: $(ASSET_ICONTOP)" >&2; exit 1; }
	@test -f "$(ASSET_CACERT)" || { echo "missing CA bundle asset: $(ASSET_CACERT)" >&2; exit 1; }

doctor-crossmix:
	@test -f "$(ASSET_ICON)" || { echo "missing icon asset: $(ASSET_ICON)" >&2; exit 1; }
	@test -f "$(ASSET_CACERT)" || { echo "missing CA bundle asset: $(ASSET_CACERT)" >&2; exit 1; }

package: package-$(PLATFORM)

package-tg5040:
	$(MAKE) PLATFORM=tg5040 package-nextui

package-nextui: doctor-release doctor-nextui
	@rm -rf build/tg5040
	$(MAKE) PLATFORM=tg5040 all
	@rm -rf "$(STAGE_ROOT)/nextui"
	@mkdir -p "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/bin/tg5040"
	@mkdir -p "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/lib/tg5040"
	@mkdir -p "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/res"
	@mkdir -p "$(STAGE_ROOT)/nextui/Tools/tg5040/.media"
	cp packaging/nextui/launch.sh "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/launch.sh"
	cp "$(ASSET_ICON)" "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/icon.png"
	cp "$(ASSET_ICON)" "$(STAGE_ROOT)/nextui/Tools/tg5040/.media/WeRead.png"
	cp "$(ASSET_CACERT)" "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/res/cacert.pem"
	cp "$(TARGET_PATH)" "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/bin/tg5040/$(TARGET)"
	sed 's/@VERSION@/$(APP_VERSION)/g' packaging/nextui/pak.json > "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/pak.json"
	chmod +x "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/launch.sh" "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/bin/tg5040/$(TARGET)"
	@for lib in $(TG5040_RUNTIME_LIBS); do \
		cp -aL "$(TG5040_SDK_USR)/lib/$$lib" "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/lib/tg5040/"; \
	done
	@test -f "$(TG5040_LIBGCC_S_PATH)" || { echo "missing libgcc_s.so.1 for $(CC)" >&2; exit 1; }
	cp -aL "$(TG5040_LIBGCC_S_PATH)" "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/lib/tg5040/libgcc_s.so.1"
	tar -C "$(STAGE_ROOT)/nextui" -czf "$(DIST_DIR)/$(APP_NAME)-nextui.tar.gz" Tools
	rm -f "$(DIST_DIR)/$(APP_NAME).pakz"
	cd "$(STAGE_ROOT)/nextui" && zip -qr "$(abspath $(DIST_DIR))/$(APP_NAME).pakz" Tools

package-stock: doctor-release doctor-stock
	@rm -rf build/tg5040
	$(MAKE) PLATFORM=tg5040 all
	@rm -rf "$(STAGE_ROOT)/stock"
	@mkdir -p "$(STAGE_ROOT)/stock/Apps/WeRead/bin/tg5040"
	@mkdir -p "$(STAGE_ROOT)/stock/Apps/WeRead/lib/tg5040"
	@mkdir -p "$(STAGE_ROOT)/stock/Apps/WeRead/res"
	cp packaging/stock/launch.sh "$(STAGE_ROOT)/stock/Apps/WeRead/launch.sh"
	cp packaging/stock/config.json "$(STAGE_ROOT)/stock/Apps/WeRead/config.json"
	cp "$(ASSET_ICON)" "$(STAGE_ROOT)/stock/Apps/WeRead/icon.png"
	cp "$(ASSET_ICONTOP)" "$(STAGE_ROOT)/stock/Apps/WeRead/icontop.png"
	cp "$(ASSET_CACERT)" "$(STAGE_ROOT)/stock/Apps/WeRead/res/cacert.pem"
	cp "$(TARGET_PATH)" "$(STAGE_ROOT)/stock/Apps/WeRead/bin/tg5040/$(TARGET)"
	chmod +x "$(STAGE_ROOT)/stock/Apps/WeRead/launch.sh" "$(STAGE_ROOT)/stock/Apps/WeRead/bin/tg5040/$(TARGET)"
	@for lib in $(TG5040_RUNTIME_LIBS); do \
		cp -aL "$(TG5040_SDK_USR)/lib/$$lib" "$(STAGE_ROOT)/stock/Apps/WeRead/lib/tg5040/"; \
	done
	@test -f "$(TG5040_LIBGCC_S_PATH)" || { echo "missing libgcc_s.so.1 for $(CC)" >&2; exit 1; }
	cp -aL "$(TG5040_LIBGCC_S_PATH)" "$(STAGE_ROOT)/stock/Apps/WeRead/lib/tg5040/libgcc_s.so.1"
	tar -C "$(STAGE_ROOT)/stock" -czf "$(DIST_DIR)/$(APP_NAME)-stock-app.tar.gz" Apps

package-crossmix: doctor-release doctor-crossmix
	@rm -rf build/tg5040
	$(MAKE) PLATFORM=tg5040 all
	@rm -rf "$(STAGE_ROOT)/crossmix"
	@mkdir -p "$(STAGE_ROOT)/crossmix/Apps/WeRead/bin/tg5040"
	@mkdir -p "$(STAGE_ROOT)/crossmix/Apps/WeRead/lib/tg5040"
	@mkdir -p "$(STAGE_ROOT)/crossmix/Apps/WeRead/res"
	cp packaging/crossmix/launch.sh "$(STAGE_ROOT)/crossmix/Apps/WeRead/launch.sh"
	cp packaging/crossmix/config.json "$(STAGE_ROOT)/crossmix/Apps/WeRead/config.json"
	cp "$(ASSET_ICON)" "$(STAGE_ROOT)/crossmix/Apps/WeRead/icon.png"
	cp "$(ASSET_CACERT)" "$(STAGE_ROOT)/crossmix/Apps/WeRead/res/cacert.pem"
	cp "$(TARGET_PATH)" "$(STAGE_ROOT)/crossmix/Apps/WeRead/bin/tg5040/$(TARGET)"
	chmod +x "$(STAGE_ROOT)/crossmix/Apps/WeRead/launch.sh" "$(STAGE_ROOT)/crossmix/Apps/WeRead/bin/tg5040/$(TARGET)"
	@for lib in $(TG5040_RUNTIME_LIBS); do \
		cp -aL "$(TG5040_SDK_USR)/lib/$$lib" "$(STAGE_ROOT)/crossmix/Apps/WeRead/lib/tg5040/"; \
	done
	@test -f "$(TG5040_LIBGCC_S_PATH)" || { echo "missing libgcc_s.so.1 for $(CC)" >&2; exit 1; }
	cp -aL "$(TG5040_LIBGCC_S_PATH)" "$(STAGE_ROOT)/crossmix/Apps/WeRead/lib/tg5040/libgcc_s.so.1"
	tar -C "$(STAGE_ROOT)/crossmix" -czf "$(DIST_DIR)/$(APP_NAME)-crossmix.tar.gz" Apps

nextui-release:
	$(MAKE) PLATFORM=tg5040 package-nextui

stock-release:
	$(MAKE) PLATFORM=tg5040 package-stock

crossmix-release:
	$(MAKE) PLATFORM=tg5040 package-crossmix

package-all: doctor-release nextui-release stock-release crossmix-release

package-audit-nextui:
	./scripts/audit_trimui_package.sh nextui dist/WeRead-nextui.tar.gz
	./scripts/audit_trimui_package.sh nextui dist/WeRead.pakz

package-audit-stock:
	./scripts/audit_trimui_package.sh stock dist/WeRead-stock-app.tar.gz

package-audit-crossmix:
	./scripts/audit_trimui_package.sh crossmix dist/WeRead-crossmix.tar.gz

package-audit-all: package-all package-audit-nextui package-audit-stock package-audit-crossmix

print-config:
	@echo "PLATFORM=$(PLATFORM)"
	@echo "CC=$(CC)"
	@echo "BUILD_DIR=$(BUILD_DIR)"
	@echo "TARGET_PATH=$(TARGET_PATH)"
	@echo "SDL_CFLAGS=$(SDL_CFLAGS)"
	@echo "SDL_LIBS=$(SDL_LIBS)"
	@echo "CURL_CFLAGS=$(CURL_CFLAGS)"
	@echo "CURL_LIBS=$(CURL_LIBS)"
	@echo "TG5040_SDK_USR=$(TG5040_SDK_USR)"
	@echo "TG5040_SDK_SHA256=$(TG5040_SDK_SHA256)"
	@echo "TG5040_TOOLCHAIN_SHA256=$(TG5040_TOOLCHAIN_SHA256)"
	@echo "TG5040_CC=$(TG5040_CC)"
	@echo "TG5040_CROSS_PREFIX=$(TG5040_CROSS_PREFIX)"
	@echo "TG5040_TARGET=$(TG5040_TARGET)"
	@echo "APP_VERSION=$(APP_VERSION)"

clean:
	rm -rf build

clean-tg5040:
	rm -rf build/tg5040 build/tg5040-sdk build/tg5040-deps "$(TG5040_CURL_PREFIX)"

clean-dist:
	rm -rf dist
