// D11 precalculation-policy pin (fix-surface-visualization-performance,
// OpenSpec task 7.3).
//
// The policy: background orbital precalculation on document load runs at most
// at OrbitalWidget::OQ_Medium resolution regardless of the persisted default
// quality; a higher persisted quality applies only to explicit per-orbital
// requests. The clamp itself is the free/static function
// OrbitalWidget::clampPrecalcQuality (unit-tested below, with the enum order
// pinned by a static_assert in orbitalwidget.h that this TU compiles by
// including the header). The wiring assertion checks that
// OrbitalExtension::precalculateOrbitals actually routes its queue resolution
// through the clamp — the string-level check follows the
// qt6signalpatternstest pattern (AVOGADRO_SOURCE_DIR compile definition),
// because constructing an OrbitalExtension with a stubbed dock/widget is
// impractical headlessly.

#include <QtTest/QtTest>
#include <QCoreApplication>

#include "orbitalwidget.h"

#include <QFile>
#include <QString>

using Avogadro::OrbitalWidget;

class PrecalcPolicyTest : public QObject
{
  Q_OBJECT

private slots:
  void clampBehaviour();
  void precalculateOrbitalsUsesClamp();
  void enumOrderPinned();
};

// The clamp itself: Low and Medium pass through, High and VeryHigh clamp to
// Medium. This is the whole D11 policy surface.
void PrecalcPolicyTest::clampBehaviour()
{
  QCOMPARE(OrbitalWidget::clampPrecalcQuality(OrbitalWidget::OQ_Low),
           OrbitalWidget::OQ_Low);
  QCOMPARE(OrbitalWidget::clampPrecalcQuality(OrbitalWidget::OQ_Medium),
           OrbitalWidget::OQ_Medium);
  QCOMPARE(OrbitalWidget::clampPrecalcQuality(OrbitalWidget::OQ_High),
           OrbitalWidget::OQ_Medium);
  QCOMPARE(OrbitalWidget::clampPrecalcQuality(OrbitalWidget::OQ_VeryHigh),
           OrbitalWidget::OQ_Medium);
}

// Wiring: precalculateOrbitals must pass its queue resolution through the
// clamp (the raw std::min call was moved into clampPrecalcQuality; the call
// site must use the named function so the policy has a single tested home).
void PrecalcPolicyTest::precalculateOrbitalsUsesClamp()
{
  QString path = QString::fromUtf8(AVOGADRO_SOURCE_DIR)
                 + "/libavogadro/src/extensions/surfaces/"
                   "orbitalextension.cpp";
  QFile source(path);
  QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text),
           qPrintable(QString("cannot open %1").arg(path)));
  const QString text = QString::fromUtf8(source.readAll());

  QVERIFY2(text.contains("clampPrecalcQuality(m_widget->defaultQuality())"),
           "precalculateOrbitals no longer routes the queue resolution "
           "through OrbitalWidget::clampPrecalcQuality — the D11 policy "
           "(precalculation bounded at Medium) is not enforced");
}

// The static_assert in orbitalwidget.h pins the enum ordering this clamp
// relies on; referencing the constant here keeps the assertion honest (it is
// compiled into this TU via the header include).
void PrecalcPolicyTest::enumOrderPinned()
{
  QVERIFY(static_cast<int>(OrbitalWidget::OQ_Low)
          < static_cast<int>(OrbitalWidget::OQ_Medium));
}

QTEST_MAIN(PrecalcPolicyTest)
#include "precalcpolicytest.moc"
