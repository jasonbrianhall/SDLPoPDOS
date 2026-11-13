#!/usr/bin/env python3
"""Make the Macintosh graphics set for SDLPoP (use_mac_graphics = true).

Reads YOUR OWN copy of the color Macintosh Prince of Persia (1992) and writes
data/MAC/<SET>/resNNN.png + OFFSETS.TXT, which SDLPoP draws in place of the DOS
sprites when use_mac_graphics is on. No Macintosh data ships with SDLPoP.

The input can be:
  * a folder with the game's files, extracted with their resource forks
    (e.g. `unar -k visible Prince_of_Persia.sit`, a Mac's own Finder copy,
    or AppleDouble "._" files), or
  * a .sit / .hqx / .bin / .zip archive of the game (needs `unar` on the PATH;
    https://theunarchiver.com/command-line).

Needed files: "Persia(COLOR)" (data and resource fork) and the game application
"Prince of Persia" (its resource fork, for the color table).

The Mac art is twice the DOS resolution, and is kept that way: with
use_mac_graphics SDLPoP draws at 640x400. Each sprite is placed where the Mac
game puts it relative to the DOS sprite it replaces (matched against the DOS
sprite at half size); the game keeps using the DOS sprite's size for
collisions, so gameplay and replays are unchanged.

Usage:  python3 tools/make_mac_graphics.py <folder-or-archive> [--data data]
Requires: Python 3.8+, Pillow, numpy  (pip install pillow numpy)

Format notes (for the curious): Persia(COLOR)'s data fork holds LZSS-packed
banks; resource SHPD <id> gives (offset, packed size, unpacked size) of bank
<id> and SHPT <id> the offset of every shape in it. A shape is a header
(width, height, origin x, origin y; big-endian int16) and run-length commands.
Bank <id>'s shape n replaces DOS resource (id mod 1000) + n (bank 960: 950 + n).
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

try:
    import numpy as np
    from PIL import Image
except ImportError:
    sys.exit("This tool needs Pillow and numpy: pip install pillow numpy")

# Mac bank -> SDLPoP data folder (DAT name).
BANKS = {
    40: 'TITLE', 50: 'TITLE', 150: 'PRINCE', 400: 'KID', 700: 'PRINCE',
    800: 'PV', 850: 'PV', 900: 'PV', 950: 'PV', 960: 'PV',
    1200: 'VDUNGEON', 1360: 'VDUNGEON', 1750: 'GUARD',
    2200: 'VPALACE', 2360: 'VPALACE', 2750: 'SKEL',
    3750: 'FAT', 4750: 'SHADOW', 5750: 'VIZIER',
}
# First DOS resource of each bank, where it is not (bank mod 1000): the Mac's second
# princess room bank continues the DOS princess room sprites (pillar, hourglass, sand).
FIRST_ID = {960: 950}
CENTERED = {40, 50, 950}   # title and cutscene screens: center on the DOS picture
SEARCHED = {150, 700}      # potions, swords: the Mac uses its own offset tables
# Torch flames: all frames share one box and one origin, which puts the flame's base where
# the DOS flame's base is. Matched one by one they would jump around, so always use it.
# Their black background (the game draws them opaque) is made transparent, for the princess room.
FLAMES = {(150, slot) for slot in range(1, 10)}
GUARD_FIRST, GUARD_COUNT = 105, 13  # palette slots recolored by the guard color tables


# ---------------------------------------------------------------- Mac files

def fork_from_appledouble(data):
    if data[:4] in (b'\x00\x05\x16\x07', b'\x00\x05\x16\x00'):  # AppleDouble / AppleSingle
        count = struct.unpack_from('>H', data, 24)[0]
        for i in range(count):
            entry, off, length = struct.unpack_from('>III', data, 26 + 12 * i)
            if entry == 2:
                return data[off:off + length]
        return b''
    return data


def parse_resources(fork):
    try:
        return _parse_resources(fork)
    except (struct.error, IndexError):
        return {}  # not a resource fork (icons, empty forks...)


def _parse_resources(fork):
    if len(fork) < 16:
        return {}
    data_off, map_off, _, map_len = struct.unpack_from('>4I', fork, 0)
    m = fork[map_off:map_off + map_len]
    type_list, name_list = struct.unpack_from('>HH', m, 24)
    out = {}
    for i in range(struct.unpack_from('>H', m, type_list)[0] + 1):
        rtype, count, ref_off = struct.unpack_from('>4sHH', m, type_list + 2 + 8 * i)
        for j in range(count + 1):
            e = type_list + ref_off + 12 * j
            rid, _, attr = struct.unpack_from('>hHI', m, e)
            at = data_off + (attr & 0xFFFFFF)
            size = struct.unpack_from('>I', fork, at)[0]
            out[(rtype.decode('mac_roman'), rid)] = fork[at + 4:at + 4 + size]
    return out


def find_files(root):
    """Return {name: {'data': bytes, 'rsrc': bytes}} for the files under root."""
    files = {}

    def add(name, kind, path):
        with open(path, 'rb') as f:
            blob = f.read()
        if kind == 'rsrc':
            blob = fork_from_appledouble(blob)
        if blob:
            files.setdefault(name, {})[kind] = blob

    for folder, dirs, names in os.walk(root):
        for n in names:
            p = os.path.join(folder, n)
            if n.startswith('._'):
                add(n[2:], 'rsrc', p)
            elif n.endswith('.rsrc'):
                add(n[:-5], 'rsrc', p)
            else:
                add(n, 'data', p)
                nf = os.path.join(p, '..namedfork', 'rsrc')   # macOS
                if os.path.exists(nf) and os.path.getsize(nf):
                    add(n, 'rsrc', nf)
    return files


def lzss(src, size):
    out = bytearray()
    i = 0
    while i < len(src) and len(out) < size:
        flags = src[i]
        i += 1
        for bit in range(8):
            if len(out) >= size or i >= len(src):
                break
            if flags & (1 << bit):
                a, b = src[i], src[i + 1]
                i += 2
                dist = (((a & 15) << 8) | b) + 1
                for _ in range((a >> 4) + 3):
                    out.append(out[-dist])
            else:
                out.append(src[i])
                i += 1
    if len(out) != size:
        raise ValueError('graphics bank did not unpack to its stated size')
    return bytes(out)


def decode_shape(buf, w, h):
    """Run-length shape -> (index array, opaque mask)."""
    idx = np.zeros((h, w), np.uint8)
    opaque = np.zeros((h, w), bool)
    i = x = y = 0
    loops = []
    for _ in range(10_000_000):
        c = buf[i]
        i += 1
        if c & 0x80:
            y += 1
            x = 0
        n = c & 0x1F
        if n == 0x1F:
            n = buf[i] + 0x20
            i += 1
        else:
            n += 1
        op = c & 0x60
        if op == 0x00:
            if n == 1:
                return idx, opaque
            v = buf[i]
            i += 1
            if y < h:
                xs = slice(max(x, 0), min(x + n, w))
                idx[y, xs] = v
                opaque[y, xs] = True
            x += n
        elif op == 0x20:
            if y < h:
                for k in range(n):
                    if 0 <= x + k < w:
                        idx[y, x + k] = buf[i + k]
                        opaque[y, x + k] = True
            i += n
            x += n
        elif op == 0x40:
            x += n
        else:  # repeat block
            n -= 1
            if n:
                loops.append([n, i])
            elif loops:
                if loops[-1][0] == 0:
                    loops.pop()
                else:
                    loops[-1][0] -= 1
                    i = loops[-1][1]
    raise ValueError('shape does not end')


def parse_clut(blob):
    count = struct.unpack_from('>H', blob, 6)[0] + 1
    out = {}
    for k in range(count):
        i, r, g, b = struct.unpack_from('>4H', blob, 8 + 8 * k)
        out[i & 0xFF if count == 256 else k] = (r >> 8, g >> 8, b >> 8)
    return out


def load_mac(root):
    files = find_files(root)
    color = files.get('Persia(COLOR)', {})
    if 'data' not in color or 'rsrc' not in color:
        raise SystemExit('Could not find "Persia(COLOR)" with its resource fork in %s.\n'
                         'Extract the game keeping resource forks (unar -k visible ...).' % root)
    app = None
    for name, forks in files.items():
        res = parse_resources(forks.get('rsrc', b''))
        if ('clut', 2000) in res:
            app = res
            break
    if app is None:
        raise SystemExit('Could not find the "Prince of Persia" application (with clut 2000).')
    palette = [(0, 0, 0)] * 256
    for k, v in parse_clut(app[('clut', 2000)]).items():
        palette[k] = v
    guards = []
    for c in range(8):
        tab = parse_clut(app.get(('clut', 3000 + c)) or app[('clut', 3000)])
        guards.append([tab.get(k, (0, 0, 0)) for k in range(GUARD_COUNT)])
    for k in range(GUARD_COUNT):  # default guard colors
        palette[GUARD_FIRST + k] = guards[0][k]

    res = parse_resources(color['rsrc'])
    shapes = {}
    for bank in BANKS:
        if ('SHPD', bank) not in res or ('SHPT', bank) not in res:
            continue
        off, packed, size = struct.unpack('>III', res[('SHPD', bank)])
        raw = color['data'][off:off + (packed or size)]
        unpacked = lzss(raw, size) if packed else raw
        table = res[('SHPT', bank)]
        for slot, at in enumerate(struct.unpack('>%dI' % (len(table) // 4), table)):
            if at == 0xFFFFFFFF:
                continue
            w, h, ox, oy = struct.unpack_from('>4h', unpacked, at)
            if not (0 < w <= 1024 and 0 < h <= 1024):
                continue
            idx, opaque = decode_shape(unpacked[at + 8:], w, h)
            shapes[(bank, slot)] = (idx, opaque, ox, oy)
    if not shapes:
        raise SystemExit('No graphics found in Persia(COLOR).')
    return palette, guards, shapes


# ---------------------------------------------------------------- conversion

def halve(idx, opaque, palette, sx, sy):
    """Halve a Mac shape; sx/sy (0 or 1) pad it so it lands on the 2x2 grid.
    Each output pixel takes the most common opaque Mac color of its 2x2 block."""
    h, w = idx.shape
    H, W = (h + sy + 1) // 2 * 2, (w + sx + 1) // 2 * 2
    I = np.zeros((H, W), np.int32)
    A = np.zeros((H, W), bool)
    I[sy:sy + h, sx:sx + w] = idx
    A[sy:sy + h, sx:sx + w] = opaque
    bi = I.reshape(H // 2, 2, W // 2, 2).transpose(0, 2, 1, 3).reshape(H // 2, W // 2, 4)
    ba = A.reshape(H // 2, 2, W // 2, 2).transpose(0, 2, 1, 3).reshape(H // 2, W // 2, 4)
    lum = np.array([sum(c) for c in palette], np.float32)[bi]
    mean = (lum * ba).sum(-1) / np.maximum(ba.sum(-1), 1)
    votes = ((bi[..., :, None] == bi[..., None, :]) & ba[..., None, :]).sum(-1)
    score = np.where(ba, votes * 10000 - np.abs(lum - mean[..., None]), -1)
    out = np.take_along_axis(bi, score.argmax(-1)[..., None], -1)[..., 0]
    return out, ba.sum(-1) >= 2


def iou(dos_mask, mac_mask, x0, y0):
    dh, dw = dos_mask.shape
    mh, mw = mac_mask.shape
    canvas = np.zeros((dh, dw), bool)
    ys, ye = max(0, y0), min(dh, y0 + mh)
    xs, xe = max(0, x0), min(dw, x0 + mw)
    if ys < ye and xs < xe:
        canvas[ys:ye, xs:xe] = mac_mask[ys - y0:ye - y0, xs - x0:xe - x0]
    union = (canvas | dos_mask).sum()
    return (canvas & dos_mask).sum() / union if union else 0.0


def grow(mask):
    m = mask.copy()
    m[1:] |= mask[:-1]; m[:-1] |= mask[1:]
    m[:, 1:] |= mask[:, :-1]; m[:, :-1] |= mask[:, 1:]
    return m


def blobs(mask):
    """The 8-connected parts of a mask, each as a mask."""
    h, w = mask.shape
    seen = np.zeros_like(mask)
    parts = []
    for sy, sx in zip(*np.nonzero(mask)):
        if seen[sy, sx]:
            continue
        part = np.zeros_like(mask)
        stack = [(sy, sx)]
        seen[sy, sx] = True
        while stack:
            y, x = stack.pop()
            part[y, x] = True
            for ny in (y - 1, y, y + 1):
                for nx in (x - 1, x, x + 1):
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
        parts.append(part)
    return parts


def search(dos_mask, mask, reach=8):
    """Best placement of mask on the DOS picture near bottom-left alignment: (score, x0, y0)."""
    dh = dos_mask.shape[0]
    ys, xs = np.nonzero(mask)
    if len(ys) == 0:
        return 0.0, 0, 0
    bx, by = -int(xs.min()), dh - 1 - int(ys.max())
    big = grow(dos_mask)
    grown = grow(mask)
    return max(((iou(big, grown, bx + dx, by + dy), bx + dx, by + dy)
                for dy in range(-reach, reach + 1) for dx in range(-reach, reach + 1)),
               key=lambda t: (round(t[0], 3), -abs(t[1] - bx) - abs(t[2] - by)))


def drop_strays(opaque, dos_size, x0, y0, margin=2):
    """Remove parts of the halved Mac picture that lie wholly outside the DOS picture's box:
    some Mac pictures bundle pieces that DOS draws as separate images."""
    dw, dh = dos_size
    keep = np.zeros_like(opaque)
    for part in blobs(opaque):
        ys, xs = np.nonzero(part)
        if ((xs + x0 >= -margin) & (xs + x0 < dw + margin) & (ys + y0 >= -margin) & (ys + y0 < dh + margin)).any():
            keep |= part
    return keep


def dos_picture(path):
    im = Image.open(path)
    if im.mode == 'P':
        mask = np.array(im) != 0
    else:
        mask = np.array(im.convert('RGBA'))[..., 3] > 0
    return im.size, mask


def write_png(path, pixels, opaque, palette, keep_palette):
    """Indexed PNG, index 0 transparent. keep_palette keeps Mac palette slots (guards)."""
    if keep_palette:
        used = sorted(set(pixels[opaque].tolist()))
        if 0 in used:
            raise ValueError('palette slot 0 is used')
        img = np.where(opaque, pixels, 0).astype(np.uint8)
        pal = [(0, 0, 0)] + list(palette[1:])
    else:
        colors = sorted(set(pixels[opaque].tolist()))
        remap = np.zeros(256, np.uint8)
        for n, c in enumerate(colors):
            remap[c] = n + 1
        img = np.where(opaque, remap[pixels], 0).astype(np.uint8)
        pal = [(0, 0, 0)] + [palette[c] for c in colors]
    im = Image.fromarray(img, 'P')
    im.putpalette([v for c in pal for v in c] + [0] * (768 - 3 * len(pal)))
    im.save(path, optimize=True)


def convert(palette, guards, shapes, data_dir, out_dir, verbose):
    made = skipped = 0
    offsets = {}
    for (bank, slot), (idx, opaque, ox, oy) in sorted(shapes.items()):
        folder = BANKS[bank]
        rid = FIRST_ID.get(bank, bank % 1000) + slot
        dos_path = os.path.join(data_dir, folder, 'res%d.png' % rid)
        if not os.path.exists(dos_path):
            continue  # nothing to replace
        (dw, dh), dos_mask = dos_picture(dos_path)
        mh, mw = idx.shape
        if bank in CENTERED:
            # Mac pixels, relative to twice the DOS picture's top-left corner.
            X, Y = (2 * dw - mw) // 2, (2 * dh - mh) // 2
            keep = opaque
        else:
            # Matching is done at DOS resolution, on the halved Mac picture: a halved
            # pixel (x, y) covers Mac pixels from (2x - sx, 2y - sy), placed at DOS (x0 + x, y0 + y),
            # so Mac pixel (0, 0) goes at (2 x0 + sx, 2 y0 + sy).
            # Where the Mac game draws it: its origin sits at twice the DOS
            # bottom-left corner, 2 Mac pixels up.
            X, Y = -ox, 2 * dh - 2 - oy
            if (bank, slot) in FLAMES:
                black = np.array([sum(c) <= 30 for c in palette])[idx]
                keep = opaque & ~black
                # Not trimmed: drawn opaque, each frame's whole box erases the one before.
                os.makedirs(os.path.join(out_dir, folder), exist_ok=True)
                write_png(os.path.join(out_dir, folder, 'res%d.png' % rid), idx, keep, palette, keep_palette=False)
                offsets.setdefault(folder, []).append((rid, X, Y))
                made += 1
                continue
            sx, sy = X & 1, Y & 1
            _, opq = halve(idx, opaque, palette, sx, sy)
            x0, y0 = (X - sx) // 2, (Y - sy) // 2
            score = iou(grow(dos_mask), grow(opq), x0, y0)
            if bank in SEARCHED or score < 0.5:
                sx = sy = 0
                _, opq = halve(idx, opaque, palette, 0, 0)
                score, x0, y0 = search(dos_mask, opq)
            # A Mac picture may bundle pieces that DOS draws separately: try each piece alone.
            if opq.shape[0] > dh + 6 or opq.shape[1] > dw + 6:
                parts = blobs(opq)
                if 1 < len(parts) <= 16:
                    for part in parts:
                        s, px0, py0 = search(dos_mask, part)
                        if s > score + 0.1:
                            score, x0, y0 = s, px0, py0
            if score < 0.45:
                skipped += 1
                if verbose:
                    print('  skip %s/res%d (does not match the DOS picture)' % (folder, rid))
                continue
            X, Y = 2 * x0 + sx, 2 * y0 + sy
            keep = drop_strays(opaque, (2 * dw, 2 * dh), X, Y, margin=4)
        if not keep.any():
            skipped += 1
            continue
        px, opq = idx, keep
        if bank not in CENTERED:  # trim empty edges
            ys, xs = np.nonzero(opq)
            px, opq = px[ys.min():ys.max() + 1, xs.min():xs.max() + 1], opq[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
            X, Y = X + int(xs.min()), Y + int(ys.min())
        x0, y0 = X, Y
        os.makedirs(os.path.join(out_dir, folder), exist_ok=True)
        write_png(os.path.join(out_dir, folder, 'res%d.png' % rid), px, opq, palette,
                  keep_palette=(bank == 1750))
        offsets.setdefault(folder, []).append((rid, x0, y0))
        made += 1
    for folder, rows in offsets.items():
        with open(os.path.join(out_dir, folder, 'OFFSETS.TXT'), 'w', newline='\r\n') as f:
            f.write('; id x y: where the picture goes, in Macintosh pixels (twice the DOS ones),\n'
                    '; relative to the top-left corner of the DOS picture it replaces\n')
            for rid, x, y in rows:
                f.write('%d %d %d\n' % (rid, x, y))
    with open(os.path.join(out_dir, 'GUARD', 'COLORS.TXT'), 'w', newline='\r\n') as f:
        f.write('; guard color n: %d colors for palette slots %d.. (r g b)\n' % (GUARD_COUNT, GUARD_FIRST))
        for n, cols in enumerate(guards):
            f.write('%d %s\n' % (n, ' '.join('%d %d %d' % c for c in cols)))
    return made, skipped


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('source', help='folder with the Mac game files, or a .sit/.hqx/.bin/.zip archive')
    ap.add_argument('--data', default=os.path.join(here, '..', 'data'),
                    help='SDLPoP data folder (default: the one next to tools/)')
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()
    data_dir = os.path.abspath(args.data)
    if not os.path.isdir(os.path.join(data_dir, 'KID')):
        sys.exit('%s does not look like SDLPoP\'s data folder (no KID folder)' % data_dir)

    tmp = None
    source = args.source
    try:
        if os.path.isfile(source):
            if shutil.which('unar') is None:
                sys.exit('Extracting %s needs `unar` (The Unarchiver) on the PATH,\n'
                         'or extract it yourself keeping resource forks and pass the folder.' % source)
            tmp = tempfile.mkdtemp(prefix='popmac')
            subprocess.run(['unar', '-q', '-k', 'visible', '-o', tmp, source], check=True)
            source = tmp
        print('Reading the Macintosh game...')
        palette, guards, shapes = load_mac(source)
        out_dir = os.path.join(data_dir, 'MAC')
        new_dir = out_dir + '.new'
        if os.path.isdir(new_dir):
            shutil.rmtree(new_dir)
        os.makedirs(new_dir)
        made, skipped = convert(palette, guards, shapes, data_dir, new_dir, args.verbose)
        if os.path.isdir(out_dir):
            shutil.rmtree(out_dir)
        os.rename(new_dir, out_dir)
        print('Wrote %d images to %s (%d did not match their DOS counterpart and were left out).'
              % (made, out_dir, skipped))
        print('Turn it on with use_mac_graphics = true in SDLPoP.ini (or Settings > Visuals).')
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    main()
