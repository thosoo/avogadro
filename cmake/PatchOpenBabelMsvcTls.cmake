# Patch the bundled thosoo/openbabel source for MSVC: OpenBabel's OB_EXTERN
# expands to a DLL interface declaration when using the OpenBabel DLL, but MSVC
# rejects DLL interface decorations on thread-local globals (C2492).
if(NOT DEFINED OPENBABEL_SOURCE_DIR)
  message(FATAL_ERROR "OPENBABEL_SOURCE_DIR must be set")
endif()

set(_ob_typer_h "${OPENBABEL_SOURCE_DIR}/include/openbabel/typer.h")
if(NOT EXISTS "${_ob_typer_h}")
  message(FATAL_ERROR "OpenBabel typer.h not found: ${_ob_typer_h}")
endif()

file(READ "${_ob_typer_h}" _ob_typer_h_contents)

if(_ob_typer_h_contents MATCHES "OB_TLS_EXTERN")
  message(STATUS "OpenBabel MSVC TLS patch already present in ${_ob_typer_h}")
  return()
endif()

set(_ob_extern_guard "#ifndef OB_EXTERN\n#error OB_EXTERN\n#endif")
set(_ob_tls_extern_guard "#ifndef OB_EXTERN\n#error OB_EXTERN\n#endif\n#if defined(_MSC_VER)\n# define OB_TLS_EXTERN extern\n#else\n# define OB_TLS_EXTERN OB_EXTERN\n#endif")

string(REPLACE "${_ob_extern_guard}" "${_ob_tls_extern_guard}"
  _ob_typer_h_contents "${_ob_typer_h_contents}")
string(REPLACE "THREAD_LOCAL OB_EXTERN OBAtomTyper      atomtyper;"
  "THREAD_LOCAL OB_TLS_EXTERN OBAtomTyper      atomtyper;"
  _ob_typer_h_contents "${_ob_typer_h_contents}")
string(REPLACE "THREAD_LOCAL OB_EXTERN OBAromaticTyper  aromtyper;"
  "THREAD_LOCAL OB_TLS_EXTERN OBAromaticTyper  aromtyper;\n#undef OB_TLS_EXTERN"
  _ob_typer_h_contents "${_ob_typer_h_contents}")

if(NOT _ob_typer_h_contents MATCHES "THREAD_LOCAL OB_TLS_EXTERN OBAtomTyper[ \\t]+atomtyper;")
  message(FATAL_ERROR "Failed to patch OpenBabel atomtyper TLS declaration")
endif()
if(NOT _ob_typer_h_contents MATCHES "THREAD_LOCAL OB_TLS_EXTERN OBAromaticTyper[ \\t]+aromtyper;")
  message(FATAL_ERROR "Failed to patch OpenBabel aromtyper TLS declaration")
endif()

file(WRITE "${_ob_typer_h}" "${_ob_typer_h_contents}")
message(STATUS "Patched OpenBabel MSVC TLS declarations in ${_ob_typer_h}")
