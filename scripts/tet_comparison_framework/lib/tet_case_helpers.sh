#!/bin/bash
# Shared process/config/bitrate helpers for TET comparison cases.

update_tet_config() {
    local source_config="$1"
    local output_config="$2"
    local enabled="$3"
    local tile_file="$4"
    local qp_value="$5"
    local dynamic_qp="$6"

    awk -v enabled="$enabled" \
        -v tile_file="$tile_file" \
        -v qp_value="$qp_value" \
        -v dynamic_qp="$dynamic_qp" '
        BEGIN { in_tet = 0; in_debug = 0 }
        /^[[:space:]]*\[TET\][[:space:]]*$/ { in_tet = 1; print; next }
        /^[[:space:]]*\[Debug\][[:space:]]*$/ { in_debug = 1; print; next }
        /^[[:space:]]*\[/ {
            if (in_tet) { in_tet = 0 }
            if (in_debug) { in_debug = 0 }
        }
        in_tet && $0 ~ /^[[:space:]]*enabled[[:space:]]*=/ { print "enabled = " enabled; next }
        in_tet && $0 ~ /^[[:space:]]*tile_file[[:space:]]*=/ { print "tile_file = " tile_file; next }
        in_tet && $0 ~ /^[[:space:]]*qp[[:space:]]*=/ { print "qp = " qp_value; next }
        in_tet && $0 ~ /^[[:space:]]*dynamic_qp[[:space:]]*=/ { print "dynamic_qp = " dynamic_qp; next }
        in_debug && $0 ~ /^[[:space:]]*enable_display_sink[[:space:]]*=/ { print "enable_display_sink = false"; next }
        in_debug && $0 ~ /^[[:space:]]*draw_bitrate[[:space:]]*=/ { print "draw_bitrate = false"; next }
        in_debug && $0 ~ /^[[:space:]]*draw_bitrate_large[[:space:]]*=/ { print "draw_bitrate_large = false"; next }
        in_debug && $0 ~ /^[[:space:]]*draw_fps[[:space:]]*=/ { print "draw_fps = false"; next }
        { print }
    ' "$source_config" > "$output_config"
}

# ---------------------------------------------------------------------------
# Helper: force encoder_cli display branch off via per-case config
# ---------------------------------------------------------------------------
update_encoder_config() {
    local source_config="$1"
    local output_config="$2"

    awk '
        BEGIN { in_debug = 0 }
        /^[[:space:]]*\[Debug\][[:space:]]*$/ { in_debug = 1; print; next }
        /^[[:space:]]*\[/ { if (in_debug) { in_debug = 0 } }
        in_debug && $0 ~ /^[[:space:]]*enable_display_branch[[:space:]]*=/ {
            print "enable_display_branch = false"; next
        }
        { print }
    ' "$source_config" > "$output_config"
}

# ---------------------------------------------------------------------------
# Helper: kill any rtp_server processes that are holding a given port.
# ---------------------------------------------------------------------------
kill_rtp_servers_on_port() {
    local port="${1:-5000}"
    local pids
    # Extract PIDs of rtp_server processes bound to this port
    pids="$(ss -lnup 2>/dev/null | grep ":${port} " | grep -oP 'pid=\K[0-9]+' | sort -u)"
    if [ -z "$pids" ]; then
        return 0
    fi
    echo "Found stale rtp_server process(es) on port ${port}: $pids"
    for pid in $pids; do
        echo "  Sending SIGINT to pid $pid..."
        kill -INT "$pid" 2>/dev/null || true
    done
    sleep 3
    for pid in $pids; do
        if kill -0 "$pid" 2>/dev/null; then
            echo "  pid $pid still alive, sending SIGKILL..."
            kill -KILL "$pid" 2>/dev/null || true
        fi
    done
    sleep 1
}

# ---------------------------------------------------------------------------
# Helper: ensure no rtp_server is occupying the handshake port.
# ---------------------------------------------------------------------------
ensure_port_free() {
    local port="${1:-5000}"
    if ss -lnup 2>/dev/null | grep -q ":${port} "; then
        kill_rtp_servers_on_port "$port"
    fi
    # Final wait — if port is still held (by a non-rtp_server process), just wait
    local attempts=0
    while ss -lnup 2>/dev/null | grep -q ":${port} " && [ "$attempts" -lt 10 ]; do
        echo "Port $port still in use, waiting..."
        sleep 2
        attempts=$((attempts + 1))
    done
    if ss -lnup 2>/dev/null | grep -q ":${port} "; then
        echo "ERROR: Port $port could not be freed."
        return 1
    fi
}

# ---------------------------------------------------------------------------
# Helper: wait for the rtp_server log to contain "Listening" (server ready).
# ---------------------------------------------------------------------------
wait_for_server_ready() {
    local log_file="$1"
    local timeout="${2:-30}"
    local server_pid="${3:-}"
    local waited=0
    echo "Waiting for rtp_server to start listening..."
    while [ "$waited" -lt "$timeout" ]; do
        if [ -f "$log_file" ] && grep -q "Listening for new clients" "$log_file" 2>/dev/null; then
            echo "rtp_server is ready (after ${waited}s)."
            return 0
        fi
        if [ -n "$server_pid" ] && ! kill -0 "$server_pid" 2>/dev/null; then
            echo "ERROR: rtp_server exited before reporting readiness."
            if [ -f "$log_file" ]; then
                local startup_error_line
                startup_error_line="$(grep -E "Inference engine startup failed|error:" "$log_file" 2>/dev/null | tail -n 1 || true)"
                if [ -n "$startup_error_line" ]; then
                    echo "Last startup error: $startup_error_line"
                fi
            fi
            return 1
        fi
        sleep 1
        waited=$((waited + 1))
    done
    echo "WARNING: rtp_server did not report listening within ${timeout}s."
    return 1
}

# ---------------------------------------------------------------------------
# Helper: gracefully stop the rtp_server.
#   - Because we use exec, $pid IS the rtp_server binary, so SIGINT goes
#     directly to the application (which has a GLib signal handler for it).
#   - Falls back to SIGTERM and then SIGKILL if needed.
# ---------------------------------------------------------------------------
shutdown_rtp() {
    local pid="$1"
    if ! kill -0 "$pid" 2>/dev/null; then
        return 0
    fi

    echo "Sending SIGINT to rtp_server (pid $pid)..."
    kill -INT "$pid" 2>/dev/null || true

    local elapsed=0
    local shutdown_elapsed=0
    local shutdown_seen=false
    local shutdown_marker=""
    while kill -0 "$pid" 2>/dev/null && [ "$elapsed" -lt "$SERVER_SHUTDOWN_TIMEOUT" ]; do
        sleep 1
        elapsed=$((elapsed + 1))
    done

    if kill -0 "$pid" 2>/dev/null; then
        echo "rtp_server did not exit after ${SERVER_SHUTDOWN_TIMEOUT}s, sending SIGTERM..."
        kill -TERM "$pid" 2>/dev/null || true
        sleep 5
    fi

    if kill -0 "$pid" 2>/dev/null; then
        echo "rtp_server still running, sending SIGKILL..."
        kill -KILL "$pid" 2>/dev/null || true
    fi

    wait "$pid" 2>/dev/null || true
}

# ---------------------------------------------------------------------------
# Helper: immediately stop the rtp_server with SIGKILL.
# Used when test-mode summary has already been produced and graceful shutdown
# only adds unnecessary delay.
# ---------------------------------------------------------------------------
kill_rtp_immediately() {
    local pid="$1"
    if ! kill -0 "$pid" 2>/dev/null; then
        return 0
    fi

    echo "Sending SIGKILL to rtp_server (pid $pid)..."
    kill -KILL "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
}

# ---------------------------------------------------------------------------
# Helper: stop encoder_cli process group started via setsid.
# Accepts PGID (which equals the leader PID from setsid command launch).
# ---------------------------------------------------------------------------
stop_encoder_process_group() {
    local pgid="$1"
    if [ -z "$pgid" ]; then
        return 0
    fi

    get_group_members() {
        pgrep -g "$pgid" 2>/dev/null || true
    }

    local members
    members="$(get_group_members)"
    if [ -z "$members" ]; then
        return 0
    fi

    local sid
    sid="$(ps -o sid= -p "$pgid" 2>/dev/null | tr -d '[:space:]' || true)"

    collect_descendant_pids() {
        local root_pid="$1"
        local queue="$root_pid"
        local descendants=""

        while [ -n "$queue" ]; do
            local next_queue=""
            local pid
            for pid in $queue; do
                local children
                children="$(pgrep -P "$pid" 2>/dev/null || true)"
                if [ -n "$children" ]; then
                    descendants="${descendants} ${children}"
                    next_queue="${next_queue} ${children}"
                fi
            done
            queue="$next_queue"
        done

        echo "$descendants"
    }

    local descendants
    descendants="$(collect_descendant_pids "$pgid")"

    local session_members=""
    if [ -n "$sid" ]; then
        session_members="$(ps -eo pid=,sid= | awk -v wanted_sid="$sid" '$2==wanted_sid {print $1}' | tr '\n' ' ')"
    fi

    local targets
    targets="$pgid $descendants $session_members"
    targets="$(echo "$targets" | tr ' ' '\n' | awk -v self_pid="$$" 'NF && $1 != self_pid && !seen[$1]++' | tr '\n' ' ')"

    echo "Stopping encoder_cli process group (pgid $pgid) with SIGTERM..."
    kill -TERM -- "-$pgid" 2>/dev/null || true
    [ -n "$targets" ] && kill -TERM $targets 2>/dev/null || true

    local elapsed=0
    while [ "$elapsed" -lt 5 ]; do
        members="$(get_group_members)"
        [ -z "$members" ] && break
        sleep 1
        elapsed=$((elapsed + 1))
    done

    members="$(get_group_members)"
    if [ -n "$members" ]; then
        echo "encoder_cli process group still running, sending SIGKILL..."
        kill -KILL -- "-$pgid" 2>/dev/null || true
        [ -n "$targets" ] && kill -KILL $targets 2>/dev/null || true

        elapsed=0
        while [ "$elapsed" -lt 3 ]; do
            members="$(get_group_members)"
            [ -z "$members" ] && break
            sleep 1
            elapsed=$((elapsed + 1))
        done
    fi

    wait "$pgid" 2>/dev/null || true
    members="$(get_group_members)"
    if [ -n "$members" ]; then
        echo "WARNING: encoder_cli process-group cleanup incomplete for pgid $pgid; remaining members:"
        pgrep -a -g "$pgid" 2>/dev/null || true
    else
        echo "encoder_cli process group stop completed for pgid $pgid"
    fi
}

# ---------------------------------------------------------------------------
# Fallback cleanup for orphaned encoder processes after interrupts/timeouts.
# ---------------------------------------------------------------------------
kill_encoder_residual_processes() {
    local residual_pids=""
    local run_pattern="${ENCODER_DIR}/run.sh file rtp"
    local bin_pattern="${ROOT_DIR}/build/encoder_cli/bin/encoder_cli"

    residual_pids="${residual_pids} $(pgrep -f "$run_pattern" 2>/dev/null || true)"
    residual_pids="${residual_pids} $(pgrep -f "$bin_pattern" 2>/dev/null || true)"
    residual_pids="$(echo "$residual_pids" | tr ' ' '\n' | awk -v self_pid="$$" 'NF && $1 != self_pid && !seen[$1]++' | tr '\n' ' ')"

    if [ -z "$residual_pids" ]; then
        return 0
    fi

    echo "Fallback cleanup: found residual encoder processes: $residual_pids"
    ps -fp $residual_pids 2>/dev/null || true
    kill -TERM $residual_pids 2>/dev/null || true
    sleep 1
    local still_alive=""
    still_alive="$(for pid in $residual_pids; do kill -0 "$pid" 2>/dev/null && echo "$pid"; done | tr '\n' ' ')"
    if [ -n "$still_alive" ]; then
        echo "Fallback cleanup: force killing residual encoder processes: $still_alive"
        kill -KILL $still_alive 2>/dev/null || true
    fi
}

# ---------------------------------------------------------------------------
# Helper: measure encoded bitrate.
#
# Tries multiple extraction methods in priority order:
#   1. Structured "Bitrate summary" line from the encoder log.
#   2. H.265 output file on disk + frame count from encoder log.
#   3. Structured "Encoded bitrate summary" from the rtp_server log.
#   4. Per-frame encoded buffer sizes from the rtp_server log
#      ("Frame #N buffer size: X bytes"), summed and converted to bps.
#   5. "Average bitrate: X.XX kbps" line from the encoder log.
#
# The result is written to ${case_dir}/bitrate_bps.txt.
# Return value:
#   0 -> bitrate was measured successfully
#   1 -> bitrate measurement failed under strict policy
# ---------------------------------------------------------------------------
measure_bitrate() {
    local case_dir="$1"
    local encoder_log="$2"
    local server_log="$3"
    local framerate="$4"

    local bitrate_bps=0

    # ---- Method 1: structured "Bitrate summary" from encoder log ----
    if [ -f "$encoder_log" ]; then
        bitrate_bps=$(python3 -c "
import re
log = open('$encoder_log', errors='ignore').read()
m = re.search(r'Bitrate summary:.*?bitrate_bps=(\d+)', log)
if m:
    print(m.group(1))
else:
    print(0)
" 2>/dev/null || echo "0")
        if [ "$bitrate_bps" -gt 0 ] 2>/dev/null; then
            echo "$bitrate_bps" > "${case_dir}/bitrate_bps.txt"
            echo "Measured bitrate (encoder Bitrate summary): ${bitrate_bps} bps"
            return 0
        fi
    fi

    # ---- Method 2: H.265 output file size on disk ----
    local h265_file=""
    for candidate in \
        "${ENCODER_DIR}/../yolo/yolo_output_with_sei.h265" \
        "${ENCODER_DIR}/../yolo/yolo_output_with_sei_packed.h265"; do
        if [ -f "$candidate" ]; then
            h265_file="$candidate"
            break
        fi
    done

    if [ -n "$h265_file" ]; then
        local file_size_bytes
        file_size_bytes=$(stat -c%s "$h265_file")

        local num_frames=0
        if [ -f "$encoder_log" ]; then
            num_frames=$(python3 -c "
import re
log = open('$encoder_log', errors='ignore').read()
m = re.search(r'Bitrate summary:.*?num_frames=(\d+)', log)
if m:
    print(m.group(1))
else:
    matches = re.findall(r'Pushed to Encoder:\s*(\d+)', log)
    if matches:
        print(matches[-1])
    else:
        print(0)
" 2>/dev/null || echo "0")
        fi

        if [ "$num_frames" -gt 0 ] 2>/dev/null; then
            bitrate_bps=$(python3 -c "
file_size = $file_size_bytes
num_frames = $num_frames
fps = $framerate
duration_s = num_frames / fps
bitrate_bps = int(file_size * 8 / duration_s) if duration_s > 0 else 0
print(bitrate_bps)
")
            if [ "$bitrate_bps" -gt 0 ] 2>/dev/null; then
                echo "$bitrate_bps" > "${case_dir}/bitrate_bps.txt"
                echo "Measured bitrate (H.265 file): ${bitrate_bps} bps (file=${file_size_bytes} bytes, frames=${num_frames}, fps=${framerate})"
                return 0
            fi
        fi
    fi

    # ---- Method 3: structured "Encoded bitrate summary" from rtp_server log ----
    if [ -f "$server_log" ]; then
        bitrate_bps=$(python3 -c "
import re
log = open('$server_log', errors='ignore').read()
m = re.search(r'Encoded bitrate summary:.*?bitrate_bps=(\d+)', log)
if m:
    print(m.group(1))
else:
    print(0)
" 2>/dev/null || echo "0")
        if [ "$bitrate_bps" -gt 0 ] 2>/dev/null; then
            echo "$bitrate_bps" > "${case_dir}/bitrate_bps.txt"
            echo "Measured bitrate (rtp_server Encoded bitrate summary): ${bitrate_bps} bps"
            return 0
        fi
    fi

    # ---- Method 4: per-frame encoded buffer sizes from rtp_server log ----
    if [ -f "$server_log" ]; then
        local parsed_bitrate=0
        local parsed_bytes=0
        local parsed_frames=0
        read -r parsed_bitrate parsed_bytes parsed_frames <<< "$(python3 -c "
import re

log = open('$server_log', errors='ignore').read()
matches = re.findall(r'Frame\\s+#(\\d+)\\s+buffer size:\\s*(\\d+)\\s+bytes\\s+for\\s+port', log)
if not matches:
    print('0 0 0')
else:
    frame_sizes = {}
    for frame_idx, size_bytes in matches:
        fi = int(frame_idx)
        sb = int(size_bytes)
        prev = frame_sizes.get(fi, 0)
        frame_sizes[fi] = sb if sb > prev else prev

    num_frames = len(frame_sizes)
    total_bytes = sum(frame_sizes.values())
    fps = float('$framerate')
    if num_frames > 0 and total_bytes > 0 and fps > 0:
        duration_s = num_frames / fps
        bitrate_bps = int(total_bytes * 8 / duration_s) if duration_s > 0 else 0
        print(f'{bitrate_bps} {total_bytes} {num_frames}')
    else:
        print('0 0 0')
" 2>/dev/null || echo "0 0 0")"

        if [ "${parsed_bitrate}" -gt 0 ] 2>/dev/null; then
            bitrate_bps="${parsed_bitrate}"
            echo "$bitrate_bps" > "${case_dir}/bitrate_bps.txt"
            echo "Measured bitrate (rtp_server frame-size logs): ${bitrate_bps} bps (bytes=${parsed_bytes}, frames=${parsed_frames}, fps=${framerate})"
            return 0
        fi
    fi

    # ---- Method 5: "Average bitrate: X.XX kbps" from encoder log ----
    if [ -f "$encoder_log" ]; then
        bitrate_bps=$(python3 -c "
import re
log = open('$encoder_log', errors='ignore').read()
m = re.search(r'Average bitrate:\s*([0-9.]+)\s*kbps', log)
if m:
    print(int(float(m.group(1)) * 1000))
else:
    print(0)
" 2>/dev/null || echo "0")
        if [ "$bitrate_bps" -gt 0 ] 2>/dev/null; then
            echo "$bitrate_bps" > "${case_dir}/bitrate_bps.txt"
            echo "Measured bitrate (encoder Average bitrate): ${bitrate_bps} bps"
            return 0
        fi
    fi

    # ---- All methods failed ----
    if [ "$BITRATE_POLICY" = "strict" ]; then
        echo "ERROR: Could not measure bitrate for this case (strict policy)." >&2
        return 1
    fi

    echo "0" > "${case_dir}/bitrate_bps.txt"
    echo "WARNING: Could not measure bitrate for this case; writing 0 due to tolerant policy."
    return 0
}

