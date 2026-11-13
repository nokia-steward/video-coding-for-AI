#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# This script generates PeopleNet configuration files with dynamic paths and video properties
# It ensures PeopleNet-compatible dimensions (multiples of 16, W ≥ 480, H ≥ 272)

# Function to get the VCAI directory path
get_vcai_path() {
    # Method 1: Use script location to find project root (most reliable)
    # Script is in encoder_cli/scripts/, so go up two levels
    local script_dir="$(cd "$(dirname "$0")" && pwd)"
    local project_root="$(cd "$script_dir/../.." && pwd)"
    
    # Verify this is the VCAI directory by checking for key directories
    if [ -d "$project_root/encoder_cli" ] && [ -d "$project_root/encoder_cli/scripts" ]; then
        echo "$project_root"
        return 0
    fi
    
    # Method 2: Start from the current directory and work up to find the VCAI root
    local current_dir="$(pwd)"
    local search_dir="$current_dir"
    
    while [ "$search_dir" != "/" ]; do
        if [ -d "$search_dir/encoder_cli" ] && [ -d "$search_dir/encoder_cli/scripts" ]; then
            echo "$search_dir"
            return 0
        fi
        search_dir="$(dirname "$search_dir")"
    done
    
    echo "Error: Could not find VCAI directory" >&2
    echo "Please ensure you are running this script from within the VCAI project" >&2
    echo "or that the VCAI directory contains encoder_cli/ subdirectory" >&2
    echo "Current directory: $(pwd)" >&2
    echo "Script location: $script_dir" >&2
    return 1
}

# Function to validate that required files exist
validate_peoplenet_files() {
    local vcai_path="$1"
    local errors=0
    
    echo "Validating PeopleNet files..."
    
    # Check for PeopleNet ONNX model
    if [ ! -f "$vcai_path/peoplenet/resnet34_peoplenet_int8.onnx" ]; then
        echo "  ✗ Missing: $vcai_path/peoplenet/resnet34_peoplenet_int8.onnx"
        errors=$((errors + 1))
    else
        echo "  ✓ Found: resnet34_peoplenet_int8.onnx"
    fi
    
    # Check for labels file
    if [ ! -f "$vcai_path/peoplenet/labels.txt" ]; then
        echo "  ✗ Missing: $vcai_path/peoplenet/labels.txt"
        errors=$((errors + 1))
    else
        echo "  ✓ Found: labels.txt"
    fi
    
    # Check for calibration file
    if [ ! -f "$vcai_path/peoplenet/resnet34_peoplenet_int8.txt" ]; then
        echo "  ✗ Missing: $vcai_path/peoplenet/resnet34_peoplenet_int8.txt"
        errors=$((errors + 1))
    else
        echo "  ✓ Found: resnet34_peoplenet_int8.txt"
    fi
    
    return $errors
}

# Main execution
main() {
    echo "Generating PeopleNet configuration with dynamic paths..."
    
    # Get video dimensions from command line arguments (if provided)
    VIDEO_WIDTH="${1:-832}"
    VIDEO_HEIGHT="${2:-480}"
    
    # PeopleNet models require input dimensions to be multiples of 16
    # and meet minimum size requirements: W ≥ 480, H ≥ 272
    # Round dimensions to nearest multiple of 16 for compatibility
    PEOPLENET_WIDTH=$(( (VIDEO_WIDTH + 15) / 16 * 16 ))
    PEOPLENET_HEIGHT=$(( (VIDEO_HEIGHT + 15) / 16 * 16 ))
    
    # Ensure minimum size requirements for PeopleNet
    if [ $PEOPLENET_WIDTH -lt 480 ]; then
        PEOPLENET_WIDTH=480
    fi
    if [ $PEOPLENET_HEIGHT -lt 272 ]; then
        PEOPLENET_HEIGHT=272
    fi
    
    echo "Using video dimensions: ${VIDEO_WIDTH}x${VIDEO_HEIGHT}"
    echo "Using PeopleNet-compatible dimensions: ${PEOPLENET_WIDTH}x${PEOPLENET_HEIGHT}"
    
    # Get the VCAI directory path
    VCAI_PATH=$(get_vcai_path)
    if [ $? -ne 0 ]; then
        echo "$VCAI_PATH"
        exit 1
    fi
    
    echo "VCAI directory: $VCAI_PATH"
    
    # Validate required files exist
    validate_peoplenet_files "$VCAI_PATH"
    local validation_result=$?
    
    if [ $validation_result -ne 0 ]; then
        echo ""
        echo "Warning: Some required files are missing, but generating configuration anyway."
        echo "Please ensure before running encoder_cli:"
        echo "1. PeopleNet model files are present in peoplenet directory"
        echo "2. Labels file exists (create labels.txt with PeopleNet class names)"
        echo ""
    fi
    
    # Generate the configuration files in the peoplenet directory
    local peoplenet_dir="$VCAI_PATH/peoplenet"
    mkdir -p "$peoplenet_dir"
    local yaml_config_file="$peoplenet_dir/peoplenet_config.yml"
    echo "Generating $yaml_config_file..."
    
    cat > "$yaml_config_file" << EOF
################################################################################
# PeopleNet Configuration for encoder_cli
# This file provides the YAML structure expected by DeepStream while using
# PeopleNet-compatible dimensions
################################################################################

streammux:
  batch-size: 1
  batched-push-timeout: 40000
  width: $VIDEO_WIDTH
  height: $VIDEO_HEIGHT

# Inference using nvinfer with PeopleNet configuration:
primary-gie:
  plugin-type: 0
  config-file-path: peoplenet_pgie_config.yml
EOF

    # Generate the PeopleNet-specific configuration file
    local pgie_config_file="$peoplenet_dir/peoplenet_pgie_config.yml"
    echo "Generating $pgie_config_file..."
    
    cat > "$pgie_config_file" << EOF
################################################################################
# PeopleNet Primary GIE Configuration
# This file provides the PeopleNet-specific inference parameters
################################################################################

property:
  gpu-id: 0
  net-scale-factor: 0.00392156862745098
  onnx-file: $VCAI_PATH/peoplenet/resnet34_peoplenet_int8.onnx
  model-engine-file: $VCAI_PATH/peoplenet/resnet34_peoplenet_int8.onnx_b1_gpu0_int8.engine
  labelfile-path: $VCAI_PATH/peoplenet/labels.txt
  int8-calib-file: $VCAI_PATH/peoplenet/resnet34_peoplenet_int8.txt
  batch-size: 1
  network-mode: 1
  num-detected-classes: 4
  interval: 0
  gie-unique-id: 1
  cluster-mode: 2
  # PeopleNet-specific settings for better accuracy
  maintain-aspect-ratio: 1
  symmetric-padding: 1

class-attrs-all:
  pre-cluster-threshold: 0.2
  topk: 20
  nms-iou-threshold: 0.4
EOF

    echo "✓ Configuration files generated:"
    echo "  - $yaml_config_file"
    echo "  - $pgie_config_file"
    echo ""
    echo "PeopleNet-compatible dimensions: ${PEOPLENET_WIDTH}x${PEOPLENET_HEIGHT}"
    echo "Original video dimensions: ${VIDEO_WIDTH}x${VIDEO_HEIGHT}"
    echo ""
    echo "Note: The encoder will automatically scale the input video to PeopleNet-compatible dimensions."
    echo "This ensures proper object detection while maintaining the original video's aspect ratio."
}

# Run main function
main "$@"
