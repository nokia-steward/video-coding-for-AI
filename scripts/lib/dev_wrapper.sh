#!/bin/bash

# Load the user's default .bashrc if it exists
if [ -f "$HOME/.bashrc" ]; then
  source "$HOME/.bashrc"
fi

# Source dev functions from setup lib (path relative to this file; lib/ is under scripts/)
_WRAPPER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
source "${_WRAPPER_DIR}/../setup/lib/dev_functions_lib.sh"
unset _WRAPPER_DIR

PS1="[Apptainer] $PS1"
