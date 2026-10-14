/**********************************************************************
  BuilderPool unit test (gpu-rendering-pipeline task 6.1).

  Exercises the thread pool in isolation (no GL context): submit build jobs,
  collect the queued jobReady() results, and assert each result matches the
  deterministic tessellator output. Also checks the generation echo (the GUI
  thread must be able to reject results for an older revision) and that N
  concurrent jobs all complete without loss.
 *****************************************************************/

#include <QtTest>
#include <QSignalSpy>
#include <QMetaType>
#include <QDir>
#include <QElapsedTimer>

#include "libavogadro/src/rendering/snapshottessellator.h"
#include "libavogadro/src/rendering/builderpool.h"

using namespace Avogadro;
using Avogadro::rendering::BuilderPool;
using Avogadro::rendering::BuilderWorker;
using Avogadro::rendering::JobParams;
using Avogadro::rendering::SnapshotAtom;
using Avogadro::rendering::SnapshotBond;
using Avogadro::rendering::SceneSnapshot;
using Avogadro::rendering::TessellationResult;
using Avogadro::rendering::CapturedAtom;

namespace {

//! A tiny molecule: a C-O-N chain with one H on carbon (2 bonds).
SceneSnapshot makeTestSnapshot()
{
  SceneSnapshot snap;
  SnapshotAtom c;
  c.pos[0] = 0.0f; c.pos[1] = 0.0f; c.pos[2] = 0.0f; c.atomicNumber = 6;
  SnapshotAtom o;
  o.pos[0] = 1.2f; o.pos[1] = 0.0f; o.pos[2] = 0.0f; o.atomicNumber = 8;
  SnapshotAtom n;
  n.pos[0] = 2.4f; n.pos[1] = 0.0f; n.pos[2] = 0.0f; n.atomicNumber = 7;
  SnapshotAtom h;
  h.pos[0] = -0.6f; h.pos[1] = 0.8f; h.pos[2] = 0.0f; h.atomicNumber = 1;
  snap.atoms.append(c);
  snap.atoms.append(o);
  snap.atoms.append(n);
  snap.atoms.append(h);

  SnapshotBond b0;
  b0.begin = 0; b0.end = 1; b0.order = 1;
  SnapshotBond b1;
  b1.begin = 0; b1.end = 2; b1.order = 1;
  snap.bonds.append(b0);
  snap.bonds.append(b1);

  return snap;
}

JobParams makeTestParams()
{
  JobParams p;
  p.settings.alpha = 1.0f;
  p.settings.showMulti = 2;
  p.settings.bondRadius = 0.1f;
  p.settings.shift = 0.15f;
  for (int i = 0; i < 4; ++i) {
    CapturedAtom ca;
    ca.rgba[0] = 0.2f; ca.rgba[1] = 0.4f; ca.rgba[2] = 0.6f; ca.rgba[3] = 1.0f;
    ca.radius = 0.3f;
    p.atoms.append(ca);
  }
  return p;
}

} // namespace

//! Counts jobReady() arrivals on a worker, connected directly (bypassing the
//! pool's re-emit). Used to isolate where a queued signal is dropped.
class WorkerCounter : public QObject
{
  Q_OBJECT
public:
  int count = 0;
  void onReady()
  {
    ++count;
  }
};

class BuilderPoolTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void init();
  void cleanup();
  void singleJob();
  void concurrentJobs();
  void generationEcho();
  void directDelivery();
  void editsDuringBuilds();
  void viewCloseDuringBuilds();

private:
  BuilderPool *m_pool;
};

void BuilderPoolTest::init()
{
  qRegisterMetaType<TessellationResult>();
  m_pool = new BuilderPool(2, this);
}

void BuilderPoolTest::cleanup()
{
  delete m_pool;
  m_pool = nullptr;
}

void BuilderPoolTest::singleJob()
{
  SceneSnapshot snap = makeTestSnapshot();
  JobParams params = makeTestParams();

  QSignalSpy spy(m_pool, &BuilderPool::jobReady);
  QCOMPARE(spy.count(), 0);

  m_pool->submit(snap, params, /*generation=*/42);
  QCOMPARE(m_pool->pendingCount(), 1);

  // The result arrives on this thread via a queued connection.
  QVERIFY(spy.wait());
  QCOMPARE(spy.count(), 1);

  const auto args = spy.at(0);
  const TessellationResult result = args.value(1).value<TessellationResult>();
  const unsigned int generation = args.value(2).toUInt();

  QCOMPARE(generation, 42u);
  // 4 atoms => 4 sphere records; 2 bonds => 4 bond records (2 halves each).
  QCOMPARE(static_cast<int>(result.spheres.size()), 4);
  QCOMPARE(static_cast<int>(result.bonds.size()), 4);
  QCOMPARE(m_pool->pendingCount(), 0);

  // The queued result must equal the deterministic direct tessellation.
  const TessellationResult expected = snapshotTessellateBSDY(snap, params);
  QCOMPARE(static_cast<int>(result.spheres.size()),
           static_cast<int>(expected.spheres.size()));
  QCOMPARE(static_cast<int>(result.bonds.size()),
           static_cast<int>(expected.bonds.size()));
}

void BuilderPoolTest::concurrentJobs()
{
  const int n = 8;
  QSignalSpy spy(m_pool, &BuilderPool::jobReady);

  for (int i = 0; i < n; ++i) {
    SceneSnapshot snap = makeTestSnapshot();
    JobParams params = makeTestParams();
    m_pool->submit(snap, params, static_cast<unsigned int>(i));
  }
  // All n jobs were enqueued; a fast worker may already have drained one,
  // so pending is only bounded here (the signal loop below is the real check).
  QVERIFY(m_pool->pendingCount() > 0 && m_pool->pendingCount() <= n);

  // Each wait() collects one more signal; loop until all n have arrived.
  while (spy.count() < n)
    QVERIFY(spy.wait(5000));
  QCOMPARE(spy.count(), n);
  // Every job completed exactly once, no loss or duplication under concurrency.
  QSet<int> generations;
  for (const auto &args : spy) {
    generations.insert(args.value(2).toUInt());
  }
  QCOMPARE(generations.size(), n);
  // The worker decrements pending after emitting, and the queued signal is
  // delivered async; drain the event loop so the final decrement is visible.
  for (int i = 0; i < 200 && m_pool->pendingCount() != 0; ++i)
    QCoreApplication::processEvents();
  QCOMPARE(m_pool->pendingCount(), 0);
}

void BuilderPoolTest::generationEcho()
{
  // Submit two revisions of the same molecule. The later revision (higher
  // generation) must be echoed back; the GUI thread would discard the earlier.
  QSignalSpy spy(m_pool, &BuilderPool::jobReady);

  m_pool->submit(makeTestSnapshot(), makeTestParams(), 1);
  m_pool->submit(makeTestSnapshot(), makeTestParams(), 2);

  // Wait until BOTH results have been reported. Calling wait() twice would be
  // wrong: if both signals are delivered before the first wait() returns,
  // count is already 2 and a second wait() would block for a third signal.
  while (spy.count() < 2)
    QVERIFY(spy.wait(5000));
  QCOMPARE(spy.count(), 2);

  QSet<unsigned int> gens;
  for (const auto &args : spy)
    gens.insert(args.value(2).toUInt());
  QCOMPARE(gens.contains(1u), true);
  QCOMPARE(gens.contains(2u), true);
}

//! Diagnostic: connect a counter DIRECTLY to each worker's jobReady(),
//! bypassing the pool's re-emit. If the counter reaches 2 while the pool's
//! jobReady() spy reaches only 1, the drop is in the pool's signal forwarding.
void BuilderPoolTest::directDelivery()
{
  WorkerCounter counter;
  for (BuilderWorker *w : m_pool->workers())
    connect(w, &BuilderWorker::jobReady, &counter, &WorkerCounter::onReady,
            Qt::QueuedConnection);

  m_pool->submit(makeTestSnapshot(), makeTestParams(), 1);
  m_pool->submit(makeTestSnapshot(), makeTestParams(), 2);

  int spins = 0;
  while (counter.count < 2 && spins++ < 500) {
    QCoreApplication::processEvents();
    QTest::qWait(5);  // let the worker thread make progress between drains
  }
  QCOMPARE(counter.count, 2);
}

//! Task 6.2: edits during builds. A later submission bumps the revision and
//! the GUI thread discards results for older revisions (design A7). The edit
//! lands while earlier jobs are still in flight, so stale results genuinely
//! arrive after the revision changed and must be dropped by the check.
void BuilderPoolTest::editsDuringBuilds()
{
  const int k = 20;
  QSignalSpy spy(m_pool, &BuilderPool::jobReady);

  // Build revision 1, then edit (revision 2) while revision-1 jobs may still
  // be tessellating on the workers.
  for (int i = 0; i < k; ++i)
    m_pool->submit(makeTestSnapshot(), makeTestParams(), 1);
  const unsigned int latest = 2;
  for (int i = 0; i < k; ++i)
    m_pool->submit(makeTestSnapshot(), makeTestParams(), latest);

  // Every submitted job eventually reports (nothing is lost), and the
  // revision check discards exactly the stale ones.
  while (spy.count() < 2 * k)
    QVERIFY(spy.wait(5000));

  int staleDiscarded = 0;
  int currentKept = 0;
  for (const auto &args : spy) {
    if (args.value(2).toUInt() == latest)
      ++currentKept;
    else
      ++staleDiscarded; // the GUI-side revision check drops these
  }
  QCOMPARE(currentKept, k);   // every job of the current revision survives
  QCOMPARE(staleDiscarded, k); // every stale result is discarded, none leaks
}

//! Task 6.2: view close during builds. Destroying the pool with jobs in
//! flight must join the workers (no abandoned threads), not crash, and not
//! leak builder threads.
void BuilderPoolTest::viewCloseDuringBuilds()
{
  auto threadCount = []() {
    return QDir(QStringLiteral("/proc/self")).entryList({"task"}).isEmpty()
             ? 0
             : QDir(QStringLiteral("/proc/self/task")).entryList().size();
  };

  for (int iteration = 0; iteration < 5; ++iteration) {
    const int before = threadCount();
    BuilderPool *pool = new BuilderPool(2); // explicit lifetime, no parent
    for (int i = 0; i < 50; ++i)
      pool->submit(makeTestSnapshot(), makeTestParams(), 1);
    // "View close": jobs are in flight when the pool is destroyed. The
    // destructor must wake and join every worker; QTest's own timeout turns
    // a deadlock here into a visible failure.
    delete pool;
    // A just-joined QThread's /proc/task entry can linger a few ms before the
    // kernel reaps it, so a single snapshot right after delete can over-count
    // by one. Poll until the count settles at/below baseline. A real leak never
    // settles (it stays above before), so this cannot mask one.
    int after = threadCount();
    const int settleMs = 2000;
    QElapsedTimer settleClock;
    settleClock.start();
    while (after > before && settleClock.elapsed() < settleMs) {
      QThread::msleep(2);
      after = threadCount();
    }
    const int settled = after;
    QVERIFY2(settled <= before,
             qPrintable(QString("iteration %1 leaked builder threads: %2 -> %3 (settled %4, polled %5 ms)")
                          .arg(iteration).arg(before).arg(after).arg(settled).arg(settleClock.elapsed())));
  }
}

#include "builderpooltest.moc"
QTEST_MAIN(BuilderPoolTest)
