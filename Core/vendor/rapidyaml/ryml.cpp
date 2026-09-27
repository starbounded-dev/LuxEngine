// The single rapidyaml implementation unit. Everything else includes ryml_all.hpp as a header only
// (through Lux::Yaml); defining the macro here compiles the library bodies exactly once.
#define RYML_SINGLE_HDR_DEFINE_NOW
#include "ryml_all.hpp"
