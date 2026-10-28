#!/usr/bin/env python3
"""Generate lossless PS5 button artwork and fallback BC3 mips from geometry.

Needs Pillow. No game artwork is copied into the generated header; only the
fingerprints of the USA button textures identify which original to replace.
"""
import argparse
import json
import pathlib
import struct
from PIL import Image, ImageDraw, ImageFont, ImageChops

ROOT = pathlib.Path(__file__).resolve().parents[3]
SCALE = 8


# Shared geometry is specified in the logical 32px texture coordinate space.
# Paths use round caps; the triangle uses joined edge planes. Pixel-centre sampling.
FACE_RADIUS = 12.15
RIM_STROKE = 1.45
SYMBOL_STROKE = 1.60
TRIANGLE_RADIUS = 7.6
TRIANGLE_OPTICAL_Y = .65


def artwork(name, output_size=32):
    import math
    size = 32*SCALE
    mask = Image.new('L', (size, size))
    fill = Image.new('L', (size, size))

    def render(distance, stroke=None):
        pixels=bytearray(size*size)
        half=stroke/2 if stroke is not None else 0
        for y in range(size):
            for x in range(size):
                value=distance((x+.5)/SCALE,(y+.5)/SCALE)
                # Coverage at the high-resolution edge, before downsampling.
                coverage=.5-(abs(value)-half)*SCALE if stroke is not None else .5-value*SCALE
                pixels[y*size+x]=round(max(0,min(1,coverage))*255)
        return Image.frombytes('L',(size,size),bytes(pixels))

    def circle(cx,cy,r):
        return lambda x,y:math.hypot(x-cx,y-cy)-r

    def rounded_box(cx,cy,w,h,r):
        def distance(x,y):
            qx=abs(x-cx)-(w/2-r);qy=abs(y-cy)-(h/2-r)
            return math.hypot(max(qx,0),max(qy,0))+min(max(qx,qy),0)-r
        return distance

    def add(shape,stroke=SYMBOL_STROKE):
        nonlocal mask
        mask=ImageChops.lighter(mask,render(shape,stroke))

    def cap(shape,stroke=RIM_STROKE):
        nonlocal fill
        fill=ImageChops.lighter(fill,render(shape))
        add(shape,stroke)

    def path(points,closed=False,stroke=SYMBOL_STROKE):
        segments=list(zip(points,points[1:]+points[:1] if closed else points[1:]))
        def distance(x,y):
            best=float('inf')
            for (ax,ay),(bx,by) in segments:
                dx,dy=bx-ax,by-ay
                t=max(0,min(1,((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy)))
                best=min(best,math.hypot(x-ax-t*dx,y-ay-t*dy))
            return best
        add(distance,stroke)

    def text(value,font_size,cx=16,cy=16):
        # Centre actual visible ink, not font ascent/descent or advance width.
        font=ImageFont.truetype(str(ROOT/'runtime/thirdparty/imgui/fonts/Roboto-Medium.ttf'),round(font_size*SCALE))
        draw=ImageDraw.Draw(mask);bounds=draw.textbbox((0,0),value,font=font)
        draw.text((cx*SCALE-(bounds[0]+bounds[2])/2,cy*SCALE-(bounds[1]+bounds[3])/2),value,font=font,fill=255)

    if name in ('Cross','Circle','Square','Triangle','PS'):
        cap(circle(16,16,FACE_RADIUS))
    if name=='Cross':
        path([(10.7,10.7),(21.3,21.3)])
        path([(21.3,10.7),(10.7,21.3)])
    elif name=='Circle':
        add(circle(16,16,6.65))
    elif name=='Square':
        add(rounded_box(16,16,11.4,11.4,.35))
    elif name=='Triangle':
        # A joined polygon outline, not three round-capped segments. Signed
        # edge planes keep all sides the same width and meet at clean mitres.
        # The optical correction centres the visible symbol inside the keycap.
        def triangle(x,y):
            dx,dy=x-16,y-16-TRIANGLE_OPTICAL_Y
            return max(dy-TRIANGLE_RADIUS/2,
                       (math.sqrt(3)*dx-dy-TRIANGLE_RADIUS)/2,
                       (-math.sqrt(3)*dx-dy-TRIANGLE_RADIUS)/2)
        add(triangle,stroke=1.4)
    elif name in ('L1','L2','R1','R2'):
        cap(rounded_box(16,16,25,17,3.5))
        text(name,14.0)
    elif name=='R3':
        cap(circle(16,18.5,9.8))
        path([(16,2),(16,6)],stroke=1.5)
        path([(12.7,3.3),(16,6.6),(19.3,3.3)],stroke=1.5)
        text('R3',11.3,cy=18.5)
    elif name=='Touchpad':
        cap(rounded_box(16,16,27,17,3.1))
        for y in (12,16,20):
            for x in (8,12,16,20,24):
                add(circle(x,y,.34),stroke=None)
    elif name=='Options':
        cap(rounded_box(16,16,14,25,6.7))
        for y in (11.5,16,20.5):path([(12.5,y),(19.5,y)],stroke=1.35)
    elif name=='PS':
        text('PS',12.2)
    else:
        raise ValueError(name)
    rgba=Image.new('RGBA',(size,size),(12,14,18,0))
    rgba.putalpha(fill.point(lambda v:round(v*235/255)))
    rgba.paste(Image.new('RGBA',(size,size),(255,255,255,255)),(0,0),mask)
    return rgba.resize((output_size,output_size),Image.Resampling.LANCZOS)


def alpha_block(values):
    """Fit BC3's two alpha modes, retaining exact 0/255 when that fits best.

    Min/max alone quantized a translucent 235-alpha keycap to218or255.
    Fitting the six-step mode keeps that fill and the opaque rim distinct.
    """
    def palette(a,b):
        return [a,b]+[((7-k)*a+k*b)//7 for k in range(1,7)] if a>b else [a,b]+[((5-k)*a+k*b)//5 for k in range(1,5)]+[0,255]
    def indices(a,b):
        p=palette(a,b)
        return [min(range(8),key=lambda k:abs(v-p[k])) for v in values]
    def fit(a,b,mode):
        for _ in range(3):
            aa=ab=bb=av=bv=0.
            for v,index in zip(values,indices(a,b)):
                if index==0:u,w=1.,0.
                elif index==1:u,w=0.,1.
                elif mode==8:u,w=(8-index)/7,(index-1)/7
                elif index<6:u,w=(6-index)/5,(index-1)/5
                else:continue  # six-step mode's exact transparent/opaque entries
                aa+=u*u;ab+=u*w;bb+=w*w;av+=u*v;bv+=w*v
            det=aa*bb-ab*ab
            if det<=1e-8:break
            x=max(0,min(255,round((av*bb-bv*ab)/det)))
            y=max(0,min(255,round((bv*aa-av*ab)/det)))
            if (mode==8 and x<=y) or (mode==6 and x>y):break
            a,b=x,y
        return a,b
    hi,lo=max(values),min(values)
    middle=sorted(v for v in values if 0<v<255)
    candidates=[(hi,lo)]
    if hi>lo:candidates.append(fit(hi,lo,8))
    if middle:
        a,b=middle[0],middle[-1]
        candidates.extend([(a,b),fit(a,b,6)])
    def error(pair):
        p=palette(*pair)
        return sum(min(abs(v-c) for c in p) for v in values)
    a,b=min(candidates,key=error)
    bits=sum(k<<(3*i) for i,k in enumerate(indices(a,b)))
    return bytes((a,b))+bits.to_bytes(6,'little')


def bc3(image):
    """BC3 with independent alpha and RGB565 endpoints per block."""
    rgba=image.convert('RGBA');w,h=rgba.size;out=bytearray()
    for y in range(0,h,4):
        for x in range(0,w,4):
            pixels=[rgba.getpixel((min(x+dx,w-1),min(y+dy,h-1))) for dy in range(4) for dx in range(4)]
            values=[p[3] for p in pixels]
            alpha=alpha_block(values)
            # The UI artwork is white/near-black. Luminance endpoints preserve
            # the keycap fill while BC3 alpha handles antialiased edges.
            visible=[p for p in pixels if p[3]>8] or [(255,255,255,0)]
            def rgb565(p):return (round(p[0]*31/255)<<11)|(round(p[1]*63/255)<<5)|round(p[2]*31/255)
            c0=rgb565(max(visible,key=lambda p:sum(p[:3])));c1=rgb565(min(visible,key=lambda p:sum(p[:3])))
            def rgb(c):return ((c>>11)*255//31,((c>>5)&63)*255//63,(c&31)*255//31)
            p0,p1=rgb(c0),rgb(c1)
            colors=[p0,p1,tuple((2*a+b)//3 for a,b in zip(p0,p1)),tuple((a+2*b)//3 for a,b in zip(p0,p1))]
            indices=sum(min(range(4),key=lambda j:sum((p[k]-colors[j][k])**2 for k in range(3))) << (2*i) for i,p in enumerate(pixels))
            out.extend(alpha+struct.pack('<HHI',c0,c1,indices))
    return bytes(out)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--preview',type=pathlib.Path)
    a=p.parse_args()
    manifest=json.loads((ROOT/'port/ps5/assets/glyph-fingerprints.json').read_text())
    names=list(dict.fromkeys(x['ps5'] for x in manifest['textures']))
    lines=['// Generated by port/ps5/tools/generate-glyphs.py. Do not edit.',
           '// Original geometric artwork. BC3 keycaps, complete 32..1 mip chain.', '#pragma once',
           '#include <cstdint>', '#include <cstddef>', 'namespace ps5::glyphs {',
           'inline constexpr size_t kMipOffsets[] = { 0, 1024, 1280, 1344, 1360, 1376 };',
           'inline constexpr size_t kMipBytes[] = { 1024, 256, 64, 16, 16, 16 };',
           'struct Glyph { const char* name; uint8_t data[1392]; };', 'inline constexpr Glyph kGlyphs[] = {']
    lossless=bytearray(struct.pack('<4sIII',b'PSG1',128,len(names),8))
    preview=Image.new('RGB',(len(names)*96,120),(32,32,32));draw=ImageDraw.Draw(preview)
    for i,name in enumerate(names):
        high=artwork(name,128)
        for level in range(8):
            lossless.extend(high.resize((max(128>>level,1),)*2,Image.Resampling.LANCZOS).tobytes())
        im=artwork(name); data=b''.join(bc3(im.resize((max(32>>l,1),)*2,Image.Resampling.LANCZOS)) for l in range(6))
        assert len(data)==1392
        lines.append('    { "'+name+'", {')
        for start in range(0,len(data),24):lines.append('        '+','.join(f'0x{v:02x}' for v in data[start:start+24])+',')
        lines.append('    } },')
        white=high.resize((80,80),Image.Resampling.LANCZOS)
        preview.paste(white,(i*96+8,24),white);draw.text((i*96+6,6),name,fill='white')
    lines.extend(['};','struct Match { uint64_t hash; size_t glyph; };','inline constexpr Match kMatches[] = {'])
    for item in manifest['textures']:
        lines.append(f'    {{ 0x{item["hash"]}ull, {names.index(item["ps5"])} }}, // {item["texture"]}')
    lines.extend(['};','''// Hash only the 8x8 active BC3 blocks, in their original 8-in-16 byte
// order. Tiled padding is deliberately ignored: allocation contents vary.
inline uint64_t Fingerprint(const uint8_t* tiled)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (uint32_t y = 0; y < 8; ++y)
        for (uint32_t x = 0; x < 8; ++x)
        {
            const uint32_t oi = ((((y >> 1) & 7) << 3) | (x & 7)) << 4;
            const uint32_t pipe = ((y >> 3) & 1) << 1;
            const uint32_t at = ((y & 1) << 4) | (pipe << 6) | (oi & 15) |
                (((oi >> 4) & 1) << 5) | (((oi >> 5) & 7) << 8) | ((oi >> 8) << 12);
            for (uint32_t k = 0; k < 16; ++k)
                hash = (hash ^ tiled[at + k]) * 0x100000001b3ull;
        }
    return hash;
}
inline const Glyph* Find(const uint8_t* tiled)
{
    const uint64_t hash = Fingerprint(tiled);
    for (const auto& match : kMatches)
        if (match.hash == hash)
            return &kGlyphs[match.glyph];
    return nullptr;
}
} // namespace ps5::glyphs'''])
    (ROOT/'port/ps5/assets/glyphs.rgba').write_bytes(lossless)
    (ROOT/'port/ps5/src/ps5_glyphs.h').write_text('\n'.join(lines)+'\n')
    if a.preview:
        a.preview.parent.mkdir(parents=True,exist_ok=True);preview.save(a.preview)
    print(f'{len(names)} PS5 glyphs, {len(manifest["textures"])} source matches, 128px RGBA / 8 mips and 32px BC3 fallback / 6 mips')

if __name__=='__main__': main()
