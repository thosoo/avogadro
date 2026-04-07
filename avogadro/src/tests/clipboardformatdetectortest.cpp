#include <QtTest/QtTest>
#include <QMimeData>

#include "../clipboardformatdetector.h"

using namespace Avogadro;

class ClipboardFormatDetectorTest : public QObject
{
  Q_OBJECT

private slots:
  void detectsRawCDXHeader();
  void detectsEmbeddedCDXHeader();
  void detectsCDXMLText();
  void detectsMolfileText();
  void ignoresPlainText();
  void detectsStrongCDXFromExplicitMime();
  void detectsStrongCDXMLFromExplicitMime();
  void allowsWeakDetectionWhenParseSucceeds();
  void weakDetectionFailureFallsBack();
  void generatesWeakCandidateFromCustomEmbeddedCDX();
  void generatesWeakCandidateFromCustomCDXML();
  void ignoresTinyBogusEmbeddedCDX();
};

void ClipboardFormatDetectorTest::detectsRawCDXHeader()
{
  QByteArray payload("VjCD0100");
  payload.append(QByteArray(24, '\x01'));
  QVERIFY(isChemDrawCDX(payload));
  QVERIFY(hasPlausibleChemDrawCDX(payload));
  QCOMPARE(findEmbeddedCDX(payload), 0);
}

void ClipboardFormatDetectorTest::detectsEmbeddedCDXHeader()
{
  QByteArray payload("xxjunkVjCD0100");
  payload.append(QByteArray(24, '\xAA'));
  QVERIFY(!isChemDrawCDX(payload));
  QCOMPARE(findEmbeddedCDX(payload), 6);
}

void ClipboardFormatDetectorTest::detectsCDXMLText()
{
  const QByteArray payload("<?xml version=\"1.0\"?><CDXML BondLength=\"14.4\"></CDXML>");
  QVERIFY(looksLikeCDXML(payload));
}

void ClipboardFormatDetectorTest::detectsMolfileText()
{
  const QByteArray payload(
    "Example\n"
    "  Avogadro\n"
    "\n"
    "  2  1  0  0  0  0            999 V2000\n"
    "    0.0000    0.0000    0.0000 C   0  0  0  0  0\n"
    "    1.2000    0.0000    0.0000 O   0  0  0  0  0\n"
    "  1  2  1  0\n"
    "M  END\n");
  QVERIFY(looksLikeMolfileText(payload));
}

void ClipboardFormatDetectorTest::ignoresPlainText()
{
  const QByteArray payload("Just a normal sentence copied from notes.");
  QVERIFY(!isChemDrawCDX(payload));
  QCOMPARE(findEmbeddedCDX(payload), -1);
  QVERIFY(!looksLikeCDXML(payload));
  QVERIFY(!looksLikeMolfileText(payload));
}

void ClipboardFormatDetectorTest::detectsStrongCDXFromExplicitMime()
{
  QMimeData mimeData;
  QByteArray payload("VjCD0100");
  payload.append(QByteArray(32, '\x01'));
  mimeData.setData("chemical/x-cdx", payload);

  const QList<ChemDrawCandidate> candidates = detectChemDrawClipboardCandidates(&mimeData);
  QCOMPARE(candidates.size(), 1);
  QCOMPARE(candidates.first().formatId, QByteArray("cdx"));
  QCOMPARE(candidates.first().strength, DetectionStrong);
  QVERIFY(candidates.first().isValid());
}

void ClipboardFormatDetectorTest::detectsStrongCDXMLFromExplicitMime()
{
  QMimeData mimeData;
  const QByteArray payload("<?xml version=\"1.0\"?><CDXML></CDXML>");
  mimeData.setData("chemical/x-cdxml", payload);

  const QList<ChemDrawCandidate> candidates = detectChemDrawClipboardCandidates(&mimeData);
  QCOMPARE(candidates.size(), 1);
  QCOMPARE(candidates.first().formatId, QByteArray("cdxml"));
  QCOMPARE(candidates.first().strength, DetectionStrong);
}

void ClipboardFormatDetectorTest::allowsWeakDetectionWhenParseSucceeds()
{
  ChemDrawCandidate weakCandidate;
  weakCandidate.formatId = "cdx";
  weakCandidate.payload = QByteArray("VjCD0100") + QByteArray(32, '\x02');
  weakCandidate.strength = DetectionWeak;

  const ChemDrawHandlingDecision decision =
    classifyChemDrawHandling(weakCandidate, true, true);
  QCOMPARE(decision, Handled);
}

void ClipboardFormatDetectorTest::weakDetectionFailureFallsBack()
{
  ChemDrawCandidate weakCandidate;
  weakCandidate.formatId = "cdxml";
  weakCandidate.payload = QByteArray("<CDXML/>");
  weakCandidate.strength = DetectionWeak;

  QCOMPARE(classifyChemDrawHandling(weakCandidate, true, false), NotHandled);
  QCOMPARE(classifyChemDrawHandling(weakCandidate, false, false), NotHandled);
}

void ClipboardFormatDetectorTest::generatesWeakCandidateFromCustomEmbeddedCDX()
{
  QMimeData mimeData;
  QByteArray wrapper("prefix-bytes");
  wrapper.append("VjCD0100");
  wrapper.append(QByteArray(24, '\x03'));
  mimeData.setData("application/x-some-custom-binary", wrapper);

  const QList<ChemDrawCandidate> candidates = detectChemDrawClipboardCandidates(&mimeData);
  QCOMPARE(candidates.size(), 1);
  QCOMPARE(candidates.first().formatId, QByteArray("cdx"));
  QCOMPARE(candidates.first().strength, DetectionWeak);
  QVERIFY(candidates.first().payload.startsWith("VjCD0100"));
  QVERIFY(!candidates.first().payload.startsWith("prefix-bytes"));
}

void ClipboardFormatDetectorTest::generatesWeakCandidateFromCustomCDXML()
{
  QMimeData mimeData;
  const QByteArray payload("<CDXML BondLength=\"14.4\"></CDXML>");
  mimeData.setData("application/x-custom-cdxml", payload);

  const QList<ChemDrawCandidate> candidates = detectChemDrawClipboardCandidates(&mimeData);
  QCOMPARE(candidates.size(), 1);
  QCOMPARE(candidates.first().formatId, QByteArray("cdxml"));
  QCOMPARE(candidates.first().strength, DetectionWeak);
  QCOMPARE(candidates.first().payload, payload);
}

void ClipboardFormatDetectorTest::ignoresTinyBogusEmbeddedCDX()
{
  const QByteArray tinyBlob("junkVjCD0100tiny");
  QVERIFY(!hasPlausibleChemDrawCDX(tinyBlob));
  QCOMPARE(findEmbeddedCDX(tinyBlob), -1);

  QMimeData mimeData;
  mimeData.setData("application/x-some-custom-binary", tinyBlob);
  const QList<ChemDrawCandidate> candidates = detectChemDrawClipboardCandidates(&mimeData);
  QCOMPARE(candidates.size(), 0);
}

QTEST_MAIN(ClipboardFormatDetectorTest)
#include "clipboardformatdetectortest.moc"
