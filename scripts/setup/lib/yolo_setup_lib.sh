#!/bin/bash

# Shared helpers for YOLO readiness checks used by runtime setup scripts.

set -euo pipefail

sf_read_ini_value() {
    local config_file="$1"
    local key_name="$2"

    if [[ ! -f "${config_file}" ]]; then
        return 0
    fi

    awk -F'=' -v key="${key_name}" '
        /^[[:space:]]*#/ { next }
        $1 ~ "^[[:space:]]*" key "[[:space:]]*$" {
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2)
            print $2
            exit
        }
    ' "${config_file}" || true
}

sf_detect_yolo_detection_config() {
    local config_file="$1"

    local detection_config
    detection_config="$(sf_read_ini_value "${config_file}" "deepstream_config_file")"
    if [[ -z "${detection_config}" ]]; then
        detection_config="$(sf_read_ini_value "${config_file}" "detection_config_file")"
    fi

    printf "%s" "${detection_config}"
}

sf_resolve_yolo_version() {
    local config_file="$1"
    local detection_config="$2"

    local yolo_version
    yolo_version="$(sf_read_ini_value "${config_file}" "yolo_version")"
    if [[ -n "${yolo_version}" ]]; then
        printf "%s" "${yolo_version}"
        return 0
    fi

    local from_path=""
    from_path="$(basename "${detection_config}" | sed -n 's/^yolo_config_\([^\.]*\)\.yml$/\1/p' || true)"
    if [[ -n "${from_path}" ]]; then
        printf "%s" "${from_path}"
        return 0
    fi

    printf "8n"
}

sf_create_coco_labels_file() {
    local labels_file="$1"

    if [[ -f "${labels_file}" ]]; then
        return 0
    fi

    echo "Creating COCO labels file: ${labels_file}"
    cat > "${labels_file}" << 'EOF'
person
bicycle
car
motorcycle
airplane
bus
train
truck
boat
traffic light
fire hydrant
stop sign
parking meter
bench
bird
cat
dog
horse
sheep
cow
elephant
bear
zebra
giraffe
backpack
umbrella
handbag
tie
suitcase
frisbee
skis
snowboard
sports ball
kite
baseball bat
baseball glove
skateboard
surfboard
tennis racket
bottle
wine glass
cup
fork
knife
spoon
bowl
banana
apple
sandwich
orange
broccoli
carrot
hot dog
pizza
donut
cake
chair
couch
potted plant
bed
dining table
toilet
tv
laptop
mouse
remote
keyboard
cell phone
microwave
oven
toaster
sink
refrigerator
book
clock
vase
scissors
teddy bear
hair drier
toothbrush
EOF
    echo "COCO labels file created"
}

sf_ensure_yolo_assets() {
    local width="$1"
    local height="$2"
    local yolo_version="$3"
    local vcai_path="$4"

    local yolo_dir="${vcai_path}/yolo"
    mkdir -p "${yolo_dir}"

    local yolo_width=$(( (width + 31) / 32 * 32 ))
    local yolo_height=$(( (height + 31) / 32 * 32 ))

    local yolo_lib="${yolo_dir}/DeepStream-Yolo/nvdsinfer_custom_impl_Yolo/libnvdsinfer_custom_impl_Yolo.so"
    local onnx_model="${yolo_dir}/yolov${yolo_version}_${yolo_width}x${yolo_height}.onnx"
    local labels_file="${yolo_dir}/labels.txt"
    local engine_file="${yolo_dir}/yolov${yolo_version}_${yolo_width}x${yolo_height}.onnx_b1_gpu0_fp16.engine"

    local setup_yolo_script="${vcai_path}/scripts/yolo/setup_yolo.sh"
    local convert_model_script="${vcai_path}/scripts/yolo/convert_yolo_model.sh"

    if [[ ! -f "${yolo_lib}" ]]; then
        echo "DeepStream-Yolo library not found: ${yolo_lib}"
        echo "Running setup_yolo.sh to compile the library..."
        if [[ ! -f "${setup_yolo_script}" ]]; then
            echo "Error: setup_yolo.sh not found at ${setup_yolo_script}"
            return 1
        fi
        "${setup_yolo_script}"
    else
        echo "DeepStream-Yolo library found"
    fi

    if [[ ! -f "${labels_file}" ]]; then
        echo "Labels file not found: ${labels_file}"
        sf_create_coco_labels_file "${labels_file}"
    else
        echo "Labels file found"
    fi

    if [[ ! -f "${onnx_model}" ]]; then
        echo "YOLO ONNX model not found: ${onnx_model}"
        echo "Creating model for dimensions ${width}x${height} (YOLO-compatible: ${yolo_width}x${yolo_height})..."
        if [[ ! -f "${convert_model_script}" ]]; then
            echo "Error: convert_yolo_model.sh not found at ${convert_model_script}"
            return 1
        fi
        "${convert_model_script}" "${width}" "${height}" "${yolo_dir}" "${yolo_version}"
    fi

    if [[ ! -f "${onnx_model}" ]]; then
        echo "Error: YOLO ONNX model still missing after conversion: ${onnx_model}"
        echo "Please ensure the DeepStream export environment is set up:"
        echo "  ${vcai_path}/scripts/setup/bootstrap.sh ds-env"
        return 1
    fi
    echo "YOLO ONNX model found"

    if [[ ! -f "${engine_file}" ]]; then
        echo "TensorRT engine file not found: ${engine_file}"
        echo "Note: Engine will be automatically generated on first run (may take several minutes)"
    else
        echo "TensorRT engine file found"
    fi

    echo "YOLO setup check complete"
}
