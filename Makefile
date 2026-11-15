# Copyparty Viewer 2.0.0 — native GNU make / mingw32-make entry point.
# CMake is a build tool only. The delivered application is one real executable.
PROJECT_VERSION := 2.0.0
CMAKE ?= cmake
BUILD_DIR ?= build/native
JOBS ?= 4
PREFIX ?= /usr/local
CMAKE_ARGS ?=
ifeq ($(origin CC),default)
CC := gcc
endif
ifeq ($(origin CXX),default)
CXX := g++
endif
ifeq ($(OS),Windows_NT)
GENERATOR ?= MinGW Makefiles
else
GENERATOR ?= Unix Makefiles
endif

.PHONY: all help configure deps build test package source fetch-deps install uninstall clean distclean
all: build

help:
	@"$(CMAKE)" -P cmake/Help.cmake

configure:
	"$(CMAKE)" -S . -B "$(BUILD_DIR)" -G "$(GENERATOR)" -DCMAKE_C_COMPILER="$(CC)" -DCMAKE_CXX_COMPILER="$(CXX)" -DCPV_JOBS=$(JOBS) $(CMAKE_ARGS)

deps: configure
	"$(CMAKE)" --build "$(BUILD_DIR)" --target deps --parallel 3

build: configure
	"$(CMAKE)" --build "$(BUILD_DIR)" --parallel 3

test: build
	"$(CMAKE)" -DCPV_BUILD_DIR="$(abspath $(BUILD_DIR))" -P cmake/RunTests.cmake

package: build
	"$(CMAKE)" -DCPV_BUILD_DIR="$(abspath $(BUILD_DIR))" -P cmake/PackageRelease.cmake

source:
	"$(CMAKE)" -P cmake/SourceArchive.cmake

fetch-deps:
	"$(CMAKE)" -P cmake/FetchDeps.cmake

install:
	"$(CMAKE)" --install "$(BUILD_DIR)/app" --prefix "$(PREFIX)"

uninstall:
	"$(CMAKE)" -DCPV_PREFIX="$(PREFIX)" -P cmake/Uninstall.cmake

clean:
	"$(CMAKE)" -E rm -rf "$(BUILD_DIR)/app" "$(BUILD_DIR)/cpv-app-prefix"

distclean:
	"$(CMAKE)" -E rm -rf "$(BUILD_DIR)"
