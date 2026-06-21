"""test_generate.py — spot-check that the generator emits the expected C++ for a
few representative members (scalar return, arg reordering, overload dispatch,
optional argument + boxing). Complements the surface-parity and compile checks.

Run:  python tests/test_generate.py
"""

from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, ROOT)

import model            # noqa: E402
import generate         # noqa: E402

CHECKS = {
    "vec2": [
        # scalar method: direct call, float-boxed return
        "return mp_obj_new_float(self->v.dot(other));",
        # binary op switch has a default (the -Werror=switch fix)
        "default: break;",
    ],
    "shape": [
        # arc reorders Python (inner, outer, from, to) -> C++ (from, to, inner, outer)
        "arc(c.x, c.y, from_a, to_a, inner, outer)",
        # custom builds the shape from a path list and boxes it
        "pv::box_shape(paths_shape)",
    ],
    "color": [
        # optional alpha defaults to 255; colour boxed by value (no leaky `new`)
        "int a = 255;",
        "pv::box_color(rgb_color_t(r, g, b, a))",
    ],
    "image": [
        # blit overloads: n_args checked first, then arg types; receiver is `src`
        "if (n_args == 3 && mp_obj_is_type(args[1], &type_image) "
        "&& mp_obj_is_type(args[2], &type_vec2)) {",
        "src->blit(self->image, source, dst, filter)",
    ],
    "brush": [
        # pixelate clamps size at 1 (Range(1, None, clamp=True))
        "if (size < 1) size = 1;",
    ],
}


def main():
    types = {t.name: t for t in model.load()}
    ok = True
    for name, needles in CHECKS.items():
        src = generate.emit_type_file(types[name])
        for needle in needles:
            if needle in src:
                print(f"✓ {name}: {needle[:60]}")
            else:
                print(f"✗ {name}: MISSING {needle!r}")
                ok = False
    print()
    print("GENERATOR OK" if ok else "GENERATOR CHECK FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
