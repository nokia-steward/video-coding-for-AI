#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# H.265 Video Player Script
# Supports both raw H.265 streams and H.265 in containers (MP4, MKV, etc.)

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
PLAYER="ffplay"
AUTO_DETECT=true
VERBOSE=false
FULLSCREEN=false
LOOP=false

# Function to print colored output
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

# Function to show usage
show_usage() {
    cat << EOF
H.265 Video Player Script

USAGE:
    $0 [OPTIONS] <video_file>

OPTIONS:
    -p, --player PLAYER     Video player to use (ffplay, vlc, mpv) [default: ffplay]
    -f, --fullscreen        Play in fullscreen mode
    -l, --loop              Loop the video
    -v, --verbose           Verbose output
    -h, --help              Show this help message

EXAMPLES:
    # Play a raw H.265 file
    $0 rtp_stream_port_5002_20241225_143052_123.h265

    # Play in fullscreen mode
    $0 -f video.h265

    # Play with VLC player
    $0 -p vlc video.h265

    # Loop the video
    $0 -l video.h265

    # Play H.265 in MP4 container
    $0 video.mp4

SUPPORTED FORMATS:
    - Raw H.265 streams (.h265, .hevc, .265)
    - H.265 in MP4 containers (.mp4)
    - H.265 in MKV containers (.mkv)
    - H.265 in AVI containers (.avi)

PLAYERS:
    - ffplay: Fast, lightweight (default)
    - vlc: Feature-rich, cross-platform
    - mpv: Modern, highly configurable

EOF
}

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to detect video format
detect_format() {
    local file="$1"
    local extension="${file##*.}"
    local basename="${file%.*}"
    
    case "${extension,,}" in
        h265|hevc|265)
            echo "raw_h265"
            ;;
        mp4|mkv|avi|mov|m4v)
            echo "container"
            ;;
        *)
            echo "unknown"
            ;;
    esac
}

# Function to check if file contains H.265
check_h265_content() {
    local file="$1"
    
    if command_exists ffprobe; then
        local codec=$(ffprobe -v quiet -select_streams v:0 -show_entries stream=codec_name -of csv=s=x:p=0 "$file" 2>/dev/null || echo "")
        if [[ "$codec" == "hevc" ]]; then
            return 0
        fi
    fi
    return 1
}

# Function to play with ffplay
play_ffplay() {
    local file="$1"
    local format="$2"
    local args=()
    
    # Add format-specific arguments
    if [[ "$format" == "raw_h265" ]]; then
        args+=(-f hevc)
    fi
    
    # Add common arguments
    if [[ "$FULLSCREEN" == true ]]; then
        args+=(-fs)
    fi
    
    if [[ "$LOOP" == true ]]; then
        args+=(-loop -1)
    fi
    
    if [[ "$VERBOSE" == true ]]; then
        args+=(-v info)
    fi
    
    args+=("$file")
    
    print_info "Playing with ffplay: ${args[*]}"
    ffplay "${args[@]}"
}

# Function to play with VLC
play_vlc() {
    local file="$1"
    local format="$2"
    local args=()
    
    # Add format-specific arguments
    if [[ "$format" == "raw_h265" ]]; then
        args+=(--demux=hevc)
    fi
    
    # Add common arguments
    if [[ "$FULLSCREEN" == true ]]; then
        args+=(--fullscreen)
    fi
    
    if [[ "$LOOP" == true ]]; then
        args+=(--input-repeat=-1)
    fi
    
    if [[ "$VERBOSE" == true ]]; then
        args+=(--verbose=2)
    fi
    
    args+=("$file")
    
    print_info "Playing with VLC: ${args[*]}"
    vlc "${args[@]}"
}

# Function to play with mpv
play_mpv() {
    local file="$1"
    local format="$2"
    local args=()
    
    # Add format-specific arguments
    if [[ "$format" == "raw_h265" ]]; then
        args+=(--demuxer=hevc)
    fi
    
    # Add common arguments
    if [[ "$FULLSCREEN" == true ]]; then
        args+=(--fullscreen)
    fi
    
    if [[ "$LOOP" == true ]]; then
        args+=(--loop=inf)
    fi
    
    if [[ "$VERBOSE" == true ]]; then
        args+=(--msg-level=all=v)
    fi
    
    args+=("$file")
    
    print_info "Playing with mpv: ${args[*]}"
    mpv "${args[@]}"
}

# Function to get file info
get_file_info() {
    local file="$1"
    
    if command_exists ffprobe; then
        print_info "File information:"
        ffprobe -v quiet -show_format -show_streams "$file" 2>/dev/null | grep -E "(codec_name|width|height|duration|bit_rate)" || true
    fi
}

# Main function
main() {
    local video_file=""
    
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -p|--player)
                PLAYER="$2"
                shift 2
                ;;
            -f|--fullscreen)
                FULLSCREEN=true
                shift
                ;;
            -l|--loop)
                LOOP=true
                shift
                ;;
            -v|--verbose)
                VERBOSE=true
                shift
                ;;
            -h|--help)
                show_usage
                exit 0
                ;;
            -*)
                print_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
            *)
                if [[ -z "$video_file" ]]; then
                    video_file="$1"
                else
                    print_error "Multiple files specified. Only one file at a time."
                    exit 1
                fi
                shift
                ;;
        esac
    done
    
    # Check if file is specified
    if [[ -z "$video_file" ]]; then
        print_error "No video file specified."
        show_usage
        exit 1
    fi
    
    # Check if file exists
    if [[ ! -f "$video_file" ]]; then
        print_error "File does not exist: $video_file"
        exit 1
    fi
    
    # Check if player exists
    if ! command_exists "$PLAYER"; then
        print_error "Player '$PLAYER' not found. Please install it or use a different player."
        print_info "Available players: ffplay, vlc, mpv"
        exit 1
    fi
    
    # Detect format
    local format=$(detect_format "$video_file")
    print_info "Detected format: $format"
    
    # Verify H.265 content for containers
    if [[ "$format" == "container" ]]; then
        if ! check_h265_content "$video_file"; then
            print_warning "File may not contain H.265 video. Attempting to play anyway..."
        fi
    fi
    
    # Show file info if verbose
    if [[ "$VERBOSE" == true ]]; then
        get_file_info "$video_file"
    fi
    
    # Play the video
    print_success "Starting playback of: $video_file"
    
    case "$PLAYER" in
        ffplay)
            play_ffplay "$video_file" "$format"
            ;;
        vlc)
            play_vlc "$video_file" "$format"
            ;;
        mpv)
            play_mpv "$video_file" "$format"
            ;;
        *)
            print_error "Unsupported player: $PLAYER"
            print_info "Supported players: ffplay, vlc, mpv"
            exit 1
            ;;
    esac
    
    print_success "Playback completed."
}

# Run main function with all arguments
main "$@"
