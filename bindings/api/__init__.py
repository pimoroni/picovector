"""api — the PicoVector API surface, described as type-annotated Python stubs.

Importing this package registers every ``@pv.api`` class (via side effects in
the submodules) into ``pv.REGISTRY`` in module-table order.  The submodules are
imported in dependency order so each type is known before any stub that refers
to it in an annotation.
"""

from . import mat3          # noqa: F401  (vec2 references mat3)
from . import vec2          # noqa: F401
from . import rect          # noqa: F401
from . import color         # noqa: F401
from . import font          # noqa: F401
from . import pixel_font    # noqa: F401
from . import brush         # noqa: F401
from . import shape         # noqa: F401
from . import image         # noqa: F401
from . import algorithm     # noqa: F401

#: MicroPython module name.
MODULE_NAME = "picovector"
