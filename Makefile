.PHONY: all Shelly1 Shelly25

MOS ?= mos
UPLOAD ?= 0

all: Shelly1 Shelly25

Shelly1: build-Shelly1
	@true

Shelly25: build-Shelly25
	@true

build-%:
	$(MOS) build --local --platform=esp8266 --build-var=MODEL=$* --build-dir=./build_$* --binary-libs-dir=./binlibs
ifeq "$(UPLOAD)" "1"
	scp ./build_$*/fw.zip rojer.me:www/files/shelly/shelly-homekit-$*.zip
endif
