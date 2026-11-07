#!/usr/bin/env python3
"""Bake UNCHARTED_KEYBOARD_MOUSE_EN_V4 SVG assets into optimized binary prompt bank with 1U square variants."""
import ctypes
import json
import os
import re
import struct
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVG_DIR = os.path.join(ROOT, "..", "UNCHARTED_KEYBOARD_MOUSE_EN_V4")
JSON_PATH = os.path.join(SVG_DIR, "KEY_MAPPING_EN.json")
OUT_BIN = os.path.join(ROOT, "gow3_pc", "gpu", "shim", "gow3_prompt_assets.bin")

# Load cairo & rsvg
os.add_dll_directory(os.path.join(ROOT, "gow3_pc", "out"))
cairo = ctypes.CDLL(os.path.join(ROOT, "gow3_pc", "out", "libcairo-2.dll"))
rsvg = ctypes.CDLL(os.path.join(ROOT, "gow3_pc", "out", "librsvg-2-2.dll"))

rsvg.rsvg_handle_new_from_file.argtypes = [ctypes.c_char_p, ctypes.c_void_p]
rsvg.rsvg_handle_new_from_file.restype = ctypes.c_void_p
rsvg.rsvg_handle_new_from_data.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p]
rsvg.rsvg_handle_new_from_data.restype = ctypes.c_void_p
cairo.cairo_image_surface_create.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
cairo.cairo_image_surface_create.restype = ctypes.c_void_p
cairo.cairo_create.argtypes = [ctypes.c_void_p]
cairo.cairo_create.restype = ctypes.c_void_p
cairo.cairo_surface_destroy.argtypes = [ctypes.c_void_p]
cairo.cairo_destroy.argtypes = [ctypes.c_void_p]
cairo.cairo_image_surface_get_data.argtypes = [ctypes.c_void_p]
cairo.cairo_image_surface_get_data.restype = ctypes.c_void_p
cairo.cairo_surface_flush.argtypes = [ctypes.c_void_p]

class RsvgRectangle(ctypes.Structure):
    _fields_ = [
        ('x', ctypes.c_double), ('y', ctypes.c_double),
        ('width', ctypes.c_double), ('height', ctypes.c_double)
    ]

rsvg.rsvg_handle_render_document.argtypes = [
    ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(RsvgRectangle), ctypes.c_void_p
]
rsvg.rsvg_handle_render_document.restype = ctypes.c_bool

def _surface_to_rgba(surf, w, h):
    cairo.cairo_surface_flush(surf)
    ptr = cairo.cairo_image_surface_get_data(surf)
    buf = (ctypes.c_uint8 * (w * h * 4)).from_address(ptr)
    raw = bytearray(buf)
    rgba = bytearray(w * h * 4)
    for i in range(w * h):
        b = raw[i * 4 + 0]
        g = raw[i * 4 + 1]
        r = raw[i * 4 + 2]
        a = raw[i * 4 + 3]
        rgba[i * 4 + 0] = r
        rgba[i * 4 + 1] = g
        rgba[i * 4 + 2] = b
        rgba[i * 4 + 3] = a
    return rgba

def render_svg(svg_path, w, h):
    handle = rsvg.rsvg_handle_new_from_file(svg_path.encode('utf-8'), None)
    if not handle:
        raise RuntimeError(f"Failed to load {svg_path}")
    surf = cairo.cairo_image_surface_create(0, w, h)
    cr = cairo.cairo_create(surf)
    vp = RsvgRectangle(0.0, 0.0, float(w), float(h))
    rsvg.rsvg_handle_render_document(handle, cr, ctypes.byref(vp), None)
    rgba = _surface_to_rgba(surf, w, h)
    cairo.cairo_destroy(cr)
    cairo.cairo_surface_destroy(surf)
    return rgba

def render_svg_str(svg_str, w, h):
    svg_bytes = svg_str.encode('utf-8')
    handle = rsvg.rsvg_handle_new_from_data(svg_bytes, len(svg_bytes), None)
    if not handle:
        raise RuntimeError("Failed to parse SVG data")
    surf = cairo.cairo_image_surface_create(0, w, h)
    cr = cairo.cairo_create(surf)
    vp = RsvgRectangle(0.0, 0.0, float(w), float(h))
    rsvg.rsvg_handle_render_document(handle, cr, ctypes.byref(vp), None)
    rgba = _surface_to_rgba(surf, w, h)
    cairo.cairo_destroy(cr)
    cairo.cairo_surface_destroy(surf)
    return rgba

def get_1u_svg_templates(theme):
    esc_file = os.path.join(SVG_DIR, theme, "ESCAPE.svg")
    with open(esc_file, "r", encoding="utf-8") as f:
        esc_svg = f.read()
    base_1u = re.sub(r'<g><path d="M169 1493H1104.*?</g>', '%%LEGEND%%', esc_svg, flags=re.DOTALL)

    ink = "#fcf0d7" if theme == "OBSIDIAN" else "#142936"
    stroke = "#040b10" if theme == "OBSIDIAN" else "#f9eacb"
    bar_stroke = "#c9aa75" if theme == "OBSIDIAN" else "#91693f"

    legends = {
        "SPACE_1U": f'<g><path d="M32.00 86 H96.00" fill="none" stroke="{bar_stroke}" stroke-width="2.2" opacity=".72"/><path d="M1104 1446V1130Q994 1185 888.5 1213.0Q783 1241 689 1241Q565 1241 505.5 1203.0Q446 1165 446 1085Q446 1025 486.0 991.5Q526 958 632 934L779 901Q1004 851 1098.0 749.0Q1192 647 1192 459Q1192 212 1060.5 91.5Q929 -29 658 -29Q530 -29 401.5 -2.0Q273 25 144 78V403Q273 327 393.0 288.5Q513 250 625 250Q738 250 798.5 292.0Q859 334 859 412Q859 482 818.0 520.0Q777 558 654 588L520 621Q319 669 225.5 774.0Q132 879 132 1057Q132 1280 262.0 1400.0Q392 1520 634 1520Q745 1520 862.0 1501.5Q979 1483 1104 1446ZM1496 1493H2071Q2328 1493 2465.0 1366.5Q2602 1240 2602 1006Q2602 771 2465.0 644.5Q2328 518 2071 518H1843V0H1496ZM1843 1214V797H2034Q2135 797 2190.0 851.5Q2245 906 2245 1006Q2245 1106 2190.0 1160.0Q2135 1214 2034 1214ZM3662 272H3120L3034 0H2686L3184 1493H3597L4095 0H3746ZM3206 549H3574L3391 1143ZM5338 82Q5242 27 5138.5 -1.0Q5035 -29 4923 -29Q4587 -29 4391.0 179.5Q4195 388 4195 745Q4195 1103 4391.0 1311.5Q4587 1520 4923 1520Q5035 1520 5138.5 1492.0Q5242 1464 5338 1409V1100Q5241 1173 5147.5 1207.0Q5054 1241 4951 1241Q4765 1241 4659.0 1109.0Q4553 977 4553 745Q4553 514 4659.0 382.0Q4765 250 4951 250Q5054 250 5147.5 284.0Q5241 318 5338 391ZM5624 1493H6559V1202H5971V924H6524V633H5971V291H6579V0H5624Z" transform="translate(15.5 73.0) scale(0.015 -0.015)" fill="{ink}" stroke="{stroke}" stroke-width=".9" vector-effect="non-scaling-stroke" stroke-linejoin="round" paint-order="stroke fill"/></g>',
        "CONTROL_1U": f'<g><path d="M1235 82Q1139 27 1035.5 -1.0Q932 -29 820 -29Q484 -29 288.0 179.5Q92 388 92 745Q92 1103 288.0 1311.5Q484 1520 820 1520Q932 1520 1035.5 1492.0Q1139 1464 1235 1409V1100Q1138 1173 1044.5 1207.0Q951 1241 848 1241Q662 1241 556.0 1109.0Q450 977 450 745Q450 514 556.0 382.0Q662 250 848 250Q951 250 1044.5 284.0Q1138 318 1235 391ZM1361 1493H2599V1202H2154V0H1807V1202H1361ZM3271 831Q3379 831 3426.5 876.0Q3474 921 3474 1024Q3474 1126 3426.5 1170.0Q3379 1214 3271 1214H3125V831ZM3125 565V0H2778V1493H3307Q3573 1493 3696.5 1394.0Q3820 1295 3820 1081Q3820 933 3756.0 838.0Q3692 743 3562 698Q3633 680 3689.5 616.5Q3746 553 3803 424L3991 0H3622L3459 371Q3409 483 3358.0 524.0Q3307 565 3223 565ZM4197 1493H4544V291H5152V0H4197Z" transform="translate(13.0 80.8) scale(0.020 -0.020)" fill="{ink}" stroke="{stroke}" stroke-width=".9" vector-effect="non-scaling-stroke" stroke-linejoin="round" paint-order="stroke fill"/></g>',
        "SHIFT_1U": f'<g><path d="M48 50 V41 H42 L56 27 L70 41 H64 V50 Z" fill="{ink}"/><path d="M1104 1446V1130Q994 1185 888.5 1213.0Q783 1241 689 1241Q565 1241 505.5 1203.0Q446 1165 446 1085Q446 1025 486.0 991.5Q526 958 632 934L779 901Q1004 851 1098.0 749.0Q1192 647 1192 459Q1192 212 1060.5 91.5Q929 -29 658 -29Q530 -29 401.5 -2.0Q273 25 144 78V403Q273 327 393.0 288.5Q513 250 625 250Q738 250 798.5 292.0Q859 334 859 412Q859 482 818.0 520.0Q777 558 654 588L520 621Q319 669 225.5 774.0Q132 879 132 1057Q132 1280 262.0 1400.0Q392 1520 634 1520Q745 1520 862.0 1501.5Q979 1483 1104 1446ZM1618 1493H1965V883H2646V1493H2993V0H2646V612H1965V0H1618ZM3348 1493H3695V0H3348ZM4061 1493H5118V1214H4408V936H5044V657H4408V0H4061ZM5663 1214H5249V1493H6424V1214H6010V0H5663Z" transform="translate(18.0 92.0) scale(0.016 -0.016)" fill="{ink}" stroke="{stroke}" stroke-width=".9" vector-effect="non-scaling-stroke" stroke-linejoin="round" paint-order="stroke fill"/></g>',
        "TAB_1U": f'<g><path d="M42 42 H76 L68 34 M76 42 L68 50 M42 66 H76" fill="none" stroke="{ink}" stroke-width="4.0" stroke-linecap="round" stroke-linejoin="round"/><path d="M68 66 L76 58 M68 66 L76 74" fill="none" stroke="{ink}" stroke-width="4.0" stroke-linecap="round" stroke-linejoin="round"/><path d="M37 1493H1613V1202H1001V0H654V1202H37ZM1854 272H2396L2482 0H2830L2332 1493H1919L1421 0H1768ZM2310 549H1942L2125 1143ZM2827 1493H3716Q4009 1493 4165.0 1373.0Q4321 1253 4321 1060Q4321 958 4268.0 878.5Q4215 799 4118 757Q4250 718 4320.0 621.0Q4390 524 4390 393Q4390 197 4218.0 84.0Q4046 -29 3716 -29H2827ZM3174 887V1214H3630Q3787 1214 3867.5 1152.0Q3948 1090 3948 997Q3948 901 3862.5 894.0Q3777 887 3630 887ZM3174 250V614H3676Q3830 614 3919.5 541.0Q4009 468 4009 359Q4009 250 3676 250Z" transform="translate(30.0 102.0) scale(0.015 -0.015)" fill="{ink}" stroke="{stroke}" stroke-width=".9" vector-effect="non-scaling-stroke" stroke-linejoin="round" paint-order="stroke fill"/></g>',
        "ENTER_1U": f'<g><path d="M78 44 V60 H50 L59 51 M50 60 L59 69" fill="none" stroke="{ink}" stroke-width="5.0" stroke-linecap="round" stroke-linejoin="round"/><path d="M169 1493H1104V1202H516V924H1069V633H516V291H1124V0H169ZM1491 1493H1811L2538 410V1493H2885V0H2565L1838 1083V0H1491ZM3063 1493H4299V1202H3854V0H3507V1202H3063ZM4480 1493H5415V1202H4827V924H5380V633H4827V291H5435V0H4480ZM6248 831Q6356 831 6403.5 876.0Q6451 921 6451 1024Q6451 1126 6403.5 1170.0Q6356 1214 6248 1214H6102V831ZM6102 565V0H5755V1493H6284Q6550 1493 6673.5 1394.0Q6797 1295 6797 1081Q6797 933 6733.0 838.0Q6669 743 6539 698Q6610 680 6666.5 616.5Q6723 553 6780 424L6968 0H6599L6436 371Q6386 483 6335.0 524.0Q6284 565 6200 565ZM7174 1493H7521V0H7174Z" transform="translate(18.0 98.0) scale(0.0125 -0.0125)" fill="{ink}" stroke="{stroke}" stroke-width=".9" vector-effect="non-scaling-stroke" stroke-linejoin="round" paint-order="stroke fill"/></g>',
        "BACKSPACE_1U": f'<g><path d="M78 52 H46 L58 40 M46 52 L58 64" fill="none" stroke="{ink}" stroke-width="5.0" stroke-linecap="round" stroke-linejoin="round"/><path d="M2827 1493H3716Q4009 1493 4165.0 1373.0Q4321 1253 4321 1060Q4321 958 4268.0 878.5Q4215 799 4118 757Q4250 718 4320.0 621.0Q4390 524 4390 393Q4390 197 4218.0 84.0Q4046 -29 3716 -29H2827ZM3174 887V1214H3630Q3787 1214 3867.5 1152.0Q3948 1090 3948 997Q3948 901 3862.5 894.0Q3777 887 3630 887ZM3174 250V614H3676Q3830 614 3919.5 541.0Q4009 468 4009 359Q4009 250 3676 250ZM4480 1493H4827V0H4480ZM6248 831Q6356 831 6403.5 876.0Q6451 921 6451 1024Q6451 1126 6403.5 1170.0Q6356 1214 6248 1214H6102V831ZM6102 565V0H5755V1493H6284Q6550 1493 6673.5 1394.0Q6797 1295 6797 1081Q6797 933 6733.0 838.0Q6669 743 6539 698Q6610 680 6666.5 616.5Q6723 553 6780 424L6968 0H6599L6436 371Q6386 483 6335.0 524.0Q6284 565 6200 565ZM7174 1493H7521V0H7174Z" transform="translate(18.0 98.0) scale(0.013 -0.013)" fill="{ink}" stroke="{stroke}" stroke-width=".9" vector-effect="non-scaling-stroke" stroke-linejoin="round" paint-order="stroke fill"/></g>',
        "ALT_1U": f'<g><path d="M1854 272H2396L2482 0H2830L2332 1493H1919L1421 0H1768ZM2310 549H1942L2125 1143ZM2827 1493H3174V291H3782V0H2827ZM4197 1493H5433V1202H4988V0H4641V1202H4197Z" transform="translate(14.0 80.8) scale(0.020 -0.020)" fill="{ink}" stroke="{stroke}" stroke-width=".9" vector-effect="non-scaling-stroke" stroke-linejoin="round" paint-order="stroke fill"/></g>'
    }

    results = {}
    for name, leg in legends.items():
        results[name] = base_1u.replace('%%LEGEND%%', leg)
    return results

def get_wide_svg_templates(theme):
    tab_file = os.path.join(SVG_DIR, theme, "TAB.svg")
    with open(tab_file, "r", encoding="utf-8") as f:
        tab_svg = f.read()
    base_wide = re.sub(r'<g>.*?</g>\s*</svg>', '%%LEGEND%%</svg>', tab_svg, flags=re.DOTALL)

    ink = "#fcf0d7" if theme == "OBSIDIAN" else "#142936"
    stroke = "#040b10" if theme == "OBSIDIAN" else "#f9eacb"

    wide_results = {}

    # TAB_WIDE: Enhanced bold arrows and large TAB text for 1.5U slots
    tab_wide_legend = (
        f'<g><path d="M30 46 H62 L52 36 M62 46 L52 56 M30 76 H62 L52 66 M62 76 L52 86" '
        f'fill="none" stroke="{ink}" stroke-width="4.8" stroke-linecap="round" stroke-linejoin="round"/>'
        f'<path d="M9 1493H1247V1202H802V0H455V1202H9ZM2242 272H1700L1614 0H1266L1764 1493H2177L2675 0H2326ZM1786 549H2154L1971 1143ZM3390 915Q3472 915 3514.5 955.0Q3557 995 3557 1073Q3557 1150 3514.5 1190.5Q3472 1231 3390 1231H3199V915ZM3402 262Q3507 262 3559.5 311.0Q3612 360 3612 459Q3612 556 3559.5 604.5Q3507 653 3402 653H3199V262ZM3724 799Q3836 763 3897.0 666.0Q3958 569 3958 428Q3958 212 3826.5 106.0Q3695 0 3427 0H2852V1493H3372Q3652 1493 3777.5 1399.0Q3903 1305 3903 1098Q3903 989 3857.5 912.5Q3812 836 3724 799Z" '
        f'transform="translate(86.0 80.0) scale(0.024 -0.024)" fill="{ink}" stroke="{stroke}" stroke-width=".9" '
        f'vector-effect="non-scaling-stroke" stroke-linejoin="round" paint-order="stroke fill"/></g>'
    )
    wide_results["TAB_WIDE"] = base_wide.replace('%%LEGEND%%', tab_wide_legend)

    # For letters A..Z and digits 0..9: center in 1.5U keycap (192 wide, center at 96 vs 64)
    for letter in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789":
        fname = f"KEY_{letter}.svg" if letter.isalpha() else f"DIGIT_{letter}.svg"
        fpath = os.path.join(SVG_DIR, theme, fname)
        if os.path.exists(fpath):
            with open(fpath, "r", encoding="utf-8") as f:
                content = f.read()
            leg_m = re.search(r'(<g>.*?</g>)\s*</svg>', content, flags=re.DOTALL)
            if leg_m:
                leg = leg_m.group(1)
                wide_leg = re.sub(
                    r'translate\(([\d.]+)\s+([\d.]+)\)',
                    lambda m: f'translate({float(m.group(1)) + 32.0:.4f} {float(m.group(2)):.4f})',
                    leg
                )
                wide_results[f"KEY_{letter}_WIDE"] = base_wide.replace('%%LEGEND%%', wide_leg)

    return wide_results

def bake():
    print(f"Loading metadata: {JSON_PATH}")
    with open(JSON_PATH, 'r', encoding='utf-8') as f:
        data = json.load(f)

    keys = data['keys']
    themes = ['OBSIDIAN', 'IVORY']
    BASE_U = 128 # Nominal 1U height in pixels

    entries = []
    blob = bytearray()

    t0 = time.time()
    for theme_idx, theme in enumerate(themes):
        print(f"Baking theme: {theme} ({len(keys)} standard keys)...")
        for k in keys:
            kid = k['id']
            size_u = k['size_u']
            uw, uh = float(size_u[0]), float(size_u[1])
            pw = max(16, int(round(uw * BASE_U)))
            ph = max(16, int(round(uh * BASE_U)))
            rel_svg = k['assets'][theme]
            svg_path = os.path.join(SVG_DIR, rel_svg)
            rgba = render_svg(svg_path, pw, ph)

            data_offset = len(blob)
            data_size = len(rgba)
            blob.extend(rgba)

            id_bytes = kid.encode('utf-8')[:31]
            entries.append({
                'id': id_bytes,
                'theme': theme_idx,
                'u_w': uw,
                'u_h': uh,
                'pw': pw,
                'ph': ph,
                'offset': data_offset,
                'size': data_size,
            })

        # Bake 1U square variants for wide keys
        print(f"Baking 1U square variants for {theme}...")
        u1_svgs = get_1u_svg_templates(theme)
        u1_aliases = {
            "SPACE_1U": ["SPACE_1U"],
            "CONTROL_1U": ["CONTROL_1U", "CONTROL_LEFT_1U", "CONTROL_RIGHT_1U"],
            "SHIFT_1U": ["SHIFT_1U", "SHIFT_LEFT_1U", "SHIFT_RIGHT_1U"],
            "TAB_1U": ["TAB_1U"],
            "ENTER_1U": ["ENTER_1U"],
            "BACKSPACE_1U": ["BACKSPACE_1U"],
            "ALT_1U": ["ALT_1U", "ALT_LEFT_1U", "ALT_RIGHT_1U"]
        }

        for base_name, svg_content in u1_svgs.items():
            pw = BASE_U
            ph = BASE_U
            rgba = render_svg_str(svg_content, pw, ph)
            data_offset = len(blob)
            data_size = len(rgba)
            blob.extend(rgba)

            aliases = u1_aliases.get(base_name, [base_name])
            for alias in aliases:
                id_bytes = alias.encode('utf-8')[:31]
                entries.append({
                    'id': id_bytes,
                    'theme': theme_idx,
                    'u_w': 1.0,
                    'u_h': 1.0,
                    'pw': pw,
                    'ph': ph,
                    'offset': data_offset,
                    'size': data_size,
                })

        # Bake 1.5U wide variants for shoulder buttons and wide slots
        print(f"Baking 1.5U wide variants for {theme}...")
        wide_svgs = get_wide_svg_templates(theme)
        for base_name, svg_content in wide_svgs.items():
            pw = int(round(1.5 * BASE_U))
            ph = BASE_U
            rgba = render_svg_str(svg_content, pw, ph)
            data_offset = len(blob)
            data_size = len(rgba)
            blob.extend(rgba)

            aliases = [base_name]
            if base_name.startswith("KEY_") and base_name.endswith("_WIDE"):
                short_name = base_name[4:-5] + "_WIDE"
                aliases.append(short_name)
            elif base_name == "TAB_WIDE":
                aliases.extend(["TAB_1_5U"])

            for alias in aliases:
                id_bytes = alias.encode('utf-8')[:31]
                entries.append({
                    'id': id_bytes,
                    'theme': theme_idx,
                    'u_w': 1.5,
                    'u_h': 1.0,
                    'pw': pw,
                    'ph': ph,
                    'offset': data_offset,
                    'size': data_size,
                })

    t1 = time.time()
    print(f"Rendered {len(entries)} items in {t1 - t0:.2f}s, blob size: {len(blob)/1024/1024:.2f} MB")

    header_size = 32
    entry_size = 64
    entries_offset = header_size
    data_offset = entries_offset + len(entries) * entry_size

    header = struct.pack(
        '<4sIIII12s',
        b'SPMP', 1, len(entries), entries_offset, data_offset, b'\x00' * 12
    )

    packed_entries = bytearray()
    for e in entries:
        id_padded = e['id'].ljust(32, b'\0')
        packed = struct.pack(
            '<32sHHHHffIIII',
            id_padded,
            e['theme'],
            e['pw'],
            e['ph'],
            0,
            e['u_w'],
            e['u_h'],
            e['offset'],
            e['size'],
            0,
            0
        )
        packed_entries.extend(packed)

    os.makedirs(os.path.dirname(OUT_BIN), exist_ok=True)
    with open(OUT_BIN, 'wb') as f:
        f.write(header)
        f.write(packed_entries)
        f.write(blob)

    print(f"Wrote {len(entries)} entries to {OUT_BIN} ({os.path.getsize(OUT_BIN)/1024/1024:.2f} MB)")

if __name__ == '__main__':
    bake()
