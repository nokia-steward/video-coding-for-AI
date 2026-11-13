#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# VCAI Docker Initialization Script
# Creates global directories and sets up default configurations

set -e

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

VCAI_HOME="${HOME}/.VCAI"

echo -e "${BLUE}=== VCAI Docker Initialization ===${NC}"
echo ""

# Create directory structure
echo -e "${YELLOW}Creating directory structure...${NC}"
mkdir -p "${VCAI_HOME}/rtp_server"
mkdir -p "${VCAI_HOME}/encoder_cli"
mkdir -p "${VCAI_HOME}/videos"
mkdir -p "${VCAI_HOME}/configs"

echo -e "${GREEN}[OK] Directory structure created${NC}"
echo ""

# Check if we're running inside a container or need to copy from image
if [ -f "/opt/vcai/data/configs/encoder_config.ini.default" ]; then
    # Running inside container - copy default configs
    echo -e "${YELLOW}Copying default configurations from image...${NC}"
    
    if [ ! -f "${VCAI_HOME}/configs/encoder_config.ini" ]; then
        cp /opt/vcai/data/configs/encoder_config.ini.default \
           "${VCAI_HOME}/configs/encoder_config.ini"
        echo -e "${GREEN}[OK] Default encoder_config.ini created${NC}"
    else
        echo -e "${YELLOW}  encoder_config.ini already exists, skipping${NC}"
    fi
else
    # Running on host - check if configs exist
    echo -e "${YELLOW}Checking for existing configurations...${NC}"
    
    if [ ! -f "${VCAI_HOME}/configs/encoder_config.ini" ]; then
        echo -e "${YELLOW}  encoder_config.ini not found${NC}"
        echo -e "${YELLOW}  You can copy it from the project or create it manually${NC}"
        echo -e "${YELLOW}  Example: cp encoder_cli/encoder_config.ini ${VCAI_HOME}/configs/${NC}"
    else
        echo -e "${GREEN}[OK] encoder_config.ini found${NC}"
    fi
fi

echo ""
echo -e "${BLUE}Directory Structure:${NC}"
echo "  ${VCAI_HOME}/"
echo "  |- rtp_server/   # RTP server outputs and logs"
echo "  |- encoder_cli/  # Encoder outputs and logs"
echo "  |- videos/       # Optional host-side video storage"
echo "  `- configs/      # Optional host-side configs"

echo ""
echo -e "${GREEN}Initialization complete!${NC}"
echo ""
echo -e "${BLUE}Next steps:${NC}"
echo "  1. Place your video files in: ${VCAI_HOME}/videos/"
echo "  2. Copy encoder_config.ini to: ${VCAI_HOME}/configs/"
echo "  3. Run Docker wrappers from project root:"
echo "     ./docker/run_rtp_server.sh rtp"
echo "     ./docker/run_encoder_cli.sh udp rtp"
