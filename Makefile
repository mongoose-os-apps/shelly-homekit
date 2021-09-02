MAKEFLAGS += --warn-undefined-variables --no-builtin-rules

.PHONY: build check-format format release upload Shelly1 Shelly1L Shelly1PM Shelly25 Shelly2 ShellyI3 ShellyPlug ShellyPlugS ShellyRGBW2 Custom2 Custom16
.SUFFIXES:

MOS ?= mos
# Build locally by default if Docker is available.
LOCAL ?= $(shell which docker> /dev/null && echo 1 || echo 0)
CLEAN ?= 0
V ?= 0
VERBOSE ?= 0
RELEASE ?= 0
RELEASE_SUFFIX ?=
MOS_BUILD_FLAGS ?=
ALLOW_DIRTY_FS ?= 0
BUILD_DIR ?= ./build_$*

MOS_BUILD_FLAGS_FINAL = $(MOS_BUILD_FLAGS)
ifeq "$(LOCAL)" "1"
  MOS_BUILD_FLAGS_FINAL += --local
endif
ifeq "$(CLEAN)" "1"
  MOS_BUILD_FLAGS_FINAL += --clean
endif
ifneq "$(VERBOSE)$(V)" "00"
  MOS_BUILD_FLAGS_FINAL += --verbose
endif

build: Shelly1 Shelly1L Shelly1PM Shelly2 Shelly25 ShellyI3 ShellyPlug ShellyPlugS ShellyRGBW2 ShellyU ShellyU25 Custom2 Custom16

release:
	$(MAKE) build CLEAN=1 RELEASE=1

PLATFORM ?= esp8266

Shelly1: build-Shelly1
	@true

Shelly1L: build-Shelly1L
	@true

Shelly1PM: build-Shelly1PM
	@true

Shelly2: build-Shelly2
	@true

Shelly25: build-Shelly25
	@true

ShellyI3: build-ShellyI3
	@true

ShellyPlug: build-ShellyPlug
	@true

ShellyPlugS: build-ShellyPlugS
	@true

ShellyRGBW2: build-ShellyRGBW2
	@true

ShellyU: PLATFORM=ubuntu
ShellyU: build-ShellyU
	@true

ShellyU25: PLATFORM=ubuntu
ShellyU25: build-ShellyU25
	@true

Custom2: build-Custom2
	@true

Custom16: PLATFORM=esp32
Custom16: build-Custom16
	@true

fs/index.html.gz: $(wildcard fs_src/*) Makefile
	mkdir -p $(BUILD_DIR)
	cat fs_src/index.html | \
	sed "s/.*<link.*rel=\"stylesheet\".*//g" | sed -e '/<style>/ r fs_src/style.css' | \
	sed "s/.*<script src=\"sha256.js\".*/<script data-src=\"sha256.js\">/g" | sed -e '/<script data-src=\"sha256.js\">/ r fs_src/sha256.js' | \
	sed "s/.*<script src=\"qrcode.js\".*/<script data-src=\"qrcode.js\">/g" | sed -e '/<script data-src=\"qrcode.js\">/ r fs_src/qrcode.js' | \
	sed "s/.*<script src=\"script.js\".*/<script data-src=\"script.js\">/g" | sed -e '/<script data-src=\"script.js\">/ r fs_src/script.js' | \
	sed -e '/.*<img.*src=".\/logo.svg".*/ {' -e 'r fs_src/logo.svg' -e 'd' -e '}' > $(BUILD_DIR)/index.html
	gzip -9 -c $(BUILD_DIR)/index.html > $@
#	zopfli -c -i30 $(BUILD_DIR)/index.html > $@
#	brotli --best -c $(BUILD_DIR)/index.html > $@

build-%: fs/index.html.gz Makefile
ifneq "$(ALLOW_DIRTY_FS)" "1"
	@[ -z "$(wildcard fs/conf*.json fs/kvs.json)" ] || { echo; echo "XXX No configs in fs allowed, or set ALLOW_DIRTY_FS=1"; echo; exit 1; }
endif
	$(MOS) build --platform=$(PLATFORM) --build-var=MODEL=$* \
	  --build-dir=$(BUILD_DIR) --binary-libs-dir=./binlibs $(MOS_BUILD_FLAGS_FINAL)
ifeq "$(RELEASE)" "1"
	[ $(PLATFORM) = ubuntu ] || \
	  (dir=releases/`jq -r .build_version $(BUILD_DIR)/gen/build_info.json`$(RELEASE_SUFFIX) && \
	    mkdir -p $$dir/misc && \
	    cp -v $(BUILD_DIR)/fw.zip $$dir/shelly-homekit-$*.zip && \
	    cp -v $(BUILD_DIR)/objs/*.elf $$dir/misc/shelly-homekit_$*.elf && \
	    cp -v $(BUILD_DIR)/gen/mgos_deps_manifest.yml $$dir/misc/shelly-homekit-$*_deps.yml)
endif

format:
	for d in src lib*; do find $$d -name \*.cpp -o -name \*.hpp | xargs clang-format -i; done

check-format: format
	@git diff --exit-code || { printf "\n== Please format your code (run make format) ==\n\n"; exit 1; }

upload:
	rsync -azv releases/* rojer.me:www/files/shelly/
