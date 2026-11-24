#!/usr/bin/env python3
"""Generates src/art_data.h: bitmap font + indexed pixel-art sprites (original art)."""
import sys, os

FONT = {
' ':["...","...","...","...","..."],
'!':[".#.",".#.",".#.","...",".#."],
'"':["#.#","#.#","...","...","..."],
'#':["#.#","###","#.#","###","#.#"],
'$':[".##","##.",".#.",".##","##."],
'%':["#..","..#",".#.","#..","..#"],
'&':[".#.","#.#",".#.","#.#",".##"],
"'":[".#.",".#.","...","...","..."],
'(':["..#",".#.",".#.",".#.","..#"],
')':["#..",".#.",".#.",".#.","#.."],
'*':["...","#.#",".#.","#.#","..."],
'+':["...",".#.","###",".#.","..."],
',':["...","...","...",".#.","#.."],
'-':["...","...","###","...","..."],
'.':["...","...","...","...",".#."],
'/':["..#","..#",".#.","#..","#.."],
'0':["###","#.#","#.#","#.#","###"],
'1':[".#.","##.",".#.",".#.","###"],
'2':["###","..#","###","#..","###"],
'3':["###","..#",".##","..#","###"],
'4':["#.#","#.#","###","..#","..#"],
'5':["###","#..","###","..#","###"],
'6':["###","#..","###","#.#","###"],
'7':["###","..#","..#",".#.",".#."],
'8':["###","#.#","###","#.#","###"],
'9':["###","#.#","###","..#","###"],
':':["...",".#.","...",".#.","..."],
';':["...",".#.","...",".#.","#.."],
'<':["..#",".#.","#..",".#.","..#"],
'=':["...","###","...","###","..."],
'>':["#..",".#.","..#",".#.","#.."],
'?':["###","..#",".#.","...",".#."],
'@':[".#.","#.#","###","#..",".##"],
'A':[".#.","#.#","###","#.#","#.#"],
'B':["##.","#.#","##.","#.#","##."],
'C':[".##","#..","#..","#..",".##"],
'D':["##.","#.#","#.#","#.#","##."],
'E':["###","#..","##.","#..","###"],
'F':["###","#..","##.","#..","#.."],
'G':[".##","#..","#.#","#.#",".##"],
'H':["#.#","#.#","###","#.#","#.#"],
'I':["###",".#.",".#.",".#.","###"],
'J':["..#","..#","..#","#.#",".#."],
'K':["#.#","#.#","##.","#.#","#.#"],
'L':["#..","#..","#..","#..","###"],
'M':["#.#","###","###","#.#","#.#"],
'N':["##.","#.#","#.#","#.#","#.#"],
'O':[".#.","#.#","#.#","#.#",".#."],
'P':["##.","#.#","##.","#..","#.."],
'Q':[".#.","#.#","#.#","##.",".##"],
'R':["##.","#.#","##.","#.#","#.#"],
'S':[".##","#..",".#.","..#","##."],
'T':["###",".#.",".#.",".#.",".#."],
'U':["#.#","#.#","#.#","#.#","###"],
'V':["#.#","#.#","#.#","#.#",".#."],
'W':["#.#","#.#","###","###","#.#"],
'X':["#.#","#.#",".#.","#.#","#.#"],
'Y':["#.#","#.#",".#.",".#.",".#."],
'Z':["###","..#",".#.","#..","###"],
'[':["##.","#..","#..","#..","##."],
'\\':["#..","#..",".#.","..#","..#"],
']':[".##","..#","..#","..#",".##"],
'^':[".#.","#.#","...","...","..."],
'_':["...","...","...","...","###"],
}

# palette char -> master palette index (see gfx.c PALETTE)
PCH = {'.':0,'k':1,'d':2,'g':3,'l':4,'w':5,'1':6,'2':7,'3':8,'b':9,'n':10,'t':11,'s':12,
       'R':13,'r':14,'o':15,'y':16,'G':17,'e':18,'L':19,'B':20,'u':21,'c':22,'P':23,'p':24,
       'i':25,'O':26,'Q':27,'m':28,'M':29,'Y':30,'f':31}

SPR = {}
SPR['hero_a'] = [
"....kkkk....",
"...kmmmmk...",
"..kmmwmmMk..",
"..kMmmmmMk..",
"..kkssssk...",
"...ksksk....",
"...kssssk...",
"..krrRrrrk..",
".krrrRrrrrk.",
".ksrrRrrrsk.",
".kskrRrrksk.",
"..kknynnkk..",
"...knnknnk..",
"...knnknnk..",
"...knk.knk..",
"...kbk.kbk..",
"..kbbk.kbbk.",
"..kkk..kkk..",
]
SPR['hero_b'] = SPR['hero_a'][:12] + [
"...knnknnk..",
"..knnk.knk..",
"..knk..knk..",
"..kbk...kbk.",
".kbbk...kbbk",
".kkk....kkk.",
]
SPR['w_sword'] = [
"..w..",
"..l..",
"..l..",
"..l..",
"..l..",
"..l..",
".yYy.",
"..n..",
"..n..",
]
SPR['w_bow'] = [
".n...",
"n.w..",
"n..w.",
"n...w",
"n...w",
"n...w",
"n...w",
"n..w.",
"n.w..",
".n...",
]
SPR['w_staff'] = [
".cc.",
"cwwc",
".cc.",
".n..",
".n..",
".n..",
".n..",
".n..",
".n..",
".n..",
".n..",
".n..",
".n..",
".b..",
]
SPR['m_rat'] = [
"......kkkk....",
"....kkddddk...",
"..kkdddddddk..",
".kwdddddddddk.",
"kddddddddddkk.",
".kkdgkkdgk.iii",
"..kk..kk......",
]
SPR['m_skel'] = [
"...kkkk...",
"..kOOOOk..",
"..kOkOkk..",
"..kOOOOk..",
"...kQQk...",
"..kkOOkk..",
".kOkOOkOk.",
".kOkQQkOk.",
".kQkOOkQk.",
"..k.QQ.k..",
"...kOOk...",
"...kOkOk..",
"...kOkOk..",
"...kOkOk..",
"..kOk.kOk.",
"..kk..kk..",
]
SPR['m_spider'] = [
"....kkkkkk....",
"..kkPPPPPPkk..",
".kPPPPPPPPPPk.",
"k.kPrPPPPrPk.k",
".k.kPPPPPPk.k.",
"k.k.kkkkkk.k.k",
".k.k.k..k.k.k.",
"k..k.k..k.k..k",
"...k......k...",
]
SPR['m_gob'] = [
"..k....k..",
".kek..kek.",
".keekkeek.",
"..keeeek..",
"..kerrek..",
"..keeeek..",
"...kkkk...",
"..kbnnbk..",
".kebnnbek.",
".kebnnbek.",
"..knnnnk..",
"..kek.kek.",
"..kk..kk..",
]
SPR['m_brute'] = [
".....kkkkkk.....",
"....kffffffk....",
"...kffkffkffk...",
"...kffrffrffk...",
"...kffffffffk...",
"...kfwkwkwffk...",
"..kkkffffffkkk..",
".kffffffffffffk.",
"kfffnnnnnnnnfffk",
"kffknnnnnnnnkffk",
"kffknnnnnnnnkffk",
".kk.knnnnnnk.kk.",
"....kbbbbbbk....",
"....kffk.kffk...",
"....kffk.kffk...",
"....kffk.kffk...",
"...kbbbk.kbbbk..",
"...kkkk...kkkk..",
]
SPR['m_wraith'] = [
"....kkkk....",
"...kPPPPk...",
"..kPPPPPPk..",
"..kPcPPcPk..",
"..kPPPPPPk..",
"..kPPkkPPk..",
".kPPPPPPPPk.",
"kPPpPPPPpPPk",
"kPpPPPPPPpPk",
".kPPPPPPPPk.",
".kPPpPPpPPk.",
"..kPPPPPPk..",
"..kPkPPkPk..",
"...k.kk.k...",
"....k..k....",
]
# ---- item icons 10x10 ----
SPR['i_sword'] = [
"........w.",
".......wl.",
"......wl..",
".....wl...",
"....wl....",
".y.wl.....",
"..Yl......",
".nYy......",
"n.........",
"..........",
]
SPR['i_bow'] = [
"...nnn....",
"..n...w...",
".n....w...",
".n....w...",
"n.....w...",
".n....w...",
".n....w...",
"..n...w...",
"...nnn....",
"..........",
]
SPR['i_staff'] = [
".......cc.",
"......cwwc",
".......cc.",
"......n...",
".....n....",
"....n.....",
"...n......",
"..n.......",
".b........",
"..........",
]
SPR['i_shield'] = [
".kkkkkkkk.",
"kmmmmmmmmk",
"kmmrrrrmmk",
"kmmryyrmmk",
"kmmryyrmmk",
".kmmrrmmk.",
".kmmmmmmk.",
"..kmmmmk..",
"...kmmk...",
"....kk....",
]
SPR['i_tome'] = [
".kkkkkkkk.",
"kRRRRRRRwk",
"kRRyRRRRwk",
"kRyYyRRRwk",
"kRRyRRRRwk",
"kRRRRRRRwk",
"kRRRRRRRwk",
"kRRRRRRRwk",
".kkkkkkkk.",
"..........",
]
SPR['i_helm'] = [
"..kkkkkk..",
".kmmmmmmk.",
"kmmwmmmmMk",
"kmmmmmmmMk",
"kMkkkkkkMk",
"kMk....kMk",
"kMk....kMk",
".k......k.",
"..........",
"..........",
]
SPR['i_amulet'] = [
".y......y.",
"..y....y..",
"...y..y...",
"....yy....",
"...kccck..",
"..kcwcck..",
"..kcccck..",
"...kcck...",
"....kk....",
"..........",
]
SPR['i_chest'] = [
".kk....kk.",
"kmmk..kmmk",
"kmmmkkmmmk",
".kmmmmmmk.",
".kmmMMmmk.",
".kmmmmmmk.",
".kmmMMmmk.",
".kmmmmmmk.",
"..kkkkkk..",
"..........",
]
SPR['i_legs'] = [
".kkkkkkkk.",
".knnnnnnk.",
".knnnnnnk.",
".knnkknnk.",
".knnkknnk.",
".knnkknnk.",
".knnkknnk.",
".kbbkkbbk.",
".kkk..kkk.",
"..........",
]
SPR['i_boots'] = [
"..........",
"..kkk.....",
"..knk.....",
"..knk..kkk",
"..knk..knk",
"..knnk.knk",
".kbbbk.knk",
".kkkkkkbbk",
".......kkk",
"..........",
]
SPR['i_gloves'] = [
"..........",
".k.k.k....",
"kn.n.nk...",
"knnnnnk...",
"knnnnnkk..",
"knnnnnnnk.",
".knnnnnk..",
"..kbbbk...",
"..kkkkk...",
"..........",
]
SPR['i_ring'] = [
"....c.....",
"...kckk...",
"..kyyyyk..",
".kyk..kyk.",
".ky....yk.",
".ky....yk.",
".kyk..kyk.",
"..kyyyyk..",
"...kkkk...",
"..........",
]
SPR['i_cape'] = [
"..kkkkkk..",
".krrrrrrk.",
".krrrrrrk.",
"krrRrrRrrk",
"krrRrrRrrk",
"krrRrrRrrk",
"krRrrrrRrk",
"kRrrrrrrRk",
"kkkkkkkkkk",
"..........",
]
SPR['i_quiver'] = [
"....w.w...",
"...wlwlw..",
"...knnnk..",
"..knnnnk..",
"..knnynk..",
".knnnnk...",
".knnynk...",
"knnnnk....",
"kkkkk.....",
"..........",
]
SPR['i_potion'] = [
"...kkk....",
"...kOk....",
"...kOk....",
"..krrrk...",
".krwrrrk..",
".krrrrrk..",
".krrrrrk..",
"..kkkkk...",
"..........",
"..........",
]
SPR['i_gold'] = [
"..........",
"..........",
"...kkk....",
"..kyyYk...",
".kyyyyYk..",
".kyYyyyk..",
"..kYYYk...",
"...kkk....",
"..........",
"..........",
]
SPR['i_chestbox'] = [
"............",
"..kkkkkkkk..",
".knnnnnnnnk.",
"knnbnnnnbnnk",
"kkkkkYYkkkkk",
"knnbnYYnbnnk",
"knnbnnnnbnnk",
"knnbnnnnbnnk",
"kkkkkkkkkkkk",
]


SPR['npc_merchant'] = [
"....bbbb....",
"...bnnnnb...",
"..bnnnnnnb..",
"..kkssssk...",
"...ksksk....",
"...kssssk...",
"..keeGeeek..",
".keeeGeeeek.",
".kseeGeeesk.",
".kskeGeeksk.",
"..kkeyeekk..",
"...keeGeek..",
"...keeGeek..",
"...keeGeek..",
"...keeGeek..",
"...kbk.kbk..",
"..kbbk.kbbk.",
"..kkk..kkk..",
]
SPR['npc_smith'] = [
"....kkkk....",
"...kssssk...",
"..kssssssk..",
"..kssssssk..",
"...ksksk....",
"...kssssk...",
"..kssnnssk..",
".ksssnnsssk.",
".ksskbnkssk.",
".kskbnnbksk.",
"..kknnnnkk..",
"...knnnnk...",
"...kMMkMMk..",
"...kMMkMMk..",
"...kMk.kMk..",
"...kbk.kbk..",
"..kbbk.kbbk.",
"..kkk..kkk..",
]
SPR['npc_jeweler'] = [
"....kkkk....",
"...kPPPPk...",
"..kYYcYYYk..",
"..kkssssk...",
"...ksksk....",
"...kssssk...",
"..kppPpppk..",
".kppppPpppk.",
".ksppPpppsk.",
".kskpPppksk.",
"..kkpyppkk..",
"...kppPpk...",
"...kppPpk...",
"...kppPpk...",
"...kppPpk...",
"...kPk.kPk..",
"..kPPk.kPPk.",
"..kkk..kkk..",
]
SPR['npc_trainer'] = [
"....kkkk....",
"...kllllk...",
"..kllllllk..",
"..kkssssk...",
"...ksksk....",
"...kssllk...",
"..kuuMuuuk..",
".kuuuMuuuuk.",
".ksuuMuuusk.",
".kskuMuuksk.",
"..kknynnkk..",
"...knnknnk..",
"...knnknnk..",
"...knnknnk..",
"...knk.knk..",
"...kbk.kbk..",
"..kbbk.kbbk.",
"..kkk..kkk..",
]
SPR['tree'] = [
".......GGGGGG.........",
".....GGeeeeeeGG.......",
"...GGeeeeLeeeeeGG.....",
"..GeeeLeeeeeeLeeeG....",
".GeeeeeeeeeeeeeeeeG...",
"GeeeLeeeeeGeeeeeLeeG..",
"GeeeeeeeGeeeeeeeeeeG..",
".GeeeeeeeeeeLeeeeeeG..",
"GeeLeeeeeeeeeeeeLeeeG.",
"GeeeeeeGeeeeLeeeeeeeG.",
".GeeeeeeeeeeeeeeeGeG..",
"..GGeeeGeeeeeeeeeGG...",
"....GGGeeeeeeGGGG.....",
".......GGGGGG.........",
".........bnb..........",
".........bnb..........",
".........bnb..........",
"........bbnbb.........",
]
SPR['gem'] = [
"..kkk..",
".kwcck.",
"kwcccck",
"kccccBk",
".kccBk.",
"..kBk..",
"...k...",
]

EXTRA_PAL = []   # imported art colors -> palette indices 32..255
ORIGIN = {}      # sprite -> (ox, oy) foot/anchor pixel
HC = []          # full-color sprites: (name, RGBA image)
HCC = []         # full-color character frames
FRAMES = {}      # animation sheet name -> frame count

def import_art(art_dir):
    """Imports baked/hand-drawn PNGs named <sprite>__<W>x<H>.png (frames side by side).
    Replaces the built-in sprite of that name. All imported colors share palette slots 32..255."""
    import glob, re
    try:
        from PIL import Image
    except ImportError:
        return []
    files = sorted(glob.glob(os.path.join(art_dir, '*.png')))
    if not files:
        return []
    loaded = []
    for f in files:
        m = re.match(r'(.+?)__(\d+)x(\d+)(?:_o(\d+)x(\d+))?\.png$', os.path.basename(f))
        if not m:
            print('skipping (name must be sprite__WxH.png):', f); continue
        name, W, H = m.group(1), int(m.group(2)), int(m.group(3))
        org = (int(m.group(4)), int(m.group(5))) if m.group(4) else (W // 2, H - 1)
        im = Image.open(f).convert('RGBA')
        n = max(1, im.width // W)
        frames = [im.crop((k * W, 0, k * W + W, H)) for k in range(n)]
        targets = {'hero': ['hero_a', 'hero_b']}.get(name)
        for k, fr in enumerate(frames):
            nm = targets[min(k, len(targets) - 1)] if targets else (name if n == 1 else f'{name}_{k}')
            loaded.append((nm, fr)); ORIGIN[nm] = org
        if targets and n == 1:
            loaded.append((targets[1], frames[0])); ORIGIN[targets[1]] = org
        if n > 1: FRAMES[name] = n
    # environment art (tiles, props, buildings, trees) stays full color (RGB565), outside the palette
    is_hc = lambda nm: nm.startswith(('t_', 'b_', 'p_', 'tree'))
    is_ch = lambda nm: nm.startswith(('hero_', 'mob_', 'npc_')) and nm not in ('hero_a', 'hero_b')
    # darker, grittier grade (v0.16): desaturate, grimy warm tint, more contrast, a little static grain
    import numpy as _np
    def grade(fr, sat, grain, dark):
        a = _np.asarray(fr.convert('RGBA')).astype(_np.float32)
        rgb, al = a[..., :3], a[..., 3:]
        keep = (rgb[..., 0] == 22) & (rgb[..., 1] == 18) & (rgb[..., 2] == 25)   # baked outline marker stays exact
        y = rgb @ _np.array([0.30, 0.59, 0.11], _np.float32)
        rgb = y[..., None] + (rgb - y[..., None]) * sat
        rgb = rgb * _np.array([1.0, 0.95, 0.86], _np.float32)            # grime
        rgb = (rgb - 118) * 1.14 + 108                                  # contrast, mids pulled down
        rgb = rgb * dark
        if grain:
            h, w = rgb.shape[:2]
            rng = _np.random.default_rng(abs(hash((w, h))) % (1 << 32))
            rgb = rgb + rng.integers(-grain, grain + 1, size=(h, w))[..., None]
        rgb = _np.clip(rgb, 0, 255)
        rgb[keep] = (22, 18, 25)
        out = _np.concatenate([rgb, al], axis=-1).astype(_np.uint8)
        return Image.fromarray(out, 'RGBA')
    loaded = [(nm, grade(fr, 0.58, 6, 0.9) if is_hc(nm) else grade(fr, 0.72, 3, 0.94) if is_ch(nm) else grade(fr, 0.9, 0, 1.0)) for nm, fr in loaded]
    HC[:] = [(nm, fr) for nm, fr in loaded if is_hc(nm)]
    HCC[:] = [(nm, fr) for nm, fr in loaded if is_ch(nm)]   # characters: full color, cropped, shaded at draw time
    loaded = [(nm, fr) for nm, fr in loaded if not is_hc(nm) and not is_ch(nm)]
    # shared palette for all other imported art (max 224 colors)
    px = [p[:3] for _, fr in loaded for p in (fr.get_flattened_data() if hasattr(fr, 'get_flattened_data') else fr.getdata()) if p[3] >= 128]
    if not px:
        return []
    uniq = sorted(set(px))
    if len(uniq) > 224:
        strip = Image.new('RGB', (len(px), 1)); strip.putdata(px)
        q = strip.quantize(colors=224, method=Image.Quantize.MEDIANCUT)
        pal = q.getpalette()[:224 * 3]
        uniq = [tuple(pal[i * 3:i * 3 + 3]) for i in range(224)]
    EXTRA_PAL[:] = uniq
    def idx(c):
        best, bd = 0, 1 << 30
        for i, u in enumerate(uniq):
            d = (c[0] - u[0]) ** 2 + (c[1] - u[1]) ** 2 + (c[2] - u[2]) ** 2
            if d < bd: bd, best = d, i
        return 32 + best
    cache = {}
    out = []
    def dilate(fr):
        px = fr.load(); w, h = fr.size
        for _ in range(6):
            src = fr.copy().load(); changed = False
            for y in range(h):
                for x in range(w):
                    if src[x, y][3] >= 128: continue
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        if 0 <= x + dx < w and 0 <= y + dy < h and src[x + dx, y + dy][3] >= 128:
                            px[x, y] = src[x + dx, y + dy][:3] + (255,); changed = True; break
            if not changed: break
        return fr
    loaded = [(nm, dilate(fr) if nm.startswith('t_') else fr) for nm, fr in loaded]
    for nm, fr in loaded:
        rows = []
        for y in range(fr.height):
            row = []
            for x in range(fr.width):
                p = fr.getpixel((x, y))
                if p[3] < 128: row.append(0)
                else:
                    c = p[:3]
                    if c not in cache: cache[c] = idx(c)
                    row.append(cache[c])
            rows.append(row)
        out.append((nm, fr.width, fr.height, rows))
        print('imported', nm, f'{fr.width}x{fr.height}')
    return out

def main(out):
    imported = import_art(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'art'))
    lines = ["// generated by tools/gen_art.py - do not edit", "#pragma once", "#include <stdint.h>", ""]
    # font: chars 32..95, 5 rows of 3 bits each packed into 15 bits
    vals = []
    for c in range(32, 96):
        g = FONT.get(chr(c), FONT['?'])
        v = 0
        for r, row in enumerate(g):
            for x, ch in enumerate(row):
                if ch == '#':
                    v |= 1 << (r * 3 + x)
        vals.append(v)
    lines.append("static const uint16_t FONT3x5[64] = {" + ",".join(str(v) for v in vals) + "};")
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import font4x7
    lines.append("static const uint32_t FONT4x7[64] = {" + ",".join(str(v) for v in font4x7.encode()) + "};   // v0.20 text font")
    lines.append("")
    lines.append(f"#define EXTRA_PAL_N {len(EXTRA_PAL)}")
    lines.append("static const uint8_t EXTRA_PAL[%d][3] = {%s};" % (max(1, len(EXTRA_PAL)), ",".join("{%d,%d,%d}" % c for c in EXTRA_PAL) or "{0,0,0}"))
    names = []
    imp = {nm: (w, h, rows) for nm, w, h, rows in imported}
    for nm, (w, h, rows) in imp.items():
        lines.append(f"#define HAVE_SPR_{nm} 1")
        data = [v for r in rows for v in r]
        lines.append(f"static const uint8_t SPR_{nm}_px[{w*h}] = {{" + ",".join(map(str, data)) + "};")
        names.append((nm, w, h))
    for name, rows in SPR.items():
        if name in imp: continue
        w = max(len(r) for r in rows); h = len(rows)
        data = []
        for r in rows:
            r = r.ljust(w, '.')
            for ch in r:
                if ch not in PCH:
                    raise SystemExit(f"bad char {ch!r} in {name}")
                data.append(PCH[ch])
        lines.append(f"static const uint8_t SPR_{name}_px[{w*h}] = {{" + ",".join(map(str, data)) + "};")
        names.append((name, w, h))
    lines.append("")
    lines.append("typedef struct { uint8_t w, h; const uint8_t *px; uint8_t ox, oy; } Sprite;")
    # full-color sprites: RGB565, 0 = transparent; shaded copies are made at boot
    lines.append("typedef struct { uint8_t w, h; const uint16_t *px; uint8_t ox, oy; uint16_t id; } HiSprite;")
    def dil(fr):
        px = fr.load(); w, h = fr.size
        for _ in range(6):
            src = fr.copy().load(); changed = False
            for y in range(h):
                for x in range(w):
                    if src[x, y][3] >= 128: continue
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        if 0 <= x + dx < w and 0 <= y + dy < h and src[x + dx, y + dy][3] >= 128:
                            px[x, y] = src[x + dx, y + dy][:3] + (255,); changed = True; break
            if not changed: break
        return fr
    total = 0; total_rows = 0
    for i, (nm, fr) in enumerate(HC):
        if nm.startswith('t_'): fr = dil(fr.copy())
        w, h = fr.size; data = []
        for (r, g, b, a) in (fr.get_flattened_data() if hasattr(fr, 'get_flattened_data') else fr.getdata()):
            if a < 128: data.append(0); continue
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            data.append(v or 0x0020)
        ox, oy = ORIGIN.get(nm, (w // 2, h - 1))
        lines.append(f"#define HAVE_HC_{nm} 1")
        lines.append(f"static const uint16_t HC_{nm}_px[{w*h}] = {{" + ",".join(map(str, data)) + "};")
        lines.append(f"static const HiSprite HC_{nm} = {{{w},{h},HC_{nm}_px,{ox},{oy},{i}}};")
        total += w * h; total_rows += h
    lines.append(f"#define HC_TOTAL_ROWS {max(1, total_rows)}")
    lines.append(f"#define HC_COUNT {len(HC)}")
    lines.append(f"#define HC_TOTAL_PX {max(1, total)}")
    lines.append("static const HiSprite *const HC_ALL[%d] = {%s};" % (max(1, len(HC)), ",".join(f"&HC_{nm}" for nm, _ in HC) or "0"))
    for name, w, h in names:
        ox, oy = ORIGIN.get(name, (w // 2, h - 1))
        lines.append(f"static const Sprite SPR_{name} = {{{w},{h},SPR_{name}_px,{ox},{oy}}};")
    # full-color character frames, cropped to their opaque box (ox/oy relative to the crop; cx/cy = crop offset in the frame)
    lines.append("typedef struct { int16_t w, h; const uint16_t *px; int16_t ox, oy, cx, cy; } CharSprite;")
    CHN = set()
    for nm, fr in HCC:
        bb = fr.split()[3].point(lambda a: 255 if a >= 128 else 0).getbbox() or (0, 0, 1, 1)
        x0, y0, x1, y1 = bb; cr = fr.crop(bb); w, h = cr.size; data = []
        for (r, g, b, a) in (cr.get_flattened_data() if hasattr(cr, 'get_flattened_data') else cr.getdata()):
            if a < 128: data.append(0); continue
            if (r, g, b) == (22, 18, 25): data.append(1); continue   # baked outline pixel -> soft edge (darkens what's behind)
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            data.append(v if v > 1 else 0x0020)
        ox, oy = ORIGIN.get(nm, (fr.width // 2, fr.height - 1))
        lines.append(f"static const uint16_t CH_{nm}_px[{w*h}] = {{" + ",".join(map(str, data)) + "};")
        lines.append(f"static const CharSprite CH_{nm} = {{{w},{h},CH_{nm}_px,{ox - x0},{oy - y0},{x0},{y0}}};")
        CHN.add(nm)
    # animation tables
    def table(cname, prefixes, clips, ndirs, maxf):
        ok = all(f'{p}_{c}_{d}' in FRAMES for p in prefixes for c in clips for d in range(ndirs))
        if not ok: return
        lines.append(f"#define HAVE_{cname} 1")
        rows = []
        for p in prefixes:
            cr = []
            for c in clips:
                dr = []
                for d in range(ndirs):
                    n = FRAMES[f'{p}_{c}_{d}']
                    pre = 'CH' if f'{p}_{c}_{d}_0' in CHN else 'SPR'
                    dr.append('{' + ','.join(f'&{pre}_{p}_{c}_{d}_{k}' if k < n else '0' for k in range(maxf)) + '}')
                cr.append('{' + ','.join(dr) + '}')
            rows.append('{' + ','.join(cr) + '}')
        ty = 'CharSprite' if f'{prefixes[0]}_{clips[0]}_0_0' in CHN else 'Sprite'
        lines.append(f"static const {ty} *const {cname}[{len(prefixes)}][{len(clips)}][{ndirs}][{maxf}] = {{" + ','.join(rows) + "};")
        lines.append(f"static const uint8_t {cname}_N[{len(prefixes)}][{len(clips)}] = {{" + ','.join('{' + ','.join(str(FRAMES[f'{p}_{c}_0']) for c in clips) + '}' for p in prefixes) + "};")
    table('HERO_ANIM', ['hero_sword', 'hero_bow', 'hero_staff'], ['idle', 'walk', 'attack', 'cast', 'roll', 'hurt', 'death'], 5, 12)
    table('MOB_ANIM', ['mob_rat', 'mob_skel', 'mob_spider', 'mob_gob', 'mob_brute', 'mob_wraith'], ['walk', 'attack'], 2, 6)
    table('NPC_ANIM', ['npc_merchant', 'npc_smith', 'npc_jeweler', 'npc_trainer'], ['idle'], 1, 6)
    open(out, "w").write("\n".join(lines) + "\n")

if __name__ == "__main__":
    main(sys.argv[1])
