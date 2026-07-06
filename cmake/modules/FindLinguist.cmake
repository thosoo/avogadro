# Locate Qt6 Linguist command line tools and provide Avogadro translation helpers.
#
# I18N_LANGUAGE - when set, only matching language PO/TS files are wrapped.

if(TARGET Qt6::lupdate)
  set(AVOGADRO_LUPDATE_COMMAND Qt6::lupdate)
else()
  find_program(AVOGADRO_LUPDATE_COMMAND NAMES lupdate-qt6 lupdate)
endif()

if(TARGET Qt6::lrelease)
  set(AVOGADRO_LRELEASE_COMMAND Qt6::lrelease)
else()
  find_program(AVOGADRO_LRELEASE_COMMAND NAMES lrelease-qt6 lrelease)
endif()

if(TARGET Qt6::lconvert)
  set(AVOGADRO_LCONVERT_COMMAND Qt6::lconvert)
else()
  find_program(AVOGADRO_LCONVERT_COMMAND NAMES lconvert-qt6 lconvert)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Linguist
  REQUIRED_VARS AVOGADRO_LUPDATE_COMMAND AVOGADRO_LRELEASE_COMMAND AVOGADRO_LCONVERT_COMMAND)

mark_as_advanced(AVOGADRO_LUPDATE_COMMAND AVOGADRO_LRELEASE_COMMAND AVOGADRO_LCONVERT_COMMAND)

function(_avogadro_should_wrap_translation out_var file_stem)
  if(NOT I18N_LANGUAGE)
    set(${out_var} ON PARENT_SCOPE)
    return()
  endif()

  string(REGEX MATCH "${I18N_LANGUAGE}" _match "${file_stem}")
  if(_match)
    set(${out_var} ON PARENT_SCOPE)
  else()
    set(${out_var} OFF PARENT_SCOPE)
  endif()
endfunction()

macro(avogadro_wrap_ts outfiles)
  foreach(it ${ARGN})
    get_filename_component(_ts_file "${it}" ABSOLUTE)
    get_filename_component(_ts_stem "${_ts_file}" NAME_WE)
    _avogadro_should_wrap_translation(_do_wrap "${_ts_stem}")
    if(_do_wrap)
      set(_qm_file "${CMAKE_CURRENT_BINARY_DIR}/${_ts_stem}.qm")
      add_custom_command(OUTPUT "${_qm_file}"
        COMMAND ${AVOGADRO_LRELEASE_COMMAND}
        ARGS -compress -removeidentical -silent "${_ts_file}" -qm "${_qm_file}"
        DEPENDS "${_ts_file}"
        VERBATIM)
      list(APPEND ${outfiles} "${_qm_file}")
    endif()
  endforeach()
endmacro()

macro(avogadro_wrap_po outfiles)
  foreach(it ${ARGN})
    get_filename_component(_po_file "${it}" ABSOLUTE)
    get_filename_component(_po_stem_with_dash "${_po_file}" NAME_WE)
    _avogadro_should_wrap_translation(_do_wrap "${_po_stem_with_dash}")
    if(_do_wrap)
      string(REPLACE "-" "_" _po_stem "${_po_stem_with_dash}")
      set(_ts_file "${CMAKE_CURRENT_BINARY_DIR}/${_po_stem}.ts")
      set(_qm_file "${CMAKE_CURRENT_BINARY_DIR}/${_po_stem}.qm")

      if(NOT EXISTS "${_po_file}")
        get_filename_component(_po_path "${_po_file}" DIRECTORY)
        string(REGEX MATCH "[^-]+$" _lang "${_po_stem_with_dash}")
        set(_po_file "${_po_path}/${_lang}.po")
      endif()

      add_custom_command(OUTPUT "${_qm_file}"
        COMMAND ${AVOGADRO_LCONVERT_COMMAND}
        ARGS -i "${_po_file}" -o "${_ts_file}"
        COMMAND ${AVOGADRO_LUPDATE_COMMAND}
        ARGS "${CMAKE_CURRENT_SOURCE_DIR}" -silent -noobsolete -ts "${_ts_file}"
        COMMAND ${AVOGADRO_LRELEASE_COMMAND}
        ARGS -compress -removeidentical -silent "${_ts_file}" -qm "${_qm_file}"
        DEPENDS "${_po_file}"
        VERBATIM)
      list(APPEND ${outfiles} "${_qm_file}")
    endif()
  endforeach()
endmacro()
