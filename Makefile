PLATFORM ?= native

APP_NAME := WeRead
TARGET := weread
DIST_DIR := dist
BUILD_DIR := build/$(PLATFORM)
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin
STAGE_ROOT := $(BUILD_DIR)/stage
TARGET_PATH := $(BIN_DIR)/$(TARGET)

SRC_DIRS := src vendor
SRCS := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS))

ASSET_FONT := assets/fonts/SourceHanSerifSC-Regular.otf
ASSET_ICON := assets/icons/weread.png
ASSET_ICONTOP := assets/icons/weread-icontop.png
ASSET_CACERT := assets/cacert.pem

PKG_CONFIG ?= pkg-config
CURL_CONFIG ?= curl-config
BREW_PREFIX ?= /opt/homebrew
SDL2_CONFIG ?= $(BREW_PREFIX)/opt/sdl2/bin/sdl2-config

TG5040_DEPS_PREFIX ?= $(abspath third_party/tg5040)
TG5040_CURL_PREFIX ?= $(TG5040_DEPS_PREFIX)/curl
TG5040_SDK_ROOT ?= $(abspath build/tg5040-sdk)
TG5040_SDK_USR ?= $(TG5040_SDK_ROOT)/sdk_usr/usr
TG5040_GCC_PATH ?=
TG5040_SYSROOT ?=

COMMON_CFLAGS := -Wall -Wextra -Wno-unused-parameter -Ivendor
COMMON_LDFLAGS := -lm

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

ifeq ($(PLATFORM),native)
  CC := cc
  ifeq ($(shell command -v $(PKG_CONFIG) >/dev/null 2>&1; echo $$?),0)
    SDL_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_ttf SDL2_image 2>/dev/null)
    SDL_LIBS ?= $(shell $(PKG_CONFIG) --libs sdl2 SDL2_ttf SDL2_image 2>/dev/null)
  else ifeq ($(shell test -x $(BREW_PREFIX)/bin/pkg-config && echo yes),yes)
    SDL_CFLAGS ?= $(shell $(BREW_PREFIX)/bin/pkg-config --cflags sdl2 SDL2_ttf SDL2_image 2>/dev/null)
    SDL_LIBS ?= $(shell $(BREW_PREFIX)/bin/pkg-config --libs sdl2 SDL2_ttf SDL2_image 2>/dev/null)
  else ifeq ($(shell test -x $(SDL2_CONFIG) && test -d $(BREW_PREFIX)/opt/sdl2_ttf && test -d $(BREW_PREFIX)/opt/sdl2_image && echo yes),yes)
    SDL_CFLAGS ?= $(shell $(SDL2_CONFIG) --cflags) -I$(BREW_PREFIX)/opt/sdl2_ttf/include -I$(BREW_PREFIX)/opt/sdl2_image/include
    SDL_LIBS ?= $(shell $(SDL2_CONFIG) --libs) -L$(BREW_PREFIX)/opt/sdl2_ttf/lib -L$(BREW_PREFIX)/opt/sdl2_image/lib -lSDL2_ttf -lSDL2_image
  else
    SDL_CFLAGS ?=
    SDL_LIBS ?=
  endif

  ifeq ($(shell command -v $(CURL_CONFIG) >/dev/null 2>&1; echo $$?),0)
    CURL_CFLAGS ?= $(shell $(CURL_CONFIG) --cflags)
    CURL_LIBS ?= $(shell $(CURL_CONFIG) --libs)
  else
    CURL_CFLAGS ?=
    CURL_LIBS ?= -lcurl
  endif
  PACKAGE_NAME := $(APP_NAME)-macos.tar.gz
else ifeq ($(PLATFORM),tg5040)
  CROSS ?= aarch64-linux-gnu-
  CC := $(if $(strip $(TG5040_GCC_PATH)),$(TG5040_GCC_PATH),$(CROSS)gcc)
  TG5040_SYSROOT := $(if $(strip $(TG5040_SYSROOT)),$(TG5040_SYSROOT),$(shell $(CC) -print-sysroot 2>/dev/null))
  SDL_CFLAGS ?= -I$(TG5040_SDK_USR)/include/SDL2 -D_REENTRANT
  TG5040_RPATH_LINKS := \
    -Wl,-rpath-link,$(TG5040_SDK_USR)/lib \
    -Wl,-rpath-link,$(TG5040_SYSROOT)/lib \
    -Wl,-rpath-link,$(TG5040_SYSROOT)/usr/lib \
    -Wl,-rpath-link,$(TG5040_SYSROOT)/lib64 \
    -Wl,-rpath-link,$(TG5040_SYSROOT)/usr/lib64
  SDL_LIBS ?= $(TG5040_RPATH_LINKS) \
    $(TG5040_SDK_USR)/lib/libSDL2.so \
    $(TG5040_SDK_USR)/lib/libSDL2_ttf.so \
    $(TG5040_SDK_USR)/lib/libSDL2_image.so \
    $(TG5040_SDK_USR)/lib/libfreetype.so \
    $(TG5040_SDK_USR)/lib/libbz2.so \
    -lz -ldl -lpthread
  CURL_CFLAGS ?= -I$(TG5040_CURL_PREFIX)/include
  CURL_LIBS ?= \
    $(TG5040_CURL_PREFIX)/lib/libcurl.a \
    $(TG5040_SDK_USR)/lib/libssl.so.1.1 \
    $(TG5040_SDK_USR)/lib/libcrypto.so.1.1 \
    -lz -ldl -lpthread
  PACKAGE_NAME := $(APP_NAME)-trimui.tar.gz
else
  $(error Unsupported PLATFORM '$(PLATFORM)')
endif

ifneq ($(strip $(SDL_CFLAGS)$(SDL_LIBS)),)
  COMMON_CFLAGS += -DHAVE_SDL=1
else
  COMMON_CFLAGS += -DHAVE_SDL=0
endif

CFLAGS := $(if $(filter tg5040,$(PLATFORM)),-mcpu=cortex-a53 -O2,-O0 -g) $(COMMON_CFLAGS) $(SDL_CFLAGS) $(CURL_CFLAGS)
LDFLAGS := $(SDL_LIBS) $(CURL_LIBS) $(COMMON_LDFLAGS) $(if $(filter tg5040,$(PLATFORM)),-lpthread,)

.PHONY: all help clean clean-native clean-tg5040 clean-dist \
	dirs native tg5040 tg5040-sdk tg5040-libcurl tg5040-bootstrap \
	package package-native package-tg5040 package-nextui package-stock package-crossmix package-all \
	macos-release nextui-release stock-release crossmix-release print-config

all: $(TARGET_PATH)

help:
	@printf '%s\n' 'Available targets:'
	@printf '%s\n' '  make macos-release      Build the macOS test archive'
	@printf '%s\n' '  make nextui-release     Build the NextUI / MinUI archive'
	@printf '%s\n' '  make stock-release      Build the TrimUI stock OS app archive'
	@printf '%s\n' '  make crossmix-release   Build the CrossMix-OS app archive'
	@printf '%s\n' '  make package-all        Build all distributable archives'
	@printf '%s\n' ''
	@printf '%s\n' 'TG5040 dependency targets:'
	@printf '%s\n' '  make tg5040-sdk         Download and extract the official TrimUI SDK userland'
	@printf '%s\n' '  make tg5040-libcurl     Build static libcurl against the TrimUI SDK'
	@printf '%s\n' '  make tg5040-bootstrap   Prepare both SDK and tg5040 libcurl'
	@printf '%s\n' ''
	@printf '%s\n' 'Build / debug targets:'
	@printf '%s\n' '  make native             Build the native macOS binary'
	@printf '%s\n' '  make tg5040             Build the tg5040 binary only'
	@printf '%s\n' '  make print-config       Print resolved build configuration'
	@printf '%s\n' ''
	@printf '%s\n' 'Cleanup targets:'
	@printf '%s\n' '  make clean              Remove all build artifacts'
	@printf '%s\n' '  make clean-native       Remove native build artifacts'
	@printf '%s\n' '  make clean-tg5040       Remove tg5040 build artifacts and SDK cache'
	@printf '%s\n' '  make clean-dist         Remove packaged archives'

dirs:
	@mkdir -p $(OBJ_DIR)/src $(OBJ_DIR)/vendor $(BIN_DIR) $(DIST_DIR)

$(TARGET_PATH): dirs $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

native:
	$(MAKE) PLATFORM=native all

tg5040:
	$(MAKE) PLATFORM=tg5040 all

tg5040-sdk:
	./scripts/bootstrap_tg5040_sdk.sh "$(TG5040_SDK_ROOT)"

tg5040-libcurl:
	./scripts/build_tg5040_libcurl.sh "$(TG5040_DEPS_PREFIX)"

tg5040-bootstrap: tg5040-sdk tg5040-libcurl

package: package-$(PLATFORM)

package-tg5040:
	$(MAKE) PLATFORM=tg5040 package-nextui

package-native: $(TARGET_PATH)
	@test -f "$(ASSET_FONT)" || { echo "missing font asset: $(ASSET_FONT)" >&2; exit 1; }
	@rm -rf "$(STAGE_ROOT)/$(APP_NAME)-macos"
	@mkdir -p "$(STAGE_ROOT)/$(APP_NAME)-macos/bin" "$(STAGE_ROOT)/$(APP_NAME)-macos/assets/fonts"
	cp "$(TARGET_PATH)" "$(STAGE_ROOT)/$(APP_NAME)-macos/bin/$(TARGET)"
	cp "$(ASSET_FONT)" "$(STAGE_ROOT)/$(APP_NAME)-macos/assets/fonts/"
	cp packaging/macos/run.sh "$(STAGE_ROOT)/$(APP_NAME)-macos/run.sh"
	chmod +x "$(STAGE_ROOT)/$(APP_NAME)-macos/run.sh"
	tar -C "$(STAGE_ROOT)" -czf "$(DIST_DIR)/$(APP_NAME)-macos.tar.gz" "$(APP_NAME)-macos"

package-nextui: tg5040-bootstrap
	@rm -rf build/tg5040
	$(MAKE) PLATFORM=tg5040 all
	@test -f "$(ASSET_FONT)" || { echo "missing font asset: $(ASSET_FONT)" >&2; exit 1; }
	@test -f "$(ASSET_ICON)" || { echo "missing icon asset: $(ASSET_ICON)" >&2; exit 1; }
	@rm -rf "$(STAGE_ROOT)/nextui"
	@mkdir -p "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/bin/tg5040"
	@mkdir -p "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/lib/tg5040"
	@mkdir -p "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/res/fonts"
	@mkdir -p "$(STAGE_ROOT)/nextui/Tools/tg5040/.media"
	cp packaging/nextui/launch.sh "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/launch.sh"
	cp "$(ASSET_ICON)" "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/icon.png"
	cp "$(ASSET_ICON)" "$(STAGE_ROOT)/nextui/Tools/tg5040/.media/WeRead.png"
	cp "$(ASSET_FONT)" "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/res/fonts/SourceHanSerifSC-Regular.otf"
	cp "$(ASSET_CACERT)" "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/res/cacert.pem"
	cp "$(TARGET_PATH)" "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/bin/tg5040/$(TARGET)"
	chmod +x "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/launch.sh" "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/bin/tg5040/$(TARGET)"
	@for lib in $(TG5040_RUNTIME_LIBS); do \
		cp -aL "$(TG5040_SDK_USR)/lib/$$lib" "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/lib/tg5040/"; \
	done
	cp -aL "$(shell $(CC) -print-sysroot 2>/dev/null)/lib/libgcc_s.so.1" "$(STAGE_ROOT)/nextui/Tools/tg5040/WeRead.pak/lib/tg5040/libgcc_s.so.1"
	tar -C "$(STAGE_ROOT)/nextui" -czf "$(DIST_DIR)/$(APP_NAME)-nextui.tar.gz" Tools

package-stock: tg5040-bootstrap
	@rm -rf build/tg5040
	$(MAKE) PLATFORM=tg5040 all
	@test -f "$(ASSET_FONT)" || { echo "missing font asset: $(ASSET_FONT)" >&2; exit 1; }
	@test -f "$(ASSET_ICON)" || { echo "missing icon asset: $(ASSET_ICON)" >&2; exit 1; }
	@test -f "$(ASSET_ICONTOP)" || { echo "missing icontop asset: $(ASSET_ICONTOP)" >&2; exit 1; }
	@rm -rf "$(STAGE_ROOT)/stock"
	@mkdir -p "$(STAGE_ROOT)/stock/Apps/WeRead/bin/tg5040"
	@mkdir -p "$(STAGE_ROOT)/stock/Apps/WeRead/lib/tg5040"
	@mkdir -p "$(STAGE_ROOT)/stock/Apps/WeRead/res/fonts"
	cp packaging/stock/launch.sh "$(STAGE_ROOT)/stock/Apps/WeRead/launch.sh"
	cp packaging/stock/config.json "$(STAGE_ROOT)/stock/Apps/WeRead/config.json"
	cp "$(ASSET_ICON)" "$(STAGE_ROOT)/stock/Apps/WeRead/icon.png"
	cp "$(ASSET_ICONTOP)" "$(STAGE_ROOT)/stock/Apps/WeRead/icontop.png"
	cp "$(ASSET_FONT)" "$(STAGE_ROOT)/stock/Apps/WeRead/res/fonts/SourceHanSerifSC-Regular.otf"
	cp "$(ASSET_CACERT)" "$(STAGE_ROOT)/stock/Apps/WeRead/res/cacert.pem"
	cp "$(TARGET_PATH)" "$(STAGE_ROOT)/stock/Apps/WeRead/bin/tg5040/$(TARGET)"
	chmod +x "$(STAGE_ROOT)/stock/Apps/WeRead/launch.sh" "$(STAGE_ROOT)/stock/Apps/WeRead/bin/tg5040/$(TARGET)"
	@for lib in $(TG5040_RUNTIME_LIBS); do \
		cp -aL "$(TG5040_SDK_USR)/lib/$$lib" "$(STAGE_ROOT)/stock/Apps/WeRead/lib/tg5040/"; \
	done
	cp -aL "$(shell $(CC) -print-sysroot 2>/dev/null)/lib/libgcc_s.so.1" "$(STAGE_ROOT)/stock/Apps/WeRead/lib/tg5040/libgcc_s.so.1"
	tar -C "$(STAGE_ROOT)/stock" -czf "$(DIST_DIR)/$(APP_NAME)-stock-app.tar.gz" Apps

package-crossmix: tg5040-bootstrap
	@rm -rf build/tg5040
	$(MAKE) PLATFORM=tg5040 all
	@test -f "$(ASSET_FONT)" || { echo "missing font asset: $(ASSET_FONT)" >&2; exit 1; }
	@test -f "$(ASSET_ICON)" || { echo "missing icon asset: $(ASSET_ICON)" >&2; exit 1; }
	@rm -rf "$(STAGE_ROOT)/crossmix"
	@mkdir -p "$(STAGE_ROOT)/crossmix/Apps/WeRead/bin/tg5040"
	@mkdir -p "$(STAGE_ROOT)/crossmix/Apps/WeRead/lib/tg5040"
	@mkdir -p "$(STAGE_ROOT)/crossmix/Apps/WeRead/res/fonts"
	cp packaging/crossmix/launch.sh "$(STAGE_ROOT)/crossmix/Apps/WeRead/launch.sh"
	cp packaging/crossmix/config.json "$(STAGE_ROOT)/crossmix/Apps/WeRead/config.json"
	cp "$(ASSET_ICON)" "$(STAGE_ROOT)/crossmix/Apps/WeRead/icon.png"
	cp "$(ASSET_FONT)" "$(STAGE_ROOT)/crossmix/Apps/WeRead/res/fonts/SourceHanSerifSC-Regular.otf"
	cp "$(ASSET_CACERT)" "$(STAGE_ROOT)/crossmix/Apps/WeRead/res/cacert.pem"
	cp "$(TARGET_PATH)" "$(STAGE_ROOT)/crossmix/Apps/WeRead/bin/tg5040/$(TARGET)"
	chmod +x "$(STAGE_ROOT)/crossmix/Apps/WeRead/launch.sh" "$(STAGE_ROOT)/crossmix/Apps/WeRead/bin/tg5040/$(TARGET)"
	@for lib in $(TG5040_RUNTIME_LIBS); do \
		cp -aL "$(TG5040_SDK_USR)/lib/$$lib" "$(STAGE_ROOT)/crossmix/Apps/WeRead/lib/tg5040/"; \
	done
	cp -aL "$(shell $(CC) -print-sysroot 2>/dev/null)/lib/libgcc_s.so.1" "$(STAGE_ROOT)/crossmix/Apps/WeRead/lib/tg5040/libgcc_s.so.1"
	tar -C "$(STAGE_ROOT)/crossmix" -czf "$(DIST_DIR)/$(APP_NAME)-crossmix.tar.gz" Apps

macos-release:
	$(MAKE) PLATFORM=native package-native

nextui-release:
	$(MAKE) PLATFORM=tg5040 package-nextui

stock-release:
	$(MAKE) PLATFORM=tg5040 package-stock

crossmix-release:
	$(MAKE) PLATFORM=tg5040 package-crossmix

package-all: macos-release nextui-release stock-release crossmix-release

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

clean:
	rm -rf build

clean-native:
	rm -rf build/native

clean-tg5040:
	rm -rf build/tg5040 build/tg5040-sdk build/tg5040-deps

clean-dist:
	rm -rf dist
