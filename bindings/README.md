# PicoVector binding DSL + generator

A single, human-readable **Python source of ground truth** for the PicoVector slice of
the Badgeware MicroPython API, plus a generator that turns it into the standard
MicroPython C++ bindings.

The stubs under [`api/`](api/) read like a `.pyi` of the library *as if it were
written in Python* - types, value ranges and full docstrings - and
[`generate.py`](generate.py) emits the binding `.cpp` from them. The generator
synthesises every argument check, conversion, overload dispatch and the
type/locals-dict/module boilerplate, using a set of lean inline helpers in
[`runtime/pv_bindings.hpp`](runtime/pv_bindings.hpp).

## Why

The hand-written MicroPython bindings involved a dense, unintuitve mess of C/C++
code which was prone to bugs and made it difficult to capture the shape of the API.

By using native Python we can describe - with caveats - exactly how we want the API
to look and feel *in the target language itself.*

## Layout

```
pv.py            authoring surface the stubs import (decorators, pseudo-types, Range)
api/*.py         one stub class per type (vec2, rect, mat3, color, brush, shape,
                 image, font, pixel_font, algorithm) - the human-edited source
model.py         internal IR + the loader that introspects the stubs
generate.py      IR -> C++ (run this to (re)generate)
runtime/
  pv_bindings.hpp lean inline helpers that replace the MPY_BIND_* macros
  pv_support.cpp  shared glue (attr action, file readers, vec2/rect/brush helpers)
  pv_objs.hpp     picovector MicroPython obj structs + type_* externs
  mp_tracked_allocator.hpp  GC-tracked allocator used by the library + bindings
native/*.cpp     hand-written bodies for the few irreducibly-procedural members,
                 plus the companion PNG/JPEG decoders (image_png/jpeg.cpp)
generated/       emitted output: <type>.cpp, types.h, picovector_bindings.c
tools/
  extract_surface.py  scrape the original bindings' surface -> snapshot
  compile_check.sh    compile the generated output with the real toolchain
  run_device_tests.sh / run_device_bench.sh   on-device tests / benchmarks
tests/
  test_parity.py        assert generated surface ⊇ original (no regressions)
  test_generate.py      generator golden-output spot checks
  original_surface.json frozen snapshot of the original surface (parity baseline)
  device_test.py / device_bench.py   on-device behaviour / timing
picovector_bindings.cmake   reference list of the binding sources + include dirs
```

This tree is self-contained: the firmware builds the bindings entirely from
`bindings/` and the original hand-written `picovector/micropython/` directory has
been retired.

## Authoring (the stub DSL)

A type is a `@pv.api`-decorated class. Methods are plain `def`s with type hints
and docstrings; class-level annotations are read/write fields. Convention covers
the common case (Python name == C++ callee, Python arg order == call order);
the small [`@pv.cpp`](pv.py) decorator handles the exceptions.

```python
@api("vec2_t", field="v", arg_read="mp_obj_get_vec2({0})", arg_type="vec2_t",
     print=("vec(%f, %f)", "self->v.x", "self->v.y"))
class vec2:
    """2D vector / point."""
    x: float
    y: float
    def __init__(self, x: float = 0, y: float = 0): "vec2() or vec2(x, y)."

    def dot(self, other: vec2) -> float: "Dot product with another vec2."
    def normalized(self) -> vec2:        "Unit vector in the same direction."
    def __add__(self, other: vec2) -> vec2: "Component-wise addition."
```

Things convention can't express, declared with `@cpp`/pseudo-types:

* **vec2-or-(x, y)** arguments → the `XY` pseudo-type (also `XYWH`, `ColorStops`,
  `Pattern8`, `PathList`, `ShapeOrList`, `Filter`).
* **argument reordering** (the C++ free function differs from the Python order)
  → `@cpp(args="c.x c.y from_a to_a inner outer")`.
* **overloads** → `typing.overload`; the generator emits guarded dispatch.
* **value ranges** → `Annotated[int, Range(0, 37)]` (raise) or
  `Range(1, None, clamp=True)` (clamp).
* **return semantics** → `-> Self` (return self for chaining) vs `-> vec2`
  (return a new boxed object); `@cpp(emit="mutate")` for in-place writes.
* **special callees** → `@cpp(call="rgb_color_t", emit="new")`,
  `emit="mnew"` (GC `m_new_class`), `emit="free"`, `recv="src"` (call on an
  argument, e.g. `image.blit`).

## Generate

```
python generate.py            # writes generated/*.cpp, types.h, picovector_bindings.c
python generate.py --list     # print the surface
```

## The native tail

A few members build nested Python containers, dispatch by name, decode files or
track a parent view - none reduce to "call one C++ function and box the result".

These are declared in the DSL (so it stays the single source of truth for the
*surface*, registration and docs) but marked `@native`, with bodies in
[`native/`](native/):

* `font.load` / `pixel_font.load` (+ their `__del__`) - binary font parsing.
* `image.load` / `load_into` / `window` / `text` / `measure_text` / `shapes` /
  `batch` - file decode, font branching, parent tracking, batched render, qstr
  dispatch.
* `algorithm.clip_line` / `dda` / `raycast` - in-place mutation and
  lambda-driven nested results.

Everything else - all the vec2/rect/mat3 maths, the colour factories, every
brush, every shape factory (incl. `custom`), and the bulk of `image` (incl.
`blit`'s four overloads and the `XY`/`XYWH` forms) - is generated.

## Verification

* **Surface parity** - `python tests/test_parity.py` asserts the generated
  surface is a superset of the original: every original method, property,
  constant, `__del__` and type feature is still present (additions are allowed
  and reported). It compares against `tests/original_surface.json`, a frozen
  snapshot of the hand-written bindings' surface (so parity survives their
  removal). Regenerate the snapshot from the original sources, if you still have
  them, with `python tools/extract_surface.py > tests/original_surface.json`.
* **Real compile** - `tools/compile_check.sh [BUILD_DIR]` compiles every
  generated/native/support source with the actual `arm-none-eabi-g++`
  Cortex-M33 flags, defines and includes from a configured `build-tufty` tree
  (`-fsyntax-only`). All 15 sources + the C module table compile clean.
* **On-device behaviour** - `tools/run_device_tests.sh [port|serial]` runs
  `tests/device_test.py` on a connected badge over `mpremote` (auto-selects the
  Tufty `2e8a:1101`; never touches flash). It exercises the real `picovector`
  module - vec2/mat3/rect maths, colour, every shape factory and its overloads,
  image raster + blit's four forms, brushes - and includes regression guards for
  the issues found bringing the bindings up on hardware (float args to int
  params, `measure_text` without a size, the XY/XYWH overloads).
* **On-device performance** - `tools/run_device_bench.sh [port|serial]` runs
  `tests/device_bench.py`, timing the hot paths (vec2/mat3 maths, shape
  construction, raster + vector draw, blit, batched vs per-shape draw) with
  `time.ticks_us` and reporting µs/op and ops/s. It is independent of the
  firmware's `[pv] fps=` line, so it's a reliable way to catch binding-overhead
  regressions.

## Wiring into the firmware

`../picovector.cmake` compiles the generated + native + support
sources directly from `bindings/` and puts `bindings/generated` and
`bindings/runtime` on the include path.
[`picovector_bindings.cmake`](picovector_bindings.cmake) is kept as a tidy,
reusable list of those sources/includes (and the `-Wno-unused-variable`
relaxation) should you need to re-derive the wiring. The generated bindings
register a `picovector` module with the same types as before, so apps need no
changes.

After editing any `api/*.py` stub, run `python generate.py` to refresh
`generated/`, then rebuild.
