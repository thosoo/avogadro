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

#include "touchsettingsdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include "glwidget.h"
#include "glwidget_touch_p.h"

namespace Avogadro {

TouchSettingsDialog::TouchSettingsDialog(QWidget *parent, QSettings *settings)
  : QWidget(parent), m_settings(settings)
{
  if (!m_settings)
    m_settings = new QSettings(this); // owned as a QObject child
  setupUi();
}

TouchSettingsDialog::~TouchSettingsDialog() = default;

qint64 TouchSettingsDialog::armMs() const
{
  return m_armMsSpin->value();
}

double TouchSettingsDialog::slopPx() const
{
  return m_slopSpin->value();
}

bool TouchSettingsDialog::orbitEnabled() const
{
  return m_orbitCheck->isChecked();
}

void TouchSettingsDialog::setupUi()
{
  m_armMsSpin = new QSpinBox(this);
  m_armMsSpin->setRange(static_cast<int>(detail::kTouchStrokeArmMsMin),
                        static_cast<int>(detail::kTouchStrokeArmMsMax));
  m_armMsSpin->setSuffix(tr(" ms"));
  m_armMsSpin->setToolTip(tr(
    "How long a single-finger stroke holds its press before it is delivered to "
    "the tool, so a second finger landing in that window forms a pinch instead "
    "of drawing."));

  m_slopSpin = new QSpinBox(this);
  m_slopSpin->setRange(static_cast<int>(detail::kTouchSlopLogicalPxMin),
                       static_cast<int>(detail::kTouchSlopLogicalPxMax));
  m_slopSpin->setSuffix(tr(" px"));
  m_slopSpin->setToolTip(tr(
    "How far a single finger must travel before it is treated as a stroke "
    "rather than a tap; jitter below this is swallowed."));

  m_orbitCheck = new QCheckBox(tr("Three-finger orbit"), this);
  m_orbitCheck->setToolTip(tr(
    "When enabled, three fingers rotate the camera. When disabled, a third "
    "finger during a two-finger gesture is treated as palm and ignored."));

  m_resetButton = new QPushButton(tr("Reset to Defaults"), this);
  connect(m_resetButton, &QPushButton::clicked, this,
          &TouchSettingsDialog::resetToDefaults);

  QFormLayout *form = new QFormLayout;
  form->addRow(tr("Stroke arming window:"), m_armMsSpin);
  form->addRow(tr("Slop radius:"), m_slopSpin);
  form->addRow(QString(), m_orbitCheck);

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addLayout(form);
  layout->addWidget(m_resetButton);
#ifdef Q_OS_WIN
  // Windows 11 routes three-/four-finger touches to the shell while the OS
  // setting 'Three- and four-finger touch gestures' (Settings > Bluetooth &
  // devices > Touch, default On) is enabled: applications cannot receive
  // them, so the three-finger orbit is inert on stock Windows (design D11).
  // Surface guidance naming that setting plus a direct deep link to the OS
  // page (task 5.8). Plain widgets only (D8); absent on other platforms.
  m_windowsHint = new QLabel(
    tr("Tip: on Windows 11, the system setting 'Three- and four-finger touch "
       "gestures' (Settings > Bluetooth & devices > Touch) sends three-finger "
       "and four-finger touches to the operating system instead of this "
       "application. Set it to Off to use the three-finger orbit."), this);
  m_windowsHint->setObjectName(QStringLiteral("touchWindowsGuidanceLabel"));
  m_windowsHint->setWordWrap(true);

  m_windowsSettingsButton =
    new QPushButton(tr("Open Windows touch settings..."), this);
  m_windowsSettingsButton->setObjectName(
    QStringLiteral("touchWindowsSettingsButton"));
  connect(m_windowsSettingsButton, &QPushButton::clicked, this,
          &TouchSettingsDialog::openWindowsTouchSettings);

  layout->addWidget(m_windowsHint);
  layout->addWidget(m_windowsSettingsButton);
#endif

  // Live application: every edit persists and applies immediately (no restart).
  connect(m_armMsSpin, qOverload<int>(&QSpinBox::valueChanged), this,
          &TouchSettingsDialog::applyAndPersist);
  connect(m_slopSpin, qOverload<int>(&QSpinBox::valueChanged), this,
          &TouchSettingsDialog::applyAndPersist);
  connect(m_orbitCheck, &QCheckBox::toggled, this,
          &TouchSettingsDialog::applyAndPersist);
}

void TouchSettingsDialog::loadSettings()
{
  const qint64 armMs = qBound<qint64>(
    detail::kTouchStrokeArmMsMin,
    m_settings->value("touchStrokeArmMs", detail::kTouchStrokeArmMs).toLongLong(),
    detail::kTouchStrokeArmMsMax);
  const double slopPx = qBound<double>(
    detail::kTouchSlopLogicalPxMin,
    m_settings->value("touchSlopPx", detail::kTouchSlopLogicalPx).toDouble(),
    detail::kTouchSlopLogicalPxMax);
  const bool orbit = m_settings->value("touchOrbitEnabled", true).toBool();

  m_armMsSpin->setValue(static_cast<int>(armMs));
  m_slopSpin->setValue(static_cast<int>(slopPx));
  m_orbitCheck->setChecked(orbit);
}

void TouchSettingsDialog::applySettings()
{
  applyAndPersist();
}

void TouchSettingsDialog::applyAndPersist()
{
  const qint64 armMs = this->armMs();
  const double slopPx = this->slopPx();
  const bool orbit = this->orbitEnabled();

  m_settings->setValue("touchStrokeArmMs", armMs);
  m_settings->setValue("touchSlopPx", slopPx);
  m_settings->setValue("touchOrbitEnabled", orbit);

  applyToViews();
}

void TouchSettingsDialog::applyToViews()
{
  const qint64 armMs = this->armMs();
  const double slopPx = this->slopPx();
  const bool orbit = this->orbitEnabled();

  const QWidgetList widgets = QApplication::allWidgets();
  for (QWidget *widget : widgets) {
    if (GLWidget *view = qobject_cast<GLWidget *>(widget))
      view->setTouchSettings(armMs, slopPx, orbit);
  }
}

void TouchSettingsDialog::resetToDefaults()
{
  m_armMsSpin->setValue(static_cast<int>(detail::kTouchStrokeArmMs));
  m_slopSpin->setValue(static_cast<int>(detail::kTouchSlopLogicalPx));
  m_orbitCheck->setChecked(true);
  // setValue/setChecked fire valueChanged/toggled -> applyAndPersist() for the
  // controls that actually changed. Persist explicitly so a reset that lands on
  // already-default values still writes the shipped constants.
  applyAndPersist();
}

void TouchSettingsDialog::openWindowsTouchSettings()
{
  // Deep link to the OS page hosting 'Three- and four-finger touch gestures'
  // (design D11, task 5.8). ms-settings URIs are opened by the shell; no
  // elevation and no modal dialog are involved.
  QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:devices-touch")));
}

} // namespace Avogadro
