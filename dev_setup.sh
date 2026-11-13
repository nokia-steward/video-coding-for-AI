#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# development setup script for VCAI

echo "=== VCAI Development Setup ==="

# Check if Apptainer is available
if ! command -v apptainer &> /dev/null; then
    echo "Error: Apptainer is not installed or not in PATH"
    exit 1
fi

# Build development container
if [ ! -f "dev.sif" ] || [ "dev.def" -nt "dev.sif" ]; then
    echo "Building development container..."
    if ! apptainer build dev.sif dev.def; then
        echo "❌ Error: Failed to build development container"
        exit 1
    fi
    echo "✅ Development container built"
else
    echo "Development container already exists"
fi

# Verify that the container file exists before proceeding
if [ ! -f "dev.sif" ]; then
    echo "❌ Error: Container file 'dev.sif' does not exist"
    exit 1
fi

echo ""
echo "✅ Development environment setup complete!"
echo ""

# Detect WSL environment
if [ -f /proc/version ] && grep -qi microsoft /proc/version; then
    echo "⚠️  WSL environment detected"
    echo ""
    
    # Check if nvidia-smi works in WSL
    if command -v nvidia-smi &> /dev/null; then
        if nvidia-smi &> /dev/null; then
            echo "✓ GPU access verified in WSL"
        else
            echo "❌ GPU access blocked in WSL!"
            echo ""
            echo "Error: 'GPU access blocked by the operating system'"
            echo ""
            echo "This must be fixed before containers can access GPU:"
            echo "  1. Install/update NVIDIA drivers on Windows (not in WSL)"
            echo "  2. Update WSL: Run 'wsl --update' in Windows PowerShell (as Admin)"
            echo "  3. Restart WSL: 'wsl --shutdown' then restart your distribution"
            echo "  4. Restart Windows if needed"
            echo "  5. Verify: Run 'nvidia-smi' in WSL (outside container)"
            echo ""
            echo "See WSL_TROUBLESHOOTING.md for detailed instructions."
            echo ""
            read -p "Continue anyway? (y/N): " continue_choice
            if [[ ! "$continue_choice" =~ ^[Yy]$ ]]; then
                echo "Exiting. Please fix GPU access first."
                exit 1
            fi
        fi
    else
        echo "⚠️  nvidia-smi not found. GPU access may not be configured."
        echo "   See WSL_TROUBLESHOOTING.md for setup instructions."
        echo ""
    fi
    
    echo "For GPU access in WSL, ensure:"
    echo "  1. NVIDIA drivers are installed on Windows (not in WSL)"
    echo "  2. CUDA on WSL is properly configured"
    echo "  3. WSL is updated: 'wsl --update' in Windows PowerShell"
    echo ""
    echo "If GPU access fails, you may need to run with additional flags:"
    echo "  apptainer exec --nv --writable-tmpfs dev.sif ./scripts/enter_dev.sh"
    echo ""
fi

echo "Entering the development container..."
echo ""

# Enter the container and run the wrapper
# Try with --nv first, fallback to --nv --writable-tmpfs if WSL is detected
if [ -f /proc/version ] && grep -qi microsoft /proc/version; then
    # WSL detected - try with writable-tmpfs which can help with GPU access
    apptainer exec --nv --writable-tmpfs dev.sif ./scripts/enter_dev.sh
else
    # Native Linux - standard --nv flag
    apptainer exec --nv dev.sif ./scripts/enter_dev.sh
fi
