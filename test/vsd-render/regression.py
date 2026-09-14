#!/usr/bin/env python3
"""Golden image regression for the VSD renderer.

Renders our SVG pages (renderer/build/vsd2svg output) with headless Chrome and compares
them against reference images (a Visio / Visio for the web screenshot of the
same page), reporting how much of the reference ink we cover and how far the
geometry is off.

    python3 test/vsd-render/regression.py \
        --pages /tmp/out1/pages --out /tmp/vsd-reg \
        --ref 1=/path/to/page-01-reference.png \
        --ref 17=/path/to/page-17-reference.png

Reference images can be produced by opening the drawing in Microsoft Visio (or
the Visio viewer on the web) and saving a screenshot of the page; with --scale
and --ox/--oy the alignment is taken as given, otherwise it is searched for.
"""
import argparse
import os
import subprocess
import sys

from PIL import Image, ImageFilter
import numpy as np

CHROME = os.environ.get(
    'CHROME_BIN', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')


def render_svg(svg_path, png_path, page_w, page_h, width_px):
    """Render an SVG to PNG of exactly width_px for the page width.

    The page is declared in points, so its intrinsic pixel size follows the CSS
    conversion of 96/72 px per point; a device scale factor makes the render
    land on the requested width.
    """
    intrinsic_w = page_w * 96.0 / 72.0
    intrinsic_h = page_h * 96.0 / 72.0
    dsf = width_px / intrinsic_w
    cmd = [
        CHROME, '--headless=new', '--disable-gpu', '--no-sandbox',
        '--hide-scrollbars', f'--force-device-scale-factor={dsf:.5f}',
        f'--window-size={int(intrinsic_w)},{int(intrinsic_h) + 2}',
        f'--screenshot={os.path.abspath(png_path)}',
        'file://' + os.path.abspath(svg_path),
    ]
    subprocess.run(cmd, check=True, capture_output=True)
    image = Image.open(png_path).convert('L')
    if image.width < 100:
        raise RuntimeError(f'failed to render {svg_path}')
    return image


def ink(image):
    return 255.0 - np.asarray(image).astype(float)


def blur(a, radius):
    return np.asarray(
        Image.fromarray(a.astype(np.uint8)).filter(ImageFilter.GaussianBlur(radius))
    ).astype(float)


def ink_bbox(image, threshold=170):
    arr = np.asarray(image)
    mask = arr < threshold
    ys, xs = np.where(mask)
    if len(xs) == 0:
        return None
    return xs.min(), ys.min(), xs.max(), ys.max()


def align(ours, ref, page_w, page_h, scale_hint=None, offset_hint=None):
    """Map page points into the reference image.

    The outermost ink of these drawings is the page frame, present in both
    renders, so matching the two ink bounding boxes gives the scale and offset
    directly; a small local search then refines the fit.
    """
    b_ours = ink_bbox(ours)
    b_ref = ink_bbox(ref)
    if not b_ref or not b_ours:
        return None

    # our render covers exactly the page rectangle
    ours_scale = ours.width / page_w
    frame_ours = [v / ours_scale for v in b_ours]
    scale = (b_ref[2] - b_ref[0]) / max(1.0, frame_ours[2] - frame_ours[0])
    ox = b_ref[0] - frame_ours[0] * scale
    oy = b_ref[1] - frame_ours[1] * scale
    if scale_hint:
        scale = scale_hint
    if offset_hint:
        ox, oy = offset_hint

    W, H = 800, max(1, int(800 * page_h / page_w))
    target = ink(ours.resize((W, H), Image.LANCZOS))
    ref_arr = np.asarray(ref)

    def page_view(s, x, y):
        """Reference resampled onto the page rectangle (offsets may be negative)."""
        canvas = Image.new('L', (max(1, int(page_w * s)), max(1, int(page_h * s))), 255)
        canvas.paste(Image.fromarray(ref_arr), (int(-x), int(-y)))
        return canvas.resize((W, H), Image.LANCZOS)

    def score(s, x, y):
        b = ink(page_view(s, x, y))
        a = target - target.mean()
        b = b - b.mean()
        denom = np.sqrt((a * a).sum() * (b * b).sum())
        return float((a * b).sum() / denom) if denom else -1.0

    best = (score(scale, ox, oy), scale, ox, oy)
    for ds in (-0.01, -0.005, 0.0, 0.005, 0.01):
        for dx in range(-8, 9, 2):
            for dy in range(-8, 9, 2):
                sc = score(scale + ds, ox + dx, oy + dy)
                if sc > best[0]:
                    best = (sc, scale + ds, ox + dx, oy + dy)
    return best


def compare(page_svg, page_w, page_h, ref_path, out_prefix, args):
    ours = render_svg(page_svg, out_prefix + '-ours.png', page_w, page_h, 1600)
    ref = Image.open(ref_path).convert('L')

    hint = args.scale if args.scale else None
    off_hint = (args.ox, args.oy) if args.ox is not None and args.oy is not None else None
    found = align(ours, ref, page_w, page_h, hint, off_hint)
    if not found:
        return None
    corr, scale, ox, oy = found

    # second pass at the reference's own resolution so hairlines and line weights
    # are compared device pixel by device pixel instead of resampled
    target_w = int(round(page_w * scale))
    if abs(target_w - ours.width) > 4:
        ours = render_svg(page_svg, out_prefix + '-ours.png', page_w, page_h, target_w)
        W, H = ours.width, ours.height
    else:
        W, H = ours.width, ours.height
    a = ink(ours)
    canvas = Image.new('L', (max(1, int(page_w * scale)), max(1, int(page_h * scale))), 255)
    canvas.paste(ref, (int(-ox), int(-oy)))
    b = ink(canvas.resize((W, H), Image.LANCZOS))
    ab, bb = blur(a, 1.2), blur(b, 1.2)

    threshold = 40.0
    mask_a, mask_b = ab > threshold, bb > threshold
    covered = float((mask_a & mask_b).sum()) / max(1.0, float(mask_b.sum()))
    extra = float((mask_a & ~mask_b).sum()) / max(1.0, float(mask_a.sum()))
    diff = float(np.abs(ab - bb).mean())

    overlay = np.zeros((H, W, 3), np.uint8)
    overlay[..., 0] = 255 - np.clip(bb * 2.0, 0, 255)
    overlay[..., 1] = 255 - np.clip(np.maximum(ab, bb) * 0.9, 0, 255)
    overlay[..., 2] = 255 - np.clip(ab * 2.0, 0, 255)
    Image.fromarray(overlay).save(out_prefix + '-overlay.png')
    return {
        'correlation': round(corr, 4),
        'scale': round(float(scale), 4),
        'offset': (round(float(ox), 1), round(float(oy), 1)),
        'referenceInkCovered': round(covered, 4),
        'extraInk': round(extra, 4),
        'meanAbsDiff': round(diff, 2),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pages', required=True, help='directory with page-NN.svg')
    ap.add_argument('--out', required=True, help='directory for artefacts')
    ap.add_argument('--ref', action='append', default=[],
                    help='PAGE=PATH reference image for a given page index')
    ap.add_argument('--scale', type=float, help='reference px per page point')
    ap.add_argument('--ox', type=float)
    ap.add_argument('--oy', type=float)
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    results = {}
    for spec in args.ref:
        page, _, ref_path = spec.partition('=')
        svg = os.path.join(args.pages, 'page-%02d.svg' % int(page))
        if not os.path.exists(svg):
            print('skip page %s: %s not found' % (page, svg))
            continue
        # page size in points comes from the SVG viewBox
        head = open(svg, encoding='utf-8').read(400)
        vb = head.split('viewBox="')[1].split('"')[0].split()
        w, h = float(vb[2]), float(vb[3])
        prefix = os.path.join(args.out, 'page-%02d' % int(page))
        result = compare(svg, w, h, ref_path, prefix, args)
        results[page] = result
        print('page %-3s %s' % (page, result))

    if not results:
        print('no references given (use --ref PAGE=PATH)')
        return 1
    ok = all(r and r['referenceInkCovered'] > 0.9 for r in results.values())
    print('RESULT:', 'PASS' if ok else 'REVIEW')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
