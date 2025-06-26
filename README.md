# Avogadro

[![GitHub release](https://img.shields.io/github/release/cryos/avogadro.svg?maxAge=86400)](https://sourceforge.net/projects/avogadro/files/latest/download)
[![Download Avogadro](https://img.shields.io/sourceforge/dt/avogadro.svg?maxAge=86400)](https://sourceforge.net/projects/avogadro/files/latest/download)
[![Google Scholar Citations](https://avogadro.cc/citations.svg?maxAge=86400)](https://scholar.google.com/scholar?cites=618227831851025693&as_sdt=5,38&sciodt=0,38&hl=en)
[![DOI Article](https://img.shields.io/badge/DOI-10.1186/1758--2946--4--17-brightgreen.svg)](http://doi.org/10.1186/1758-2946-4-17)

Avogadro is an advanced molecular editor designed for cross-platform use
in computational chemistry, molecular modeling, bioinformatics, materials
science, and related areas. It offers flexible rendering and a powerful
plugin architecture.

* **Cross-Platform**: Molecular builder/editor for Windows, Linux, and Mac OS X.
* **Free, Open Source**: Easy to install and all source code is available under the GNU GPL.
* **International**: Translations into over 25 languages, including Chinese, French, German, Italian, Russian, and Spanish, with more languages to come.
* **Intuitive**: Built to work easily for students and advanced researchers both.
* **Fast**: Supports multi-threaded rendering and computation.
* **Extensible:** Plugin architecture for developers, including rendering, interactive tools, commands, and Python scripts.
* **Flexible**: Features include Open Babel import of chemical files, input generation for multiple computational chemistry packages, crystallography, and biomolecules.

For more information, see <http://avogadro.cc/>

Manual: <http://manual.avogadro.cc/content/>

Discussion Forum: <http://discuss.avogadro.cc/>

>Avogadro is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

> Avogadro is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

See INSTALL file for installation instructions.

## Running under WSL

Avogadro relies on an OpenGL-capable system. When running inside Windows
Subsystem for Linux (WSL), ensure WSL2 with WSLg or another GPU provider is
available. If you see errors such as `ZINK: failed to choose pdev` or
`glx: failed to create drisw screen` followed by a segmentation fault,
your environment does not provide a valid OpenGL context.
Run `glxinfo -B` to confirm that WSL exposes your GPU. If the vendor shows
"Microsoft" or the context fails, install the necessary GPU drivers or run the
native Windows version of Avogadro instead. Setting `MESA_D3D12_DEFAULT_ADAPTER_NAME`
to your GPU name can also help WSL pick the correct adapter.
If hardware acceleration still fails, you can force Mesa's software renderer by
setting `LIBGL_ALWAYS_SOFTWARE=1` before launching Avogadro, which avoids
segmentation faults at the cost of slower rendering.

If you run Valgrind under WSL, you may see invalid write warnings in
`libnvwgf2umx.so` (the proprietary Nvidia driver). These originate from the
driver itself and are not indicative of an Avogadro bug. Mesa's software
renderer can show similar warnings inside `libgallium` as it manages GPU
buffers.

Automated Windows installer builds are generated with GitHub Actions and can be
found in the workflow artifacts. The workflow builds OpenBabel from
<https://github.com/openbabel/openbabel> and bundles it with Avogadro.

# Backers
Support us with a monthly donation and help us continue our activities. [[Become a backer](https://opencollective.com/avogadro#backer)]
