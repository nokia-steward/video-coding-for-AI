#!/usr/bin/env python3
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

"""
Export the adaptive luma input analyzer PyTorch checkpoint to ONNX for use by encoder_cli.

Usage:
  python export_input_analyzer_onnx.py <path_to_model_weights.pt> [output.onnx]

Requires: torch, torchvision (pip install torch torchvision).

The exported ONNX has two inputs:
  - image: float32 [1, 4, 512, 512] (RGB normalized + QP channel, NCHW)
  - size:  float32 [1, 2] (width/1024, height/1024)

One output: float32 [1, 2] (downscale factor, backscaling_ratio after sigmoid).
"""

import sys
from pathlib import Path

# Optional: add aidoc to path for model_library (same layout as input_analyzer_example.py)
SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent
AIDOC = PROJECT_ROOT / "aidoc"
if AIDOC.exists():
    sys.path.insert(0, str(AIDOC))

def main():
    if len(sys.argv) < 2:
        print("Usage: export_input_analyzer_onnx.py <model_weights.pt> [output.onnx]", file=sys.stderr)
        sys.exit(1)
    pt_path = Path(sys.argv[1])
    if not pt_path.is_file():
        print(f"Error: not a file: {pt_path}", file=sys.stderr)
        sys.exit(1)
    onnx_path = Path(sys.argv[2]) if len(sys.argv) > 2 else pt_path.with_suffix(".onnx")

    import torch
    import torch.nn as nn

    # Replicate the model layout from aidoc/input_analyzer_example.py
    class RoundClamp_02_10(torch.autograd.Function):
        @staticmethod
        def forward(ctx, input):
            clamped = torch.clamp(input, 0.2, 1.0)
            rounded = torch.round(clamped, decimals=1)
            factor = rounded / clamped
            ctx.save_for_backward(factor)
            return rounded

    class ClampRoundEstimator(nn.Module):
        def forward(self, x):
            return RoundClamp_02_10.apply(x)

    class CustomSigmoid(nn.Module):
        def __init__(self, slope=5):
            super().__init__()
            self.slope = slope
        def forward(self, x):
            return torch.sigmoid(self.slope * x)

    from torchvision.models import shufflenet_v2_x0_5, ShuffleNet_V2_X0_5_Weights
    weights = ShuffleNet_V2_X0_5_Weights.DEFAULT
    backbone_model = shufflenet_v2_x0_5(weights=weights)
    model_headless = torch.nn.Sequential(*(list(backbone_model.children())[:-1]))
    model_headless[0][0] = nn.Conv2d(4, 24, kernel_size=(3, 3), stride=(2, 2), padding=(1, 1), bias=False)

    class InputAnalyzerExport(nn.Module):
        """Takes 4ch image and size; outputs (d, backscaling_ratio)."""
        def __init__(self):
            super().__init__()
            self.backbone = model_headless
            self.size_prompter = nn.Sequential(
                nn.Linear(2, 4, bias=True), nn.ReLU(inplace=True), nn.Linear(4, 4, bias=True)
            )
            self.head = nn.Sequential(
                nn.Linear(1024 + 4, 500, bias=True), nn.ReLU(inplace=True), nn.Linear(500, 2, bias=True)
            )
            self.rounder = ClampRoundEstimator()
            self.sigmoid = CustomSigmoid()

        def forward(self, image_4ch, size):
            # image_4ch: [B, 4, 512, 512], size: [B, 2]
            features = self.backbone(image_4ch)
            mean_features = features.mean([2, 3])
            size_prompt = self.size_prompter(size)
            output = self.head(torch.cat([mean_features, size_prompt], dim=1))
            out1 = self.rounder(output[:, 0])
            out2 = self.sigmoid(output[:, 1])
            return torch.stack([out1, out2], dim=1)

    model = InputAnalyzerExport()
    checkpoint = torch.load(pt_path, map_location="cpu", weights_only=False)
    # Original model has backbone + size_prompter + head + rounder + sigmoid; state_dict keys may match
    state = checkpoint.get("model_state_dict")
    if state is None:
        print("Error: checkpoint has no 'model_state_dict'", file=sys.stderr)
        sys.exit(1)
    # Load with strict=False to allow minor key mismatch (e.g. original has different first layer)
    model.load_state_dict(state, strict=False)
    model.eval()

    dummy_image = torch.randn(1, 4, 512, 512)
    dummy_size = torch.tensor([[1920.0 / 1024.0, 1080.0 / 1024.0]])
    torch.onnx.export(
        model,
        (dummy_image, dummy_size),
        str(onnx_path),
        input_names=["image", "size"],
        output_names=["output"],
        dynamic_axes=None,
        opset_version=14,
    )
    print(f"Exported: {onnx_path}")

if __name__ == "__main__":
    main()
