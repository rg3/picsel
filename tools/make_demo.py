#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds a demo folder of synthetic landscape pictures for picsel screenshots.

usage: make_demo.py OUTDIR

Creates OUTDIR/Pictures/2026-06-14-Lake-Weekend/pending-selection with drawn
(not photographed) lake scenes over two days, some in bursts a few seconds
apart so picsel marks them as similar, plus a short video when ffmpeg is
available. Needs Pillow. Everything is generated, so the pictures can be
published freely.
"""
import math
import os
import random
import shutil
import sqlite3
import subprocess
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 3000, 2000  # landscape; portrait scenes swap them


def lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def gradient(size, top, bottom):
    """Vertical gradient, built as a 1-pixel column and stretched."""
    w, h = size
    col = Image.new("RGB", (1, 256))
    for y in range(256):
        col.putpixel((0, y), lerp(top, bottom, y / 255))
    return col.resize((w, h), Image.BILINEAR)


def ridge(rng, w, base, amp, waves):
    """Mountain outline: a few sines with random phases."""
    terms = [(rng.uniform(0.5, 1.0) / (k + 1), rng.uniform(1, 3) * (k + 1), rng.uniform(0, 6.3))
             for k in range(waves)]
    pts = []
    for i in range(0, 121):
        x = w * i / 120
        y = base - amp * sum(a * math.sin(f * math.pi * x / w + p) for a, f, p in terms)
        pts.append((x, y))
    return pts


def pine(draw, x, ground, height, colour):
    tiers = 4
    for t in range(tiers):
        top = ground - height * (t + 1) / tiers - height * 0.15
        half = height * 0.28 * (1 - t / (tiers + 1))
        draw.polygon([(x - half, ground - height * t / tiers), (x + half, ground - height * t / tiers),
                      (x, top)], fill=colour)
    draw.rectangle([x - height * 0.03, ground - height * 0.1, x + height * 0.03, ground], fill=colour)


def render(scene, variant):
    rng = random.Random(scene["seed"])
    w, h = (H, W) if scene.get("portrait") else (W, H)
    water = round(h * scene.get("water", 0.62))
    img = gradient((w, h), scene["sky"][0], scene["sky"][1])
    draw = ImageDraw.Draw(img)

    if scene.get("stars"):
        srng = random.Random(scene["seed"] + 1)
        for _ in range(500):
            x, y, r = srng.uniform(0, w), srng.uniform(0, water * 0.8), srng.choice((1, 1, 2, 3))
            draw.ellipse([x - r, y - r, x + r, y + r], fill=(235, 235, 255))

    if "sun" in scene:
        sx, sy, sr, sc = scene["sun"]
        sx, sy = sx * w + variant * w * 0.004, sy * h
        glow = Image.new("L", (w // 8, h // 8), 0)
        ImageDraw.Draw(glow).ellipse([(sx - sr * 4) / 8, (sy - sr * 4) / 8, (sx + sr * 4) / 8, (sy + sr * 4) / 8],
                                     fill=110)
        glow = glow.filter(ImageFilter.GaussianBlur(sr / 4)).resize((w, h), Image.BILINEAR)
        img = Image.composite(Image.new("RGB", (w, h), sc), img, glow)
        draw = ImageDraw.Draw(img)
        draw.ellipse([sx - sr, sy - sr, sx + sr, sy + sr], fill=sc)

    if scene.get("clouds"):
        clouds = Image.new("L", (w // 8, h // 8), 0)
        cd = ImageDraw.Draw(clouds)
        crng = random.Random(scene["seed"] + 2)
        for _ in range(scene["clouds"]):
            cx, cy = crng.uniform(0, w / 8), crng.uniform(h / 80, water / 8 * 0.6)
            for _ in range(6):
                rx, ry = crng.uniform(15, 45), crng.uniform(6, 14)
                ox, oy = crng.uniform(-40, 40), crng.uniform(-6, 6)
                cd.ellipse([cx + ox - rx, cy + oy - ry, cx + ox + rx, cy + oy + ry], fill=200)
        clouds = clouds.filter(ImageFilter.GaussianBlur(6)).resize((w, h), Image.BILINEAR)
        img = Image.composite(Image.new("RGB", (w, h), scene.get("cloud", (245, 245, 250))), img, clouds)
        draw = ImageDraw.Draw(img)

    # Mountain ranges, far to near.
    for i, colour in enumerate(scene["hills"]):
        base = water - h * 0.02 * (len(scene["hills"]) - i - 1)
        amp = h * (0.16 - 0.035 * i)
        pts = ridge(rng, w, base - h * 0.06, amp, 4)
        draw.polygon(pts + [(w, water), (0, water)], fill=colour)

    # Shore with pines.
    shore = scene["hills"][-1]
    trees = scene.get("trees", 0)
    for i in range(trees):
        x = rng.uniform(-0.02, 1.02) * w
        pine(draw, x, water + 2, rng.uniform(0.09, 0.2) * h * scene.get("tree_scale", 1), shore)

    # Lake: the scene above the waterline, mirrored, darker and rippled.
    above = img.crop((0, max(0, 2 * water - h), w, water))
    refl = above.transpose(Image.FLIP_TOP_BOTTOM).resize((w, h - water))
    refl = ImageChops.multiply(refl, Image.new("RGB", refl.size, scene.get("water_tint", (170, 185, 200))))
    refl = refl.filter(ImageFilter.GaussianBlur(4))
    rd = ImageDraw.Draw(refl)
    for _ in range(260):
        y = rng.uniform(0, h - water)
        x = rng.uniform(0, w)
        length = rng.uniform(40, 260) * (0.4 + y / (h - water))
        rd.line([x, y, x + length, y], fill=lerp(scene["sky"][1], (255, 255, 255), 0.3), width=2)
    img.paste(refl, (0, water))
    draw = ImageDraw.Draw(img)

    if scene.get("boat"):
        bx = w * (0.3 + 0.03 * variant)
        by = water + (h - water) * 0.35
        draw.polygon([(bx - 110, by), (bx + 110, by), (bx + 80, by + 40), (bx - 80, by + 40)], fill=(70, 45, 35))
        draw.polygon([(bx - 5, by), (bx - 5, by - 260), (bx + 140, by - 10)], fill=(240, 236, 225))
        draw.line([bx - 5, by, bx - 5, by - 270], fill=(60, 40, 30), width=6)

    if scene.get("mist"):
        mist = gradient((w, h), (0, 0, 0), (0, 0, 0)).convert("L")
        md = ImageDraw.Draw(mist)
        md.rectangle([0, water - h * 0.12, w, water + h * 0.05], fill=150)
        mist = mist.filter(ImageFilter.GaussianBlur(h * 0.05))
        img = Image.composite(Image.new("RGB", (w, h), (240, 228, 235)), img, mist)

    # Slight framing differences inside a burst.
    if variant:
        dx, dy = round(w * 0.006 * variant), round(h * 0.004 * variant)
        img = img.crop((dx, dy, w - dx, h - dy)).resize((w, h), Image.BICUBIC)
    return img


SCENES = [
    # name, date, burst count, scene
    ("DSC_0412.JPG", "2026:06:13 09:12:03", 1, dict(seed=11, sky=((96, 156, 222), (196, 222, 240)),
     sun=(0.78, 0.16, 70, (255, 250, 225)), clouds=6,
     hills=[(128, 150, 170), (82, 118, 96), (44, 82, 58)], trees=14)),
    ("DSC_0418.JPG", "2026:06:13 11:40:10", 3, dict(seed=12, sky=((70, 140, 220), (180, 215, 240)),
     clouds=9, hills=[(110, 140, 160), (60, 100, 80)], trees=6, boat=True, water=0.55)),
    ("DSC_0425.JPG", "2026:06:13 12:05:41", 1, dict(seed=13, portrait=True, sky=((88, 150, 215), (205, 225, 238)),
     hills=[(140, 160, 180), (70, 105, 85), (30, 66, 44)], trees=10, tree_scale=2.2, water=0.7)),
    ("DSC_0431.JPG", "2026:06:13 13:30:17", 1, dict(seed=14, sky=((120, 170, 220), (225, 232, 240)),
     clouds=16, hills=[(150, 160, 175), (100, 120, 110)], trees=4)),
    ("DSC_0447.JPG", "2026:06:13 19:48:20", 2, dict(seed=15, sky=((236, 170, 92), (252, 222, 160)),
     sun=(0.62, 0.38, 90, (255, 236, 170)), hills=[(196, 128, 96), (128, 76, 66), (70, 42, 44)], trees=8,
     water_tint=(230, 190, 170))),
    ("DSC_0452.JPG", "2026:06:13 20:31:05", 1, dict(seed=16, sky=((70, 50, 120), (246, 120, 80)),
     sun=(0.42, 0.56, 110, (255, 170, 90)), hills=[(110, 60, 100), (60, 30, 60)], trees=12,
     water_tint=(200, 150, 170), water=0.6)),
    ("DSC_0460.JPG", "2026:06:13 22:40:33", 1, dict(seed=17, sky=((8, 12, 40), (30, 50, 96)), stars=True,
     sun=(0.2, 0.18, 55, (240, 240, 228)), hills=[(28, 36, 64), (14, 20, 40)], trees=10,
     water_tint=(140, 150, 190))),
    ("DSC_0466.JPG", "2026:06:14 06:15:01", 3, dict(seed=18, sky=((150, 140, 196), (250, 200, 196)),
     sun=(0.7, 0.5, 60, (255, 226, 200)), hills=[(170, 150, 180), (120, 110, 140), (80, 76, 100)], trees=9,
     mist=True, water_tint=(220, 200, 215))),
    ("DSC_0473.JPG", "2026:06:14 08:52:44", 1, dict(seed=19, portrait=True, sky=((110, 170, 225), (215, 232, 242)),
     clouds=5, hills=[(120, 150, 175), (50, 96, 70)], trees=7, tree_scale=2.6, water=0.74)),
    ("DSC_0480.JPG", "2026:06:14 12:10:09", 1, dict(seed=20, sky=((40, 130, 200), (160, 210, 230)),
     sun=(0.15, 0.12, 60, (255, 252, 235)), hills=[(90, 140, 150), (40, 110, 100), (20, 70, 60)], trees=16,
     water_tint=(140, 200, 200))),
    ("DSC_0491.JPG", "2026:06:14 16:02:58", 1, dict(seed=21, sky=((92, 100, 112), (170, 176, 182)),
     clouds=22, cloud=(120, 126, 136), hills=[(90, 98, 108), (56, 66, 70)], trees=11,
     water_tint=(150, 160, 170))),
    ("DSC_0497.JPG", "2026:06:14 19:30:12", 1, dict(seed=22, sky=((60, 90, 160), (240, 170, 120)),
     sun=(0.85, 0.45, 75, (255, 200, 130)), clouds=5, cloud=(250, 190, 170),
     hills=[(120, 90, 120), (70, 56, 80), (36, 30, 46)], trees=6, water_tint=(210, 170, 170))),
]
VIDEO = ("DSC_0485.MOV", "2026-06-14T12:31:40Z", 10)  # name, creation time, scene index


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    folder = os.path.join(os.path.abspath(sys.argv[1]), "Pictures", "2026-06-14-Lake-Weekend")
    dest = os.path.join(folder, "pending-selection")
    if os.path.exists(folder):
        shutil.rmtree(folder)
    os.makedirs(dest)

    stills = []
    for name, date, burst, scene in SCENES:
        stem, ext = os.path.splitext(name)
        number = int(stem[4:])
        hh, mm, ss = (int(x) for x in date[11:].split(":"))
        for v in range(burst):
            path = os.path.join(dest, f"DSC_{number + v:04d}{ext}")
            img = render(scene, v)
            img.save(path, quality=88)
            t = ss + 2 * v
            stamp = f"{date[:11]}{hh:02d}:{mm:02d}:{t:02d}"
            subprocess.run([sys.executable, "-I", os.path.join(HERE, "set_exif.py"), "--date", stamp,
                            "--make", "Demo", "--model", "Demo Camera", path], check=True)
            if v == 0:
                stills.append(path)

    name, when, index = VIDEO
    if shutil.which("ffmpeg"):
        # A slow zoom into one of the scenes.
        subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-y", "-loop", "1", "-i", stills[index],
                        "-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo", "-t", "4",
                        "-vf", "scale=3840:2560,zoompan=z='1+0.0015*on':d=120:s=1920x1080:fps=30",
                        "-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p", "-c:a", "aac",
                        "-shortest", "-metadata", f"creation_time={when}", os.path.join(dest, name)],
                       check=True)
    else:
        print("ffmpeg not found: no video", file=sys.stderr)

    subprocess.run([sys.executable, "-I", os.path.join(HERE, "init_db.py"), dest, folder], check=True)
    db = sqlite3.connect(os.path.join(dest, "picsel.db"))
    db.execute("INSERT OR REPLACE INTO config VALUES('sort_mode', 'date')")
    db.commit()
    print(dest)


if __name__ == "__main__":
    main()
