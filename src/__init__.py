# Hardware backends are optional: a missing SDK only disables that module.
import warnings

try:
    from .atlas import *
except ImportError as e:
    warnings.warn(f"src.atlas unavailable (missing {e.name}, needs arena_api)")

try:
    from .basler import *
except ImportError as e:
    warnings.warn(f"src.basler unavailable (missing {e.name})")

try:
    from .triton import *
except ImportError as e:
    warnings.warn(f"src.triton unavailable (missing {e.name}, needs arena_api)")

try:
    from .display import *
except ImportError as e:
    warnings.warn(f"src.display unavailable (missing {e.name}, needs glfw and PyOpenGL)")

try:
    from .stage import *
except ImportError as e:
    warnings.warn(f"src.stage unavailable (missing {e.name}, needs pyserial)")
