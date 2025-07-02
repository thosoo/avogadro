include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(LINK_LIBS avogadro)
set(PLUGIN_LABEL extensions)
set(PLUGIN_TARGET extensions)

### Vibrations
avogadro_plugin_nogl(vibrationextension
  "vibrationextension.cpp;vibrationwidget.cpp"
  vibrationwidget.ui)

### Spectra
set(spectraextension_SRCS
  spectraextension.cpp
  spectradialog.cpp
  spectratype.cpp
  abstract_ir.cpp
  ir.cpp
  nmr.cpp
  dos.cpp
  uv.cpp
  cd.cpp
  raman.cpp
  abstract_orcaspec.cpp
  energy.cpp
  emission.cpp
  absorption.cpp
  nearir.cpp
)

avogadro_plugin_nogl(spectraextension
  "${spectraextension_SRCS}"
  "spectradialog.ui;tab_ir_raman.ui;tab_nmr.ui;tab_dos.ui;tab_uv.ui;tab_cd.ui;tab_orcaspec.ui;tab_energy.ui")
