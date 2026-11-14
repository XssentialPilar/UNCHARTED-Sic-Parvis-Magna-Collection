"""Rasterize the supplied journal/map SVGs once; runtime decompresses only requested prompts.

Requires the existing Windows librsvg/Cairo/GObject DLLs. SVG viewBox proportions and
transparency are preserved. Cairo's PNG writer converts its premultiplied surface correctly.
The source archive is read only; the output is a separate generated resource bank.
"""
import argparse
import ctypes as C
import hashlib
import io
import json
import os
from pathlib import Path
import zipfile
import xml.etree.ElementTree as ET


class Rectangle(C.Structure):
    _fields_ = [(field, C.c_double) for field in ("x", "y", "width", "height")]


class Renderer:
    def __init__(self, dll_dir):
        self.dll_search = os.add_dll_directory(str(dll_dir.resolve()))
        self.cairo = C.CDLL(str(dll_dir / "libcairo-2.dll"))
        self.rsvg = C.CDLL(str(dll_dir / "librsvg-2-2.dll"))
        self.gobject = C.CDLL(str(dll_dir / "libgobject-2.0-0.dll"))
        self.rsvg.rsvg_handle_new_from_data.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p]
        self.rsvg.rsvg_handle_new_from_data.restype = C.c_void_p
        self.rsvg.rsvg_handle_render_document.argtypes = [C.c_void_p, C.c_void_p, C.POINTER(Rectangle), C.c_void_p]
        self.rsvg.rsvg_handle_render_document.restype = C.c_bool
        self.gobject.g_object_unref.argtypes = [C.c_void_p]
        self.gobject.g_object_unref.restype = None
        self.cairo.cairo_image_surface_create.argtypes = [C.c_int, C.c_int, C.c_int]
        self.cairo.cairo_image_surface_create.restype = C.c_void_p
        self.cairo.cairo_create.argtypes = [C.c_void_p]
        self.cairo.cairo_create.restype = C.c_void_p
        self.cairo.cairo_destroy.argtypes = [C.c_void_p]
        self.cairo.cairo_destroy.restype = None
        self.cairo.cairo_surface_destroy.argtypes = [C.c_void_p]
        self.cairo.cairo_surface_destroy.restype = None
        self.write_callback_type = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p, C.c_uint)
        self.cairo.cairo_surface_write_to_png_stream.argtypes = [C.c_void_p, self.write_callback_type, C.c_void_p]
        self.cairo.cairo_surface_write_to_png_stream.restype = C.c_int

    def render(self, svg):
        handle = self.rsvg.rsvg_handle_new_from_data(svg, len(svg), None)
        if not handle:
            raise ValueError("SVG parse failed")
        surface = self.cairo.cairo_image_surface_create(0, 512, 256)
        context = self.cairo.cairo_create(surface)
        chunks = []

        @self.write_callback_type
        def write(_closure, data, length):
            chunks.append(C.string_at(data, length))
            return 0

        try:
            if not self.rsvg.rsvg_handle_render_document(handle, context, C.byref(Rectangle(0, 0, 512, 256)), None):
                raise ValueError("SVG rendering failed")
            if self.cairo.cairo_surface_write_to_png_stream(surface, write, None) != 0:
                raise ValueError("PNG encoding failed")
            return b"".join(chunks)
        finally:
            self.cairo.cairo_destroy(context)
            self.cairo.cairo_surface_destroy(surface)
            self.gobject.g_object_unref(handle)


def without_binding(svg):
    """Keep the supplied journal/map illustration when its action is unbound."""
    root = ET.fromstring(svg)
    controls = [child for child in root if child.tag == "{http://www.w3.org/2000/svg}svg"]
    if len(controls) != 1:
        raise ValueError("Expected one explicit keyboard/mouse layer in the supplied SVG")
    root.remove(controls[0])
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def bake(source, output, dll_dir):
    renderer = Renderer(dll_dir)
    source_bytes = source.read_bytes()
    archive = zipfile.ZipFile(io.BytesIO(source_bytes))
    if archive.testzip() is not None:
        raise ValueError("Source archive failed its CRC check")
    prefix = "UNCHARTED_NATE_JOURNAL_MAP_HUD_V1/"
    manifest = json.loads(archive.read(prefix + "MANIFEST.json"))
    files = manifest["files"]
    if len(files) != 596:
        raise ValueError("Expected 596 SVG variants")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".building.zip")
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_STORED) as bank:
            for index, entry in enumerate(files):
                relative = entry["asset"]
                if relative != f'{entry["object"]}/{entry["theme"]}/{entry["key"]}.svg':
                    raise ValueError("Unexpected SVG path in manifest")
                png = renderer.render(archive.read(prefix + relative))
                bank.writestr(zipfile.ZipInfo(relative[:-4] + ".png"), png)
                if index % 100 == 0:
                    print(f"Rendered {index + 1}/{len(files)}", flush=True)
            # An unassigned action keeps the real illustration and has no invented key.
            # Only the nested control layer is removed; embedded art, glow and bounds stay.
            for kind in ("JOURNAL", "MAP"):
                for theme in ("OBSIDIAN", "IVORY"):
                    svg = archive.read(prefix + f"{kind}/{theme}/TAB.svg")
                    bank.writestr(zipfile.ZipInfo(f"{kind}/{theme}/UNBOUND.png"),
                                  renderer.render(without_binding(svg)))
            provenance = {"source": source.name, "source_sha256": hashlib.sha256(source_bytes).hexdigest(),
                          "variants": len(files), "generated_unbound_variants": 4,
                          "raster_size": [512, 256], "uniform_scaling": True}
            bank.writestr(zipfile.ZipInfo("manifest.json"), json.dumps(provenance, indent=2))
        with zipfile.ZipFile(temporary) as check:
            if check.testzip() is not None:
                raise ValueError("Generated bank failed its CRC check")
        os.replace(temporary, output)
    finally:
        archive.close()
    print(f"Verified bank: {output} ({output.stat().st_size} bytes)", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--dll-dir", type=Path, required=True)
    args = parser.parse_args()
    bake(args.source, args.output, args.dll_dir)
