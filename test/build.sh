#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# VCAI Test Build Script
# This script configures and builds test binaries incrementally by default.

set -e

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Get the script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_BUILD_DIR="$SCRIPT_DIR/test_build"

show_usage() {
    echo "VCAI Test Build Script"
    echo ""
    echo "Usage: $0 [clean] [debug|release]"
    echo ""
    echo "Options:"
    echo "  clean        Remove test build directory before configuring"
    echo "  debug        Configure Debug build type (default)"
    echo "  release      Configure Release build type"
    echo "  help         Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0           # Incremental configure/build with Debug type"
    echo "  $0 clean     # Clean rebuild with Debug type"
    echo "  $0 release   # Incremental configure/build with Release type"
}

check_cmake() {
    if ! command -v cmake &> /dev/null; then
        print_error "cmake is not installed or not in PATH"
        exit 1
    fi
}

check_common_issues() {
    print_info "Checking for common issues..."

    if [ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]; then
        print_error "CMakeLists.txt not found. Please run this script from the project root or test directory."
        exit 1
    fi

    check_cmake

    if ! pkg-config --exists gio-2.0; then
        print_warning "gio-2.0 not found. Some tests may fail."
    fi

    if ! pkg-config --exists gstreamer-1.0; then
        print_warning "gstreamer-1.0 not found. Some tests may fail."
    fi
}

clean_build() {
    print_info "Cleaning build directory..."
    if [ -d "$TEST_BUILD_DIR" ]; then
        rm -rf "$TEST_BUILD_DIR"
        print_info "Build directory cleaned"
    else
        print_info "Build directory does not exist, nothing to clean"
    fi
}

# Set up test build environment for GStreamer.
setup_test_build_environment() {
    if [ -z "${ORIGINAL_PKG_CONFIG_PATH+x}" ]; then
        export ORIGINAL_PKG_CONFIG_PATH="${PKG_CONFIG_PATH:-}"
        export ORIGINAL_LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
        export ORIGINAL_CMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-}"
        export ORIGINAL_CMAKE_LIBRARY_PATH="${CMAKE_LIBRARY_PATH:-}"
        export ORIGINAL_GST_PLUGIN_PATH="${GST_PLUGIN_PATH:-}"
        export ORIGINAL_GST_PLUGIN_SCANNER="${GST_PLUGIN_SCANNER:-}"
    fi
}

# Restore environment variables changed by setup_test_build_environment.
restore_original_gstreamer() {
    if [ -n "${ORIGINAL_PKG_CONFIG_PATH+x}" ]; then
        export PKG_CONFIG_PATH="${ORIGINAL_PKG_CONFIG_PATH}"
        export LD_LIBRARY_PATH="${ORIGINAL_LD_LIBRARY_PATH}"
        export CMAKE_PREFIX_PATH="${ORIGINAL_CMAKE_PREFIX_PATH}"
        export CMAKE_LIBRARY_PATH="${ORIGINAL_CMAKE_LIBRARY_PATH}"
        export GST_PLUGIN_PATH="${ORIGINAL_GST_PLUGIN_PATH}"
        export GST_PLUGIN_SCANNER="${ORIGINAL_GST_PLUGIN_SCANNER}"
        unset ORIGINAL_PKG_CONFIG_PATH
        unset ORIGINAL_LD_LIBRARY_PATH
        unset ORIGINAL_CMAKE_PREFIX_PATH
        unset ORIGINAL_CMAKE_LIBRARY_PATH
        unset ORIGINAL_GST_PLUGIN_PATH
        unset ORIGINAL_GST_PLUGIN_SCANNER
    else
        unset PKG_CONFIG_PATH
        unset LD_LIBRARY_PATH
        unset CMAKE_PREFIX_PATH
        unset CMAKE_LIBRARY_PATH
        unset GST_PLUGIN_PATH
        unset GST_PLUGIN_SCANNER
    fi
}

configure_cmake() {
    local build_type="$1"

    print_info "Configuring CMake for ${build_type} build..."

    cmake -S "$PROJECT_ROOT" \
          -B "$TEST_BUILD_DIR" \
          -DBUILD_ENCODER_CLI=ON \
          -DBUILD_RTP_SERVER=ON \
          -DBUILD_VCAI_DECODER=ON \
          -DBUILD_TESTS=ON \
          -DCMAKE_PREFIX_PATH= \
          -DCMAKE_LIBRARY_PATH= \
          -DCMAKE_BUILD_TYPE="${build_type}"

    print_success "CMake configuration successful"
}

build_tests() {
    print_info "Building tests..."

    local jobs
    jobs="$(nproc 2>/dev/null || echo 4)"
    local build_failed=false

    print_info "Building test_encoder..."
    if cmake --build "$TEST_BUILD_DIR" --target test_encoder -- -j"${jobs}"; then
        print_success "test_encoder built successfully"
    else
        print_error "test_encoder build failed"
        build_failed=true
    fi

    print_info "Building test_rtp_server..."
    setup_test_build_environment
    if cmake --build "$TEST_BUILD_DIR" --target test_rtp_server -- -j"${jobs}"; then
        print_success "test_rtp_server built successfully"
    else
        print_error "test_rtp_server build failed"
        build_failed=true
    fi
    restore_original_gstreamer

    if [ "$build_failed" = "true" ]; then
        print_error "Test build failed"
        exit 1
    fi

    print_success "All tests built successfully"
}

main() {
    local clean_requested=false
    local build_type="Debug"

    for arg in "$@"; do
        case "$arg" in
            clean)
                clean_requested=true
                ;;
            debug)
                build_type="Debug"
                ;;
            release)
                build_type="Release"
                ;;
            help|-h|--help)
                show_usage
                exit 0
                ;;
            *)
                print_error "Unknown argument: $arg"
                echo ""
                show_usage
                exit 1
                ;;
        esac
    done

    check_common_issues

    if [ "$clean_requested" = "true" ]; then
        clean_build
    fi

    configure_cmake "$build_type"
    build_tests
}

main "$@"
