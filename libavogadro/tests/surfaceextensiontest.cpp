// Regression test for fix-second-document-open-crash (tasks 1.3 / 2.2):
// SurfaceExtension's N.1 stale-callback guard compared
// m_runningGeneration != m_generation, but m_runningGeneration was never
// captured at calculation launch, so after setMolecule bumped m_generation the
// guard rejected every calculateDone() forever and no surface could render.
//
// The test drives the real calculate() path (which captures
// m_runningGeneration = m_generation at the top before launching the async
// VdW cube calc) and asserts the captured epoch equals the current generation
// -- exactly the invariant the calculateDone() guard checks -- proving the
// guard now passes for the current document's callbacks.
//
// White-box by design: TestSurfaceExtension subclasses SurfaceExtension and
// accesses protected internals (the N.1 generation-guard members, the VdW
// watcher plumbing, and loadBasis) to assert the invariant directly. Those
// members are declared `protected` in surfaceextension.h for exactly this
// purpose (OpenSpec task 1.1); loadBasis() is re-exported publicly via a
// `using`-declaration in the subclass because SurfacesGuardTest is not a
// derived class and otherwise could not name a protected base member. This is
// standard C++, so no access-suppression flags are used.

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QMetaObject>
#include <QElapsedTimer>
#include <QThreadPool>

#include "surfaces/surfaceextension.h"
#include "surfaces/surfacedialog.h"
#include "surfaces/vdwsurface.h"

#include <avogadro/molecule.h>
#include <avogadro/moleculefile.h>
#include <avogadro/meshgenerator.h>

using namespace Avogadro;

// Crude crash-isolation marker (replaces QDEBUG which QtTest would swallow).
#define QDEBUG_MARKER(msg) do { \
  std::fprintf(stderr, "[MARKER] %s\n", (msg)); \
  std::fflush(stderr); \
} while (0)

// Expose the N.1 generation members and the private plumbing the test needs to
// drive calculate() and quiesce the async VdW calc deterministically (the
// ~SurfaceExtension destructor deletes m_VdWsurface with no quiesce, so a
// running calc would be a cross-thread UAF -- the bug this change prevents).
namespace {
class TestSurfaceExtension : public SurfaceExtension
{
public:
  // Re-export loadBasis() publicly: SurfacesGuardTest is not a derived class,
  // so standard C++ bars it from calling a protected base member directly (the
  // removed -fno-access-control previously masked this). loadBasis() itself
  // stays protected in SurfaceExtension (OpenSpec task 1.1); this using-
  // declaration is the only change needed to make line 108 legal. It is not a
  // functional change: no assertion or scenario is touched.
  using SurfaceExtension::loadBasis;

  unsigned long generation() const { return m_generation; }
  unsigned long runningGeneration() const { return m_runningGeneration; }

  // Own the dialog so calculate() has a non-null m_surfaceDialog.
  void setDialog(SurfaceDialog *d) { m_surfaceDialog = d; }

  // setMolecule() only forwards the molecule to the dialog; it never stores
  // it in m_molecule (a latent plugin gap). The test injects it directly so
  // loadBasis()/calculate() have a valid document.
  void setMoleculePtr(Molecule *m) { m_molecule = m; }

  // The async VdW calc connects its watcher's finished() -> calculateDone().
  // Detach that so the mesh-calculation cascade does not run under test.
  void disconnectVdWCallback()
  {
    if (m_VdWsurface)
      disconnect(&m_VdWsurface->watcher(), 0, this, 0);
  }

  // Join the async VdW cube calculation before destruction.
  void waitForVdW()
  {
    if (m_VdWsurface && m_VdWsurface->watcher().isRunning())
      m_VdWsurface->watcher().future().waitForFinished();
  }
};
}

class SurfacesGuardTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void testSurfaceGuardCapturesGeneration()
  {
    QString dataDir = QString::fromUtf8(TESTDATA_DIR) + "/koffein_orca.out";
    MoleculeFile *mf = MoleculeFile::readFile(dataDir.toUtf8().constData());
    QVERIFY(mf);
    Molecule *mol = mf->molecule(0);
    QVERIFY(mol);
    QVERIFY(mol->numAtoms() > 0);
    // MatchBasisSet() resolves the basis set from the molecule's file name
    // (it finds koffein_orca.molden next to the ORCA output).
    mol->setFileName(dataDir);

    // Wire a real dialog into the extension FIRST: both setMolecule and
    // loadBasis dereference m_surfaceDialog (setMOs/setHOMO/setLUMO), and
    // calculate() dereferences it too. The dialog defaults to VdW.
    QDEBUG_MARKER("before new dialog");
    SurfaceDialog *dialog = new SurfaceDialog();
    dialog->setMolecule(mol);
    QCOMPARE(dialog->cubeType(), Cube::VdW);
    QDEBUG_MARKER("after dialog setup");

    TestSurfaceExtension ext;
    ext.setDialog(dialog);
    ext.setMoleculePtr(mol);
    ext.setMolecule(mol);
    QDEBUG_MARKER("after ext.setMolecule");
    QVERIFY2(ext.loadBasis(), "basis set should load from the ORCA output");
    QDEBUG_MARKER("after loadBasis");

    // N.1 bug condition: setMolecule bumps m_generation and resets
    // m_runningGeneration to 0. Without the calculate() capture the
    // calculateDone() guard would reject every legitimate callback forever.
    QVERIFY(ext.generation() >= 1);
    QCOMPARE(ext.runningGeneration(), static_cast<unsigned long>(0));
    QDEBUG_MARKER("after generation asserts");
    QObject::connect(dialog, SIGNAL(calculate()), &ext, SLOT(calculate()));

    // Real calculate(): with the fix it captures m_runningGeneration =
    // m_generation at the top before launching the async VdW calculation.
    QVERIFY(QMetaObject::invokeMethod(&ext, "calculate"));
    QCOMPARE(ext.runningGeneration(), ext.generation());

    // Detach the watcher's finished() -> calculateDone() so the mesh-calc
    // cascade does not run, then join the async VdW calc before destruction
    // (the destructor deletes m_VdWsurface with no quiesce).
    ext.disconnectVdWCallback();
    ext.waitForVdW();
  }
};

QTEST_MAIN(SurfacesGuardTest)

#include "surfaceextensiontest.moc"
