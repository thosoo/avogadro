# Patch the bundled thosoo/openbabel source for MSVC: OpenBabel's OB_EXTERN
# expands to a DLL interface declaration when using the OpenBabel DLL, but MSVC
# rejects DLL interface decorations on thread-local globals (C2492). For MSVC,
# keep these two globals DLL-visible but make them ordinary non-TLS globals.
if(NOT DEFINED OPENBABEL_SOURCE_DIR)
  message(FATAL_ERROR "OPENBABEL_SOURCE_DIR must be set")
endif()

set(_ob_typer_h "${OPENBABEL_SOURCE_DIR}/include/openbabel/typer.h")
set(_ob_typer_cpp "${OPENBABEL_SOURCE_DIR}/src/typer.cpp")
if(NOT EXISTS "${_ob_typer_h}")
  message(FATAL_ERROR "OpenBabel typer.h not found: ${_ob_typer_h}")
endif()
if(NOT EXISTS "${_ob_typer_cpp}")
  message(FATAL_ERROR "OpenBabel typer.cpp not found: ${_ob_typer_cpp}")
endif()

file(READ "${_ob_typer_h}" _ob_typer_h_contents)
file(READ "${_ob_typer_cpp}" _ob_typer_cpp_contents)

if(_ob_typer_h_contents MATCHES "(^|\n)OB_EXTERN OBAtomTyper[ \t]+atomtyper;" AND
   _ob_typer_h_contents MATCHES "(^|\n)OB_EXTERN OBAromaticTyper[ \t]+aromtyper;" AND
   _ob_typer_cpp_contents MATCHES "#if defined\\(_MSC_VER\\)")
  message(STATUS "OpenBabel MSVC TLS patch already present")
  return()
endif()

set(_ob_atom_decl "THREAD_LOCAL OB_EXTERN OBAtomTyper      atomtyper;")
set(_ob_atom_decl_msvc "#if defined(_MSC_VER)\nOB_EXTERN OBAtomTyper      atomtyper;\n#else\nTHREAD_LOCAL OB_EXTERN OBAtomTyper      atomtyper;\n#endif")
set(_ob_arom_decl "THREAD_LOCAL OB_EXTERN OBAromaticTyper  aromtyper;")
set(_ob_arom_decl_msvc "#if defined(_MSC_VER)\nOB_EXTERN OBAromaticTyper  aromtyper;\n#else\nTHREAD_LOCAL OB_EXTERN OBAromaticTyper  aromtyper;\n#endif")

string(FIND "${_ob_typer_h_contents}" "${_ob_atom_decl}" _ob_atom_decl_pos)
if(_ob_atom_decl_pos EQUAL -1)
  message(FATAL_ERROR "Expected OpenBabel atomtyper declaration not found in ${_ob_typer_h}")
endif()
string(FIND "${_ob_typer_h_contents}" "${_ob_arom_decl}" _ob_arom_decl_pos)
if(_ob_arom_decl_pos EQUAL -1)
  message(FATAL_ERROR "Expected OpenBabel aromtyper declaration not found in ${_ob_typer_h}")
endif()

string(REPLACE "${_ob_atom_decl}" "${_ob_atom_decl_msvc}"
  _ob_typer_h_contents "${_ob_typer_h_contents}")
string(REPLACE "${_ob_arom_decl}" "${_ob_arom_decl_msvc}"
  _ob_typer_h_contents "${_ob_typer_h_contents}")

set(_ob_typer_defs "  THREAD_LOCAL OBAromaticTyper  aromtyper;\n  THREAD_LOCAL OBAtomTyper      atomtyper;")
set(_ob_typer_defs_msvc "#if defined(_MSC_VER)\n  OBAromaticTyper  aromtyper;\n  OBAtomTyper      atomtyper;\n#else\n  THREAD_LOCAL OBAromaticTyper  aromtyper;\n  THREAD_LOCAL OBAtomTyper      atomtyper;\n#endif")
string(FIND "${_ob_typer_cpp_contents}" "${_ob_typer_defs}" _ob_typer_defs_pos)
if(_ob_typer_defs_pos EQUAL -1)
  message(FATAL_ERROR "Expected OpenBabel typer definitions not found in ${_ob_typer_cpp}")
endif()
string(REPLACE "${_ob_typer_defs}" "${_ob_typer_defs_msvc}"
  _ob_typer_cpp_contents "${_ob_typer_cpp_contents}")

if(_ob_typer_h_contents MATCHES "THREAD_LOCAL[ \t]+extern")
  message(FATAL_ERROR "OpenBabel MSVC TLS patch must not create THREAD_LOCAL extern declarations")
endif()
if(NOT _ob_typer_h_contents MATCHES "(^|\n)OB_EXTERN OBAtomTyper[ \t]+atomtyper;")
  message(FATAL_ERROR "Failed to patch OpenBabel atomtyper MSVC declaration")
endif()
if(NOT _ob_typer_h_contents MATCHES "THREAD_LOCAL OB_EXTERN OBAtomTyper[ \t]+atomtyper;")
  message(FATAL_ERROR "Failed to preserve OpenBabel atomtyper non-MSVC declaration")
endif()
if(NOT _ob_typer_h_contents MATCHES "(^|\n)OB_EXTERN OBAromaticTyper[ \t]+aromtyper;")
  message(FATAL_ERROR "Failed to patch OpenBabel aromtyper MSVC declaration")
endif()
if(NOT _ob_typer_h_contents MATCHES "THREAD_LOCAL OB_EXTERN OBAromaticTyper[ \t]+aromtyper;")
  message(FATAL_ERROR "Failed to preserve OpenBabel aromtyper non-MSVC declaration")
endif()
if(NOT _ob_typer_cpp_contents MATCHES "(^|\n)  OBAromaticTyper[ \t]+aromtyper;")
  message(FATAL_ERROR "Failed to patch OpenBabel aromtyper MSVC definition")
endif()
if(NOT _ob_typer_cpp_contents MATCHES "(^|\n)  OBAtomTyper[ \t]+atomtyper;")
  message(FATAL_ERROR "Failed to patch OpenBabel atomtyper MSVC definition")
endif()
if(NOT _ob_typer_cpp_contents MATCHES "THREAD_LOCAL OBAromaticTyper[ \t]+aromtyper;")
  message(FATAL_ERROR "Failed to preserve OpenBabel aromtyper non-MSVC definition")
endif()
if(NOT _ob_typer_cpp_contents MATCHES "THREAD_LOCAL OBAtomTyper[ \t]+atomtyper;")
  message(FATAL_ERROR "Failed to preserve OpenBabel atomtyper non-MSVC definition")
endif()

file(WRITE "${_ob_typer_h}" "${_ob_typer_h_contents}")
file(WRITE "${_ob_typer_cpp}" "${_ob_typer_cpp_contents}")
message(STATUS "Patched OpenBabel MSVC typer globals in ${_ob_typer_h} and ${_ob_typer_cpp}")
