#!/usr/bin/env python3
"""Bake UNCHARTED_KEYBOARD_MOUSE_EN_V4 SVG assets into optimized binary prompt bank."""
import ctypes
import json
import os
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

def render_svg(svg_path, w, h):
    handle = rsvg.rsvg_handle_new_from_file(svg_path.encode('utf-8'), None)
    if not handle:
        raise RuntimeError(f"Failed to load {svg_path}")
    surf = cairo.cairo_image_surface_create(0, w, h) # CAIRO_FORMAT_ARGB32
    cr = cairo.cairo_create(surf)
    vp = RsvgRectangle(0.0, 0.0, float(w), float(h))
    ok = rsvg.rsvg_handle_render_document(handle, cr, ctypes.byref(vp), None)
    cairo.cairo_surface_flush(surf)
    ptr = cairo.cairo_image_surface_get_data(surf)
    # Read raw ARGB data and convert to RGBA
    buf = (ctypes.c_uint8 * (w * h * 4)).from_address(ptr)
    raw = bytearray(buf)
    rgba = bytearray(w * h * 4)
    # Cairo ARGB32 (little endian: B, G, R, A)
    for i in range(w * h):
        b = raw[i * 4 + 0]
        g = raw[i * 4 + 1]
        r = raw[i * 4 + 2]
        a = raw[i * 4 + 3]
        rgba[i * 4 + 0] = r
        rgba[i * 4 + 1] = g
        rgba[i * 4 + 2] = b
        rgba[i * 4 + 3] = a
    cairo.cairo_destroy(cr)
    cairo.cairo_surface_destroy(surf)
    return rgba

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
        print(f"Baking theme: {theme} ({len(keys)} keys)...")
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

    t1 = time.time()
    print(f"Rendered {len(entries)} items in {t1 - t0:.2f}s, blob size: {len(blob)/1024/1024:.2f} MB")

    # Binary Format:
    # Header:
    #   magic: 4 bytes 'SPMP' (0x504D5053)
    #   version: u32 (1)
    #   entry_count: u32
    #   entries_offset: u32
    #   data_offset: u32
    # Entry (64 bytes each):
    #   char id[32]
    #   u16 theme
    #   u16 pw
    #   u16 ph
    #   u16 reserved
    #   float u_w
    #   float u_h
    #   u32 offset
    #   u32 size
    #   u32 reserved2[2]

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
