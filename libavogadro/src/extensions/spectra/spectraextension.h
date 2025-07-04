/**********************************************************************
  SpectraExtension - Visualize spectral data from QM calculations

  Copyright (C) 2009 by David C. Lonie

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

#ifndef SPECTRAEXTENSION_H
#define SPECTRAEXTENSION_H

#include "spectradialog.h"

#include <avogadro/extension.h>

#include <QObject>
#include <QList>
#include <QString>
#include <QUndoCommand>

namespace Avogadro {

 class SpectraExtension : public Extension
  {
    Q_OBJECT
    AVOGADRO_EXTENSION("Spectra", tr("Spectra"),
                       tr("Visualize spectral data from quantum chemistry calculations"))

    public:
      //! Constructor
      SpectraExtension(QObject *parent=0);
      //! Deconstructor
      virtual ~SpectraExtension();

      //! Perform Action
      virtual QList<QAction *> actions() const;
      virtual QUndoCommand* performAction(QAction *action, GLWidget *widget);
      virtual QString menuPath(QAction *action) const;
      virtual void setMolecule(Molecule *molecule);
      void writeSettings(QSettings &settings) const;
      void readSettings(QSettings &settings);

    private:
      QList<QAction *> m_actions;
      SpectraDialog *m_dialog;
      Molecule *m_molecule;
  };

  class SpectraExtensionFactory : public QObject, public PluginFactory
  {
      Q_OBJECT
      Q_INTERFACES(Avogadro::PluginFactory)
    Q_PLUGIN_METADATA(IID "net.sourceforge.avogadro.pluginfactory/1.5")
      AVOGADRO_EXTENSION_FACTORY(SpectraExtension)
  };


} // end namespace Avogadro

#endif
