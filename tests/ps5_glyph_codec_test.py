#!/usr/bin/env python3
"""Decode generated BC3 with Pillow, independently of the encoder."""
import importlib.util
import io
import json
import pathlib
import struct
import unittest
from PIL import Image, ImageChops, ImageStat

ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('glyphs',ROOT/'port/ps5/tools/generate-glyphs.py')
glyphs=importlib.util.module_from_spec(spec);spec.loader.exec_module(glyphs)

class CodecTest(unittest.TestCase):
    def test_shipped_lossless_asset_matches_geometry_and_manifest_order(self):
        manifest=json.loads((ROOT/'port/ps5/assets/glyph-fingerprints.json').read_text())
        names=list(dict.fromkeys(item['ps5'] for item in manifest['textures']))
        data=(ROOT/'port/ps5/assets/glyphs.rgba').read_bytes()
        self.assertEqual(struct.unpack('<4sIII',data[:16]),(b'PSG1',128,len(names),8))
        offset=16
        for name in names:
            image=glyphs.artwork(name,128)
            for level in range(8):
                size=128>>level
                with self.subTest(name=name,level=level):
                    expected=image.resize((size,size),Image.Resampling.LANCZOS).tobytes()
                    self.assertEqual(data[offset:offset+len(expected)],expected)
                    offset+=len(expected)
        self.assertEqual(len(data),offset)

    def test_all_symbols_and_mips(self):
        names=('Cross','Circle','Square','Triangle','L1','L2','R1','R2','Options','Touchpad','R3','PS')
        for name in names:
            artwork=glyphs.artwork(name)
            for level in range(6):
                with self.subTest(name=name,level=level):
                    size=max(32>>level,1)
                    reference=artwork.resize((size,size),Image.Resampling.LANCZOS)
                    data=glyphs.bc3(reference)
                    header=(b'DDS '+struct.pack('<7I',124,0x81007,size,size,len(data),0,1)+bytes(44)
                            +struct.pack('<II4s5I',32,4,b'DXT5',0,0,0,0,0)+struct.pack('<5I',0x1000,0,0,0,0))
                    decoded=Image.open(io.BytesIO(header+data)).convert('RGBA')
                    error=ImageStat.Stat(ImageChops.difference(decoded.getchannel('A'),reference.getchannel('A'))).mean[0]
                    self.assertLess(error,9)
                    # Compare RGB only where visible; transparent RGB is irrelevant.
                    mask=reference.getchannel('A').point(lambda a: 255 if a>128 else 0)
                    for channel in 'RGB':
                        diff=ImageChops.difference(decoded.getchannel(channel),reference.getchannel(channel))
                        diff=ImageChops.multiply(diff,mask)
                        self.assertLess(ImageStat.Stat(diff).mean[0],18)

if __name__=='__main__':unittest.main()
