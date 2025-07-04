/**********************************************************************
  InvertChirality - Invert selected stereocenters

  Copyright (C) 2010 by Geoffrey R. Hutchison

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  Some code is based on Open Babel
  For more information, see <http://openbabel.sourceforge.net/>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 ***********************************************************************/

#ifndef INVERTCHIRALEXTENSION_H
#define INVERTCHIRALEXTENSION_H

#include <avogadro/glwidget.h>
#include <avogadro/extension.h>
#include <avogadro/idlist.h>

#include <QObject>
#include <QList>
#include <QString>
#include <QUndoCommand>

namespace Avogadro {

 class InvertChiralExtension : public Extension
  {
    Q_OBJECT
    AVOGADRO_EXTENSION("InvertChiral", tr("InvertChiral"),
                       tr("Invert chiral centers"))

    public:
      //! Constructor
      InvertChiralExtension(QObject *parent=0);
      //! Deconstructor
      virtual ~InvertChiralExtension();

      //! Perform Action
      virtual QList<QAction *> actions() const;
      virtual QUndoCommand* performAction(QAction *action, GLWidget *widget);
      virtual QString menuPath(QAction *action) const;
      //@}

      void setMolecule(Molecule *molecule);

    private:
      QList<QAction *> m_actions;
      Molecule *m_molecule;
  };

  class InvertChiralExtensionFactory : public QObject, public PluginFactory
  {
      Q_OBJECT
      Q_INTERFACES(Avogadro::PluginFactory)
    Q_PLUGIN_METADATA(IID "net.sourceforge.avogadro.pluginfactory/1.5")
      AVOGADRO_EXTENSION_FACTORY(InvertChiralExtension)
  };


} // end namespace Avogadro

#endif
