# device_test.py — on-device behavioural tests for the generated picovector
# bindings. Runs under MicroPython on the badge (push with mpremote, see
# tools/run_device_tests.sh). Depends only on the `picovector` module, so it has
# no app/screen side effects.
#
# Each test asserts a behaviour the API is expected to honour. Several are
# regression guards for issues found while bringing the generated bindings up on
# hardware (float args to int params, measure_text without a size, …). The final
# line is "DEVICE TESTS: <p> passed, <f> failed" which the host runner checks.

import picovector as pv
from picovector import vec2, rect, mat3, color, brush, shape, image

_p = 0
_f = 0


def ok(name, cond):
    global _p, _f
    if cond:
        _p += 1
        print("PASS", name)
    else:
        _f += 1
        print("FAIL", name)


def near(a, b, eps=0.01):
    return abs(a - b) < eps


def raises(name, excs, fn):
    try:
        fn()
        ok(name + " (no raise)", False)
    except excs:
        ok(name, True)
    except Exception as e:
        ok(name + " (wrong exc: " + type(e).__name__ + ")", False)


# ── vec2 ─────────────────────────────────────────────────────────────────────
v = vec2(3, 4)
ok("vec2 fields", v.x == 3 and v.y == 4)
ok("vec2 length", near(v.length(), 5))
ok("vec2 dot", near(v.dot(vec2(1, 0)), 3))
ok("vec2 add", (v + vec2(1, 1)).x == 4)
ok("vec2 scalar mul", (v * 2).y == 8)
ok("vec2 eq/ne", vec2(1, 2) == vec2(1, 2) and vec2(1, 2) != vec2(1, 3))
ok("vec2 normalized", near(v.normalized().length(), 1))

# ── mat3 ─────────────────────────────────────────────────────────────────────
m = mat3().translate(5, 0)
p = vec2(1, 1)
p.transform(m)
ok("mat3 translate+vec2.transform", near(p.x, 6) and near(p.y, 1))
s = mat3().scale(2)
q = vec2(3, 4)
q.transform(s)
ok("mat3 uniform scale", near(q.x, 6) and near(q.y, 8))

# ── rect ─────────────────────────────────────────────────────────────────────
r = rect(10, 20, 30, 40)
ok("rect fields", r.x == 10 and r.w == 30)
ok("rect aliases l/t", r.l == 10 and r.t == 20)
ok("rect computed r/b", r.r == 40 and r.b == 60)
ok("rect contains(vec2) inside", rect(0, 0, 10, 10).contains(vec2(5, 5)))
ok("rect contains(vec2) outside", not rect(0, 0, 10, 10).contains(vec2(20, 20)))
ok("rect intersects", rect(0, 0, 10, 10).intersects(rect(5, 5, 10, 10)))

# ── color (incl. float-arg regression) ──────────────────────────────────────
ok("color.rgb int", isinstance(color.rgb(255, 128, 0).p, int))
# REGRESSION: float-valued args (e.g. 100 * pulse) must be accepted, not rejected
ok("color.rgb float alpha", isinstance(color.rgb(0, 0, 0, 100 * 0.85).p, int))
ok("color.hsv", isinstance(color.hsv(180, 200, 200).p, int))
ok("color palette", isinstance(color.red.p, int) and isinstance(color.white.p, int))

# ── shape factories + overloads ─────────────────────────────────────────────
ok("shape.circle(x, y, r)", shape.circle(8, 8, 4).bounds().w > 0)
ok("shape.circle(vec2, r)  [XY overload]", shape.circle(vec2(8, 8), 4).bounds().w > 0)
ok("shape.rectangle(x, y, w, h)", shape.rectangle(0, 0, 10, 10).bounds().w > 0)
ok("shape.rectangle(rect)  [XYWH overload]", shape.rectangle(rect(0, 0, 10, 10)).bounds().w > 0)
ok("shape.arc (arg reorder)", shape.arc(8, 8, 2, 6, 0, 180).bounds().w > 0)
ok("shape.star (arg count)", shape.star(vec2(8, 8), 5, 8, 4).bounds().w > 0)
ok("shape.squircle (arg count)", shape.squircle(vec2(8, 8), 6).bounds().w > 0)
ok("shape.custom(points)", shape.custom([vec2(0, 0), vec2(10, 0), vec2(5, 10)]).bounds().w > 0)
ok("shape.stroke() returns shape", shape.circle(8, 8, 4).stroke(2).bounds().w > 0)

# ── image: raster + float args + draw + overloads ────────────────────────────
img = image(32, 32)
ok("image size", img.width == 32 and img.height == 32)
img.pen = color.rgb(255, 0, 0)
img.clear()
# REGRESSION: float w/h to a raster primitive
img.rectangle(1, 1, 4.0, 4.0)
ok("image.rectangle(float)", True)
img.line(vec2(0, 0), vec2(10, 10))     # vec2 form
img.line(0, 0, 5, 5)                   # x,y,x,y form
ok("image.line overloads", True)
img.circle(vec2(8, 8), 4)
img.circle(16, 16, 3)
ok("image.circle overloads", True)
img.put(vec2(2, 2))
got = img.get(vec2(2, 2))
ok("image.get returns color", isinstance(got.p, int))
img.shape(shape.circle(16, 16, 6))
img.shape([shape.circle(4, 4, 2), shape.rectangle(0, 0, 4, 4)])  # list form
ok("image.shape + list", True)
ok("image props antialias/fill_rule/alpha",
   (setattr(img, "antialias", img.X2), setattr(img, "fill_rule", img.NON_ZERO),
    setattr(img, "alpha", 200), img.alpha == 200)[-1])

# blit overloads (all four forms must dispatch)
src = image(8, 8)
src.pen = color.blue
src.clear()
img.blit(src, vec2(0, 0))
img.blit(src, 2, 2)
img.blit(src, rect(0, 0, 8, 8))
img.blit(src, rect(0, 0, 4, 4), rect(0, 0, 8, 8))
ok("image.blit (4 overloads)", True)

# REGRESSION: measure_text(text) with no size must reach the body (font check),
# i.e. raise OSError "no font" — NOT TypeError "missing required arguments".
raises("image.measure_text(text) arg count", OSError, lambda: image(8, 8).measure_text("hi"))
raises("image.text(...) needs a font", OSError, lambda: image(8, 8).text("hi", vec2(0, 0)))

# ── brush ────────────────────────────────────────────────────────────────────
img.pen = brush.gradient(brush.LINEAR, 0, 0, 1, 0, [(0.0, color.black), (1.0, color.white)])
ok("brush.gradient + image.pen=brush", True)
img.pen = brush.pixelate(2)
img.pen = brush.lighten(40)
ok("brush effects assignable to pen", True)
# pattern index is range-checked (0..37)
raises("brush.pattern index range", (ValueError, TypeError),
       lambda: brush.pattern(color.red, color.black, 99))
ok("brush.pattern(index)", brush.pattern(color.red, color.black, 3) is not None)
ok("brush.pattern(tuple[8])",
   brush.pattern(color.red, color.black, (1, 2, 4, 8, 16, 32, 64, 128)) is not None)

print("DEVICE TESTS:", _p, "passed,", _f, "failed")
