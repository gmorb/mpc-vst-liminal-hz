#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""make_banners.py [page-render.png] -- the repo's images, 1280x640, into docs/images/:
  banner.png (the README's top image and the GitHub social preview), page.png, shape.png, controls.png.
page-render.png: a render of the page with a preset loaded (the kit's preview composed with
mpc-plugin-ui's honest_preview.py, sample text drawn in, preview-only outlines removed).
Data shown is real: knob values of factory presets, and the shape columns of factory "Dream Ping-Pong" from the
engine. Fonts from mpc-vst-plugins (MPC_VST, default ../mpc-vst-plugins)."""
import os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageEnhance
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
F = os.environ.get('MPC_VST', os.path.join(ROOT, '..', 'mpc-vst-plugins')) + '/tools/html_art/fonts/'
A = os.path.join(ROOT, 'vst', 'art') + '/'
OUTD = os.environ.get('BANNER_OUT', os.path.join(ROOT, 'docs', 'images'))
PAGE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'docs', 'images', 'page-render.png')
W,H=1280,640
IV=(232,226,214); GR=(150,145,136); DIM=(112,107,99); AMB=(214,164,92)
fR=lambda s: ImageFont.truetype(F+'TitilliumWeb-Regular.ttf',s); fS=lambda s: ImageFont.truetype(F+'TitilliumWeb-SemiBold.ttf',s); fB=lambda s: ImageFont.truetype(F+'TitilliumWeb-Bold.ttf',s)

def wordmark(d, x, y, size, by=True):
    """LIMINAL (spaced) + Hz (heavier) + by Gm0rb (quiet): returns the right edge"""
    f, fh = fR(size), fS(size)
    track = size * 0.22
    for ch in "LIMINAL":
        d.text((x, y), ch, font=f, fill=(206,198,184), anchor='ls'); x += d.textlength(ch, font=f) + track
    x += size * 0.22
    d.text((x, y), "Hz", font=fh, fill=IV, anchor='ls'); x += d.textlength("Hz", font=fh)
    if by:
        x += size * 0.35; d.text((x, y), "by Gm0rb", font=fR(int(size*0.36)), fill=GR, anchor='ls'); x += d.textlength("by Gm0rb", font=fR(int(size*0.36)))
    return x

def bg(strength=1.0):
    im=Image.open(A+'bg_threshold_air.png').convert('RGB').resize((W,int(628*W/1280)))
    out=Image.new('RGB',(W,H),(9,10,11)); out.paste(im,(0,(H-im.height)//2))
    return ImageEnhance.Brightness(out).enhance(strength)

def vignette(im, k=0.55):
    a=np.asarray(im,float); yy,xx=np.mgrid[0:H,0:W]
    r=np.sqrt(((xx-W/2)/(W/2))**2+((yy-H/2)/(H/2))**2); a*=(1-k*np.clip(r-0.45,0,1)**1.4)[...,None]
    return Image.fromarray(np.clip(a,0,255).astype(np.uint8))

# 1. The page, floating over the room
im=bg(0.9); page=Image.open(PAGE).convert('RGB')
pw=930; ph=int(page.height*pw/page.width); page=page.resize((pw,ph),Image.LANCZOS)
px,py=(W-pw)//2+40, 168
sh=Image.new('RGBA',(W,H),(0,0,0,0)); ImageDraw.Draw(sh).rounded_rectangle([px-6,py+10,px+pw+6,py+ph+22],radius=14,fill=(0,0,0,200))
im=Image.alpha_composite(im.convert('RGBA'),sh.filter(ImageFilter.GaussianBlur(18)))
im.paste(page,(px,py)); d=ImageDraw.Draw(im)
d.rounded_rectangle([px-1,py-1,px+pw,py+ph],radius=3,outline=(255,255,255,40),width=1)
wordmark(d, 64, 104, 54)
d.text((66,146),'Convolution reverb, echoes and strange spaces for Akai Force & MPC',font=fR(24),fill=GR,anchor='ls')
vignette(im.convert('RGB'),0.35).save(os.path.join(OUTD,'page.png'))

# 2. Threshold: the room, the doorway, the name
im=vignette(bg(1.55),0.5); d=ImageDraw.Draw(im)
wordmark(d, 84, 300, 96, by=False)
d.text((88,350),'by Gm0rb',font=fR(30),fill=GR,anchor='ls')
d.text((88,440),'Real rooms, halls and echoes from impulse responses.',font=fR(30),fill=(196,190,178),anchor='ls')
d.text((88,482),'The space for your NAM amp  \u00b7  Akai Force & MPC  \u00b7  TONE3000 IRs',font=fR(22),fill=DIM,anchor='ls')
im.save(os.path.join(OUTD,'banner.png'))

# 3. Impulse: the IR painting, large, with the glow
im=vignette(bg(0.75),0.5).convert('RGBA')
cols=[23,23,14,14,6,5,5,4,4,4,2,0]; cw,chh=52,150; scale=1.75   # factory 'Dream Ping-Pong', from the engine
paint=Image.new('RGBA',(12*56,chh),(0,0,0,0))
for c,v in enumerate(cols): paint.alpha_composite(Image.open(A+'wave4_%d.png'%v),(c*56,0))
pw=int(paint.width*scale); paint=paint.resize((pw,int(chh*scale)),Image.LANCZOS)
ox=(W-pw)//2; oy=200; im.alpha_composite(paint,(ox,oy))
strip=Image.open(A+'glow5_8.png').convert('RGBA'); strip=strip.resize((pw,int(strip.height*scale)),Image.LANCZOS)
im.alpha_composite(strip,(ox,oy+int(chh*scale)+14))
d=ImageDraw.Draw(im)
wordmark(d, ox, 140, 56)
d.text((ox, oy+int(chh*scale)+110),'decay 4.8 s+  \u00b7  bright  \u00b7  darkens  \u00b7  stereo  \u00b7  echoes',font=fS(30),fill=IV,anchor='ls')
d.text((ox, oy+int(chh*scale)+148),'See the shape of every space. Convolution for Akai Force & MPC.',font=fR(22),fill=GR,anchor='ls')
im.convert('RGB').save(os.path.join(OUTD,'shape.png'))

# 4. Controls: the engraved knobs at real values
im=vignette(bg(0.8),0.5).convert('RGBA'); d=ImageDraw.Draw(im)
big=Image.open(A+'knob_big.png'); fw=big.width
knobs=[('Mix','40%',0.40),('Pre-delay','35 ms',0.14),('Feedback','12%',0.13),('Low Cut','80 Hz',0.06),('High Cut','11.0 kHz',0.53),('Width','150%',0.75),('Output','0.0 dB',0.67)]
size=150; gap=(W-120-size*len(knobs))/(len(knobs)-1); x=60
for nm,val,v in knobs:
    f=int(round(v*127)); fr=big.crop((0,f*fw,fw,(f+1)*fw)).resize((size,size),Image.LANCZOS)
    im.alpha_composite(fr,(int(x),250))
    cx=int(x+size/2); d.text((cx,438),nm,font=fS(24),fill=GR,anchor='ms'); d.text((cx,478),val,font=fS(32),fill=IV,anchor='ms')
    x+=size+gap
wordmark(d, 64, 150, 60)
d.text((66,560),'14 factory spaces & echoes  \u00b7  IRs up to 5 s  \u00b7  stretch, reverse, fades  \u00b7  TONE3000 browsing',font=fR(22),fill=DIM,anchor='ls')
im.convert('RGB').save(os.path.join(OUTD,'controls.png'))

print('banners written to', OUTD)
