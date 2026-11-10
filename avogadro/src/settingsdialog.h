/**********************************************************************
  SettingsDialog - Settings Dialog

  Copyright (C) 2007-2008 Donald Ephraim Curtis

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

#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "ui_settingsdialog.h"

class QAbstractButton;
class QDialogButtonBox;
namespace Avogadro {

  class MainWindow;
  class TouchSettingsDialog;
  class SettingsDialog : public QDialog
  {
    Q_OBJECT

    public:
      SettingsDialog(MainWindow *mainWindow);

      void insertWidget(int index, QWidget *widget);

    private:
      Ui::SettingsDialog ui;

      MainWindow *m_mainWindow;
      //! Embeddable touch-settings panel hosted in the General tab (task 6.3).
      TouchSettingsDialog *m_touchPanel = nullptr;

    private Q_SLOTS:
      void buttonClicked(QAbstractButton *button);
      void saveValues();
      void loadValues();
      // Disable the quality/fog sliders on a renderer tier that would silently
      // no-op them, with a visible reason (gpu-rendering-pipeline delta: render
      // settings effective or honestly gated). Called from loadValues().
      void applyTierGating();
      void qualityChanged(int value);
      void fogChanged(int value);
  };

} // end namespace Avogadro

#endif
