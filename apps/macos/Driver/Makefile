SHELL := /bin/bash

CONFIGURATION ?= Debug
ARCH := arm64
DEPLOYMENT_TARGET := 14.0
PRODUCT_NAME := CueletVirtualAudio
DRIVER_NAME := $(PRODUCT_NAME).driver

SDK_PATH := $(shell xcrun --sdk macosx --show-sdk-path)
CLANG := $(shell xcrun --sdk macosx --find clang)

ROOT := $(abspath .)
INCLUDE_DIR := $(ROOT)/include
SOURCE_DIR := $(ROOT)/src
TEST_DIR := $(ROOT)/tests
DIAGNOSTIC_SOURCE := $(SOURCE_DIR)/CueletVirtualAudioDiagnostics.c
BUILD_ROOT := $(ROOT)/build
CONFIG_BUILD_DIR := $(BUILD_ROOT)/$(CONFIGURATION)
BUNDLE_DIR := $(CONFIG_BUILD_DIR)/$(DRIVER_NAME)
CONTENTS_DIR := $(BUNDLE_DIR)/Contents
EXECUTABLE_DIR := $(CONTENTS_DIR)/MacOS
EXECUTABLE := $(EXECUTABLE_DIR)/$(PRODUCT_NAME)
INFO_PLIST := $(CONTENTS_DIR)/Info.plist
TEST_BUILD_DIR := $(BUILD_ROOT)/Tests
TEST_EXECUTABLE := $(TEST_BUILD_DIR)/CueletVirtualAudioCoreTests
CONTRACT_TEST_EXECUTABLE := $(TEST_BUILD_DIR)/CueletVirtualAudioDriverContractTests
DISABLED_DIAGNOSTIC_CONTRACT_TEST_EXECUTABLE := $(TEST_BUILD_DIR)/CueletVirtualAudioDriverNoDiagnosticsTests
LUNA_REPLAY_TEST_EXECUTABLE := $(TEST_BUILD_DIR)/CueletVirtualAudioLunaReplayTests
DIAGNOSTIC_TEST_EXECUTABLE := $(TEST_BUILD_DIR)/CueletVirtualAudioDiagnosticsTests
LUNA_REPLAY_FIXTURE := $(TEST_DIR)/fixtures/luna-012-events.csv
BUNDLE_SMOKE_TEST_EXECUTABLE := $(CONFIG_BUILD_DIR)/CueletVirtualAudioBundleSmoke-$(DIAGNOSTICS)
SMOKE_DRIVER_EXECUTABLE ?= $(EXECUTABLE)
TOOLS_BUILD_DIR := $(BUILD_ROOT)/Tools
DIAGNOSTIC_INSPECTOR := $(TOOLS_BUILD_DIR)/cuelet-driver-diagnostics
DIAGNOSTIC_PROPERTY_PROBE := $(TOOLS_BUILD_DIR)/cuelet-driver-property-probe
HAL_INJECTOR := $(TOOLS_BUILD_DIR)/cuelet-hal-injector
HAL_RECEIVER := $(TOOLS_BUILD_DIR)/cuelet-hal-receiver
AUHAL_RECEIVER := $(TOOLS_BUILD_DIR)/cuelet-auhal-receiver

COMMON_CFLAGS := \
	-std=c11 \
	-target $(ARCH)-apple-macos$(DEPLOYMENT_TARGET) \
	-isysroot $(SDK_PATH) \
	-I$(INCLUDE_DIR) \
	-Wall -Wextra -Wpedantic -Werror \
	-fvisibility=hidden

ifeq ($(CONFIGURATION),Release)
CONFIGURATION_CFLAGS := -Os -DNDEBUG=1
DIAGNOSTICS ?= 0
else ifeq ($(CONFIGURATION),Debug)
CONFIGURATION_CFLAGS := -O0 -g -DDEBUG=1
DIAGNOSTICS ?= 1
else
$(error CONFIGURATION must be Debug or Release)
endif

ifeq ($(DIAGNOSTICS),1)
DIAGNOSTIC_CFLAGS := -DCUELET_AUDIO_DIAGNOSTICS=1
else
DIAGNOSTIC_CFLAGS :=
endif

.PHONY: all bundle bundle-smoke diagnostic-bundle tools clean clean-all test analyzer-test stress-test asan-test ubsan-test tsan-test analyze

all: bundle

bundle: $(EXECUTABLE) $(INFO_PLIST)
	codesign --force --sign - $(BUNDLE_DIR) >/dev/null
	@echo "Built $(BUNDLE_DIR)"

$(EXECUTABLE): \
		$(SOURCE_DIR)/CueletVirtualAudioCore.c \
		$(SOURCE_DIR)/CueletVirtualAudioDriver.c \
		$(DIAGNOSTIC_SOURCE) \
		$(INCLUDE_DIR)/CueletVirtualAudioCore.h \
		$(INCLUDE_DIR)/CueletVirtualAudioDiagnostics.h
	mkdir -p $(EXECUTABLE_DIR)
	$(CLANG) $(COMMON_CFLAGS) $(CONFIGURATION_CFLAGS) $(DIAGNOSTIC_CFLAGS) -fblocks -bundle \
		$(SOURCE_DIR)/CueletVirtualAudioCore.c \
		$(SOURCE_DIR)/CueletVirtualAudioDriver.c \
		$(DIAGNOSTIC_SOURCE) \
		-framework CoreAudio -framework CoreFoundation \
		-o $(EXECUTABLE)

$(INFO_PLIST): Resources/Info.plist
	mkdir -p $(CONTENTS_DIR)
	cp Resources/Info.plist $(INFO_PLIST)
	plutil -lint $(INFO_PLIST)

$(TEST_EXECUTABLE): \
		$(SOURCE_DIR)/CueletVirtualAudioCore.c \
		$(TEST_DIR)/CueletVirtualAudioCoreTests.c \
		$(INCLUDE_DIR)/CueletVirtualAudioCore.h
	mkdir -p $(TEST_BUILD_DIR)
	$(CLANG) $(COMMON_CFLAGS) -O1 -g -DCUELET_AUDIO_TESTING=1 -pthread \
		$(SOURCE_DIR)/CueletVirtualAudioCore.c \
		$(TEST_DIR)/CueletVirtualAudioCoreTests.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(TEST_EXECUTABLE)

$(CONTRACT_TEST_EXECUTABLE): \
		$(SOURCE_DIR)/CueletVirtualAudioCore.c \
		$(SOURCE_DIR)/CueletVirtualAudioDriver.c \
		$(DIAGNOSTIC_SOURCE) \
		$(TEST_DIR)/CueletVirtualAudioDriverContractTests.c \
		$(INCLUDE_DIR)/CueletVirtualAudioCore.h \
		$(INCLUDE_DIR)/CueletVirtualAudioDiagnosticClient.h
	mkdir -p $(TEST_BUILD_DIR)
	$(CLANG) $(COMMON_CFLAGS) -O1 -g $(DIAGNOSTIC_CFLAGS) -fblocks -pthread \
		$(SOURCE_DIR)/CueletVirtualAudioCore.c \
		$(SOURCE_DIR)/CueletVirtualAudioDriver.c \
		$(DIAGNOSTIC_SOURCE) \
		$(TEST_DIR)/CueletVirtualAudioDriverContractTests.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(CONTRACT_TEST_EXECUTABLE)

$(BUNDLE_SMOKE_TEST_EXECUTABLE): \
		$(TEST_DIR)/CueletVirtualAudioBundleSmoke.c \
		$(INCLUDE_DIR)/CueletVirtualAudioCore.h \
		$(INCLUDE_DIR)/CueletVirtualAudioDiagnosticClient.h \
		$(INCLUDE_DIR)/CueletVirtualAudioDiagnostics.h
	mkdir -p $(TEST_BUILD_DIR)
	$(CLANG) $(COMMON_CFLAGS) -O1 -g \
		-DCUELET_BUNDLE_SMOKE_EXPECT_DIAGNOSTICS=$(DIAGNOSTICS) \
		$(TEST_DIR)/CueletVirtualAudioBundleSmoke.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(BUNDLE_SMOKE_TEST_EXECUTABLE)

test: $(TEST_EXECUTABLE) $(CONTRACT_TEST_EXECUTABLE) $(DISABLED_DIAGNOSTIC_CONTRACT_TEST_EXECUTABLE) $(LUNA_REPLAY_TEST_EXECUTABLE) $(DIAGNOSTIC_TEST_EXECUTABLE) analyzer-test
	$(TEST_EXECUTABLE)
	$(CONTRACT_TEST_EXECUTABLE)
	$(DISABLED_DIAGNOSTIC_CONTRACT_TEST_EXECUTABLE)
	$(LUNA_REPLAY_TEST_EXECUTABLE) $(LUNA_REPLAY_FIXTURE)
	$(DIAGNOSTIC_TEST_EXECUTABLE)

analyzer-test: $(DIAGNOSTIC_INSPECTOR)
	/usr/bin/python3 $(TEST_DIR)/test_analyze_cuelet_capture.py
	/usr/bin/python3 $(TEST_DIR)/test_analyze_cuelet_receiver_pipeline.py
	$(DIAGNOSTIC_INSPECTOR) selftest

$(DISABLED_DIAGNOSTIC_CONTRACT_TEST_EXECUTABLE): \
		$(SOURCE_DIR)/CueletVirtualAudioCore.c \
		$(SOURCE_DIR)/CueletVirtualAudioDriver.c \
		$(TEST_DIR)/CueletVirtualAudioDriverContractTests.c \
		$(INCLUDE_DIR)/CueletVirtualAudioCore.h \
		$(INCLUDE_DIR)/CueletVirtualAudioDiagnosticClient.h \
		$(INCLUDE_DIR)/CueletVirtualAudioDiagnostics.h
	mkdir -p $(TEST_BUILD_DIR)
	$(CLANG) $(COMMON_CFLAGS) -O1 -g -fblocks -pthread \
		$(SOURCE_DIR)/CueletVirtualAudioCore.c \
		$(SOURCE_DIR)/CueletVirtualAudioDriver.c \
		$(TEST_DIR)/CueletVirtualAudioDriverContractTests.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(DISABLED_DIAGNOSTIC_CONTRACT_TEST_EXECUTABLE)

$(DIAGNOSTIC_TEST_EXECUTABLE): \
		$(SOURCE_DIR)/CueletVirtualAudioDiagnostics.c \
		$(TEST_DIR)/CueletVirtualAudioDiagnosticsTests.c \
		$(INCLUDE_DIR)/CueletVirtualAudioCore.h \
		$(INCLUDE_DIR)/CueletVirtualAudioDiagnostics.h
	mkdir -p $(TEST_BUILD_DIR)
	$(CLANG) $(COMMON_CFLAGS) -O1 -g -DCUELET_AUDIO_DIAGNOSTICS=1 -pthread \
		$(SOURCE_DIR)/CueletVirtualAudioDiagnostics.c \
		$(TEST_DIR)/CueletVirtualAudioDiagnosticsTests.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(DIAGNOSTIC_TEST_EXECUTABLE)

$(LUNA_REPLAY_TEST_EXECUTABLE): \
		$(SOURCE_DIR)/CueletVirtualAudioCore.c \
		$(SOURCE_DIR)/CueletVirtualAudioDriver.c \
		$(DIAGNOSTIC_SOURCE) \
		$(TEST_DIR)/CueletVirtualAudioLunaReplayTests.c \
		$(LUNA_REPLAY_FIXTURE) \
		$(INCLUDE_DIR)/CueletVirtualAudioCore.h \
		$(INCLUDE_DIR)/CueletVirtualAudioDiagnostics.h
	mkdir -p $(TEST_BUILD_DIR)
	$(CLANG) $(COMMON_CFLAGS) -O1 -g -DCUELET_AUDIO_DIAGNOSTICS=1 -fblocks -pthread \
		$(SOURCE_DIR)/CueletVirtualAudioCore.c \
		$(SOURCE_DIR)/CueletVirtualAudioDriver.c \
		$(DIAGNOSTIC_SOURCE) \
		$(TEST_DIR)/CueletVirtualAudioLunaReplayTests.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(LUNA_REPLAY_TEST_EXECUTABLE)

bundle-smoke: $(EXECUTABLE) $(INFO_PLIST) $(BUNDLE_SMOKE_TEST_EXECUTABLE)
	$(BUNDLE_SMOKE_TEST_EXECUTABLE) $(SMOKE_DRIVER_EXECUTABLE)

diagnostic-bundle:
	$(MAKE) CONFIGURATION=Debug DIAGNOSTICS=1 bundle tools

tools: $(DIAGNOSTIC_INSPECTOR) $(DIAGNOSTIC_PROPERTY_PROBE) $(HAL_INJECTOR) $(HAL_RECEIVER) $(AUHAL_RECEIVER)

$(DIAGNOSTIC_INSPECTOR): \
		$(ROOT)/tools/cuelet-driver-diagnostics.c \
		$(ROOT)/tools/cuelet-driver-property-probe.c \
		$(INCLUDE_DIR)/CueletVirtualAudioCore.h \
		$(INCLUDE_DIR)/CueletVirtualAudioDiagnosticClient.h \
		$(INCLUDE_DIR)/CueletVirtualAudioDiagnostics.h
	mkdir -p $(TOOLS_BUILD_DIR)
	$(CLANG) $(COMMON_CFLAGS) -O2 -DCUELET_PROPERTY_PROBE_NO_MAIN=1 \
		$(ROOT)/tools/cuelet-driver-diagnostics.c \
		$(ROOT)/tools/cuelet-driver-property-probe.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(DIAGNOSTIC_INSPECTOR)

$(DIAGNOSTIC_PROPERTY_PROBE): \
		$(ROOT)/tools/cuelet-driver-property-probe.c \
		$(INCLUDE_DIR)/CueletVirtualAudioCore.h \
		$(INCLUDE_DIR)/CueletVirtualAudioDiagnosticClient.h \
		$(INCLUDE_DIR)/CueletVirtualAudioDiagnostics.h
	mkdir -p $(TOOLS_BUILD_DIR)
	$(CLANG) $(COMMON_CFLAGS) -O2 \
		$(ROOT)/tools/cuelet-driver-property-probe.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(DIAGNOSTIC_PROPERTY_PROBE)

$(HAL_INJECTOR): $(ROOT)/tools/cuelet-hal-injector.c
	mkdir -p $(TOOLS_BUILD_DIR)
	$(CLANG) $(COMMON_CFLAGS) -O2 \
		$(ROOT)/tools/cuelet-hal-injector.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(HAL_INJECTOR)

$(HAL_RECEIVER): $(ROOT)/tools/cuelet-hal-receiver.c
	mkdir -p $(TOOLS_BUILD_DIR)
	$(CLANG) $(COMMON_CFLAGS) -O2 \
		$(ROOT)/tools/cuelet-hal-receiver.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(HAL_RECEIVER)

$(AUHAL_RECEIVER): $(ROOT)/tools/cuelet-auhal-receiver.c
	mkdir -p $(TOOLS_BUILD_DIR)
	$(CLANG) $(COMMON_CFLAGS) -O2 \
		$(ROOT)/tools/cuelet-auhal-receiver.c \
		-framework AudioToolbox -framework CoreAudio -framework CoreFoundation \
		-o $(AUHAL_RECEIVER)

stress-test: $(TEST_EXECUTABLE)
	CUELET_DRIVER_STRESS_ITERATIONS=$${CUELET_DRIVER_STRESS_ITERATIONS:-100000} \
		$(TEST_EXECUTABLE)

asan-test:
	mkdir -p $(BUILD_ROOT)/Sanitizers/Address
	$(CLANG) $(COMMON_CFLAGS) -O1 -g -DCUELET_AUDIO_TESTING=1 -pthread \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(SOURCE_DIR)/CueletVirtualAudioCore.c \
		$(TEST_DIR)/CueletVirtualAudioCoreTests.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(BUILD_ROOT)/Sanitizers/Address/CueletVirtualAudioCoreTests
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1 \
		$(BUILD_ROOT)/Sanitizers/Address/CueletVirtualAudioCoreTests
	$(CLANG) $(COMMON_CFLAGS) -O1 -g -DCUELET_AUDIO_DIAGNOSTICS=1 -fblocks -pthread \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(SOURCE_DIR)/CueletVirtualAudioCore.c \
		$(SOURCE_DIR)/CueletVirtualAudioDriver.c \
		$(DIAGNOSTIC_SOURCE) \
		$(TEST_DIR)/CueletVirtualAudioDriverContractTests.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(BUILD_ROOT)/Sanitizers/Address/CueletVirtualAudioDriverContractTests
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1 \
		$(BUILD_ROOT)/Sanitizers/Address/CueletVirtualAudioDriverContractTests
	$(CLANG) $(COMMON_CFLAGS) -O1 -g -DCUELET_AUDIO_DIAGNOSTICS=1 -pthread \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(DIAGNOSTIC_SOURCE) \
		$(TEST_DIR)/CueletVirtualAudioDiagnosticsTests.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(BUILD_ROOT)/Sanitizers/Address/CueletVirtualAudioDiagnosticsTests
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1 \
		$(BUILD_ROOT)/Sanitizers/Address/CueletVirtualAudioDiagnosticsTests

ubsan-test:
	mkdir -p $(BUILD_ROOT)/Sanitizers/Undefined
	$(CLANG) $(COMMON_CFLAGS) -O1 -g -DCUELET_AUDIO_TESTING=1 -pthread \
		-fsanitize=undefined -fno-omit-frame-pointer \
		$(SOURCE_DIR)/CueletVirtualAudioCore.c \
		$(TEST_DIR)/CueletVirtualAudioCoreTests.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(BUILD_ROOT)/Sanitizers/Undefined/CueletVirtualAudioCoreTests
	UBSAN_OPTIONS=halt_on_error=1 \
		$(BUILD_ROOT)/Sanitizers/Undefined/CueletVirtualAudioCoreTests
	$(CLANG) $(COMMON_CFLAGS) -O1 -g -DCUELET_AUDIO_DIAGNOSTICS=1 -fblocks -pthread \
		-fsanitize=undefined -fno-omit-frame-pointer \
		$(SOURCE_DIR)/CueletVirtualAudioCore.c \
		$(SOURCE_DIR)/CueletVirtualAudioDriver.c \
		$(DIAGNOSTIC_SOURCE) \
		$(TEST_DIR)/CueletVirtualAudioDriverContractTests.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(BUILD_ROOT)/Sanitizers/Undefined/CueletVirtualAudioDriverContractTests
	UBSAN_OPTIONS=halt_on_error=1 \
		$(BUILD_ROOT)/Sanitizers/Undefined/CueletVirtualAudioDriverContractTests
	$(CLANG) $(COMMON_CFLAGS) -O1 -g -DCUELET_AUDIO_DIAGNOSTICS=1 -pthread \
		-fsanitize=undefined -fno-omit-frame-pointer \
		$(DIAGNOSTIC_SOURCE) \
		$(TEST_DIR)/CueletVirtualAudioDiagnosticsTests.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(BUILD_ROOT)/Sanitizers/Undefined/CueletVirtualAudioDiagnosticsTests
	UBSAN_OPTIONS=halt_on_error=1 \
		$(BUILD_ROOT)/Sanitizers/Undefined/CueletVirtualAudioDiagnosticsTests

tsan-test:
	mkdir -p $(BUILD_ROOT)/Sanitizers/Thread
	$(CLANG) $(COMMON_CFLAGS) -O1 -g -DCUELET_AUDIO_TESTING=1 -pthread \
		-fsanitize=thread -fno-omit-frame-pointer \
		$(SOURCE_DIR)/CueletVirtualAudioCore.c \
		$(TEST_DIR)/CueletVirtualAudioCoreTests.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(BUILD_ROOT)/Sanitizers/Thread/CueletVirtualAudioCoreTests
	TSAN_OPTIONS=halt_on_error=1 \
		$(BUILD_ROOT)/Sanitizers/Thread/CueletVirtualAudioCoreTests
	$(CLANG) $(COMMON_CFLAGS) -O1 -g -DCUELET_AUDIO_DIAGNOSTICS=1 -fblocks -pthread \
		-fsanitize=thread -fno-omit-frame-pointer \
		$(SOURCE_DIR)/CueletVirtualAudioCore.c \
		$(SOURCE_DIR)/CueletVirtualAudioDriver.c \
		$(DIAGNOSTIC_SOURCE) \
		$(TEST_DIR)/CueletVirtualAudioDriverContractTests.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(BUILD_ROOT)/Sanitizers/Thread/CueletVirtualAudioDriverContractTests
	TSAN_OPTIONS=halt_on_error=1 \
		$(BUILD_ROOT)/Sanitizers/Thread/CueletVirtualAudioDriverContractTests
	$(CLANG) $(COMMON_CFLAGS) -O1 -g -DCUELET_AUDIO_DIAGNOSTICS=1 -pthread \
		-fsanitize=thread -fno-omit-frame-pointer \
		$(DIAGNOSTIC_SOURCE) \
		$(TEST_DIR)/CueletVirtualAudioDiagnosticsTests.c \
		-framework CoreAudio -framework CoreFoundation \
		-o $(BUILD_ROOT)/Sanitizers/Thread/CueletVirtualAudioDiagnosticsTests
	TSAN_OPTIONS=halt_on_error=1 \
		$(BUILD_ROOT)/Sanitizers/Thread/CueletVirtualAudioDiagnosticsTests

analyze:
	mkdir -p $(BUILD_ROOT)/Analyzer
	$(CLANG) $(COMMON_CFLAGS) $(DIAGNOSTIC_CFLAGS) -fblocks --analyze \
		-Xanalyzer -analyzer-output=text \
		$(SOURCE_DIR)/CueletVirtualAudioCore.c \
		-o $(BUILD_ROOT)/Analyzer/CueletVirtualAudioCore.plist
	$(CLANG) $(COMMON_CFLAGS) $(DIAGNOSTIC_CFLAGS) -fblocks --analyze \
		-Xanalyzer -analyzer-output=text \
		$(SOURCE_DIR)/CueletVirtualAudioDiagnostics.c \
		-o $(BUILD_ROOT)/Analyzer/CueletVirtualAudioDiagnostics.plist
	$(CLANG) $(COMMON_CFLAGS) $(DIAGNOSTIC_CFLAGS) -fblocks --analyze \
		-Xanalyzer -analyzer-output=text \
		$(SOURCE_DIR)/CueletVirtualAudioDriver.c \
		-o $(BUILD_ROOT)/Analyzer/CueletVirtualAudioDriver.plist

clean:
	rm -rf $(CONFIG_BUILD_DIR)

clean-all:
	rm -rf $(BUILD_ROOT)
