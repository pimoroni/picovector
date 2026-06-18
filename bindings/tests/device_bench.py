# device_bench.py — basic on-device microbenchmarks for the generated picovector
# bindings. Runs under MicroPython on the badge (push with mpremote, see
# tools/run_device_bench.sh). Times the API directly with time.ticks_us — it
# does NOT depend on the firmware's "[pv] fps=" debug line.
#
# Each row reports microseconds per operation and operations/second for a tight
# loop of one API call, so you can spot the cost of binding/marshalling vs the
# underlying C++ work, and catch performance regressions in the hot paths
# (vec2 maths, shape construction, raster + vector draw, blit).
#
# Numbers include Python loop + binding overhead (i.e. what an app actually
# pays). gc.collect() runs before each case to keep GC noise out of the timing
# where possible; allocation-heavy cases still amortise GC, which is realistic.

import time
import gc
from picovector import vec2, rect, mat3, color, brush, shape, image

print("%-34s %7s %11s %12s" % ("benchmark", "iters", "us/op", "ops/s"))
print("-" * 68)


def bench(name, n, fn):
    gc.collect()
    t0 = time.ticks_us()
    fn(n)
    dt = time.ticks_diff(time.ticks_us(), t0)
    per = dt / n
    ops = 1000000.0 / per if per > 0 else 0
    print("%-34s %7d %11.3f %12.0f" % (name, n, per, ops))


# ── vec2 / mat3 maths (hot path) ─────────────────────────────────────────────
def b_vec2_add(n):
    a, b = vec2(1, 2), vec2(3, 4)
    for _ in range(n):
        c = a + b

def b_vec2_dot(n):
    a, b = vec2(1, 2), vec2(3, 4)
    for _ in range(n):
        d = a.dot(b)

def b_vec2_length(n):
    a = vec2(3, 4)
    for _ in range(n):
        d = a.length()

def b_vec2_normalized(n):
    a = vec2(3, 4)
    for _ in range(n):
        c = a.normalized()

def b_vec2_transform(n):
    m = mat3().translate(1, 1).rotate(10)
    for _ in range(n):
        p = vec2(2, 3)
        p.transform(m)

def b_mat3_mul(n):
    a = mat3().translate(1, 2)
    b = mat3().rotate(30)
    for _ in range(n):
        c = a.multiply(b)

bench("vec2 add", 4000, b_vec2_add)
bench("vec2 dot", 4000, b_vec2_dot)
bench("vec2 length", 4000, b_vec2_length)
bench("vec2 normalized (alloc)", 3000, b_vec2_normalized)
bench("vec2 transform (mat3)", 3000, b_vec2_transform)
bench("mat3 multiply (alloc)", 3000, b_mat3_mul)


# ── colour + shape construction ──────────────────────────────────────────────
def b_color_rgb(n):
    for _ in range(n):
        c = color.rgb(200, 100, 50)

def b_shape_circle(n):
    for _ in range(n):
        s = shape.circle(20, 20, 12)

def b_shape_rectangle(n):
    for _ in range(n):
        s = shape.rectangle(0, 0, 24, 24)

def b_shape_custom(n):
    pts = [vec2(0, 0), vec2(20, 0), vec2(20, 20), vec2(0, 20)]
    for _ in range(n):
        s = shape.custom(pts)

def b_shape_stroke(n):
    for _ in range(n):
        s = shape.circle(20, 20, 12).stroke(2)

bench("color.rgb (alloc)", 4000, b_color_rgb)
bench("shape.circle (alloc)", 1500, b_shape_circle)
bench("shape.rectangle (alloc)", 2000, b_shape_rectangle)
bench("shape.custom 4pt (alloc)", 1500, b_shape_custom)
bench("shape.circle.stroke (alloc)", 800, b_shape_stroke)


# ── image: raster primitives + vector render + blit ──────────────────────────
W, H = 128, 96
img = image(W, H)
img.pen = color.rgb(255, 80, 40)
src = image(32, 32)
src.pen = color.blue
src.clear()


def b_clear(n):
    for _ in range(n):
        img.clear()

def b_rectangle(n):
    for _ in range(n):
        img.rectangle(8, 8, 32, 24)

def b_circle(n):
    for _ in range(n):
        img.circle(40, 40, 16)

def b_line(n):
    for _ in range(n):
        img.line(0, 0, W, H)

def b_put(n):
    for _ in range(n):
        img.put(10, 10)

def b_get(n):
    for _ in range(n):
        c = img.get(10, 10)

def b_blit(n):
    for _ in range(n):
        img.blit(src, vec2(8, 8))

bench("image.clear %dx%d" % (W, H), 400, b_clear)
bench("image.rectangle 32x24", 1500, b_rectangle)
bench("image.circle r16", 800, b_circle)
bench("image.line diag", 1500, b_line)
bench("image.put", 4000, b_put)
bench("image.get", 4000, b_get)
bench("image.blit 32x32", 800, b_blit)


# vector shape render (the real draw cost), AA off vs X4, and batched vs loop
circ = shape.circle(64, 48, 24)


def b_shape_aa_off(n):
    img.antialias = img.OFF
    for _ in range(n):
        img.shape(circ)

def b_shape_aa_x4(n):
    img.antialias = img.X4
    for _ in range(n):
        img.shape(circ)

bench("image.shape circle (AA off)", 500, b_shape_aa_off)
bench("image.shape circle (AA x4)", 200, b_shape_aa_x4)

img.antialias = img.OFF
batch = [shape.circle(12 + (i * 13) % 100, 12 + (i * 7) % 70, 8) for i in range(8)]


def b_shapes_batched(n):
    for _ in range(n):
        img.shapes(batch)

def b_shapes_loop(n):
    for _ in range(n):
        for s in batch:
            img.shape(s)

bench("image.shapes([8]) batched", 300, b_shapes_batched)
bench("image.shape x8 (py loop)", 300, b_shapes_loop)

print("-" * 68)
print("DEVICE BENCH: done")
