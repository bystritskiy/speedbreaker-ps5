#!/usr/bin/env python3
"""Convert the approved imagegen cutout to the game's byte order (no edits).
The C++ runtime composes exact native leader lines and its mip chain.
"""
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parents[3]
image=Image.open(root/'port/ps5/assets/dualsense.png').convert('RGBA')
image=image.resize((256,256),Image.Resampling.LANCZOS)
(root/'port/ps5/assets/dualsense.bgra').write_bytes(image.tobytes('raw','BGRA'))
print('DualSense: 256x256 BGRA, 262144 bytes')
