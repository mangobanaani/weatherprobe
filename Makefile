# Makefile -- Convenience wrapper around idf.py for the WeatherProbe firmware.
#
# Usage:
#   make build        Build the firmware
#   make flash        Flash to the connected ESP32
#   make monitor      Open the serial monitor
#   make fm           Flash and immediately monitor (most common workflow)
#   make clean        Remove the build directory
#   make fullclean    Remove build dir and managed components
#   make menuconfig   Open the sdkconfig TUI
#   make size         Show firmware size breakdown
#   make erase        Erase the entire flash
#   make provision    Run the credential provisioning script

# Serial port -- override on the command line if needed:
#   make flash PORT=/dev/ttyUSB1
PORT ?= /dev/ttyUSB0

.PHONY: build flash monitor fm clean fullclean menuconfig size erase provision

build:
	idf.py build

flash:
	idf.py -p $(PORT) flash

monitor:
	idf.py -p $(PORT) monitor

fm:
	idf.py -p $(PORT) flash monitor

clean:
	idf.py fullclean

fullclean:
	rm -rf build managed_components

menuconfig:
	idf.py menuconfig

size:
	idf.py size

erase:
	idf.py -p $(PORT) erase-flash

provision:
	./provision.sh
