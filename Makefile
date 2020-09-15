.PHONY: build format fs upload upload-beta Shelly1 Shelly1PM Shelly25 Shelly2 Shelly-Plug-S

MOS ?= mos
LOCAL ?= 0
CLEAN ?= 0
VERBOSE ?= 0
MOS_BUILD_FLAGS ?=
BUILD_DIR ?= ./build_$*

MAKEFLAGS += --warn-undefined-variables

MOS_BUILD_FLAGS_FINAL = $(MOS_BUILD_FLAGS)
ifeq "$(LOCAL)" "1"
  MOS_BUILD_FLAGS_FINAL += --local
endif
ifeq "$(CLEAN)" "1"
  MOS_BUILD_FLAGS_FINAL += --clean
endif
ifeq "$(VERBOSE)" "1"
  MOS_BUILD_FLAGS_FINAL += --verbose
endif

build: Shelly1 Shelly1PM Shelly2 Shelly25 ShellyPlugS

upload: upload-Shelly1 upload-Shelly1PM upload-Shelly2 upload-Shelly25 upload-ShellyPlugS

upload-beta: upload-beta-Shelly1 upload-beta-Shelly1PM upload-beta-Shelly2 upload-beta-Shelly25 upload-beta-ShellyPlugS

PLATFORM ?= esp8266

Shelly1: build-Shelly1
	@true

Shelly1PM: build-Shelly1PM
	@true

ShellyPlugS: build-ShellyPlugS
	@true

Shelly2: build-Shelly2
	@true

Shelly25: build-Shelly25
	@true

ShellyU: PLATFORM=ubuntu
ShellyU: MOS_BUILD_FLAGS=--build-var=ASAN=1 --build-var=UBSAN=1
ShellyU: build-ShellyU
	@true

fs/index.html.gz: fs_src/index.html
	gzip -9 -c fs_src/index.html > fs/index.html.gz

fs/style.css.gz: fs_src/style.css
	gzip -9 -c fs_src/style.css > fs/style.css.gz

build-%: fs/index.html.gz fs/style.css.gz
	$(MOS) build --platform=$(PLATFORM) --build-var=MODEL=$* \
	  --build-dir=$(BUILD_DIR) --binary-libs-dir=./binlibs $(MOS_BUILD_FLAGS_FINAL)
	cp $(BUILD_DIR)/fw.zip shelly-homekit-$*.zip

upload-%:
	scp shelly-homekit-$*.zip rojer.me:www/files/shelly/shelly-homekit-$*.zip

upload-beta-%:
	scp shelly-homekit-$*.zip rojer.me:www/files/shelly/beta/shelly-homekit-$*.zip

format:
	clang-format -i src/shelly_*
