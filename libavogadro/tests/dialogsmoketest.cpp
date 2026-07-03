/**********************************************************************
  DialogSmokeTest - offscreen smoke tests for important Avogadro dialogs
 ***********************************************************************/

#include <QtTest>

#include <memory>

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QWidget>

#include <Eigen/Core>

#include <avogadro/molecule.h>
#include "../../avogadro/src/addenginedialog.h"
#include "../../avogadro/src/updatedialog.h"
#include "../src/extensions/conformersearchdialog.h"
#include "../src/extensions/constraintsdialog.h"
#include "../src/extensions/forcefielddialog.h"
#include "../src/extensions/insertfragmentdialog.h"
#include "../src/extensions/orca/orcaanalysedialog.h"
#include "../src/extensions/orca/orcainputdialog.h"
#include "../src/extensions/quantuminput/abinitinputdialog.h"
#include "../src/extensions/quantuminput/daltoninputdialog.h"
#include "../src/extensions/quantuminput/gamessinputdata.h"
#include "../src/extensions/quantuminput/gamessinputdialog.h"
#include "../src/extensions/quantuminput/gamessukinputdialog.h"
#include "../src/extensions/quantuminput/gaussianinputdialog.h"
#include "../src/extensions/quantuminput/inputdialog.h"
#include "../src/extensions/quantuminput/lammpsinputdialog.h"
#include "../src/extensions/quantuminput/molproinputdialog.h"
#include "../src/extensions/quantuminput/mopacinputdialog.h"
#include "../src/extensions/quantuminput/nwcheminputdialog.h"
#include "../src/extensions/quantuminput/psi4inputdialog.h"
#include "../src/extensions/quantuminput/qcheminputdialog.h"
#include "../src/extensions/quantuminput/teracheminputdialog.h"

using Avogadro::AbinitInputDialog;
using Avogadro::AddEngineDialog;
using Avogadro::ConformerSearchDialog;
using Avogadro::ConstraintsDialog;
using Avogadro::DaltonInputDialog;
using Avogadro::ForceFieldDialog;
using Avogadro::GAMESSUKInputDialog;
using Avogadro::GamessInputData;
using Avogadro::GamessInputDialog;
using Avogadro::GaussianInputDialog;
using Avogadro::InputDialog;
using Avogadro::InsertFragmentDialog;
using Avogadro::LammpsInputDialog;
using Avogadro::MolproInputDialog;
using Avogadro::Molecule;
using Avogadro::MOPACInputDialog;
using Avogadro::NWChemInputDialog;
using Avogadro::OrcaAnalyseDialog;
using Avogadro::OrcaInputDialog;
using Avogadro::Psi4InputDialog;
using Avogadro::QChemInputDialog;
using Avogadro::TeraChemInputDialog;
using Avogadro::UpdateDialog;

namespace {

void populateSmokeMolecule(Molecule &molecule)
{
  molecule.addAtom(8, Eigen::Vector3d(0.0, 0.0, 0.0));
  molecule.addAtom(1, Eigen::Vector3d(0.0, 0.0, 0.96));
  molecule.addAtom(1, Eigen::Vector3d(0.0, 0.76, -0.24));
}

void processUiEvents()
{
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  QTest::qWait(0);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}

void smokeShowWidget(const QString &name, QWidget *widget)
{
  QVERIFY2(widget, qPrintable(name + QStringLiteral(" factory returned null")));
  widget->setAttribute(Qt::WA_DeleteOnClose, false);
  widget->show();

  const bool exposed = QTest::qWaitForWindowExposed(widget, 100);
  Q_UNUSED(exposed);
  processUiEvents();

  QVERIFY2(widget->isVisible(), qPrintable(name + QStringLiteral(" closed during show")));
  widget->close();
  processUiEvents();
}

template<typename DialogFactory>
void smokeShowDialog(const QString &name, DialogFactory factory)
{
  std::unique_ptr<QWidget> widget(factory());
  smokeShowWidget(name, widget.get());
}

} // namespace

class DialogSmokeTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void inventory();
  void orcaInputDialogOpens();
  void orcaAnalyseDialogOpens();
  void quantumInputDialogsOpen();
  void coreAppDialogsOpen();
  void recentExtensionDialogsOpen();
};

void DialogSmokeTest::inventory()
{
  // Inventory-driven priority list for this smoke suite.
  //
  // P0 covered here:
  // - ORCA input generator and ORCA analyse dialog.
  // - Quantum input dialogs constructible without external programs or modal UI.
  // - Recently Qt6-touched forcefield/constraint/conformer and insert-fragment dialogs.
  //
  // Inventory only because abstract:
  // - InputDialog is the abstract quantum-input base class. It is covered through
  //   concrete subclasses, not directly instantiated.
  // - SpectraType, AbstractIRSpectra, and AbstractOrcaSpec are abstract/internal
  //   spectra bases, not user-facing smoke factories.
  //
  // Skipped for now:
  // - SpectraDialog and IR/Raman spectra internals remain covered by the static
  //   Qt6 signal-pattern test until their plugin target setup is made MOC-safe
  //   for a standalone smoke executable.
  // - Core app dialogs in avogadro/src often require MainWindow/plugin-manager state.
  // - GL-heavy widgets such as detached GL views and orbital rendering widgets need a
  //   stable GL scene/context-specific harness.
  // - File-dialog/export paths and external-program workflows are skipped to avoid
  //   modal native dialogs, network access, or ORCA/MOPAC/xTB/GAMESS executables.
  QVERIFY(true);
}

void DialogSmokeTest::orcaInputDialogOpens()
{
  Molecule molecule;
  populateSmokeMolecule(molecule);
  smokeShowDialog(QStringLiteral("OrcaInputDialog"), [&]() {
    auto *dialog = new OrcaInputDialog;
    dialog->setMolecule(&molecule);
    return dialog;
  });
}

void DialogSmokeTest::orcaAnalyseDialogOpens()
{
  smokeShowDialog(QStringLiteral("OrcaAnalyseDialog"), []() {
    return new OrcaAnalyseDialog;
  });
}

void DialogSmokeTest::quantumInputDialogsOpen()
{
  Molecule molecule;
  populateSmokeMolecule(molecule);

  auto smokeInputDialog = [&](const QString &name, InputDialog *dialog) {
    std::unique_ptr<InputDialog> widget(dialog);
    widget->setMolecule(&molecule);
    smokeShowWidget(name, widget.get());
  };

  smokeInputDialog(QStringLiteral("AbinitInputDialog"), new AbinitInputDialog);
  smokeInputDialog(QStringLiteral("DaltonInputDialog"), new DaltonInputDialog);
  smokeInputDialog(QStringLiteral("GAMESSUKInputDialog"), new GAMESSUKInputDialog);
  smokeInputDialog(QStringLiteral("GaussianInputDialog"), new GaussianInputDialog);
  smokeInputDialog(QStringLiteral("LammpsInputDialog"), new LammpsInputDialog);
  smokeInputDialog(QStringLiteral("MolproInputDialog"), new MolproInputDialog);
  smokeInputDialog(QStringLiteral("MOPACInputDialog"), new MOPACInputDialog);
  smokeInputDialog(QStringLiteral("NWChemInputDialog"), new NWChemInputDialog);
  smokeInputDialog(QStringLiteral("Psi4InputDialog"), new Psi4InputDialog);
  smokeInputDialog(QStringLiteral("QChemInputDialog"), new QChemInputDialog);
  smokeInputDialog(QStringLiteral("TeraChemInputDialog"), new TeraChemInputDialog);

  GamessInputData gamessData(&molecule);
  GamessInputDialog gamessDialog(&gamessData);
  smokeShowWidget(QStringLiteral("GamessInputDialog"), &gamessDialog);
}

void DialogSmokeTest::coreAppDialogsOpen()
{
  smokeShowDialog(QStringLiteral("AddEngineDialog"), []() { return new AddEngineDialog; });
  smokeShowDialog(QStringLiteral("UpdateDialog"), []() {
    return new UpdateDialog(nullptr, QStringLiteral("Smoke-test update text"));
  });
}

void DialogSmokeTest::recentExtensionDialogsOpen()
{
  smokeShowDialog(QStringLiteral("ForceFieldDialog"), []() { return new ForceFieldDialog; });
  smokeShowDialog(QStringLiteral("ConstraintsDialog"), []() { return new ConstraintsDialog; });
  smokeShowDialog(QStringLiteral("ConformerSearchDialog"), []() { return new ConformerSearchDialog; });
  smokeShowDialog(QStringLiteral("InsertFragmentDialog"), []() { return new InsertFragmentDialog; });
}

QTEST_MAIN(DialogSmokeTest)
#include "dialogsmoketest.moc"
