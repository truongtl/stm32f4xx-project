CMAKE ?= "C:/Program Files/CMake/bin/cmake.exe"
PROJECTS := $(notdir $(wildcard examples/*))
BUILD_DIR := build
TOOLCHAIN ?= arm-none-eabi.cmake

.DEFAULT_GOAL := all
.PHONY: all $(PROJECTS) clean list

all: $(PROJECTS)

list:
	@echo "Available projects: $(PROJECTS)"

$(PROJECTS):
	@if [ "$(firstword $(MAKECMDGOALS))" = "clean" ]; then \
		: ; \
	else \
		echo ">>> Building project: $@"; \
		$(CMAKE) -E rm -rf $(BUILD_DIR)/$@; \
		$(CMAKE) -S . -B $(BUILD_DIR)/$@ -G Ninja \
		  -DPROJECT=$@ \
		  -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN) \
		  $(if $(USE_BOOT_APP_LD),-DUSE_BOOT_APP_LD=$(USE_BOOT_APP_LD),) \
		|| $(CMAKE) -S . -B $(BUILD_DIR)/$@ -G "Unix Makefiles" \
		  -DPROJECT=$@ \
		  -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN) \
		  $(if $(USE_BOOT_APP_LD),-DUSE_BOOT_APP_LD=$(USE_BOOT_APP_LD),); \
		$(CMAKE) --build $(BUILD_DIR)/$@ --config Release; \
		$(CMAKE) -E echo ">>> Artifacts:"; \
		$(CMAKE) -E echo "    ELF: $(BUILD_DIR)/$@/out/$@.elf"; \
		$(CMAKE) -E echo "    HEX: $(BUILD_DIR)/$@/out/$@.hex"; \
		$(CMAKE) -E echo "    BIN: $(BUILD_DIR)/$@/out/$@.bin"; \
	fi


CLEAN_TARGETS := $(filter $(PROJECTS),$(MAKECMDGOALS))

clean:
	@if [ -z "$(CLEAN_TARGETS)" ]; then \
		$(CMAKE) -E rm -rf $(BUILD_DIR); \
		echo ">>> All builds cleaned"; \
	else \
		for e in $(CLEAN_TARGETS); do \
			$(CMAKE) -E rm -rf "$(BUILD_DIR)/$$e"; \
			echo ">>> $$e cleaned"; \
		done; \
	fi
