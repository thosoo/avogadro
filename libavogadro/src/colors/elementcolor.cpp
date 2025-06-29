/**********************************************************************
 ElementColor - Default class for coloring atoms based on element

  Copyright (C) 2007 Geoffrey R. Hutchison

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Avogadro is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.
 **********************************************************************/

#include "elementcolor.h"

#include <avogadro/primitive.h>
#include <avogadro/atom.h>
#include <QtPlugin>

#include <openbabel/mol.h>
#include <openbabel/elements.h>

namespace Avogadro {

  /// Constructor
  ElementColor::ElementColor()
  { }

  /// Destructor
  ElementColor::~ElementColor()
  { }

  void ElementColor::setFromPrimitive(const Primitive *p)
  {
    if (!p || p->type() != Primitive::AtomType)
      return;

    const Atom *atom = static_cast<const Atom*>(p);

    if (atom->atomicNumber()) {
      double r, g, b;
      OpenBabel::OBElements::GetRGB(atom->atomicNumber(), &r, &g, &b);
      m_channels[0] = r;
      m_channels[1] = g;
      m_channels[2] = b;
    } else {
      m_channels[0] = 0.2;
      m_channels[1] = 0.2;
      m_channels[2] = 0.2;
    }

    m_channels[3] = 1.0;
  }

}

