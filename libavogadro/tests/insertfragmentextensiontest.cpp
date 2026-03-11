/**********************************************************************
  InsertFragmentExtensionTest - regression tests for fragment insertion
 ***********************************************************************/

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTreeView>
#include <QSignalSpy>

#include <avogadro/molecule.h>

#define private public
#include "../src/extensions/insertfragmentextension.cpp"
#undef private

using Avogadro::InsertFragmentDialog;
using Avogadro::InsertFragmentExtension;
using Avogadro::Molecule;

class InsertFragmentExtensionTest : public QObject
{
  Q_OBJECT

private:
  static QModelIndex findFileIndex(QAbstractItemModel *model,
                                   const QModelIndex &parent,
                                   const QString &fileName)
  {
    for (int row = 0; row < model->rowCount(parent); ++row) {
      QModelIndex idx = model->index(row, 0, parent);
      if (model->data(idx, QFileSystemModel::FileNameRole).toString() == fileName)
        return idx;

      QModelIndex child = findFileIndex(model, idx, fileName);
      if (child.isValid())
        return child;
    }

    return QModelIndex();
  }

private Q_SLOTS:
  void insertFragmentWithNullWidgetDoesNotCrash();
};

void InsertFragmentExtensionTest::insertFragmentWithNullWidgetDoesNotCrash()
{
  const QString fragmentDir = QCoreApplication::applicationDirPath() +
                              "/../share/avogadro/fragments";
  QVERIFY2(QDir().mkpath(fragmentDir), "Could not create fragments directory");

  const QString fileName = "codex_fragment_regression.xyz";
  const QString filePath = fragmentDir + "/" + fileName;
  QFile file(filePath);
  QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text),
           "Could not create regression fragment file");
  file.write("3\nwater\nO 0.0 0.0 0.0\nH 0.0 0.0 1.0\nH 0.0 1.0 0.0\n");
  file.close();

  InsertFragmentExtension extension;
  Molecule molecule;
  extension.setMolecule(&molecule);
  extension.m_widget = 0;

  InsertFragmentDialog *dialog = extension.m_fragmentDialog;
  QVERIFY(dialog);

  QTreeView *tree = dialog->findChild<QTreeView*>("directoryTreeView");
  QVERIFY(tree);
  QVERIFY(tree->model());

  QTRY_VERIFY(tree->model()->rowCount(tree->rootIndex()) >= 1);

  QModelIndex fileIndex = findFileIndex(tree->model(), tree->rootIndex(), fileName);
  QVERIFY2(fileIndex.isValid(), "Could not find test fragment in dialog model");

  tree->setCurrentIndex(fileIndex);
  tree->selectionModel()->select(fileIndex,
                                 QItemSelectionModel::ClearAndSelect |
                                 QItemSelectionModel::Rows);

  QSignalSpy performCommandSpy(&extension, SIGNAL(performCommand(QUndoCommand*)));
  emit dialog->performInsert();

  QCOMPARE(performCommandSpy.count(), 1);

  QFile::remove(filePath);
}

QTEST_MAIN(InsertFragmentExtensionTest)
#include "insertfragmentextensiontest.moc"
