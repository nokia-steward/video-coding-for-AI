#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

set -o pipefail

echo "=========================================="
echo "  Read-Only Package Check"
echo "=========================================="
echo "Checking status of GStreamer dependency packages..."
echo "This script is read-only and will not install or modify anything."
echo

# --- Define the list of packages to check ---
readarray -t packages_to_check << EOL
gobject-introspection
libgirepository1.0-dev
libxml2-utils
EOL

# --- Arrays to hold results ---
installed_ok=()
installed_broken=()
not_installed=()

echo "Total packages to check: ${#packages_to_check[@]}"
echo

# --- Main loop ---
for pkg in "${packages_to_check[@]}"; do
    echo "--- Checking: $pkg ---"

    # Check 1: Is package installed and in a good state?
    # We redirect stderr to /dev/null to suppress "package not found" messages
    status=$(dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null)

    if [[ $? -ne 0 || "$status" != "install ok installed" ]]; then
        # Package is not installed or not fully installed
        if [[ $? -ne 0 ]]; then
            echo "  -> Status: Not Installed"
        else
            echo "  -> Status: Not fully installed (State: $status)"
        fi
        not_installed+=("$pkg")
    else
        # Package is installed.
        echo "  -> Status: Installed. Verifying files..."

        # Check 2: Verify all files are present and correct
        # `dpkg --verify` outputs a list of modified/missing files.
        # If the output is empty, all files are OK.
        verify_output=$(dpkg --verify "$pkg" 2>&1)

        if [[ -z "$verify_output" ]]; then
            echo "  -> Verification: OK"
            installed_ok+=("$pkg")
        else
            echo "  -> Verification: FAILED (Missing or modified files)"
            installed_broken+=("$pkg")
        fi
    fi
done

# --- Print the final report ---
echo
echo "=========================================="
echo "      Package Pre-build Check Report      "
echo "=========================================="
echo
echo "This report shows the status of the required packages in the *base* image."
echo

echo "## 🔴 Not Installed (${#not_installed[@]})"
echo "These packages are not in the base image. You must add them to your build file."
if [[ ${#not_installed[@]} -gt 0 ]]; then
    last_index=$((${#not_installed[@]} - 1))
    if [[ $last_index -ge 0 ]]; then
        for i in $(seq 0 $(($last_index - 1))); do
            echo "${not_installed[$i]} \\"
        done
        echo "${not_installed[$last_index]}"
    fi
else
    echo "None"
fi
echo
echo "---"

echo "## 🟡 Installed but Broken/Modified (${#installed_broken[@]})"
echo "These packages are present but failed verification. You should force-reinstall them in your build file."
if [[ ${#installed_broken[@]} -gt 0 ]]; then
    last_index=$((${#installed_broken[@]} - 1))
    if [[ $last_index -ge 0 ]]; then
        for i in $(seq 0 $(($last_index - 1))); do
            echo "${installed_broken[$i]} \\"
        done
        echo "${installed_broken[$last_index]}"
    fi
else
    echo "None"
fi
echo
echo "---"

echo "## 🟢 Installed and OK (${#installed_ok[@]})"
echo "These packages are already present and seem fine. You may not need to add them to your build file."
if [[ ${#installed_ok[@]} -gt 0 ]]; then
    last_index=$((${#installed_ok[@]} - 1))
    if [[ $last_index -ge 0 ]]; then
        for i in $(seq 0 $(($last_index - 1))); do
            echo "${installed_ok[$i]} \\"
        done
        echo "${installed_ok[$last_index]}"
    fi
else
    echo "None"
fi
echo
echo "=========================================="
echo "Check complete."
