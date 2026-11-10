/**********************************************************************
  TouchSettingsExtension - Extension mounting "Settings → Touch Interaction…"

  Copyright (C) 2026 by the Avogadro developers.

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 ***********************************************************************/

#ifndef TOUCHSETTINGSEXTENSION_H
#define TOUCHSETTINGSEXTENSION_H

#include <avogadro/extension.h>

#include <QList>
#include <QObject>
#include <QString>

namespace Avogadro {

class TouchSettingsDialog;

class TouchSettingsExtension : public Extension
{
  Q_OBJECT
  AVOGADRO_EXTENSION("TouchSettings", tr("Touch Settings"),
                     tr("Adjust touch interaction settings (add-touch-navigation)."))

public:
  //! Constructor.
  explicit TouchSettingsExtension(QObject *parent = nullptr);
  //! Destructor.
  ~TouchSettingsExtension() override;

  //! The single "Touch Interaction..." action.
  QList<QAction *> actions() const override;
  //! Mount under the Settings menu.
  QString menuPath(QAction *action) const override;
  //! Show the TouchSettingsDialog.
  QUndoCommand *performAction(QAction *action, GLWidget *widget) override;

private:
  QList<QAction *> m_actions;
  TouchSettingsDialog *m_dialog;
};

} // namespace Avogadro

#endif // TOUCHSETTINGSEXTENSION_H
