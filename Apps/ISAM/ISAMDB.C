/*
 * DXISAM amalgamation unit for dcc.
 *
 * Compile this file instead of compiling the implementation files below as
 * separate modules. A single translation unit prevents dcc-generated private
 * symbols from colliding at the CP/M linker.
 */
#include "DXISAM.C"
#include "DXPKEY.C"
#include "DXFILE.C"
#ifdef DXISAM_LEGACY_INDEX
#include "DXINDEX.C"
#endif
