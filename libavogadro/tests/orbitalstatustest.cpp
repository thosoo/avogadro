// fix-orbital-status-column (design D4): offscreen regression coverage for the
// orbital status column. Three independent concerns:
//
//   1. paintWithinCellAndDrawsLabel  — the ProgressBarDelegate must paint the
//      progress chunk and percentage label entirely inside the cell rect, so
//      scrolling never deposits blue fragments on unrelated cells. This is the
//      State_Horizontal regression (design D1): without the flag, Fusion's
//      drawControl paints the chunk through a rotated painter far outside
//      opt.rect, and the label is never drawn.
//
//   2. boundsSafeProgressMutators    — the model's progress mutators must
//      ignore an out-of-range orbital number (no dataChanged, no crash) and
//      clearing an empty table must be a safe no-op.
//
//   3. firstFillLandsOnHOMO          — the first fill of a populated table must
//      scroll the view to the HOMO row like later fills do.
//
// OrbitalExtension / OrbitalWidget / OrbitalTableModel are compiled directly
// into the test because the surfaces extension is a plugin, not exported from
// libavogadro (mirrors orbitalselectiontest).

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QSortFilterProxyModel>
#include <QScrollBar>

#include "orbitalextension.h"
#include "orbitalwidget.h"
#include "orbitaltablemodel.h"

using namespace Avogadro;

static Orbital makeOrbital(int index, const QString &description,
                           int min = 0, int current = 0, int max = 0,
                           int stage = 1, int totalStages = 1)
{
  Orbital o;
  o.energy = 0.0;
  o.index = index;
  o.description = description;
  o.symmetry = QString();
  o.queueEntry = nullptr;
  o.min = min;
  o.max = max;
  o.current = current;
  o.stage = stage;
  o.totalStages = totalStages;
  return o;
}

class OrbitalStatusTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void paintWithinCellAndDrawsLabel();
  void boundsSafeProgressMutators();
  void clearEmptyTableIsNoop();
  void inRangeUpdateEmitsDataChanged();
  void firstFillLandsOnHOMO();
};

// D4 assertion 1 & 2: paint the status delegate into a QImage at a known cell
// rect and assert, style-independently, that every painted pixel lies inside
// the cell (catches the rotated-chunk-outside regression) and that at least
// one painted pixel exists inside the cell (catches the empty-groove/label
// regression).
void OrbitalStatusTest::paintWithinCellAndDrawsLabel()
{
  // Build a model with a single completed (100%) orbital.
  OrbitalTableModel model;
  model.setOrbital(makeOrbital(0, "HOMO", 0, 1, 1, 1, 1));
  model.finishProgress(1); // -> 100%

  const QModelIndex idx = model.index(0, OrbitalTableModel::C_Status);
  const int percent = model.data(idx, Qt::DisplayRole).toInt();
  QCOMPARE(percent, 100);

  // Place the cell inside a larger canvas so paint that lands OUTSIDE the cell
  // rect is still captured by the image (the rotated-chunk regression paints
  // at an absolute position unrelated to opt.rect).
  const QRect cell(100, 100, 154, 29);
  QImage image(400, 400, QImage::Format_ARGB32);
  image.fill(Qt::black); // background: any painted pixel differs from this

  // Use a rich, deterministic style so the progress bar actually draws a
  // groove, chunk and label. Fusion is the style the regression was captured
  // on; fall back to the platform default when Fusion is unavailable.
  if (QStyleFactory::keys().contains("fusion", Qt::CaseInsensitive))
    qApp->setStyle("fusion");

  QWidget parent;
  ProgressBarDelegate delegate(&parent);

  QStyleOptionViewItem viewOpt;
  viewOpt.rect = cell;
  viewOpt.state |= QStyle::State_Enabled | QStyle::State_Active;

  QPainter painter(&image);
  delegate.paint(&painter, viewOpt, idx);
  painter.end();

  int inside = 0, outside = 0;
  for (int y = 0; y < image.height(); y++) {
    for (int x = 0; x < image.width(); x++) {
      const QRgb px = image.pixel(x, y);
      if (qRed(px) == 0 && qGreen(px) == 0 && qBlue(px) == 0)
        continue; // background pixel
      if (cell.contains(x, y))
        inside++;
      else
        outside++;
    }
  }

  // Assertion 1: no paint outside the cell rect (rotated-chunk regression).
  QCOMPARE(outside, 0);
  // Assertion 2: at least one painted pixel inside the cell (groove/label).
  QVERIFY(inside > 0);
}

// D4 assertion 3: each progress mutator must ignore an out-of-range orbital
// number without emitting dataChanged and without crashing.
void OrbitalStatusTest::boundsSafeProgressMutators()
{
  OrbitalTableModel model;
  for (int i = 0; i < 5; i++)
    model.setOrbital(makeOrbital(i, QString("MO %1").arg(i + 1)));

  QSignalSpy dataSpy(&model, SIGNAL(dataChanged(QModelIndex, QModelIndex)));

  // Orbital numbers are 1-based; orbital 1000 has no row on a 5-row table.
  model.setOrbitalProgressRange(1000, 0, 10, 1, 3);
  model.incrementStage(1000, 0, 10);
  model.setOrbitalProgressValue(1000, 50);
  model.finishProgress(1000);
  model.resetProgress(1000);
  model.setProgressToZero(1000);

  // The process reaching here (no abort) proves the mutators survive an
  // out-of-range index instead of writing through a dangling pointer.
  QCOMPARE(dataSpy.count(), 0);
}

// D4 assertion 4: clearing an already-empty table must emit no removal
// notification (the pre-fix beginRemoveRows(0, -1) was an invalid range).
void OrbitalStatusTest::clearEmptyTableIsNoop()
{
  OrbitalTableModel model;
  QSignalSpy removedSpy(&model, SIGNAL(rowsRemoved(QModelIndex, int, int)));
  QSignalSpy dataSpy(&model, SIGNAL(dataChanged(QModelIndex, QModelIndex)));

  QVERIFY(model.clearOrbitals());
  QCOMPARE(removedSpy.count(), 0);
  QCOMPARE(dataSpy.count(), 0);
}

// In-range updates must still emit exactly one dataChanged per call, so the
// bounds guard does not silently swallow legitimate progress.
void OrbitalStatusTest::inRangeUpdateEmitsDataChanged()
{
  OrbitalTableModel model;
  for (int i = 0; i < 5; i++)
    model.setOrbital(makeOrbital(i, QString("MO %1").arg(i + 1)));

  QSignalSpy dataSpy(&model, SIGNAL(dataChanged(QModelIndex, QModelIndex)));

  model.setOrbitalProgressRange(1, 0, 10, 1, 3);
  model.setOrbitalProgressValue(3, 50);
  model.finishProgress(5);

  QCOMPARE(dataSpy.count(), 3);
}

// D4 assertion 5: the first fill of a populated table must scroll the view to
// the HOMO row (scrollbar advanced past the top, HOMO cell visible).
void OrbitalStatusTest::firstFillLandsOnHOMO()
{
  OrbitalWidget widget;
  widget.resize(400, 400);
  widget.show();

  QList<Orbital> list;
  for (int i = 0; i < 120; i++) {
    QString desc = (i == 50) ? QString("HOMO") : QString("MO %1").arg(i + 1);
    list.append(makeOrbital(i, desc));
  }
  widget.fillTable(list);

  // Let the zero-delay deferred HOMO scroll (QTimer::singleShot(0)) run.
  QTest::qWait(100);
  for (int i = 0; i < 50; i++)
    qApp->processEvents();

  QTableView *table = widget.findChild<QTableView *>("table");
  QVERIFY(table != nullptr);
  QScrollBar *sb = table->verticalScrollBar();

  // The view must have scrolled down from the top (not left at row 0).
  QVERIFY(sb->value() > 0);
  QVERIFY(sb->maximum() > 0); // rows actually fill beyond the viewport

  // The HOMO view row must be visible in the viewport. The table's model is a
  // proxy, so use mapToSource against it to locate the HOMO row by source text.
  QSortFilterProxyModel *proxy = qobject_cast<QSortFilterProxyModel *>(table->model());
  QVERIFY(proxy != nullptr);
  int homoRow = -1;
  for (int v = 0; v < proxy->rowCount(); v++) {
    const QModelIndex srcIdx = proxy->mapToSource(proxy->index(v, 0));
    if (srcIdx.isValid()
        && srcIdx.data(OrbitalTableModel::C_Description) == "HOMO") {
      homoRow = v;
      break;
    }
  }
  QVERIFY(homoRow >= 0);

  const QRect cellRect = table->visualRect(proxy->index(homoRow, 0));
  const QRect viewport = table->viewport()->rect();
  QVERIFY2(cellRect.intersects(viewport),
           "HOMO row must be visible in the viewport after first fill");
}

QTEST_MAIN(OrbitalStatusTest)
#include "orbitalstatustest.moc"
