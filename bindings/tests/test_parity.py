"""test_parity.py — prove the generated bindings cover the original surface.

Compares the surface scraped from the hand-written ``micropython/*.cpp`` (via
tools/extract_surface.py) against the surface described by the DSL stubs (via
model.load()).  The generated surface must be a *superset* — every original
method, property, constant, ``__del__`` and type feature must still be present —
so no app can break.  New additions (enhancements) are allowed and reported.

Run:  python tests/test_parity.py    (exit code 0 = parity holds)
"""

from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, "tools"))

import json                        # noqa: E402

import model                       # noqa: E402

SNAPSHOT = os.path.join(HERE, "original_surface.json")


def load_original():
    """Original surface to compare against. Prefer the frozen snapshot
    (tests/original_surface.json) so parity works after the hand-written
    micropython/*.cpp are removed; fall back to a live scrape if the snapshot is
    missing but the sources are still present. Regenerate the snapshot with
    `python tools/extract_surface.py > tests/original_surface.json`."""
    if os.path.exists(SNAPSHOT):
        with open(SNAPSHOT) as f:
            return json.load(f)
    import extract_surface
    return extract_surface.original_surface()

FEATURE_FLAGS = {
    "make_new": "has_make_new", "print": "has_print", "attr": "has_attr",
    "binary_op": "has_binary_op", "buffer": "has_buffer",
}


def generated_surface():
    out = {}
    for t in model.load():
        members = {m.name for m in t.members}
        members |= {p.name for p in t.props}
        for p in t.props:
            members |= set(p.aliases)
        members |= {c.name for c in t.consts}
        members |= {pc.name for pc in t.palette}
        if t.has_del:
            members.add("__del__")
        features = {f for f, attr in FEATURE_FLAGS.items() if getattr(t, attr)}
        out[t.name] = {"members": members, "features": features}
    return out


def main():
    orig = load_original()
    gen = generated_surface()

    ok = True
    for name, o in orig.items():
        g = gen.get(name)
        if g is None:
            print(f"✗ {name}: MISSING from generated bindings")
            ok = False
            continue
        missing = set(o["members"]) - g["members"]
        feat_missing = set(o["features"]) - g["features"]
        added = g["members"] - set(o["members"])
        if missing:
            print(f"✗ {name}: missing members {sorted(missing)}")
            ok = False
        if feat_missing:
            print(f"✗ {name}: missing features {sorted(feat_missing)}")
            ok = False
        if not missing and not feat_missing:
            extra = f"  (+{sorted(added)})" if added else ""
            print(f"✓ {name}: {len(o['members'])} members, "
                  f"features {sorted(o['features'])} all present{extra}")

    print()
    print("PARITY OK — generated surface covers the original."
          if ok else "PARITY FAILED — see ✗ above.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
