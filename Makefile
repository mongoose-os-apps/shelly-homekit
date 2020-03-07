.PHONY: all Shelly1 Shelly1PM Shelly25 Shelly2 Shelly-Plug-S

MOS ?= mos
LOCAL ?= 0
UPLOAD ?= 0
MOS_BUILD_FLAGS ?=
BUILD_DIR ?= ./build

ifeq "$(LOCAL)" "1"
	MOS_BUILD_FLAGS += --local
	BUILD_DIR = ./build_$*
endif

all: Shelly1 Shelly1PM Shelly2 Shelly25 ShellyPlugS

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

build-%:
	$(MOS) build --platform=esp8266 --build-var=MODEL=$* $(MOS_BUILD_FLAGS) --build-dir=$(BUILD_DIR) --binary-libs-dir=./binlibs
ifeq "$(UPLOAD)" "1"
	scp ./build_$*/fw.zip rojer.me:www/files/shelly/shelly-homekit-$*.zip
endif
