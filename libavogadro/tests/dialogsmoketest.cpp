/**********************************************************************
  DialogSmokeTest - offscreen smoke tests for important Avogadro dialogs
 ***********************************************************************/

#include <QtTest>

#include <memory>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QWidget>

#include <Eigen/Core>

#include <avogadro/molecule.h>
#include "../../avogadro/src/addenginedialog.h"
#include "../../avogadro/src/updatedialog.h"
#include "../src/extensions/constraintsdialog.h"
#include "../src/extensions/forcefielddialog.h"
#include "../src/extensions/insertfragmentdialog.h"
#include "../src/extensions/orca/orcaanalysedialog.h"
#include "../src/extensions/orca/orcaextension.h"
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
using Avogadro::ConstraintsDialog;
using Avogadro::ConstraintsModel;
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
using Avogadro::OrcaExtension;
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
  qInfo() << "Smoke-opening" << name;
  std::unique_ptr<QWidget> widget(factory());
  smokeShowWidget(name, widget.get());
}

} // namespace

class DialogSmokeTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void inventory();
  void orcaAnalyseDialogOpens();
  void orcaInputDialogOpens();
  void orcaGenerateActionOpens();
  void quantumInputDialogsOpen();
  void coreAppDialogsOpen();
  void forceFieldDialogOpens();
  void constraintsDialogOpens();
  void insertFragmentDialogOpens();
};

void DialogSmokeTest::inventory()
{
  // Inventory-driven priority list for this smoke suite.
  //
  // P0 covered here:
  // - ORCA analyse dialog and ORCA input generator extension action path.
  // - Quantum input dialogs constructible without external programs or modal UI.
  // - Recently Qt6-touched forcefield/constraint and insert-fragment dialogs.
  //
  // Inventory only because abstract:
  // - InputDialog is the abstract quantum-input base class. It is covered through
  //   concrete subclasses, not directly instantiated.
  // - SpectraType, AbstractIRSpectra, and AbstractOrcaSpec are abstract/internal
  //   spectra bases, not user-facing smoke factories.
  //
  // Skipped for now:
  // - ConformerSearchDialog references ForceFieldCommand from the forcefield
  //   extension implementation. It is inventory-only until a complete, stable
  //   force-field plugin test closure is added.
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

void DialogSmokeTest::orcaAnalyseDialogOpens()
{
  smokeShowDialog(QStringLiteral("OrcaAnalyseDialog"), []() {
    return new OrcaAnalyseDialog;
  });
}

void DialogSmokeTest::orcaInputDialogOpens()
{
  Molecule molecule;
  populateSmokeMolecule(molecule);

  qInfo() << "Smoke-opening" << QStringLiteral("OrcaInputDialog");
  auto dialog = std::make_unique<OrcaInputDialog>();
  dialog->setMolecule(&molecule);
  smokeShowWidget(QStringLiteral("OrcaInputDialog"), dialog.get());
}

void DialogSmokeTest::orcaGenerateActionOpens()
{
  QWidget parent;
  OrcaExtension extension(&parent);
  Molecule molecule;
  populateSmokeMolecule(molecule);
  extension.setMolecule(&molecule);

  QAction *generateAction = nullptr;
  const QList<QAction *> actions = extension.actions();
  for (QAction *action : actions) {
    if (action && action->text().contains(QStringLiteral("Generate Orca Input"))) {
      generateAction = action;
      break;
    }
  }

  QVERIFY2(generateAction, "ORCA generate action was not registered");
  qInfo() << "Smoke-opening" << QStringLiteral("OrcaExtension Generate Orca Input action");
  extension.performAction(generateAction, nullptr);
  processUiEvents();

  OrcaInputDialog *dialog = parent.findChild<OrcaInputDialog *>();
  if (!dialog) {
    const QWidgetList widgets = QApplication::topLevelWidgets();
    for (QWidget *widget : widgets) {
      dialog = qobject_cast<OrcaInputDialog *>(widget);
      if (dialog)
        break;
    }
  }

  QVERIFY2(dialog, "ORCA generate action did not open OrcaInputDialog");
  QVERIFY2(dialog->isVisible(), "ORCA generate action dialog is not visible");
  processUiEvents();
  dialog->close();
  processUiEvents();
}

void DialogSmokeTest::quantumInputDialogsOpen()
{
  Molecule molecule;
  populateSmokeMolecule(molecule);

  auto smokeInputDialog = [&](const QString &name, auto factory) {
    qInfo() << "Smoke-opening" << name;
    std::unique_ptr<InputDialog> widget(factory());
    widget->setMolecule(&molecule);
    smokeShowWidget(name, widget.get());
  };

  smokeInputDialog(QStringLiteral("AbinitInputDialog"), []() { return new AbinitInputDialog; });
  smokeInputDialog(QStringLiteral("DaltonInputDialog"), []() { return new DaltonInputDialog; });
  smokeInputDialog(QStringLiteral("GAMESSUKInputDialog"), []() { return new GAMESSUKInputDialog; });
  smokeInputDialog(QStringLiteral("GaussianInputDialog"), []() { return new GaussianInputDialog; });
  smokeInputDialog(QStringLiteral("LammpsInputDialog"), []() { return new LammpsInputDialog; });
  smokeInputDialog(QStringLiteral("MolproInputDialog"), []() { return new MolproInputDialog; });
  smokeInputDialog(QStringLiteral("MOPACInputDialog"), []() { return new MOPACInputDialog; });
  smokeInputDialog(QStringLiteral("NWChemInputDialog"), []() { return new NWChemInputDialog; });
  smokeInputDialog(QStringLiteral("Psi4InputDialog"), []() { return new Psi4InputDialog; });
  smokeInputDialog(QStringLiteral("QChemInputDialog"), []() { return new QChemInputDialog; });
  smokeInputDialog(QStringLiteral("TeraChemInputDialog"), []() { return new TeraChemInputDialog; });

  GamessInputData gamessData(&molecule);
  qInfo() << "Smoke-opening"
          << QStringLiteral("GamessInputDialog");
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

void DialogSmokeTest::forceFieldDialogOpens()
{
  smokeShowDialog(QStringLiteral("ForceFieldDialog"), []() { return new ForceFieldDialog; });
}

void DialogSmokeTest::constraintsDialogOpens()
{
  Molecule molecule;
  populateSmokeMolecule(molecule);
  ConstraintsModel constraints;

  qInfo() << "Smoke-opening" << QStringLiteral("ConstraintsDialog");
  auto dialog = std::make_unique<ConstraintsDialog>();
  dialog->setModel(&constraints);
  dialog->setMolecule(&molecule);
  smokeShowWidget(QStringLiteral("ConstraintsDialog"), dialog.get());
}

void DialogSmokeTest::insertFragmentDialogOpens()
{
  smokeShowDialog(QStringLiteral("InsertFragmentDialog"), []() { return new InsertFragmentDialog; });
}

QTEST_MAIN(DialogSmokeTest)
#include "dialogsmoketest.moc"
