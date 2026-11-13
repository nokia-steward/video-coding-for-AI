#!/bin/bash
# Shared setup/path/dataset helpers for TET comparison.

resolve_path() {
    local candidate="$1"
    [ -z "$candidate" ] && { echo ""; return; }
    [[ "$candidate" == /* ]] && { echo "$candidate"; return; }
    [ -e "${ROOT_DIR}/${candidate}" ] && { echo "${ROOT_DIR}/${candidate}"; return; }
    echo "$candidate"
}

read_ini_value_in_group() {
    local config_file="$1"
    local section_name="$2"
    local key_name="$3"
    if [ ! -f "$config_file" ]; then
        echo ""
        return 0
    fi
    awk -F'=' -v section_name="$section_name" -v key_name="$key_name" '
        BEGIN { in_section = 0 }
        /^[[:space:]]*[#;]/ { next }
        /^[[:space:]]*\[/ {
            in_section = ($0 ~ "^[[:space:]]*\\[" section_name "\\][[:space:]]*$")
            next
        }
        in_section && $1 ~ "^[[:space:]]*" key_name "[[:space:]]*$" {
            value = $2
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
            print value
            exit
        }
    ' "$config_file" || true
}

resolve_path_from_base() {
    local candidate="$1"
    local base_dir="$2"
    if [ -z "$candidate" ]; then
        echo ""
        return 0
    fi
    if [[ "$candidate" == /* ]]; then
        echo "$candidate"
        return 0
    fi
    if [ -f "$base_dir/$candidate" ] || [ -d "$base_dir/$candidate" ]; then
        echo "$base_dir/$candidate"
        return 0
    fi
    if [ -f "$ROOT_DIR/$candidate" ] || [ -d "$ROOT_DIR/$candidate" ]; then
        echo "$ROOT_DIR/$candidate"
        return 0
    fi
    echo "$base_dir/$candidate"
}

path_exists() {
    local candidate="$1"
    [ -f "$candidate" ] || [ -d "$candidate" ]
}

resolve_preferred_existing_path() {
    local candidate="$1"
    shift
    local base_dir=""
    local resolved=""

    if [ -z "$candidate" ]; then
        echo ""
        return 0
    fi

    if [[ "$candidate" == /* ]] && path_exists "$candidate"; then
        echo "$candidate"
        return 0
    fi

    for base_dir in "$@"; do
        [ -n "$base_dir" ] || continue
        resolved="$(resolve_path_from_base "$candidate" "$base_dir")"
        if path_exists "$resolved"; then
            echo "$resolved"
            return 0
        fi
    done

    # Deterministic fallback when no candidate exists on disk.
    if [ "$#" -gt 0 ] && [ -n "$1" ]; then
        resolve_path_from_base "$candidate" "$1"
        return 0
    fi
    resolve_path "$candidate"
}

is_semseg_nvinfer_config_file() {
    local config_path="$1"
    [ -f "$config_path" ] || return 1
    awk '
        BEGIN { found = 0 }
        /^[[:space:]]*[#;]/ { next }
        {
            line = tolower($0)
            if (line ~ /network-type[[:space:]]*[:=][[:space:]]*2([[:space:]]|$)/) {
                found = 1
            } else if (line ~ /network-type[[:space:]]*[:=][[:space:]]*segmentation([[:space:]]|$)/) {
                found = 1
            }
        }
        END { exit(found ? 0 : 1) }
    ' "$config_path"
}

is_semseg_deepstream_config_file() {
    local config_path="$1"
    [ -f "$config_path" ] || return 1

    local lower_name
    lower_name="$(basename "$config_path" | tr '[:upper:]' '[:lower:]')"
    if [[ "$lower_name" == *semseg* ]] || [[ "$lower_name" == *segmentation* ]]; then
        return 0
    fi

    if is_semseg_nvinfer_config_file "$config_path"; then
        return 0
    fi

    local infer_path=""
    infer_path="$(awk -F':' '
        /^[[:space:]]*[#;]/ { next }
        /^[[:space:]]*config-file-path[[:space:]]*:/ {
            value = $2
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
            print value
            exit
        }
    ' "$config_path" || true)"

    if [ -n "$infer_path" ]; then
        infer_path="$(resolve_path_from_base "$infer_path" "$(dirname "$config_path")")"
        if is_semseg_nvinfer_config_file "$infer_path"; then
            return 0
        fi
    fi

    return 1
}

semseg_deepstream_config_has_engine_target() {
    local config_path="$1"
    [ -f "$config_path" ] || return 1

    local infer_path=""
    infer_path="$(awk -F':' '
        /^[[:space:]]*[#;]/ { next }
        /^[[:space:]]*config-file-path[[:space:]]*:/ {
            value = $2
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
            print value
            exit
        }
    ' "$config_path" || true)"

    if [ -n "$infer_path" ]; then
        infer_path="$(resolve_path_from_base "$infer_path" "$(dirname "$config_path")")"
    else
        infer_path="$config_path"
    fi

    [ -f "$infer_path" ] || return 1

    read_semseg_infer_value() {
        local file_path="$1"
        local key_name="$2"
        awk -v key_name="$key_name" '
            /^[[:space:]]*[#;]/ { next }
            {
                line = $0
                sub(/^[[:space:]]+/, "", line)
                lower = tolower(line)
                target = tolower(key_name)
                if (lower ~ ("^" target "[[:space:]]*[:=]")) {
                    match(line, /[:=]/)
                    value = substr(line, RSTART + 1)
                    gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
                    gsub(/^["'\'']|["'\'']$/, "", value)
                    print value
                    exit
                }
            }
        ' "$file_path" || true
    }

    local engine_rel=""
    local engine_path=""
    local onnx_rel=""
    local onnx_path=""
    local tlt_rel=""
    local tlt_path=""
    engine_rel="$(read_semseg_infer_value "$infer_path" "model-engine-file")"
    [ -n "$engine_rel" ] || return 1
    engine_path="$(resolve_path_from_base "$engine_rel" "$(dirname "$infer_path")")"
    if [ -f "$engine_path" ]; then
        return 0
    fi

    onnx_rel="$(read_semseg_infer_value "$infer_path" "onnx-file")"
    local batch_size=""
    local gpu_id=""
    local network_mode=""
    local precision_suffix="fp16"
    local expected_engine_path=""
    local sibling_engine=""
    batch_size="$(read_semseg_infer_value "$infer_path" "batch-size")"
    gpu_id="$(read_semseg_infer_value "$infer_path" "gpu-id")"
    network_mode="$(read_semseg_infer_value "$infer_path" "network-mode")"

    [ -n "$batch_size" ] || batch_size="1"
    [ -n "$gpu_id" ] || gpu_id="0"
    network_mode="$(echo "$network_mode" | tr '[:upper:]' '[:lower:]')"
    case "$network_mode" in
        0|fp32) precision_suffix="fp32" ;;
        2|int8) precision_suffix="int8" ;;
        *) precision_suffix="fp16" ;;
    esac

    discover_engine_sibling_for_model() {
        local model_path="$1"
        local pattern="${model_path}_b${batch_size}_gpu${gpu_id}_"'*.engine'
        local candidate=""
        for candidate in $pattern; do
            [ -f "$candidate" ] || continue
            if [ "$candidate" != "$engine_path" ]; then
                echo "$candidate"
                return 0
            fi
        done
        echo ""
    }

    if [ -n "$onnx_rel" ]; then
        onnx_path="$(resolve_path_from_base "$onnx_rel" "$(dirname "$infer_path")")"
        [ -f "$onnx_path" ] || return 1
        expected_engine_path="${onnx_path}_b${batch_size}_gpu${gpu_id}_${precision_suffix}.engine"
        if [ "$engine_path" = "$expected_engine_path" ]; then
            return 0
        fi
        sibling_engine="$(discover_engine_sibling_for_model "$onnx_path")"
        [ -n "$sibling_engine" ]
        return $?
    fi

    tlt_rel="$(read_semseg_infer_value "$infer_path" "tlt-encoded-model")"
    [ -n "$tlt_rel" ] || return 1
    tlt_path="$(resolve_path_from_base "$tlt_rel" "$(dirname "$infer_path")")"
    [ -f "$tlt_path" ] || return 1
    expected_engine_path="${tlt_path}_b${batch_size}_gpu${gpu_id}_${precision_suffix}.engine"
    if [ "$engine_path" = "$expected_engine_path" ]; then
        return 0
    fi
    sibling_engine="$(discover_engine_sibling_for_model "$tlt_path")"
    [ -n "$sibling_engine" ]
}

validate_semseg_model_label_coverage_or_fail() {
    # Deprecated: semseg DeepStream model coverage checks are no longer used.
    # AH0187 semseg evaluation is executed by
    # scripts/tet_comparison_framework/ah0187/evaluate_semseg_sparse.py.
    return 0
    local deepstream_config="$1"
    local semseg_groundtruth_file="$2"

    if [ ! -f "$deepstream_config" ]; then
        echo "ERROR: SemSeg DeepStream config file not found for label coverage check: $deepstream_config" >&2
        return 1
    fi
    if [ ! -f "$semseg_groundtruth_file" ]; then
        echo "ERROR: SemSeg groundtruth file not found for label coverage check: $semseg_groundtruth_file" >&2
        return 1
    fi

    python3 - "$deepstream_config" "$semseg_groundtruth_file" <<'PY'
import json
import pathlib
import re
import sys


def strip_inline_comment(line: str) -> str:
    out = line
    if "#" in out:
        out = out.split("#", 1)[0]
    if ";" in out and out.lstrip().startswith(";"):
        out = ""
    return out.strip()


def unquote(value: str) -> str:
    return value.strip().strip("'").strip('"')


def resolve_path(base: pathlib.Path, candidate: str) -> pathlib.Path:
    p = pathlib.Path(candidate)
    return p if p.is_absolute() else (base / p)


def canonical_label(name: str) -> str:
    key = re.sub(r"[^a-z0-9]+", "", name.lower())
    aliases = {
        "person": "pedestrian",
        "people": "pedestrian",
        "pedestrian": "pedestrian",
        "pedestrians": "pedestrian",
        "motorbike": "motorcycle",
        "motorbikes": "motorcycle",
        "motorcycle": "motorcycle",
        "motorcycles": "motorcycle",
        "bike": "bicycle",
        "bikes": "bicycle",
        "bicycle": "bicycle",
        "bicycles": "bicycle",
        "cyclist": "bicycle",
        "cyclists": "bicycle",
        "road": "road",
        "roads": "road",
        "sidewalk": "sidewalk",
        "sidewalks": "sidewalk",
        "pavement": "sidewalk",
        "pavements": "sidewalk",
        "vegetation": "vegetation",
        "tree": "vegetation",
        "trees": "vegetation",
        "car": "car",
        "cars": "car",
        "truck": "truck",
        "trucks": "truck",
        "bus": "bus",
        "buses": "bus",
        "building": "building",
        "buildings": "building",
        "background": "background",
    }
    if key in aliases:
        return aliases[key]
    if key.endswith("s") and key[:-1] in aliases:
        return aliases[key[:-1]]
    return key


deepstream_cfg = pathlib.Path(sys.argv[1])
semseg_gt_file = pathlib.Path(sys.argv[2])

infer_rel = ""
with deepstream_cfg.open("r", encoding="utf-8", errors="ignore") as handle:
    for raw_line in handle:
        line = strip_inline_comment(raw_line)
        if not line:
            continue
        match = re.match(r"(?i)^config-file-path\s*:\s*(.+)$", line)
        if match:
            infer_rel = unquote(match.group(1))
            break

infer_cfg = resolve_path(deepstream_cfg.parent, infer_rel) if infer_rel else deepstream_cfg
if not infer_cfg.is_file():
    print(f"ERROR: SemSeg nvinfer config missing for coverage check: {infer_cfg}", file=sys.stderr)
    sys.exit(1)

labels_rel = ""
num_detected_classes = 0
with infer_cfg.open("r", encoding="utf-8", errors="ignore") as handle:
    for raw_line in handle:
        line = strip_inline_comment(raw_line)
        if not line:
            continue
        match = re.match(r"(?i)^labelfile-path\s*[:=]\s*(.+)$", line)
        if match:
            labels_rel = unquote(match.group(1))
        class_match = re.match(r"(?i)^num-detected-classes\s*[:=]\s*([0-9]+)\s*$", line)
        if class_match:
            num_detected_classes = int(class_match.group(1))

if not labels_rel:
    print(f"ERROR: SemSeg infer config does not set labelfile-path: {infer_cfg}", file=sys.stderr)
    sys.exit(1)

labels_file = resolve_path(infer_cfg.parent, labels_rel)
if not labels_file.is_file():
    print(f"ERROR: SemSeg labels file not found: {labels_file}", file=sys.stderr)
    sys.exit(1)

labels = []
with labels_file.open("r", encoding="utf-8", errors="ignore") as handle:
    for raw_line in handle:
        line = strip_inline_comment(raw_line)
        if line:
            labels.append(line)

if not labels:
    print(f"ERROR: SemSeg labels file is empty: {labels_file}", file=sys.stderr)
    sys.exit(1)

with semseg_gt_file.open("r", encoding="utf-8", errors="ignore") as handle:
    semseg_gt = json.load(handle)

categories = {}
for category in semseg_gt.get("categories", []):
    if isinstance(category, dict) and "id" in category and "name" in category:
        try:
            categories[int(category["id"])] = str(category["name"])
        except Exception:
            pass

eval_ids = semseg_gt.get("eval_category_ids", [])
if not isinstance(eval_ids, list) or not eval_ids:
    eval_ids = sorted(categories.keys())

expected_names = []
for cid in eval_ids:
    try:
        cid_int = int(cid)
    except Exception:
        continue
    if cid_int in categories:
        expected_names.append(categories[cid_int])

if not expected_names:
    print("ERROR: No valid eval categories found in semseg groundtruth JSON.", file=sys.stderr)
    sys.exit(1)

expected_by_canonical = {}
for name in expected_names:
    canonical = canonical_label(name)
    if canonical:
        expected_by_canonical.setdefault(canonical, name)

labels_by_canonical = {}
for label in labels:
    canonical = canonical_label(label)
    if canonical:
        labels_by_canonical.setdefault(canonical, label)

if num_detected_classes > 0 and num_detected_classes < len(expected_by_canonical):
    print("ERROR: SemSeg model class-count check failed.", file=sys.stderr)
    print(f"  nvinfer config:            {infer_cfg}", file=sys.stderr)
    print(f"  num-detected-classes:      {num_detected_classes}", file=sys.stderr)
    print(f"  required eval categories:  {len(expected_by_canonical)}", file=sys.stderr)
    print("  hint: use a semseg model with enough classes for VCM evaluation.", file=sys.stderr)
    sys.exit(1)

missing_expected = [expected_by_canonical[key] for key in sorted(expected_by_canonical.keys())
                    if key not in labels_by_canonical]
matched_pairs = [f"{expected_by_canonical[key]} <- {labels_by_canonical[key]}"
                 for key in sorted(expected_by_canonical.keys()) if key in labels_by_canonical]

if missing_expected:
    print("ERROR: SemSeg model label coverage check failed.", file=sys.stderr)
    print(f"  DeepStream config: {deepstream_cfg}", file=sys.stderr)
    print(f"  nvinfer config:    {infer_cfg}", file=sys.stderr)
    print(f"  labels file:       {labels_file}", file=sys.stderr)
    print(f"  expected eval categories ({len(expected_by_canonical)}): "
          f"{', '.join(expected_by_canonical[key] for key in sorted(expected_by_canonical.keys()))}",
          file=sys.stderr)
    print(f"  model labels ({len(labels)}): {', '.join(labels)}", file=sys.stderr)
    if matched_pairs:
        print(f"  matched:           {', '.join(matched_pairs)}", file=sys.stderr)
    print(f"  missing:           {', '.join(missing_expected)}", file=sys.stderr)
    print("  hint: use a VCM-compatible semseg model/labels (e.g. labels_vcm_eval.txt coverage).", file=sys.stderr)
    sys.exit(1)

print(f"SemSeg label coverage check passed: {len(matched_pairs)}/{len(expected_by_canonical)} categories matched.")
PY
}

ensure_pandaset_python_env() {
    local venv_dir="$PANDASET_PYTHON_VENV_DIR"
    local venv_python="${venv_dir}/bin/python3"

    if [ ! -x "$venv_python" ]; then
        echo "Creating local PandaSet Python environment: $venv_dir"
        if ! python3 -m venv "$venv_dir"; then
            echo "ERROR: Failed to create Python virtual environment at $venv_dir" >&2
            return 1
        fi
    fi

    if "$venv_python" -c "import numpy, pandaset" >/dev/null 2>&1; then
        PANDASET_PYTHON_BIN="$venv_python"
        return 0
    fi

    echo "Installing PandaSet conversion dependencies into $venv_dir ..."
    if ! "$venv_python" -m pip install --upgrade pip setuptools wheel; then
        echo "ERROR: Failed to bootstrap pip tooling in $venv_dir" >&2
        return 1
    fi
    if ! "$venv_python" -m pip install numpy pandas; then
        echo "ERROR: Failed to install numpy/pandas into $venv_dir" >&2
        return 1
    fi
    if ! "$venv_python" -m pip install "git+https://github.com/scaleapi/pandaset-devkit.git@59be180e2a3f3e37f6d66af9e67bf944ccbf6ec0#subdirectory=python"; then
        echo "ERROR: Failed to install pandaset-devkit into $venv_dir" >&2
        echo "       Check internet access, then rerun." >&2
        return 1
    fi
    if ! "$venv_python" -c "import numpy, pandaset" >/dev/null 2>&1; then
        echo "ERROR: pandaset dependencies are still unavailable after installation." >&2
        return 1
    fi

    PANDASET_PYTHON_BIN="$venv_python"
    return 0
}

run_semseg_converter_with_auto_env() {
    local dataset_root="$1"
    local output_path="$2"
    local converter="${ROOT_DIR}/scripts/tet_comparison_framework/tools/convert_pandaset_semseg_pkl_to_json.py"

    local conversion_log=""
    local conversion_rc=0

    set +e
    conversion_log="$(python3 "$converter" --dataset-dir "$dataset_root" --output "$output_path" 2>&1)"
    conversion_rc=$?
    set -e
    echo "$conversion_log"
    if [ "$conversion_rc" -eq 0 ]; then
        return 0
    fi

    if [[ "$conversion_log" != *"Failed to import required modules"* ]] && \
       [[ "$conversion_log" != *"No module named 'pandaset'"* ]] && \
       [[ "$conversion_log" != *"No module named 'numpy'"* ]]; then
        return "$conversion_rc"
    fi

    echo "Missing PandaSet Python modules detected; bootstrapping local environment..."
    if ! ensure_pandaset_python_env; then
        return 1
    fi

    set +e
    conversion_log="$("$PANDASET_PYTHON_BIN" "$converter" --dataset-dir "$dataset_root" --output "$output_path" 2>&1)"
    conversion_rc=$?
    set -e
    echo "$conversion_log"
    return "$conversion_rc"
}

discover_first_existing_file() {
    local pattern
    for pattern in "$@"; do
        # shellcheck disable=SC2086
        for candidate in $pattern; do
            if [ -f "$candidate" ]; then
                echo "$candidate"
                return 0
            fi
        done
    done
    echo ""
}

discover_default_semseg_config_file() {
    local semseg_dir="$ROOT_DIR/yolo/semseg"
    discover_first_existing_file \
        "$semseg_dir/semseg_config_default.yml" \
        "$semseg_dir/semseg_config_*.yml" \
        "$semseg_dir/*semseg*.yml" \
        "$semseg_dir/*.yml"
}

discover_default_semseg_engine_file() {
    local semseg_dir="$ROOT_DIR/yolo/semseg"
    discover_first_existing_file \
        "$semseg_dir/semseg*.engine" \
        "$semseg_dir/*.engine"
}

discover_default_semseg_model_file() {
    local semseg_dir="$ROOT_DIR/yolo/semseg"
    discover_first_existing_file \
        "$semseg_dir/semseg*.etlt" \
        "$semseg_dir/*.etlt" \
        "$semseg_dir/semseg*.onnx" \
        "$semseg_dir/*.onnx"
}

discover_default_semseg_labels_file() {
    local semseg_dir="$ROOT_DIR/yolo/semseg"
    discover_first_existing_file \
        "$semseg_dir/labels*.txt" \
        "$semseg_dir/labels_vcm_eval.txt" \
        "$semseg_dir/*.txt"
}

INPUT_FILE="$(resolve_path "$INPUT_FILE")"
GROUNDTRUTH_FILE="$(resolve_path "$GROUNDTRUTH_FILE")"
TILE_FILE="$(resolve_path "$TILE_FILE")"
DATASET_DIR="$(resolve_path "$DATASET_DIR")"
SEMSEG_GROUNDTRUTH_FILE="$(resolve_path "$SEMSEG_GROUNDTRUTH_FILE")"

validate_pandaset_root_markers() {
    local candidate="$1"
    [ -z "$candidate" ] && return 1
    [ -d "${candidate}/camera/front_camera" ] || return 1
    [ -d "${candidate}/annotations/cuboids" ] || return 1
    return 0
}

infer_default_output_dataset_name() {
    if [ -n "$DATASET_DIR" ]; then
        basename "$DATASET_DIR"
        return 0
    fi
    if [ -n "$INPUT_FILE" ]; then
        basename "$(dirname "$INPUT_FILE")"
        return 0
    fi
    if [ -n "$GROUNDTRUTH_FILE" ]; then
        local gt_parent
        gt_parent="$(basename "$(dirname "$GROUNDTRUTH_FILE")")"
        if [ "$gt_parent" = "annotations" ] || [ "$gt_parent" = "annotation" ]; then
            basename "$(dirname "$(dirname "$GROUNDTRUTH_FILE")")"
            return 0
        fi
        echo "$gt_parent"
        return 0
    fi
    echo "run"
}

format_elapsed_runtime() {
    local now_ts elapsed hours minutes seconds
    now_ts="$(date +%s)"
    elapsed=$((now_ts - SCRIPT_START_TS))
    if [ "$elapsed" -lt 0 ]; then
        elapsed=0
    fi
    hours=$((elapsed / 3600))
    minutes=$(((elapsed % 3600) / 60))
    seconds=$((elapsed % 60))
    printf "%02d:%02d:%02d (%ss)" "$hours" "$minutes" "$seconds" "$elapsed"
}

infer_dataset_root_from_groundtruth() {
    local gt_path="$1"
    local gt_dir
    gt_dir="$(dirname "$gt_path")"
    if [ "$(basename "$gt_dir")" = "annotations" ]; then
        dirname "$gt_dir"
    else
        echo "$gt_dir"
    fi
}

infer_dataset_root_from_input() {
    local input_path="$1"
    dirname "$input_path"
}

resolve_pandaset_dataset_root() {
    local explicit_dir="$1"
    local input_path="$2"
    local groundtruth_path="$3"

    if [ -n "$explicit_dir" ]; then
        if validate_pandaset_root_markers "$explicit_dir"; then
            echo "$explicit_dir"
            return 0
        fi
        echo "ERROR: --dataset-dir does not look like PandaSet root: $explicit_dir" >&2
        return 1
    fi

    local inferred_from_groundtruth
    inferred_from_groundtruth="$(infer_dataset_root_from_groundtruth "$groundtruth_path")"
    if validate_pandaset_root_markers "$inferred_from_groundtruth"; then
        echo "$inferred_from_groundtruth"
        return 0
    fi

    local inferred_from_input
    inferred_from_input="$(infer_dataset_root_from_input "$input_path")"
    if validate_pandaset_root_markers "$inferred_from_input"; then
        echo "$inferred_from_input"
        return 0
    fi

    echo "ERROR: Could not resolve PandaSet dataset root. Provide --dataset-dir explicitly." >&2
    return 1
}

ensure_input_yuv_exists_or_generate() {
    local input_path="$1"
    local explicit_dataset_dir="$2"
    local groundtruth_path="$3"

    if [ -n "$input_path" ] && [ -f "$input_path" ]; then
        INPUT_FILE="$input_path"
        return 0
    fi

    if [ -n "$input_path" ]; then
        echo "Input YUV missing: $input_path"
    else
        echo "Input YUV not provided."
    fi
    echo "Attempting PandaSet YUV generation..."

    local dataset_root
    dataset_root="$(resolve_pandaset_dataset_root "$explicit_dataset_dir" "$input_path" "$groundtruth_path")" || return 1

    local generation_log
    generation_log="$(python3 "${ROOT_DIR}/scripts/tet_comparison_framework/tools/generate_pandaset_front_camera_yuv.py" \
        --dataset-dir "$dataset_root" 2>&1)" || {
        echo "$generation_log"
        return 1
    }
    echo "$generation_log"

    local generated_output_path=""
    while IFS= read -r line; do
        case "$line" in
            "  Output: "*) generated_output_path="${line#  Output: }" ;;
        esac
    done <<< "$generation_log"

    if [ -z "$generated_output_path" ]; then
        echo "ERROR: Could not parse generated YUV path from generator output." >&2
        return 1
    fi
    generated_output_path="$(resolve_path "$generated_output_path")"
    if [ ! -f "$generated_output_path" ]; then
        echo "ERROR: YUV generation finished but output is still missing: $generated_output_path" >&2
        return 1
    fi

    if [ -z "$input_path" ]; then
        INPUT_FILE="$generated_output_path"
        echo "Using generated input YUV: $INPUT_FILE"
        return 0
    fi

    local requested_dir
    local requested_base
    local requested_prefix
    local generated_suffix
    local canonical_input_path
    requested_dir="$(dirname "$input_path")"
    requested_base="$(basename "$input_path")"
    requested_prefix="$(derive_input_prefix_for_generated_yuv "$requested_base")"
    generated_suffix="$(extract_resolution_fps_suffix_from_yuv "$(basename "$generated_output_path")")" || {
        echo "ERROR: Generated YUV name does not contain expected _<W>x<H>_<FPS>.yuv suffix: $generated_output_path" >&2
        return 1
    }
    canonical_input_path="${requested_dir}/${requested_prefix}_${generated_suffix}.yuv"
    mkdir -p "$requested_dir"

    if [ "$canonical_input_path" != "$generated_output_path" ]; then
        ln -sfn "$generated_output_path" "$canonical_input_path"
        echo "Created canonical input alias: $canonical_input_path -> $generated_output_path"
    fi

    INPUT_FILE="$canonical_input_path"
    echo "Using generated input YUV: $INPUT_FILE"
}

derive_input_prefix_for_generated_yuv() {
    local requested_basename="$1"
    local stem="${requested_basename%.*}"

    if [[ "$stem" =~ ^(.+)_([0-9]+)x([0-9]+)(_([0-9]+))?$ ]]; then
        stem="${BASH_REMATCH[1]}"
    elif [[ "$stem" =~ ^(.+)_([0-9]+)_([0-9]+)(_([0-9]+))?$ ]]; then
        stem="${BASH_REMATCH[1]}"
    fi

    while [[ "$stem" == *_ ]]; do
        stem="${stem%_}"
    done

    if [ -z "$stem" ]; then
        echo "pandaset"
    else
        echo "$stem"
    fi
}

extract_resolution_fps_suffix_from_yuv() {
    local yuv_basename="$1"
    if [[ "$yuv_basename" =~ _([0-9]+x[0-9]+_[0-9]+)\.yuv$ ]]; then
        echo "${BASH_REMATCH[1]}"
        return 0
    fi
    return 1
}

extract_fps_from_input_filename() {
    local input_basename="$1"
    python3 -c "
import re
name = '$input_basename'
m = re.search(r'_[0-9]+x[0-9]+_([0-9]+)\.yuv$', name)
if m:
    print(m.group(1))
else:
    print('30')
"
}

extract_fps_from_encoder_log() {
    local encoder_log="$1"
    if [ ! -f "$encoder_log" ]; then
        echo ""
        return 0
    fi
    python3 -c "
import re
log = open('$encoder_log', errors='ignore').read()
m = re.search(r'Video framerate set from command line:\s*([0-9]+(?:\.[0-9]+)?)', log)
if m:
    print(int(round(float(m.group(1)))))
" 2>/dev/null || true
}

extract_expected_frame_count_from_encoder_log() {
    local encoder_log="$1"
    if [ ! -f "$encoder_log" ]; then
        echo "0"
        return 0
    fi
    python3 -c "
import re
log = open('$encoder_log', errors='ignore').read()
patterns = [
    r'File input frame count:\\s*([0-9]+)',
    r'frame_number=([0-9]+)\\s+expected=([0-9]+)',
]
for pattern in patterns:
    match = re.search(pattern, log)
    if not match:
        continue
    if len(match.groups()) == 1:
        print(int(match.group(1)))
        raise SystemExit(0)
    print(int(match.group(2)))
    raise SystemExit(0)
print(0)
" 2>/dev/null || echo "0"
}

count_semseg_png_frames() {
    local frames_dir="$1"
    [ -d "$frames_dir" ] || { echo "0"; return 0; }
    python3 -c "
import pathlib
path = pathlib.Path('$frames_dir')
print(sum(1 for _ in path.glob('frame_*.png')))
" 2>/dev/null || echo "0"
}

wait_for_semseg_frame_drain() {
    local frames_dir="$1"
    local expected_count="$2"
    local timeout_seconds="$3"

    local elapsed=0
    local last_count=-1
    local stable_seconds=0
    local current_count=0
    local reason="timeout"

    while [ "$elapsed" -lt "$timeout_seconds" ]; do
        current_count="$(count_semseg_png_frames "$frames_dir")"
        if [ "$current_count" -ne "$last_count" ]; then
            echo "Semseg frame export progress: ${current_count} frame(s) captured (expected: ${expected_count})." >&2
            last_count="$current_count"
            stable_seconds=0
        else
            stable_seconds=$((stable_seconds + 1))
        fi

        if [ "$expected_count" -gt 0 ] && [ "$current_count" -ge "$expected_count" ]; then
            reason="expected_reached"
            break
        fi
        if [ "$expected_count" -le 0 ] && [ "$stable_seconds" -ge 3 ]; then
            reason="stable_without_expected"
            break
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done

    if [ "$reason" = "timeout" ]; then
        current_count="$(count_semseg_png_frames "$frames_dir")"
    fi
    echo "${current_count} ${reason} ${elapsed}"
}

calculate_frame_capture_ratio() {
    local captured="$1"
    local expected="$2"
    python3 - "$captured" "$expected" <<'PY'
import sys
captured = int(sys.argv[1])
expected = int(sys.argv[2])
if expected <= 0:
    print("1.0")
else:
    print(f"{captured/expected:.6f}")
PY
}

normalize_input_filename_for_encoder_cli() {
    local input_path="$1"
    local input_dir
    local input_base
    input_dir="$(dirname "$input_path")"
    input_base="$(basename "$input_path")"

    # encoder_cli expects NAME_WIDTHxHEIGHT[_FPS].ext
    if [[ "$input_base" =~ _[0-9]+x[0-9]+(_[0-9]+)?\.yuv$ ]]; then
        echo "$input_path"
        return 0
    fi

    # Common typo variant: NAME_WIDTH_HEIGHT_FPS.yuv -> NAME_WIDTHxHEIGHT_FPS.yuv
    if [[ "$input_base" =~ ^(.+)_([0-9]+)_([0-9]+)_([0-9]+)\.yuv$ ]]; then
        local prefix="${BASH_REMATCH[1]}"
        local width="${BASH_REMATCH[2]}"
        local height="${BASH_REMATCH[3]}"
        local fps="${BASH_REMATCH[4]}"
        local normalized_path="${input_dir}/${prefix}_${width}x${height}_${fps}.yuv"
        if [ ! -e "$normalized_path" ]; then
            ln -s "$input_path" "$normalized_path"
            echo "Created encoder_cli-compatible alias: $normalized_path -> $input_path"
        fi
        echo "$normalized_path"
        return 0
    fi

    # Secondary typo variant: NAME_WIDTH_HEIGHT.yuv -> NAME_WIDTHxHEIGHT.yuv
    if [[ "$input_base" =~ ^(.+)_([0-9]+)_([0-9]+)\.yuv$ ]]; then
        local prefix="${BASH_REMATCH[1]}"
        local width="${BASH_REMATCH[2]}"
        local height="${BASH_REMATCH[3]}"
        local normalized_path="${input_dir}/${prefix}_${width}x${height}.yuv"
        if [ ! -e "$normalized_path" ]; then
            ln -s "$input_path" "$normalized_path"
            echo "Created encoder_cli-compatible alias: $normalized_path -> $input_path"
        fi
        echo "$normalized_path"
        return 0
    fi

    echo "ERROR: Input filename must match NAME_WIDTHxHEIGHT[_FPS].yuv for encoder_cli: $input_base" >&2
    echo "       Example: pandaset_1920x1080_30.yuv" >&2
    return 1
}

ensure_groundtruth_exists_or_generate() {
    local groundtruth_path="$1"
    local explicit_dataset_dir="$2"
    local input_path="$3"

    if [ -f "$groundtruth_path" ]; then
        return 0
    fi

    echo "Groundtruth JSON missing: $groundtruth_path"
    echo "Attempting PandaSet COCO conversion..."

    local dataset_root
    dataset_root="$(resolve_pandaset_dataset_root "$explicit_dataset_dir" "$input_path" "$groundtruth_path")" || return 1

    mkdir -p "$(dirname "$groundtruth_path")"
    python3 "${ROOT_DIR}/scripts/tet_comparison_framework/tools/convert_pandaset_to_coco.py" \
        --dataset-dir "$dataset_root" \
        --output "$groundtruth_path"

    if [ ! -f "$groundtruth_path" ]; then
        echo "ERROR: COCO conversion finished but groundtruth is still missing: $groundtruth_path" >&2
        return 1
    fi
}

derive_default_semseg_groundtruth_path() {
    local dataset_root="$1"
    if [ -z "$dataset_root" ]; then
        echo ""
        return 0
    fi
    echo "${dataset_root}/annotations/groundtruth_semseg_sparse_vcm.json"
}

ensure_semseg_groundtruth_exists_or_generate() {
    local semseg_groundtruth_path="$1"
    local explicit_dataset_dir="$2"
    local input_path="$3"
    local bbox_groundtruth_path="$4"

    if [ -n "$semseg_groundtruth_path" ] && [ -f "$semseg_groundtruth_path" ]; then
        SEMSEG_GROUNDTRUTH_FILE="$semseg_groundtruth_path"
        return 0
    fi

    local dataset_root
    dataset_root="$(resolve_pandaset_dataset_root "$explicit_dataset_dir" "$input_path" "$bbox_groundtruth_path")" || return 1

    if [ -z "$semseg_groundtruth_path" ]; then
        semseg_groundtruth_path="$(derive_default_semseg_groundtruth_path "$dataset_root")"
    fi

    if [ -f "$semseg_groundtruth_path" ]; then
        SEMSEG_GROUNDTRUTH_FILE="$semseg_groundtruth_path"
        return 0
    fi

    echo "Semantic-segmentation groundtruth missing: $semseg_groundtruth_path"
    echo "Attempting PandaSet semseg conversion from .pkl files..."
    mkdir -p "$(dirname "$semseg_groundtruth_path")"
    if ! run_semseg_converter_with_auto_env "$dataset_root" "$semseg_groundtruth_path"; then
        return 1
    fi

    if [ ! -f "$semseg_groundtruth_path" ]; then
        echo "ERROR: SemSeg conversion finished but output is still missing: $semseg_groundtruth_path" >&2
        return 1
    fi

    SEMSEG_GROUNDTRUTH_FILE="$semseg_groundtruth_path"
}

ensure_ah0187_python_runtime() {
    if [ "$AH0187_PYTHON_RESOLVED" = true ]; then
        return 0
    fi

    ah0187_python_has_modules() {
        local python_bin="$1"
        "$python_bin" -c "import detectron2, cv2, numpy" >/dev/null 2>&1
    }

    discover_conda_binary() {
        if [ -n "${AH0187_SEMSEG_CONDA_BIN:-}" ] && [ -x "${AH0187_SEMSEG_CONDA_BIN}" ]; then
            echo "${AH0187_SEMSEG_CONDA_BIN}"
            return 0
        fi
        if command -v conda >/dev/null 2>&1; then
            command -v conda
            return 0
        fi
        local candidate=""
        for candidate in \
            "$HOME/miniconda3/bin/conda" \
            "$HOME/anaconda3/bin/conda" \
            "/opt/conda/bin/conda"
        do
            if [ -x "$candidate" ]; then
                echo "$candidate"
                return 0
            fi
        done
        echo ""
    }

    bootstrap_ah0187_python_venv() {
        if ! command -v python3 >/dev/null 2>&1; then
            echo "ERROR: python3 is required to auto-create AH0187 semseg environment." >&2
            return 1
        fi

        local venv_python="${AH0187_PYTHON_VENV_DIR}/bin/python"
        local venv_pip="${AH0187_PYTHON_VENV_DIR}/bin/pip"
        if [ ! -x "$venv_python" ]; then
            echo "Creating AH0187 Python virtual environment: ${AH0187_PYTHON_VENV_DIR}"
            python3 -m venv "$AH0187_PYTHON_VENV_DIR"
        fi

        echo "Installing AH0187 semseg Python dependencies (this may take several minutes)..."
        "$venv_pip" install --upgrade pip setuptools wheel
        # Detectron2 currently imports pkg_resources at runtime; keep a setuptools
        # release that still ships pkg_resources in this venv.
        "$venv_pip" install "setuptools<81"
        "$venv_pip" install numpy opencv-python

        if ! "$venv_python" -c "import torch" >/dev/null 2>&1; then
            if [ -n "${AH0187_TORCH_INDEX_URL:-}" ]; then
                "$venv_pip" install torch torchvision --index-url "${AH0187_TORCH_INDEX_URL}"
            else
                "$venv_pip" install torch torchvision
            fi
        fi

        if ! "$venv_python" -c "import pkg_resources" >/dev/null 2>&1; then
            echo "ERROR: pkg_resources is unavailable after setuptools install in ${AH0187_PYTHON_VENV_DIR}." >&2
            return 1
        fi

        # If detectron2 is already importable after setuptools repair, skip rebuild.
        if "$venv_python" -c "import detectron2" >/dev/null 2>&1; then
            ah0187_python_has_modules "$venv_python"
            return $?
        fi

        # Detectron2 needs torch during wheel build; disable pip build isolation so
        # the build environment can reuse the torch installed in this venv.
        if ! "$venv_pip" install --no-build-isolation "git+https://github.com/facebookresearch/detectron2.git"; then
            echo "ERROR: Failed to install detectron2 into ${AH0187_PYTHON_VENV_DIR}." >&2
            echo "If this persists, provide a preconfigured interpreter via AH0187_SEMSEG_PYTHON." >&2
            return 1
        fi

        # Some dependency resolution paths may upgrade setuptools again; enforce a
        # pkg_resources-providing version after detectron2 install as well.
        "$venv_pip" install "setuptools<81"
        if ! "$venv_python" -c "import pkg_resources" >/dev/null 2>&1; then
            echo "ERROR: pkg_resources is unavailable after detectron2 installation in ${AH0187_PYTHON_VENV_DIR}." >&2
            return 1
        fi
        ah0187_python_has_modules "$venv_python"
    }

    if [ -n "${AH0187_SEMSEG_PYTHON:-}" ]; then
        if [ ! -x "${AH0187_SEMSEG_PYTHON}" ]; then
            echo "ERROR: AH0187_SEMSEG_PYTHON is not executable: ${AH0187_SEMSEG_PYTHON}" >&2
            return 1
        fi
        if ah0187_python_has_modules "${AH0187_SEMSEG_PYTHON}"; then
            AH0187_PYTHON_MODE="direct"
            AH0187_PYTHON_BIN="${AH0187_SEMSEG_PYTHON}"
            AH0187_PYTHON_RESOLVED=true
            return 0
        fi
        echo "ERROR: AH0187_SEMSEG_PYTHON does not have required modules (detectron2, opencv, numpy)." >&2
        return 1
    fi

    if command -v python3 >/dev/null 2>&1 && ah0187_python_has_modules "python3"; then
        AH0187_PYTHON_MODE="direct"
        AH0187_PYTHON_BIN="python3"
        AH0187_PYTHON_RESOLVED=true
        return 0
    fi

    local conda_bin=""
    conda_bin="$(discover_conda_binary)"
    if [ -n "$conda_bin" ] && "$conda_bin" run -n "$AH0187_CONDA_ENV" python -c "import detectron2, cv2, numpy" >/dev/null 2>&1; then
        AH0187_PYTHON_MODE="conda_env"
        AH0187_CONDA_BIN="$conda_bin"
        AH0187_PYTHON_BIN="python"
        AH0187_PYTHON_RESOLVED=true
        return 0
    fi

    if [ -x "${AH0187_PYTHON_VENV_DIR}/bin/python" ] && ah0187_python_has_modules "${AH0187_PYTHON_VENV_DIR}/bin/python"; then
        AH0187_PYTHON_MODE="direct"
        AH0187_PYTHON_BIN="${AH0187_PYTHON_VENV_DIR}/bin/python"
        AH0187_PYTHON_RESOLVED=true
        return 0
    fi

    if [ "$AH0187_AUTO_SETUP" = "true" ]; then
        if bootstrap_ah0187_python_venv; then
            AH0187_PYTHON_MODE="direct"
            AH0187_PYTHON_BIN="${AH0187_PYTHON_VENV_DIR}/bin/python"
            AH0187_PYTHON_RESOLVED=true
            return 0
        fi
    fi

    echo "ERROR: Could not find a Python runtime with detectron2 for AH0187 semseg evaluation." >&2
    echo "Tried: AH0187_SEMSEG_PYTHON, python3, conda env '${AH0187_CONDA_ENV}', ${AH0187_PYTHON_VENV_DIR}" >&2
    echo "Options:" >&2
    echo "  1) Set AH0187_SEMSEG_PYTHON=/path/to/python" >&2
    echo "  2) Set AH0187_SEMSEG_CONDA_ENV=<env> and optionally AH0187_SEMSEG_CONDA_BIN=/path/to/conda" >&2
    echo "  3) Keep AH0187_AUTO_SETUP=true to let this script auto-create ${AH0187_PYTHON_VENV_DIR}" >&2
    return 1
}

run_ah0187_semseg_evaluator() {
    local frames_dir="$1"
    local groundtruth_json="$2"
    local output_json="$3"
    local output_md="$4"
    local port="$5"

    if [ ! -f "$SEMSEG_EVALUATOR_SCRIPT" ]; then
        echo "ERROR: AH0187 semseg evaluator script not found: $SEMSEG_EVALUATOR_SCRIPT" >&2
        return 1
    fi
    if [ ! -d "$frames_dir" ]; then
        echo "ERROR: AH0187 semseg evaluator frames directory missing: $frames_dir" >&2
        return 1
    fi
    if [ ! -f "$groundtruth_json" ]; then
        echo "ERROR: AH0187 semseg evaluator groundtruth file missing: $groundtruth_json" >&2
        return 1
    fi
    if ! ensure_ah0187_python_runtime; then
        return 1
    fi

    if [ "$AH0187_PYTHON_MODE" = "direct" ]; then
        "$AH0187_PYTHON_BIN" "$SEMSEG_EVALUATOR_SCRIPT" \
            --frames-dir "$frames_dir" \
            --groundtruth-json "$groundtruth_json" \
            --output-json "$output_json" \
            --output-md "$output_md" \
            --port "$port"
        return $?
    fi

    "$AH0187_CONDA_BIN" run -n "$AH0187_CONDA_ENV" "$AH0187_PYTHON_BIN" "$SEMSEG_EVALUATOR_SCRIPT" \
        --frames-dir "$frames_dir" \
        --groundtruth-json "$groundtruth_json" \
        --output-json "$output_json" \
        --output-md "$output_md" \
        --port "$port"
}

discover_semseg_frames_directory() {
    local case_root_dir="$1"
    local debug_root="${case_root_dir}/debug_frames"
    shopt -s nullglob
    local candidates=( "${debug_root}"/port_* )
    shopt -u nullglob
    if [ "${#candidates[@]}" -eq 0 ]; then
        echo ""
        return 0
    fi
    echo "${candidates[0]}"
}

apply_semseg_case_rtp_server_overrides() {
    local config_path="$1"
    local temp_path="${config_path}.semseg_tmp"
    awk '
        BEGIN { in_inference = 0; in_borders = 0; in_debug = 0 }
        /^[[:space:]]*\[Inference\][[:space:]]*$/ { in_inference = 1; in_borders = 0; in_debug = 0; print; next }
        /^[[:space:]]*\[Borders\][[:space:]]*$/ { in_inference = 0; in_borders = 1; in_debug = 0; print; next }
        /^[[:space:]]*\[Debug\][[:space:]]*$/ { in_inference = 0; in_borders = 0; in_debug = 1; print; next }
        /^[[:space:]]*\[/ { in_inference = 0; in_borders = 0; in_debug = 0 }
        in_inference && $0 ~ /^[[:space:]]*inference_engine[[:space:]]*=/ { print "inference_engine = false"; next }
        in_inference && $0 ~ /^[[:space:]]*labels[[:space:]]*=/ { print "labels = "; next }
        in_borders && $0 ~ /^[[:space:]]*draw_borders[[:space:]]*=/ { print "draw_borders = false"; next }
        in_debug && $0 ~ /^[[:space:]]*save_frames[[:space:]]*=/ { print "save_frames = true"; next }
        { print }
    ' "$config_path" > "$temp_path"
    mv "$temp_path" "$config_path"
}

extract_dimensions_from_input_filename() {
    local input_basename="$1"
    local parsed=""
    if [[ "$input_basename" =~ _([0-9]+)x([0-9]+)(_([0-9]+))?\. ]]; then
        parsed="${BASH_REMATCH[1]} ${BASH_REMATCH[2]}"
    elif [[ "$input_basename" =~ _([0-9]+)_([0-9]+)(_([0-9]+))?\. ]]; then
        parsed="${BASH_REMATCH[1]} ${BASH_REMATCH[2]}"
    fi
    echo "$parsed"
}

resolve_semseg_inference_config_or_generate() {
    echo "ERROR: DeepStream semseg auto-resolution is deprecated in this script." >&2
    echo "Semseg mode now uses the AH0187 evaluator backend only." >&2
    return 1
    local current_cli_path="$1"
    local input_path="$2"
    local rtp_server_ini="$RTP_SERVER_DIR/rtp_server_config.ini"

    SEMSEG_CONFIG_RESOLUTION_SOURCE=""

    if [ -n "$current_cli_path" ]; then
        if [ -f "$current_cli_path" ]; then
            SEMSEG_DEEPSTREAM_CONFIG_FILE="$current_cli_path"
            SEMSEG_CONFIG_RESOLUTION_SOURCE="cli"
            return 0
        fi
        echo "SemSeg DeepStream config file not found: $current_cli_path" >&2
        return 1
    fi

    local config_semseg_path=""
    local config_generic_path=""
    local semseg_profile=""
    local semseg_model_file=""
    local semseg_model_key=""
    local semseg_engine_file=""
    local semseg_labels_file=""
    local common_width=""
    local common_height=""
    local discovered_semseg_default=""

    config_semseg_path="$(read_ini_value_in_group "$rtp_server_ini" "Inference" "semseg_deepstream_config_file")"
    config_generic_path="$(read_ini_value_in_group "$rtp_server_ini" "Inference" "deepstream_config_file")"
    semseg_profile="$(read_ini_value_in_group "$rtp_server_ini" "Inference" "semseg_profile")"
    semseg_model_file="$(read_ini_value_in_group "$rtp_server_ini" "Inference" "semseg_model_file")"
    semseg_model_key="$(read_ini_value_in_group "$rtp_server_ini" "Inference" "semseg_model_key")"
    semseg_engine_file="$(read_ini_value_in_group "$rtp_server_ini" "Inference" "semseg_engine_file")"
    semseg_labels_file="$(read_ini_value_in_group "$rtp_server_ini" "Inference" "semseg_labels_file")"
    common_width="$(read_ini_value_in_group "$rtp_server_ini" "Common" "width")"
    common_height="$(read_ini_value_in_group "$rtp_server_ini" "Common" "height")"

    if [ -n "$config_semseg_path" ]; then
        config_semseg_path="$(resolve_path_from_base "$config_semseg_path" "$RTP_SERVER_DIR")"
        if [ -f "$config_semseg_path" ]; then
            if semseg_deepstream_config_has_engine_target "$config_semseg_path"; then
                SEMSEG_DEEPSTREAM_CONFIG_FILE="$config_semseg_path"
                SEMSEG_CONFIG_RESOLUTION_SOURCE="config_semseg_deepstream_config_file"
                return 0
            fi
            echo "Configured semseg config is missing model-engine-file target; will regenerate: $config_semseg_path"
        else
            echo "Configured semseg_deepstream_config_file is missing: $config_semseg_path"
        fi
    fi

    discovered_semseg_default="$(discover_default_semseg_config_file)"
    if [ -n "$discovered_semseg_default" ] && is_semseg_deepstream_config_file "$discovered_semseg_default"; then
        if semseg_deepstream_config_has_engine_target "$discovered_semseg_default"; then
            SEMSEG_DEEPSTREAM_CONFIG_FILE="$discovered_semseg_default"
            SEMSEG_CONFIG_RESOLUTION_SOURCE="autodiscovered_semseg_config"
            echo "Using auto-discovered semseg config from yolo/semseg: $SEMSEG_DEEPSTREAM_CONFIG_FILE"
            return 0
        fi
        echo "Auto-discovered semseg config is missing model-engine-file target; will regenerate: $discovered_semseg_default"
    fi

    if [ -n "$config_generic_path" ]; then
        config_generic_path="$(resolve_path_from_base "$config_generic_path" "$RTP_SERVER_DIR")"
        if [ -f "$config_generic_path" ]; then
            if is_semseg_deepstream_config_file "$config_generic_path"; then
                if semseg_deepstream_config_has_engine_target "$config_generic_path"; then
                    SEMSEG_DEEPSTREAM_CONFIG_FILE="$config_generic_path"
                    SEMSEG_CONFIG_RESOLUTION_SOURCE="config_deepstream_config_file_semseg_compatible"
                    echo "Using [Inference] deepstream_config_file as semseg fallback: $config_generic_path"
                    return 0
                fi
                echo "Semseg-compatible deepstream_config_file is missing model-engine-file target; will regenerate: $config_generic_path"
            else
                echo "Skipping [Inference] deepstream_config_file fallback because it does not look like semseg config: $config_generic_path"
            fi
        else
            echo "Configured deepstream_config_file is missing: $config_generic_path"
        fi
    fi

    if [ -z "$semseg_profile" ]; then
        semseg_profile="default"
    fi

    if [ -n "$semseg_model_file" ]; then
        semseg_model_file="$(resolve_path_from_base "$semseg_model_file" "$RTP_SERVER_DIR")"
    fi
    if [ -n "$semseg_engine_file" ]; then
        semseg_engine_file="$(resolve_path_from_base "$semseg_engine_file" "$RTP_SERVER_DIR")"
    fi
    if [ -n "$semseg_labels_file" ]; then
        semseg_labels_file="$(resolve_path_from_base "$semseg_labels_file" "$RTP_SERVER_DIR")"
    fi

    if [ -z "$semseg_engine_file" ]; then
        semseg_engine_file="$(discover_default_semseg_engine_file)"
    fi
    if [ -z "$semseg_model_file" ]; then
        semseg_model_file="$(discover_default_semseg_model_file)"
    fi
    if [ -z "$semseg_labels_file" ]; then
        semseg_labels_file="$(discover_default_semseg_labels_file)"
    fi

    local semseg_width="$common_width"
    local semseg_height="$common_height"
    local input_dimensions=""
    input_dimensions="$(extract_dimensions_from_input_filename "$(basename "$input_path")")"
    if [ -n "$input_dimensions" ]; then
        semseg_width="$(echo "$input_dimensions" | awk '{print $1}')"
        semseg_height="$(echo "$input_dimensions" | awk '{print $2}')"
    fi
    if [ -z "$semseg_width" ] || ! [[ "$semseg_width" =~ ^[0-9]+$ ]]; then
        semseg_width="640"
    fi
    if [ -z "$semseg_height" ] || ! [[ "$semseg_height" =~ ^[0-9]+$ ]]; then
        semseg_height="480"
    fi

    if [ -z "$semseg_model_file" ] && [ -z "$semseg_engine_file" ]; then
        local semseg_converter_script="$ROOT_DIR/scripts/yolo/semseg/convert_semseg_model.sh"
        if [ -f "$semseg_converter_script" ]; then
            echo "No semseg model/engine found; attempting automatic semseg model setup..."
            if bash "$semseg_converter_script" "$semseg_width" "$semseg_height" "$ROOT_DIR/yolo/semseg"; then
                semseg_engine_file="$(discover_default_semseg_engine_file)"
                semseg_model_file="$(discover_default_semseg_model_file)"
                if [ -z "$semseg_labels_file" ]; then
                    semseg_labels_file="$(discover_default_semseg_labels_file)"
                fi
            fi
        fi
    fi

    local semseg_infer_width="$semseg_width"
    local semseg_infer_height="$semseg_height"
    local semseg_model_dimensions=""
    if [ -n "$semseg_model_file" ]; then
        semseg_model_dimensions="$(extract_dimensions_from_input_filename "$(basename "$semseg_model_file")")"
    elif [ -n "$semseg_engine_file" ]; then
        semseg_model_dimensions="$(extract_dimensions_from_input_filename "$(basename "$semseg_engine_file")")"
    fi
    if [ -n "$semseg_model_dimensions" ]; then
        semseg_infer_width="$(echo "$semseg_model_dimensions" | awk '{print $1}')"
        semseg_infer_height="$(echo "$semseg_model_dimensions" | awk '{print $2}')"
    fi

    local generated_config="$ROOT_DIR/yolo/semseg/semseg_config_${semseg_profile}.yml"
    if [ -f "$generated_config" ] && is_semseg_deepstream_config_file "$generated_config"; then
        if semseg_deepstream_config_has_engine_target "$generated_config"; then
            SEMSEG_DEEPSTREAM_CONFIG_FILE="$generated_config"
            SEMSEG_CONFIG_RESOLUTION_SOURCE="generated_existing_profile"
            echo "Using existing generated semseg config: $SEMSEG_DEEPSTREAM_CONFIG_FILE"
            return 0
        fi
        echo "Existing generated semseg config is missing model-engine-file target; regenerating: $generated_config"
    fi

    if [ -z "$semseg_model_file" ] && [ -z "$semseg_engine_file" ]; then
        echo "ERROR: No semseg model/engine configured for auto-generation." >&2
        echo "Set one of:" >&2
        echo "  - --semseg-deepstream-config PATH" >&2
        echo "  - [Inference] semseg_deepstream_config_file in rtp_server/rtp_server_config.ini" >&2
        echo "  - [Inference] semseg_model_file or semseg_engine_file in rtp_server/rtp_server_config.ini" >&2
        echo "  - Place a semseg .engine, .onnx, or .etlt file under yolo/semseg (auto-discovery)" >&2
        return 1
    fi

    echo "SemSeg DeepStream config not provided; generating under yolo/semseg..."
    local generator_cmd=(
        bash "$ROOT_DIR/scripts/yolo/semseg/generate_semseg_config.sh"
        --video-width "$semseg_width"
        --video-height "$semseg_height"
        --infer-width "$semseg_infer_width"
        --infer-height "$semseg_infer_height"
        --profile "$semseg_profile"
    )
    if [ -n "$semseg_model_file" ]; then
        local semseg_model_file_lower
        semseg_model_file_lower="$(echo "$semseg_model_file" | tr '[:upper:]' '[:lower:]')"
        if [[ "$semseg_model_file_lower" == *.etlt ]]; then
            local semseg_effective_model_key="${semseg_model_key:-tlt_encode}"
            generator_cmd+=(--tlt-encoded-model "$semseg_model_file" --tlt-model-key "$semseg_effective_model_key")
        else
            generator_cmd+=(--onnx-file "$semseg_model_file")
        fi
    fi
    if [ -n "$semseg_engine_file" ]; then
        generator_cmd+=(--engine-file "$semseg_engine_file")
    fi
    if [ -n "$semseg_labels_file" ]; then
        generator_cmd+=(--labels-file "$semseg_labels_file")
    fi

    if ! "${generator_cmd[@]}"; then
        echo "ERROR: Failed to auto-generate semseg DeepStream config." >&2
        echo "Set one of:" >&2
        echo "  - --semseg-deepstream-config PATH" >&2
        echo "  - [Inference] semseg_deepstream_config_file in rtp_server/rtp_server_config.ini" >&2
        echo "  - [Inference] semseg_model_file (.onnx/.etlt) or semseg_engine_file for generation" >&2
        return 1
    fi

    if [ ! -f "$generated_config" ]; then
        echo "ERROR: Generated semseg config file is missing: $generated_config" >&2
        return 1
    fi
    SEMSEG_DEEPSTREAM_CONFIG_FILE="$generated_config"
    SEMSEG_CONFIG_RESOLUTION_SOURCE="generated_new_profile"
    echo "Using generated semseg config: $SEMSEG_DEEPSTREAM_CONFIG_FILE"
}

strip_leading_zeros() {
    local value="$1"
    local stripped="${value#"${value%%[!0]*}"}"
    [ -z "$stripped" ] && stripped="0"
    echo "$stripped"
}

discover_pandaset_sequence_dirs() {
    local dataset_root="$1"
    [ -d "$dataset_root" ] || return 0

    local -a candidates=()
    local child
    local folder_name
    shopt -s nullglob
    for child in "$dataset_root"/*; do
        [ -d "$child" ] || continue
        folder_name="$(basename "$child")"
        [[ "$folder_name" == _* ]] && continue
        [[ "$folder_name" =~ ^[0-9]+$ ]] || continue
        validate_pandaset_root_markers "$child" || continue
        candidates+=("$folder_name")
    done
    shopt -u nullglob

    [ "${#candidates[@]}" -eq 0 ] && return 0
    printf '%s\n' "${candidates[@]}" | sort -V
}

