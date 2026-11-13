#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# This script sets up a dedicated Python environment for DeepStream YOLO model conversion
# It uses a stable PyTorch version (1.13) that is compatible with DeepStream 7.1's TensorRT

set -e

echo "=== Setting up DeepStream YOLO Export Environment ==="

# Script lives at scripts/setup/bin; repo root is three levels up
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
ENV_DIR="$PROJECT_ROOT/.env/ds_export"

echo "Project root: $PROJECT_ROOT"
echo "Environment directory: $ENV_DIR"

# Check if environment already exists
if [ -d "$ENV_DIR" ]; then
    rm -rf "$ENV_DIR"
fi

echo ""
echo "Creating Python virtual environment..."

# Check if python3-venv is available
if ! python3 -m venv --help > /dev/null 2>&1; then
    echo "Error: python3-venv is not available."
    echo "Please install it first:"
    echo "  sudo apt install python3.10-venv"
    echo "Then run this script again."
    exit 1
fi

# Create virtual environment
python3 -m venv "$ENV_DIR"

# Activate the environment
source "$ENV_DIR/bin/activate"

echo "Virtual environment created and activated."
echo "Python version: $(python --version)"
echo "Pip version: $(pip --version)"

echo ""
echo "Installing stable PyTorch 1.13.1 (CPU version - compatible with DeepStream 7.1)..."

# Install PyTorch 1.13.1 without CUDA suffix (compatible with DeepStream 7.1)
# Note: CUDA-suffixed versions (1.13.1+cu117, 1.13.1+cu116) are no longer available
# Check if running in non-interactive mode (e.g., from setup_dependencies function)
NON_INTERACTIVE=${NON_INTERACTIVE:-0}
if [ -t 0 ] && [ "$NON_INTERACTIVE" != "1" ]; then
    # Interactive mode - original behavior
    if ! pip install torch==1.13.1 torchvision==0.14.1; then
        echo ""
        echo "❌ ERROR: Failed to install PyTorch 1.13.1"
        echo ""
        echo "The PyTorch installation failed. This could be due to network issues or package conflicts."
        echo ""
        echo "🔧 RESOLUTION OPTIONS:"
        echo ""
        echo "Option 1: Retry PyTorch 1.13.1 installation (recommended)"
        echo "  pip install torch==1.13.1 torchvision==0.14.1"
        echo ""
        echo "Option 2: Use latest PyTorch with CUDA (may have compatibility issues)"
        echo "  pip install torch torchvision --index-url https://download.pytorch.org/whl/cu117"
        echo ""
        echo "Option 3: Use latest PyTorch CPU-only"
        echo "  pip install torch torchvision"
        echo ""
        echo "⚠️  Note: DeepStream 7.1 works best with PyTorch 1.13.x series."
        echo "    If you choose Option 2 or 3, you may encounter TensorRT compatibility issues."
        echo ""
        read -p "Enter your choice (1-4, default 1): " choice
        choice=${choice:-1}

        case $choice in
            1)
                echo "Retrying PyTorch 1.13.1 installation..."
                pip install torch==1.13.1 torchvision==0.14.1 || exit 1
                ;;
            2)
                echo "Installing latest PyTorch with CUDA..."
                pip install torch torchvision --index-url https://download.pytorch.org/whl/cu117 || exit 1
                ;;
            3)
                echo "Installing latest PyTorch CPU-only..."
                pip install torch torchvision || exit 1
                ;;
            4)
                echo "Exiting. Please resolve the PyTorch installation manually."
                exit 1
                ;;
            *)
                echo "Invalid choice. Exiting."
                exit 1
                ;;
        esac
    fi
else
    # Non-interactive mode - fail immediately on error
    if ! pip install torch==1.13.1 torchvision==0.14.1; then
        echo ""
        echo "❌ ERROR: Failed to install PyTorch 1.13.1"
        echo ""
        echo "Running in non-interactive mode. Installation failed."
        echo "To retry manually, run:"
        echo "  source $ENV_DIR/bin/activate"
        echo "  pip install torch==1.13.1 torchvision==0.14.1"
        echo ""
        exit 1
    fi
fi

echo ""
echo "Installing compatible NumPy version first..."

# Install compatible NumPy version (1.x for PyTorch 1.13.1 compatibility)
if ! pip install "numpy<2.0"; then
    echo ""
    echo "❌ ERROR: Failed to install NumPy"
    if [ "$NON_INTERACTIVE" = "1" ] || [ ! -t 0 ]; then
        echo "Running in non-interactive mode. Trying fallback installation..."
        pip install numpy==1.24.3 || {
            echo "NumPy installation failed. Continuing anyway - you may encounter compatibility issues."
        }
    else
        echo "🔧 RESOLUTION:"
        echo "Try installing NumPy manually:"
        echo "  pip install numpy==1.24.3"
        echo ""
        echo "Or continue without NumPy constraints:"
        echo "  pip install numpy"
        echo ""
        read -p "Continue with manual NumPy installation? (y/N): " continue_choice
        if [[ "$continue_choice" =~ ^[Yy]$ ]]; then
            pip install numpy==1.24.3 || echo "NumPy installation failed. Continuing anyway."
        else
            echo "Skipping NumPy installation. You may encounter compatibility issues."
        fi
    fi
fi

echo ""
echo "Installing ultralytics and ONNX tools..."

# Create a constraints file to force NumPy 1.x compatibility
echo "numpy<2.0" > /tmp/constraints.txt

# Install ultralytics and ONNX tools with compatible versions
ultralytics_installed=0
echo "Installing ultralytics==8.3.0 (YOLOv11 compatible)..."
set +e  # Temporarily disable exit on error for this section
pip install ultralytics==8.3.0 --constraint /tmp/constraints.txt > /tmp/ultralytics_install.log 2>&1
pip_exit_code=$?
set -e  # Re-enable exit on error

if [ $pip_exit_code -eq 0 ]; then
    ultralytics_installed=1
    echo "✓ Ultralytics installed successfully with constraints"
else
    echo ""
    echo "⚠️  WARNING: Failed to install ultralytics with constraints"
    echo "Error details:"
    tail -20 /tmp/ultralytics_install.log 2>/dev/null || echo "   (Could not retrieve error details)"
    if [ "$NON_INTERACTIVE" = "1" ] || [ ! -t 0 ]; then
        echo "Running in non-interactive mode. Trying fallback installation without constraints..."
        set +e
        pip install ultralytics==8.3.0 > /tmp/ultralytics_install.log 2>&1
        pip_exit_code=$?
        set -e
        if [ $pip_exit_code -eq 0 ]; then
            ultralytics_installed=1
            echo "✓ Ultralytics installed successfully without constraints"
        else
            echo "⚠️  Ultralytics installation failed. Error details:"
            tail -20 /tmp/ultralytics_install.log 2>/dev/null || echo "   (Could not retrieve error details)"
            echo ""
            echo "   Continuing anyway - you may need to install it manually:"
            echo "   source $ENV_DIR/bin/activate"
            echo "   pip install ultralytics==8.0.196"
        fi
    else
        echo "🔧 RESOLUTION:"
        echo "Try installing ultralytics without constraints:"
    echo "  pip install ultralytics==8.3.0"
        echo ""
        echo "Or install latest version:"
        echo "  pip install ultralytics"
        echo ""
        read -p "Continue with ultralytics installation? (y/N): " continue_choice
        if [[ "$continue_choice" =~ ^[Yy]$ ]]; then
            set +e
            pip install ultralytics==8.3.0 2>&1
            pip_exit_code=$?
            set -e
            if [ $pip_exit_code -eq 0 ]; then
                ultralytics_installed=1
            else
                echo "Ultralytics installation failed."
            fi
        else
            echo "Skipping ultralytics installation."
        fi
    fi
fi
rm -f /tmp/ultralytics_install.log

if ! pip install onnx==1.14.0 onnxsim==0.4.33 onnxslim; then
    echo ""
    echo "❌ ERROR: Failed to install ONNX tools"
    if [ "$NON_INTERACTIVE" = "1" ] || [ ! -t 0 ]; then
        echo "Running in non-interactive mode. Trying individual installation..."
        pip install onnx==1.14.0 || echo "ONNX installation failed."
        pip install onnxsim==0.4.33 || echo "ONNXSim installation failed."
        pip install onnxslim || echo "ONNXSlim installation failed."
    else
        echo "🔧 RESOLUTION:"
        echo "Try installing ONNX tools individually:"
        echo "  pip install onnx==1.14.0"
        echo "  pip install onnxsim==0.4.33"
        echo "  pip install onnxslim"
        echo ""
        read -p "Continue with ONNX tools installation? (y/N): " continue_choice
        if [[ "$continue_choice" =~ ^[Yy]$ ]]; then
            pip install onnx==1.14.0
            pip install onnxsim==0.4.33
            pip install onnxslim
        else
            echo "Skipping ONNX tools installation."
        fi
    fi
fi

# Clean up constraints file
rm /tmp/constraints.txt

echo ""
echo "Installing additional dependencies..."

# Install additional dependencies that might be needed
if ! pip install opencv-python pillow pyyaml; then
    echo ""
    echo "❌ ERROR: Failed to install additional dependencies"
    if [ "$NON_INTERACTIVE" = "1" ] || [ ! -t 0 ]; then
        echo "Running in non-interactive mode. Trying individual installation..."
        pip install opencv-python || echo "OpenCV installation failed."
        pip install pillow || echo "Pillow installation failed."
        pip install pyyaml || echo "PyYAML installation failed."
    else
        echo "🔧 RESOLUTION:"
        echo "Try installing dependencies individually:"
        echo "  pip install opencv-python"
        echo "  pip install pillow"
        echo "  pip install pyyaml"
        echo ""
        read -p "Continue with individual dependency installation? (y/N): " continue_choice
        if [[ "$continue_choice" =~ ^[Yy]$ ]]; then
            pip install opencv-python
            pip install pillow
            pip install pyyaml
        else
            echo "Skipping additional dependencies installation."
        fi
    fi
fi

echo ""
echo "Verifying installation..."

# Verify the installation with error handling
echo "Checking installed packages..."

# Check PyTorch
if python -c 'import torch; print(f"PyTorch version: {torch.__version__}")' 2>/dev/null; then
    python -c 'import torch; print(f"CUDA available: {torch.cuda.is_available()}")' 2>/dev/null || echo "CUDA check failed"
else
    echo "❌ PyTorch not installed or not working"
fi

# Check NumPy
if python -c 'import numpy; print(f"NumPy version: {numpy.__version__}")' 2>/dev/null; then
    :
else
    echo "❌ NumPy not installed or not working"
fi

# Check Ultralytics
if python -c 'import ultralytics; print(f"Ultralytics version: {ultralytics.__version__}")' 2>/dev/null; then
    echo "✓ Ultralytics installed and working"
else
    echo "❌ Ultralytics not installed or not working"
    # Try to diagnose the issue
    echo "   Attempting to diagnose..."
    python -c 'import ultralytics' 2>&1 | head -10 || echo "   Import failed - check error above"
fi

# Check ONNX
if python -c 'import onnx; print(f"ONNX version: {onnx.__version__}")' 2>/dev/null; then
    :
else
    echo "❌ ONNX not installed or not working"
fi

# Check ONNXSlim
if python -c 'import onnxslim; print("ONNXSlim available: Yes")' 2>/dev/null; then
    :
else
    echo "ONNXSlim available: No"
fi

echo ""
echo "=== Environment Setup Complete ==="
echo ""
echo "Environment location: $ENV_DIR"
echo ""
echo "To activate this environment:"
echo "  source $ENV_DIR/bin/activate"
echo ""
echo "To deactivate:"
echo "  deactivate"
echo ""
echo "📋 INSTALLATION SUMMARY:"
echo ""

# Check what was successfully installed
installed_packages=()
failed_packages=()

if python -c 'import torch' 2>/dev/null; then
    installed_packages+=("PyTorch")
else
    failed_packages+=("PyTorch")
fi

if python -c 'import numpy' 2>/dev/null; then
    installed_packages+=("NumPy")
else
    failed_packages+=("NumPy")
fi

# Check Ultralytics - verify both import and pip installation
if python -c 'import ultralytics' 2>/dev/null; then
    installed_packages+=("Ultralytics")
elif pip show ultralytics >/dev/null 2>&1; then
    echo "⚠️  Ultralytics package installed but import failed - may have dependency issues"
    installed_packages+=("Ultralytics (package installed, import failed)")
else
    failed_packages+=("Ultralytics")
fi

if python -c 'import onnx' 2>/dev/null; then
    installed_packages+=("ONNX")
else
    failed_packages+=("ONNX")
fi

# Check OpenCV - try both cv2 and opencv-python
if python -c 'import cv2; print(f"OpenCV version: {cv2.__version__}")' 2>/dev/null; then
    installed_packages+=("OpenCV")
elif python -c 'import cv2' 2>/dev/null; then
    installed_packages+=("OpenCV")
else
    # Check if opencv-python package is installed even if import fails
    if pip show opencv-python >/dev/null 2>&1; then
        installed_packages+=("OpenCV (package installed, import may need system libraries)")
    else
        failed_packages+=("OpenCV")
    fi
fi

if python -c 'import PIL' 2>/dev/null; then
    installed_packages+=("Pillow")
else
    failed_packages+=("Pillow")
fi

if python -c 'import yaml' 2>/dev/null; then
    installed_packages+=("PyYAML")
else
    failed_packages+=("PyYAML")
fi

if [ ${#installed_packages[@]} -gt 0 ]; then
    echo "✅ Successfully installed:"
    for package in "${installed_packages[@]}"; do
        echo "   - $package"
    done
fi

if [ ${#failed_packages[@]} -gt 0 ]; then
    echo ""
    echo "⚠️  Some packages failed verification:"
    for package in "${failed_packages[@]}"; do
        echo "   - $package"
    done
    echo ""
    echo "🔧 To fix failed installations:"
    echo "   1. Activate the environment: source $ENV_DIR/bin/activate"
    echo "   2. Install missing packages manually:"
    for package in "${failed_packages[@]}"; do
        case "$package" in
            "Ultralytics")
                echo "      pip install ultralytics==8.3.0"
                ;;
            "OpenCV")
                echo "      pip install opencv-python"
                ;;
            *)
                echo "      pip install $(echo "$package" | tr '[:upper:]' '[:lower:]')"
                ;;
        esac
    done
    echo "   3. Check the error messages above for specific commands"
    echo ""
    echo "Note: Some packages may be installed but have import issues."
    echo "      Try importing them manually to diagnose the problem."
fi

echo ""
echo "This environment is intended for:"
echo "  - PyTorch 1.13.1 (CPU version, compatible with DeepStream 7.1)"
echo "  - Ultralytics 8.0.196 (stable YOLOv8 support)"
echo "  - ONNX 1.14.0 (compatible with TensorRT in DeepStream 7.1)"
echo ""
echo "Note: This PyTorch version does not include CUDA support, but is optimized"
echo "      for model conversion and export. DeepStream will handle GPU acceleration"
echo "      during inference using TensorRT."
echo ""
echo "You can now use this environment for YOLO model conversion."
