"""extract_surface.py — scrape the *original* hand-written PicoVector bindings
for their public surface, so we can prove the generated bindings cover it with
no regressions.

For each ``micropython/<type>.cpp`` it collects:
  * the locals-dict member/constant qstrs (methods, statics, constants, __del__)
  * the type's MicroPython feature slots (make_new / print / attr / binary_op /
    buffer)

Run directly to dump the surface as JSON.
"""

from __future__ import annotations

import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ORIG = os.path.normpath(os.path.join(HERE, "..", "..", "micropython"))

# original file stem -> type name (all match except the module table)
TYPES = ["vec2", "rect", "mat3", "color", "brush", "shape", "image",
         "font", "pixel_font", "algorithm"]

_ROM_PTR = re.compile(r"MPY_BIND_ROM_PTR(?:_STATIC)?\((\w+)\)")
_ROM_INT = re.compile(r"MPY_BIND_ROM_INT\((\w+)")
_QSTR = re.compile(r"MP_QSTR_([A-Za-z_][A-Za-z0-9_]*)")
_ATTR_CASE = re.compile(r"case\s+MP_QSTR_(\w+)")
_TYPEDEF = re.compile(r"MP_DEFINE_CONST_OBJ_TYPE\((.*?)\);", re.S)

FEATURES = ("make_new", "print", "attr", "binary_op", "buffer")
# qstrs that appear in attr handlers but aren't part of the public surface
_IGNORE = {"raw"}  # `raw` is an internal framebuffer view (kept, see note below)
_IGNORE = set()


_COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)


def _read(name):
    path = os.path.join(ORIG, f"{name}.cpp")
    with open(path) as f:
        src = f.read()
    # strip comments so disabled/commented-out members aren't counted
    return _COMMENT.sub("", src)


def surface_of(name):
    src = _read(name)
    # locals-dict block: from the MPY_BIND_LOCALS_DICT macro to the type def
    li = src.find("MPY_BIND_LOCALS_DICT")
    ti = src.find("MP_DEFINE_CONST_OBJ_TYPE")
    block = src[li:ti] if li >= 0 and ti >= 0 else ""

    members = set(_ROM_PTR.findall(block)) | set(_ROM_INT.findall(block))
    members |= set(_QSTR.findall(block))            # inline { MP_QSTR_x, ... }
    if "MPY_BIND_ROM_PTR_DEL" in block:
        members.add("__del__")
    members |= set(_ATTR_CASE.findall(src))         # attr properties
    members -= _IGNORE

    features = set()
    for tdef in _TYPEDEF.findall(src):
        if f"type_{name}" not in tdef:
            continue
        for feat in FEATURES:
            if re.search(rf"\b{feat}\b\s*,", tdef):
                features.add(feat)
    return {"members": sorted(members), "features": sorted(features)}


def original_surface():
    return {name: surface_of(name) for name in TYPES}


if __name__ == "__main__":
    print(json.dumps(original_surface(), indent=2))
