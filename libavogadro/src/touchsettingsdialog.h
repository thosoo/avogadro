/**********************************************************************
  TouchSettingsDialog - Settings panel for touch interaction (add-touch-navigation)

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

#ifndef TOUCHSETTINGSDIALOG_H
#define TOUCHSETTINGSDIALOG_H

#include <avogadro/global.h>

#include <QDialog>

class QCheckBox;
class QLabel;
class QPushButton;
class QSettings;
class QSpinBox;

namespace Avogadro {

/**
 * @class TouchSettingsDialog touchsettingsdialog.h <avogadro/touchsettingsdialog.h>
 * @brief Modeless dialog adjusting touch interaction settings (design D10).
 *
 * Exposes exactly three controls: the stroke-arming window duration in
 * milliseconds, the slop (jitter) radius in logical pixels, and whether the
 * three-finger orbit gesture is enabled, plus a reset-to-defaults button. The
 * values are persisted at the root of QSettings (the dialog is the sole writer
 * of the `touchStrokeArmMs` / `touchSlopPx` / `touchOrbitEnabled` keys) and
 * live-applied to every active GLWidget view without a restart. The dialog is
 * built from plain Qt widgets only (no .ui file) so it is MSVC-safe under
 * -std=c++17 -permissive- (design D8).
 */
class A_EXPORT TouchSettingsDialog : public QDialog
{
  Q_OBJECT

public:
  //! Constructor. @p settings is the QSettings to read/write; a null pointer
  //! makes the dialog use the application-default QSettings.
  explicit TouchSettingsDialog(QWidget *parent = nullptr,
                               QSettings *settings = nullptr);
  //! Destructor.
  ~TouchSettingsDialog() override;

  //! Read the touch settings from QSettings, clamp out-of-range values into
  //! the documented ranges, and populate the controls.
  void loadSettings();
  //! Write the current control values to QSettings and live-apply them to
  //! every active GLWidget view.
  void applySettings();

  //! Current stroke-arming window (ms).
  qint64 armMs() const;
  //! Current slop radius (logical px).
  double slopPx() const;
  //! Whether the three-finger orbit gesture is enabled.
  bool orbitEnabled() const;

public slots:
  //! Restore the shipped defaults (the named constants) and apply them.
  void resetToDefaults();
  //! Apply the current controls and close the dialog.
  void accept() override;

private slots:
  //! Windows only: open the OS touch settings page (ms-settings:devices-touch)
  //! where the system 'Three- and four-finger touch gestures' toggle lives
  //! (design D11, task 5.8). The guidance row itself is created only on
  //! Windows builds.
  void openWindowsTouchSettings();

private:
  void setupUi();
  //! Persist the current controls and live-apply them to every active
  //! GLWidget view. Connected to the controls' valueChanged/toggled signals so
  //! every edit takes effect immediately (live application, no restart).
  void applyAndPersist();
  //! Call GLWidget::setTouchSettings on every active GLWidget view.
  void applyToViews();

  QSettings *m_settings; //!< Not owned; null means the app-default QSettings.
  QSpinBox *m_armMsSpin;
  QSpinBox *m_slopSpin;
  QCheckBox *m_orbitCheck;
  QPushButton *m_resetButton;
  QPushButton *m_okButton;
  QPushButton *m_cancelButton;
  //! Windows-only guidance row (design D11, task 5.8): null on other
  //! platforms, where the OS cannot preempt multi-finger touch.
  QLabel *m_windowsHint = nullptr;
  QPushButton *m_windowsSettingsButton = nullptr;
};

} // namespace Avogadro

#endif // TOUCHSETTINGSDIALOG_H
