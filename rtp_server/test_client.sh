#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# RTP Test Client Script for VCAI RTP Server
# This script demonstrates how to connect to the RTP server and stream H.265 test video

set -e

# Default values
SERVER_HOST="127.0.0.1"
HANDSHAKE_PORT="5000"
PATTERN="ball"
WIDTH="640"
HEIGHT="480"
DURATION="5"
VIDEO_FILE=""
DEBUG_MODE=false
VERBOSE_MODE=false

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Help function
show_help() {
    echo "RTP Test Client Script for VCAI RTP Server"
    echo ""
    echo "Usage: $0 [OPTIONS] [VIDEO_FILE]"
    echo ""
    echo "Options:"
    echo "  -s, --server HOST     RTP server IP address (default: 127.0.0.1)"
    echo "  -p, --port PORT       Handshake port (default: 5000)"
    echo "  -t, --pattern PATTERN Test pattern: ball, smpte, colors, circular, snow, black, white (default: ball)"
    echo "  -w, --width WIDTH     Video width in pixels (default: 640)"
    echo "  -h, --height HEIGHT   Video height in pixels (default: 480)"
    echo "  -d, --duration SEC    Stream duration in seconds, 0 for infinite (default: 5)"
    echo "  -f, --file FILE       Path to H.265 video file to stream (overrides pattern)"
    echo "  -v, --verbose          Enable verbose output with detailed pipeline information"
    echo "  --debug               Enable debug mode with GStreamer debug output"
    echo "  --help                Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                                    # Basic usage with default test pattern"
    echo "  $0 --pattern smpte                   # Use SMPTE test pattern"
    echo "  $0 --duration 60                     # Stream for 60 seconds"
    echo "  $0 --server 192.168.1.100            # Connect to remote server"
    echo "  $0 --width 1280 --height 720         # High resolution stream"
    echo "  $0 video.h265                        # Stream H.265 video file"
    echo "  $0 --file /path/to/video.h265        # Stream H.265 video file"
    echo "  $0 --verbose video.h265              # Stream with verbose output"
    echo "  $0 --debug --pattern smpte           # Debug test pattern streaming"
    echo ""
    echo "Multiple clients (run in separate terminals):"
    echo "  $0 --pattern ball &"
    echo "  $0 --pattern smpte &"
    echo "  $0 video1.h265 &"
    echo "  $0 video2.h265 &"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -s|--server)
            SERVER_HOST="$2"
            shift 2
            ;;
        -p|--port)
            HANDSHAKE_PORT="$2"
            shift 2
            ;;
        -t|--pattern)
            PATTERN="$2"
            shift 2
            ;;
        -w|--width)
            WIDTH="$2"
            shift 2
            ;;
        -h|--height)
            HEIGHT="$2"
            shift 2
            ;;
        -d|--duration)
            DURATION="$2"
            shift 2
            ;;
        -f|--file)
            VIDEO_FILE="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE_MODE=true
            shift
            ;;
        --debug)
            DEBUG_MODE=true
            shift
            ;;
        --help)
            show_help
            exit 0
            ;;
        -*)
            echo "Unknown option: $1"
            show_help
            exit 1
            ;;
        *)
            # Positional argument - treat as video file if no --file option was used
            if [[ -z "$VIDEO_FILE" ]]; then
                VIDEO_FILE="$1"
            else
                echo "Error: Multiple video files specified"
                show_help
                exit 1
            fi
            shift
            ;;
    esac
done

# Validate video file if provided
if [[ -n "$VIDEO_FILE" ]]; then
    if [[ ! -f "$VIDEO_FILE" ]]; then
        echo -e "${RED}Error: Video file '$VIDEO_FILE' does not exist${NC}"
        exit 1
    fi
    
    # Check if file has H.265 extension or content
    if [[ ! "$VIDEO_FILE" =~ \.(h265|hevc|hvc1)$ ]]; then
        echo -e "${YELLOW}Warning: File '$VIDEO_FILE' doesn't have H.265 extension (.h265, .hevc, .hvc1)${NC}"
        echo -e "${YELLOW}Proceeding anyway - make sure it's a valid H.265 file${NC}"
    fi
else
    # Validate pattern only if no video file is provided
    case $PATTERN in
        ball|smpte|colors|circular|snow|black|white)
            ;;
        *)
            echo -e "${RED}Error: Invalid pattern '$PATTERN'${NC}"
            echo "Valid patterns: ball, smpte, colors, circular, snow, black, white"
            exit 1
            ;;
    esac
fi

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}RTP Test Client Starting${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "Server: ${GREEN}$SERVER_HOST:$HANDSHAKE_PORT${NC}"

if [[ -n "$VIDEO_FILE" ]]; then
    echo -e "Video File: ${GREEN}$VIDEO_FILE${NC}"
    echo -e "Mode: ${GREEN}File Streaming${NC}"
else
    echo -e "Pattern: ${GREEN}$PATTERN${NC}"
    echo -e "Resolution: ${GREEN}${WIDTH}x${HEIGHT}${NC}"
    echo -e "Mode: ${GREEN}Test Pattern${NC}"
fi

echo -e "Duration: ${GREEN}${DURATION}s${NC}"
echo ""

# Check if required tools are available
check_tools() {
    local tools_missing=()

    if ! command -v socat &> /dev/null; then
        tools_missing+=("socat")
    fi

    if ! command -v gst-launch-1.0 &> /dev/null; then
        tools_missing+=("gst-launch-1.0")
    fi

    if [[ ${#tools_missing[@]} -gt 0 ]]; then
        echo -e "${RED}Error: Missing required tools: ${tools_missing[*]}${NC}"
        echo "Please install: sudo apt-get install socat gstreamer1.0-tools"
        return 1
    fi

    return 0
}

# Check GStreamer plugins availability
check_gstreamer_plugins() {
    local missing_plugins=()
    
    # Check for essential plugins
    if ! gst-inspect-1.0 x265enc &> /dev/null; then
        missing_plugins+=("x265enc")
    fi
    
    if ! gst-inspect-1.0 rtph265pay &> /dev/null; then
        missing_plugins+=("rtph265pay")
    fi
    
    if ! gst-inspect-1.0 udpsink &> /dev/null; then
        missing_plugins+=("udpsink")
    fi
    
    if ! gst-inspect-1.0 h265parse &> /dev/null; then
        missing_plugins+=("h265parse")
    fi
    
    if [[ ${#missing_plugins[@]} -gt 0 ]]; then
        echo -e "${RED}Error: Missing GStreamer plugins: ${missing_plugins[*]}${NC}"
        echo "Please install: sudo apt-get install gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-plugins-good"
        return 1
    fi
    
    return 0
}

# Test network connectivity to server
test_network_connectivity() {
    echo -e "${YELLOW}Testing network connectivity to $SERVER_HOST:$HANDSHAKE_PORT...${NC}"
    
    # Test UDP connectivity by sending a simple handshake and checking for response
    local test_response
    test_response=$(echo "TEST" | timeout 3 socat - UDP4-DATAGRAM:$SERVER_HOST:$HANDSHAKE_PORT,so-broadcast=1 2>/dev/null)
    
    if [[ -n "$test_response" ]]; then
        echo -e "${GREEN}✓ Network connectivity OK${NC}"
        return 0
    else
        echo -e "${RED}✗ Cannot connect to $SERVER_HOST:$HANDSHAKE_PORT${NC}"
        echo -e "${YELLOW}Troubleshooting tips:${NC}"
        echo "  - Make sure the RTP server is running"
        echo "  - Check if the server is listening on UDP port $HANDSHAKE_PORT"
        echo "  - Verify the server IP address: $SERVER_HOST"
        echo "  - Check firewall settings for UDP traffic"
        echo "  - The server might be running but not responding to test messages"
        echo "  - Try running without connectivity test: the handshake might still work"
        return 1
    fi
}


# Stream video to the assigned port
stream_video() {
    local port="$1"

    echo -e "${YELLOW}Step 2: Starting video stream to port $port...${NC}"

    # Build GStreamer pipeline command
    local cmd="gst-launch-1.0"
    
    # Add debug options if enabled
    if [[ "$DEBUG_MODE" == true ]]; then
        cmd="$cmd --gst-debug-level=3"
    fi
    
    if [[ "$VERBOSE_MODE" == true ]]; then
        cmd="$cmd -v"
    fi

    if [[ -n "$VIDEO_FILE" ]]; then
        # Stream from H.265 file
        echo -e "${BLUE}File mode: Streaming from $VIDEO_FILE${NC}"
        cmd="$cmd filesrc location=$VIDEO_FILE"
        cmd="$cmd ! h265parse"
        
        # Add duration if specified (for looping)
        if [[ "$DURATION" -gt 0 ]]; then
            cmd="$cmd ! identity single-segment=true"
        fi
        
        cmd="$cmd ! rtph265pay"
    else
        # Generate test pattern
        echo -e "${BLUE}Test pattern mode: Generating $PATTERN pattern${NC}"
        # Add duration if specified
        if [[ "$DURATION" -gt 0 ]]; then
            local num_buffers=$((DURATION * 30))  # Assuming 30 FPS
            cmd="$cmd videotestsrc pattern=$PATTERN num-buffers=$num_buffers"
        else
            cmd="$cmd videotestsrc pattern=$PATTERN"
        fi

        cmd="$cmd ! video/x-raw,width=$WIDTH,height=$HEIGHT"
        cmd="$cmd ! x265enc tune=zerolatency"
        cmd="$cmd ! rtph265pay"
    fi

    cmd="$cmd ! udpsink host=$SERVER_HOST port=$port"

    echo -e "${BLUE}Running command: $cmd${NC}"
    echo ""

    # Execute the command and capture output
    local gst_output
    local gst_exit_code
    
    if [[ "$VERBOSE_MODE" == true || "$DEBUG_MODE" == true ]]; then
        # Show all output for debugging
        if eval "$cmd"; then
            echo -e "${GREEN}Stream completed successfully${NC}"
        else
            echo -e "${RED}Error: Stream failed${NC}"
            return 1
        fi
    else
        # Capture output for error analysis
        if gst_output=$(eval "$cmd" 2>&1); then
            echo -e "${GREEN}Stream completed successfully${NC}"
        else
            gst_exit_code=$?
            echo -e "${RED}Error: Stream failed (exit code: $gst_exit_code)${NC}"
            echo ""
            echo -e "${YELLOW}GStreamer output:${NC}"
            echo "$gst_output"
            echo ""
            echo -e "${YELLOW}Troubleshooting tips:${NC}"
            
            # Analyze common error patterns
            if echo "$gst_output" | grep -q "Could not open"; then
                echo "  - File access issue: Check if the video file exists and is readable"
            fi
            
            if echo "$gst_output" | grep -q "No such element"; then
                echo "  - Missing GStreamer plugin: Run with --debug to see detailed plugin errors"
            fi
            
            if echo "$gst_output" | grep -q "Could not link"; then
                echo "  - Pipeline linking issue: Check video format compatibility"
            fi
            
            if echo "$gst_output" | grep -q "Connection refused"; then
                echo "  - Network issue: Check if server is running and port is available"
            fi
            
            if echo "$gst_output" | grep -q "Permission denied"; then
                echo "  - Permission issue: Check file permissions or network access"
            fi
            
            echo ""
            echo -e "${YELLOW}For more detailed debugging, run with --verbose or --debug${NC}"
            return 1
        fi
    fi
}

# Main execution
main() {
    # Check tools
    if ! check_tools; then
        exit 1
    fi

    # Check GStreamer plugins
    if ! check_gstreamer_plugins; then
        exit 1
    fi

    # Test network connectivity (non-blocking - just informational)
    test_network_connectivity

    # Perform handshake
    echo -e "${YELLOW}Step 1: Performing handshake with server...${NC}"
    local assigned_port
    local handshake_output
    handshake_output=$(echo "HELLO" | socat - UDP4-DATAGRAM:$SERVER_HOST:$HANDSHAKE_PORT,so-broadcast=1 2>&1)
    assigned_port=$(echo "$handshake_output" | head -n1)

    if [[ -z "$assigned_port" ]]; then
        echo -e "${RED}Error: No response from server${NC}"
        echo ""
        echo -e "${YELLOW}Handshake output:${NC}"
        echo "$handshake_output"
        echo ""
        echo -e "${YELLOW}Troubleshooting tips:${NC}"
        echo "  - Make sure the RTP server is running on $SERVER_HOST:$HANDSHAKE_PORT"
        echo "  - Check if the server is listening for UDP connections"
        echo "  - Verify the server IP address and port"
        echo "  - Check firewall settings for UDP traffic"
        exit 1
    fi

    echo -e "${GREEN}Server assigned port: $assigned_port${NC}"

    # Stream video
    stream_video "$assigned_port"
    if [[ $? -ne 0 ]]; then
        exit 1
    fi

    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${GREEN}RTP Test Client Completed Successfully${NC}"
    echo -e "${BLUE}========================================${NC}"
}

# Run main function
main "$@"
