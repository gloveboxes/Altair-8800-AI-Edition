/*
 * Banked ISAM Library amalgamation unit for dcc.
 *
 * Compile this file instead of compiling the implementation files below as
 * separate modules. A single translation unit prevents dcc-generated private
 * symbols from colliding at the CP/M linker.
 */
#include "iscore.c"
#include "isacache.c"
#include "isindex.c"
#include "isfile.c"
#undef close
#include "ixbuild.c"
