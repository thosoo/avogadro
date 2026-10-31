// Regression guard for the OpenSpec task 5 (2.3, D3) refactor: the OpenQube ->
// Avogadro cube-data handoff copy MUST run on a worker thread and resume the
// phase machine exactly once via a queued continuation. A regression that
// invokes the continuation both synchronously and from the worker (the bug this
// guards) calls calculateMesh() twice, which spawns two concurrent mesh
// generators and leaves the molecule with 4 meshes instead of 2.
//
// This drives the real MO path end-to-end (async cube calc -> async copy ->
// queued continuation -> mesh generation) and asserts:
//   1. the molecule ends up with exactly 2 meshes (one positive, one negative
//      lobe) -- i.e. calculateMesh ran once, not twice, and
//   2. both lobes actually became stable (the queued continuation fired and
//      the mesh generators ran to completion).
#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QComboBox>
#include <QMetaObject>
#include <QElapsedTimer>
#include <QThread>

#include "surfaces/surfaceextension.h"
#include "surfaces/surfacedialog.h"
#include "surfaces/vdwsurface.h"

#include <avogadro/molecule.h>
#include <avogadro/moleculefile.h>
#include <avogadro/mesh.h>

using namespace Avogadro;

// Select the MO entry on the dialog's combo boxes. The surface combo order is
// VdW=0, ESP=1, ElectronDensity=2, MO=3 (see SurfaceDialog::setMOs). We reach
// the combos by object name via findChild() rather than reaching into the
// private Ui member, so this stays a public-API white-box test.
class TestSurfaceDialog : public SurfaceDialog
{
public:
  void selectMO(int moIndex0)
  {
    QComboBox *surfaceCombo = findChild<QComboBox *>("surfaceCombo");
    QComboBox *moCombo = findChild<QComboBox *>("moCombo");
    QVERIFY(surfaceCombo != nullptr);
    QVERIFY(moCombo != nullptr);
    surfaceCombo->setCurrentIndex(3);
    moCombo->setCurrentIndex(moIndex0);
  }
};

class TestSurfaceExtension : public SurfaceExtension
{
public:
  using SurfaceExtension::loadBasis;
  void setDialogPub(SurfaceDialog *d) { m_surfaceDialog = d; }
  void setMoleculePub(Molecule *m) { m_molecule = m; }

  int numMeshes() { return m_molecule ? m_molecule->numMeshes() : 0; }

  // Poll until both MO lobes are stable or the timeout elapses, processing
  // events so the async cube calc, the D3 copy worker, and the mesh
  // generators all drain.
  bool waitMeshesStable(int timeoutMs = 15000)
  {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      QList<Mesh *> meshes =
          m_molecule ? m_molecule->meshes() : QList<Mesh *>();
      if (meshes.size() == 2 && meshes.at(0)->stable() &&
          meshes.at(1)->stable())
        return true;
    }
    return false;
  }
};

class SurfaceMOPathTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void testMOAsyncCopyDrivesMeshOnce()
  {
    QString dataDir = QString::fromUtf8(TESTDATA_DIR) + "/koffein_orca.out";
    MoleculeFile *mf = MoleculeFile::readFile(dataDir.toUtf8().constData());
    QVERIFY(mf);
    Molecule *mol = mf->molecule(0);
    QVERIFY(mol);
    QVERIFY(mol->numAtoms() > 0);
    mol->setFileName(dataDir);
    // Fresh molecule has no meshes before the MO path runs.
    QCOMPARE(mol->numMeshes(), 0);

    TestSurfaceDialog dialog;
    dialog.setMolecule(mol);

    TestSurfaceExtension ext;
    ext.setDialogPub(&dialog);
    ext.setMoleculePub(mol);
    QVERIFY2(ext.loadBasis(), "basis set should load from the ORCA output");
    // loadBasis() calls setMOs()/updateCubes(), which clears and repopulates
    // the surface combo (resetting its index to VdW). Select the MO entry
    // AFTER loadBasis() so the selection survives.
    dialog.selectMO(0); // select MO #1
    QCOMPARE(dialog.cubeType(), Cube::MO);

    // Drive the real calculate() path. The async MO cube calc -> D3 copy ->
    // queued continuation -> calculateMesh cascade runs on worker + GUI
    // threads while we pump the event loop.
    QVERIFY(QMetaObject::invokeMethod(&ext, "calculate"));
    QVERIFY2(ext.waitMeshesStable(),
             "MO lobes never became stable (continuation did not fire or the "
             "mesh generators did not run)");

    // calculateMesh MUST run exactly once: one positive + one negative lobe.
    // A double continuation would produce 4 meshes (two orphaned lobes).
    QCOMPARE(ext.numMeshes(), 2);
  }
};

QTEST_MAIN(SurfaceMOPathTest)
#include "surfaceextensionmothrottletest.moc"
