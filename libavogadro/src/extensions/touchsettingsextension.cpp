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

#include "touchsettingsextension.h"

#include <avogadro/glwidget.h>
#include <avogadro/touchsettingsdialog.h>

#include <QAction>
#include <QWidget>

namespace Avogadro {

TouchSettingsExtension::TouchSettingsExtension(QObject *parent)
  : Extension(parent), m_dialog(nullptr)
{
  QAction *action = new QAction(this);
  action->setText(tr("Touch Interaction..."));
  m_actions.append(action);
}

TouchSettingsExtension::~TouchSettingsExtension()
{
  if (m_dialog)
    m_dialog->close();
}

QList<QAction *> TouchSettingsExtension::actions() const
{
  return m_actions;
}

QString TouchSettingsExtension::menuPath(QAction *) const
{
  // The root Settings menu title in mainwindow.ui is "Se&ttings" (mnemonic on
  // the second 't'). addActionsToMenu matches the menu by exact string
  // equality, so this must reproduce that exact text.
  return tr("Se&ttings");
}

QUndoCommand *TouchSettingsExtension::performAction(QAction *, GLWidget *)
{
  if (!m_dialog) {
    m_dialog = new TouchSettingsDialog(qobject_cast<QWidget *>(parent()));
    m_dialog->loadSettings();
  }
  m_dialog->show();
  m_dialog->raise();
  m_dialog->activateWindow();
  return nullptr;
}

} // namespace Avogadro
