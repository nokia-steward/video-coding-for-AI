#!/bin/bash
# Development convenience functions for VCAI (sourced by scripts/lib/dev_wrapper.sh)
# Setup scripts live under scripts/setup/bin/

# =============================================================================
# SETUP FUNCTIONS
# =============================================================================

# Set up all dependencies (first time only)
setup_dependencies() {
    echo "Setting up VCAI dependencies..."

    local failed=0
    local repo_root
    repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

    if [ -f "$repo_root/scripts/setup/bootstrap.sh" ]; then
        echo "Running host dependency bootstrap (doctor/core/kvazaar/openhevc/ds-env)..."
        if ! bash "$repo_root/scripts/setup/bootstrap.sh" all; then
            echo "Error: bootstrap setup failed"
            failed=1
        fi
    else
        echo "Error: scripts/setup/bootstrap.sh not found"
        failed=1
    fi

    if [ $failed -eq 1 ]; then
        echo "Dependencies setup failed. Please check the error messages above."
        return 1
    fi

    echo "Dependencies setup complete!"
    return 0
}

# Build all components
build_all() {
    echo "Building all VCAI components..."
    local repo_root
    repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
    cd "$repo_root" || return 1

    echo "Building encoder_cli..."
    cd encoder_cli && ./build.sh && cd ..

    echo "Building rtp_server..."
    cd rtp_server && ./build.sh && cd ..

    echo "All components built!"
}

# =============================================================================
# ENCODER_CLI RUN SCRIPTS
# =============================================================================

run_encoder_camera() {
    local repo_root
    repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
    echo "Running encoder_cli with camera input and RTP output..."
    cd "$repo_root/encoder_cli" && ./run.sh camera rtp "$@" && cd "$repo_root"
}

run_encoder_camera_rtp() {
    local repo_root
    repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
    echo "Running encoder_cli with camera input and RTP output..."
    cd "$repo_root/encoder_cli" && ./run.sh camera rtp "$@" && cd "$repo_root"
}

run_encoder_rtp() {
    local repo_root
    repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
    echo "Running encoder_cli with file input and RTP output..."
    cd "$repo_root/encoder_cli" && ./run.sh file rtp "$@" && cd "$repo_root"
}

run_encoder() {
    local repo_root
    repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
    echo "Running encoder_cli with general input using run.sh..."
    cd "$repo_root/encoder_cli" && ./run.sh "$@" && cd "$repo_root"
}

show_webcam() {
    echo "Available webcam devices:"
    ls -la /dev/video* 2>/dev/null || echo "No webcam devices found"
}

# =============================================================================
# RTP_SERVER RUN SCRIPTS
# =============================================================================

run_rtp_server() {
    local repo_root
    repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
    echo "Running RTP server using run.sh..."
    cd "$repo_root/rtp_server"
    if [ "$#" -eq 0 ]; then
        ./run.sh rtp
    else
        ./run.sh "$@"
    fi
    cd "$repo_root"
}

# =============================================================================
# HELP FUNCTIONS
# =============================================================================

show_commands() {
    echo "Available development commands:"
    echo ""
    echo "Setup commands:"
    echo "  setup_dependencies  - Set up all dependencies (first time only)"
    echo "  build_all           - Build all components"
    echo "  ./scripts/setup/bootstrap.sh - Host setup subcommands"
    echo ""
    echo "Encoder CLI commands:"
    echo "  run_encoder_camera [args]     - Run encoder with camera input"
    echo "  run_encoder_camera_rtp [args] - Run encoder with camera + RTP output"
    echo "  run_encoder_rtp [video_file]   - Run encoder with file input + RTP output"
    echo "  run_encoder [args]            - Run encoder with explicit modes"
    echo "  show_webcam                   - Show available webcam devices"
    echo ""
    echo "RTP Server commands:"
    echo "  run_rtp_server [rtp|webrtc] [args] - Run RTP server (default: rtp)"
    echo ""
    echo "Note: Each run script handles its own environment setup"
}

show_encoder_help() {
    echo "Encoder CLI Commands:"
    echo "  run_encoder_camera [device]                        - Camera input + RTP output"
    echo "  run_encoder_camera_rtp [device]                    - Camera input + RTP output"
    echo "  run_encoder_rtp [video_file]                       - File input + RTP output"
    echo "  run_encoder <input_mode> <output_mode> [args]      - General consolidated runner"
    echo "  show_webcam  - List available webcam devices"
}

show_decoder_help() {
    echo "Decoder CLI commands are no longer available in this repository."
    echo "Use rtp_server and encoder_cli run scripts instead."
}

show_rtp_help() {
    echo "RTP Server Commands:"
    echo "  run_rtp_server [rtp|webrtc] [args]  - Start RTP server (default: rtp)"
}
