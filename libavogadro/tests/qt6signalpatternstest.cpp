/**********************************************************************
  Qt6SignalPatternsTest - reject removed Qt5 signal signatures
 ***********************************************************************/

#include <QtTest>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

#ifndef AVOGADRO_SOURCE_DIR
#define AVOGADRO_SOURCE_DIR ""
#endif

class Qt6SignalPatternsTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void removedQt5SignalSignaturesAreNotUsed();
};

void Qt6SignalPatternsTest::removedQt5SignalSignaturesAreNotUsed()
{
  const QDir root(QString::fromLocal8Bit(AVOGADRO_SOURCE_DIR));
  QVERIFY2(root.exists(), "AVOGADRO_SOURCE_DIR does not exist");

  const QStringList scanRoots = {
    root.filePath(QStringLiteral("avogadro/src")),
    root.filePath(QStringLiteral("libavogadro/src/extensions")),
    root.filePath(QStringLiteral("libavogadro/src/tools"))
  };

  const QStringList sourceSuffixes = {
    QStringLiteral(".cpp"), QStringLiteral(".h"), QStringLiteral(".hpp"),
    QStringLiteral(".ui")
  };

  const QList<QRegularExpression> rejectedPatterns = {
    QRegularExpression(QStringLiteral(R"(currentIndexChanged\s*\(\s*(const\s+)?QString)")),
    QRegularExpression(QStringLiteral(R"(activated\s*\(\s*(const\s+)?QString)")),
    QRegularExpression(QStringLiteral(R"(highlighted\s*\(\s*(const\s+)?QString)")),
    QRegularExpression(QStringLiteral(R"(valueChanged\s*\(\s*(const\s+)?QString)")),
    QRegularExpression(QStringLiteral(R"(buttonClicked\s*\(\s*int)")),
    QRegularExpression(QStringLiteral(R"(buttonPressed\s*\(\s*int)")),
    QRegularExpression(QStringLiteral(R"(buttonReleased\s*\(\s*int)")),
    QRegularExpression(QStringLiteral(R"(buttonToggled\s*\(\s*int)"))
  };

  QStringList offenders;
  for (const QString &scanRoot : scanRoots) {
    QDirIterator it(scanRoot, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString path = it.next();
      bool isSource = false;
      for (const QString &suffix : sourceSuffixes) {
        if (path.endsWith(suffix)) {
          isSource = true;
          break;
        }
      }
      if (!isSource)
        continue;

      QFile file(path);
      QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
               qPrintable(QStringLiteral("Could not open %1").arg(path)));
      QTextStream stream(&file);
      int lineNumber = 0;
      bool inBlockComment = false;
      while (!stream.atEnd()) {
        QString line = stream.readLine();
        ++lineNumber;
        QString trimmed = line.trimmed();
        if (inBlockComment) {
          if (trimmed.contains(QStringLiteral("*/")))
            inBlockComment = false;
          continue;
        }
        if (trimmed.startsWith(QStringLiteral("/*"))) {
          if (!trimmed.contains(QStringLiteral("*/")))
            inBlockComment = true;
          continue;
        }
        if (trimmed.startsWith(QStringLiteral("//")))
          continue;

        for (const QRegularExpression &pattern : rejectedPatterns) {
          if (pattern.match(line).hasMatch()) {
            offenders << QStringLiteral("%1:%2: %3")
                           .arg(root.relativeFilePath(path))
                           .arg(lineNumber)
                           .arg(trimmed);
            break;
          }
        }
      }
    }
  }

  QVERIFY2(offenders.isEmpty(),
           qPrintable(QStringLiteral("Removed Qt5 signal signatures found:\n%1")
                        .arg(offenders.join(QLatin1Char('\n')))));
}

QTEST_MAIN(Qt6SignalPatternsTest)
#include "qt6signalpatternstest.moc"
