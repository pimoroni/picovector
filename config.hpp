// Configuration: an embedder may provide a picovector.config.hpp on the include
// path (e.g. the picovector-micropython bindings) to override allocators and
// build settings; config_default.hpp then fills in anything left unset.
#if __has_include("picovector.config.hpp")
#  include "picovector.config.hpp"
#endif
#include "config_default.hpp"