#!/usr/bin/env python3
"""Measures each victorian dial's tick-arc ANGULAR EXTENT directly from
brand/mocks/victorian/master-01-base.png. This design's dials carry NO
numerals (see brand/mocks/victorian/prompts.md's "IMPORTANT SCALE
CORRECTION" note: numerals were deliberately dropped from the production
master, ticks only) - so, unlike sibling basilica-audio/aureate's tubecomp
dial (which has 9 printed labels to calibrate a per-label table against),
this script measures the tick arc's overall angular SPAN and its start/end
angle only, which PluginEditorLayout.h then maps LINEARLY across each
meter's own chosen dB range (see that file's own per-dial docs for the
linear-mapping rationale and the honest "not a calibrated multi-point
table" caveat this implies).

Method
------
1. Ticks rotate/read relative to the NEEDLE's own measured hub pivot
   (components/needle-*.json's pivotXInMaster/pivotYInMaster) - this
   design's pivot sits well below the dial's own geometric centre (the
   visible brass gear hub), and the tick arc is laid out CONCENTRIC with
   that same pivot (standard analog-meter geometry: the needle tip must
   sweep along the tick arc as it rotates about its own off-centre pivot),
   not concentric with the dial face's geometric centre - confirmed
   visually (overlay_*_band.png, generated during development).
2. For a swept range of candidate radii (from the pivot), this script
   samples colour-distance-from-background along a wide angular arc at
   each radius and computes the total HIGH-FREQUENCY VARIATION (sum of
   absolute sample-to-sample differences) - the actual tick marks are
   discrete alternating dark/light dashes (high variation), while this
   render's inner-bezel shadow/reflection gradient (a genuine artifact of
   the photoreal render, confirmed by visual overlay - NOT tick ink) is
   smooth (low variation) despite being comparably dark on average. The
   radius with PEAK variation is therefore the tick ring's own radius -
   this cleanly separates the tick ring from both the gear hub (small
   radius, low variation - mostly smooth metal) and the shadow band
   (larger radius, low variation).
3. Within a band around that peak radius, every pixel darker than the
   locally-sampled parchment background by more than INK_DISTANCE_THRESHOLD
   is classified as tick ink; its angle (0deg = straight up from the pivot,
   positive = clockwise, matching components/needle-*.json's own
   bakedAngleConvention) contributes to the [min, max] angular extent.
4. Grand meter (mainVU) only: pixels within the same band that are also
   distinctly REDDER than green/blue are separately classified as the
   baked red zone, giving that zone's own [min, max] angular extent.

Verified visually: overlay_<dial>_band.png (generated during this script's
own development, not committed - re-run save_overlay() below if the master
render is ever replaced) shows the classified ink cleanly tracing every
individual tick dash with no shadow-band or gear-hub contamination.
"""

import math
import numpy as np
from PIL import Image

MASTER_PATH = "/Users/yves/Development/Audio/brand/mocks/victorian/master-01-base.png"

# (cx, cy, r) from brand/mocks/victorian/layout-manifest.json "meters";
# pivot from components/needle-<role>.json's pivotXInMaster/pivotYInMaster.
# rSearchLo/rSearchHi bound the peak-variation radius search (from the
# pivot) - chosen generously inside each dial's own glass boundary (see
# this script's top-of-file docs) so the search can never lock onto the
# outer brass bezel's own engraved detail instead of the tick ring.
# diskFraction: fraction of the dial's own measured r (from its GEOMETRIC
# CENTRE cx/cy, not the pivot) that stays inside the glass face - an
# unconditional AND-mask against the brass bezel/frame regardless of angle,
# needed because the tick-radius band (measured from the off-centre pivot)
# alone is not a fixed-glass-boundary test: at angles closer to horizontal
# from the pivot, that same radius band reaches past the glass into the
# bezel/rivets (confirmed visually - an early pass without this mask
# misclassified the bezel's dark rivets as a false "red zone" at wide
# angles). Measured per-dial from a horizontal radial brightness profile
# (see this repo's PR description / handoff notes for the raw probe).
DIALS = {
    "mainVU": dict(cx=395.3, cy=377.7, r=205.7, pivot=(378.99, 456.0), rSearchLo=115, rSearchHi=200, angHalf=110, bandHalfWidth=14, diskFraction=0.66),
    "smallMeterTop": dict(cx=1178.6, cy=201.2, r=52.8, pivot=(1194.02, 221.0), rSearchLo=15, rSearchHi=55, angHalf=110, bandHalfWidth=8, diskFraction=0.55),
    "smallMeterMid": dict(cx=1186.5, cy=375.7, r=58.1, pivot=(1194.9, 409.0), rSearchLo=15, rSearchHi=62, angHalf=110, bandHalfWidth=8, diskFraction=0.62),
    "smallMeterBottom": dict(cx=1185.7, cy=551.9, r=60.7, pivot=(1196.18, 580.0), rSearchLo=15, rSearchHi=65, angHalf=110, bandHalfWidth=8, diskFraction=0.56),
}

INK_DISTANCE_THRESHOLD = 40.0
RED_R_MINUS_GB_THRESHOLD = 18.0


def bilinear(arr, px, py):
    x0, y0 = int(math.floor(px)), int(math.floor(py))
    fx, fy = px - x0, py - y0
    c00, c10 = arr[y0, x0], arr[y0, x0 + 1]
    c01, c11 = arr[y0 + 1, x0], arr[y0 + 1, x0 + 1]
    return (c00 * (1 - fx) + c10 * fx) * (1 - fy) + (c01 * (1 - fx) + c11 * fx) * fy


def find_peak_variation_radius(arr, pivot, bg, rlo, rhi, angHalf):
    angs = np.arange(-angHalf, angHalf, 0.15)
    best_r, best_var = rlo, -1.0
    step = max(1, (rhi - rlo) // 60)
    for rad in range(rlo, rhi, step):
        vals = []
        for a in angs:
            rr = math.radians(a)
            px = pivot[0] + rad * math.sin(rr)
            py = pivot[1] - rad * math.cos(rr)
            vals.append(np.linalg.norm(bilinear(arr, px, py) - bg))
        vals = np.array(vals)
        variation = float(np.sum(np.abs(np.diff(vals))))
        if variation > best_var:
            best_var = variation
            best_r = rad
    return best_r, best_var


def angle_deg(px, py, pivot):
    return math.degrees(math.atan2(px - pivot[0], -(py - pivot[1])))


def measure(name, d, arr):
    cx, cy, r = d["cx"], d["cy"], d["r"]
    pivot = d["pivot"]
    bg = arr[int(cy + 0.05 * r) - 3:int(cy + 0.05 * r) + 3, int(cx) - 3:int(cx) + 3].reshape(-1, 3).mean(axis=0)

    peak_r, peak_var = find_peak_variation_radius(arr, pivot, bg, d["rSearchLo"], d["rSearchHi"], d["angHalf"])
    rlo, rhi = peak_r - d["bandHalfWidth"], peak_r + d["bandHalfWidth"]

    y0, y1 = int(pivot[1] - rhi - 2), int(pivot[1] + rhi + 2)
    x0, x1 = int(pivot[0] - rhi - 2), int(pivot[0] + rhi + 2)
    ys, xs = np.mgrid[y0:y1, x0:x1]
    region = arr[y0:y1, x0:x1]

    radii = np.sqrt((xs - pivot[0]) ** 2 + (ys - pivot[1]) ** 2)
    dx = xs - pivot[0]
    dy = ys - pivot[1]
    angles = np.degrees(np.arctan2(dx, -dy))
    centreRadii = np.sqrt((xs - cx) ** 2 + (ys - cy) ** 2)
    glassMask = centreRadii <= (r * d["diskFraction"])

    dist = np.linalg.norm(region - bg[None, None, :], axis=2)
    bandMask = (radii > rlo) & (radii < rhi) & (angles > -d["angHalf"]) & (angles < d["angHalf"]) & glassMask
    inkMask = bandMask & (dist > INK_DISTANCE_THRESHOLD)

    redMask = inkMask & ((region[..., 0] - region[..., 1]) > RED_R_MINUS_GB_THRESHOLD) \
                        & ((region[..., 0] - region[..., 2]) > RED_R_MINUS_GB_THRESHOLD)

    tickAngles = angles[inkMask]
    redAngles = angles[redMask]

    print(f"== {name} ==")
    print(f"  peak-variation tick radius from pivot = {peak_r}px (variation={peak_var:.0f}), band=[{rlo},{rhi}]")
    print(f"  ink pixels in band: {inkMask.sum()}")
    if tickAngles.size:
        print(f"  TICK angular extent: [{tickAngles.min():.2f}, {tickAngles.max():.2f}] deg (span {tickAngles.max()-tickAngles.min():.2f})")
    else:
        print("  NO ink pixels found")
    if redAngles.size:
        print(f"  RED ZONE angular extent: [{redAngles.min():.2f}, {redAngles.max():.2f}] deg ({redAngles.size}px)")
    else:
        print("  no red-zone pixels")

    # Verification overlay - green = classified tick ink, red = classified
    # red-zone ink, blue dot = pivot.
    from PIL import ImageDraw
    out = np.array(Image.fromarray(np.clip(region, 0, 255).astype("uint8")).convert("RGB"))
    out[inkMask] = [0, 255, 0]
    out[redMask] = [255, 0, 255]
    outimg = Image.fromarray(out)
    dd = ImageDraw.Draw(outimg)
    pv = (pivot[0] - x0, pivot[1] - y0)
    dd.ellipse([pv[0] - 4, pv[1] - 4, pv[0] + 4, pv[1] + 4], fill=(0, 0, 255))
    scale = 2 if r > 100 else 4
    outimg = outimg.resize((outimg.width * scale, outimg.height * scale), Image.NEAREST)
    outimg.save(f"overlay_{name}_band.png")
    print()

    return tickAngles, redAngles


def main():
    img = Image.open(MASTER_PATH).convert("RGB")
    arr = np.array(img).astype(float)

    for name, d in DIALS.items():
        measure(name, d, arr)


if __name__ == "__main__":
    main()
