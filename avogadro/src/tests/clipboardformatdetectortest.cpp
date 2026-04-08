#include <QtTest/QtTest>
#include <QMimeData>
#include <openbabel/mol.h>
#include <openbabel/obconversion.h>

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
  void repeatedReadClonesStatefulFormatInstance();
  void repeatedCDXMLReadsSucceedWhenReaderAvailable();
  void repeatedCDXReadsSucceedWhenReaderAvailable();
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

namespace {

class StatefulReadOnceFormat : public OpenBabel::OBFormat
{
public:
  StatefulReadOnceFormat(bool registerFormat = false) : m_alreadyRead(false)
  {
    if (registerFormat)
      this->RegisterFormat("stateful-read-once");
  }

  const char* Description() override { return "Stateful read-once test format"; }
  unsigned int Flags() override { return READONEONLY; }
  const char* SpecificationURL() override { return ""; }
  bool ReadMolecule(OpenBabel::OBBase *pOb, OpenBabel::OBConversion *) override
  {
    if (m_alreadyRead)
      return false;
    m_alreadyRead = true;

    OpenBabel::OBMol *mol = dynamic_cast<OpenBabel::OBMol*>(pOb);
    if (!mol)
      return false;
    mol->NewAtom();
    return true;
  }
  bool WriteMolecule(OpenBabel::OBBase *, OpenBabel::OBConversion *) override
  {
    return false;
  }
  OpenBabel::OBFormat* MakeNewInstance() override
  {
    return new StatefulReadOnceFormat;
  }

private:
  bool m_alreadyRead;
};

StatefulReadOnceFormat g_statefulReadOnceFormat(true);

} // namespace

void ClipboardFormatDetectorTest::repeatedReadClonesStatefulFormatInstance()
{
  OpenBabel::OBMol firstRead;
  bool firstReaderAvailable = false;
  QVERIFY(tryReadClipboardPayloadAsFormat(
    firstRead, QByteArray("dummy"), QByteArray("stateful-read-once"), &firstReaderAvailable));
  QVERIFY(firstReaderAvailable);
  QCOMPARE(firstRead.NumAtoms(), 1u);

  OpenBabel::OBMol secondRead;
  bool secondReaderAvailable = false;
  QVERIFY(tryReadClipboardPayloadAsFormat(
    secondRead, QByteArray("dummy"), QByteArray("stateful-read-once"), &secondReaderAvailable));
  QVERIFY(secondReaderAvailable);
  QCOMPARE(secondRead.NumAtoms(), 1u);
}

void ClipboardFormatDetectorTest::repeatedCDXMLReadsSucceedWhenReaderAvailable()
{
  const QByteArray cdxml(
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<CDXML><page><fragment><n id=\"1\" Element=\"6\" p=\"0 0\"/>"
    "<n id=\"2\" Element=\"8\" p=\"10 0\"/>"
    "<b id=\"3\" B=\"1\" E=\"2\" Order=\"1\"/></fragment></page></CDXML>");

  OpenBabel::OBMol firstRead;
  bool firstReaderAvailable = false;
  const bool firstOk = tryReadClipboardPayloadAsFormat(
    firstRead, cdxml, QByteArray("cdxml"), &firstReaderAvailable);
  if (!firstReaderAvailable)
    QSKIP("Open Babel cdxml reader unavailable in this build.");
  QVERIFY(firstOk);
  QVERIFY(firstRead.NumAtoms() > 0);

  OpenBabel::OBMol secondRead;
  bool secondReaderAvailable = false;
  const bool secondOk = tryReadClipboardPayloadAsFormat(
    secondRead, cdxml, QByteArray("cdxml"), &secondReaderAvailable);
  QVERIFY(secondReaderAvailable);
  QVERIFY(secondOk);
  QCOMPARE(secondRead.NumAtoms(), firstRead.NumAtoms());
}

void ClipboardFormatDetectorTest::repeatedCDXReadsSucceedWhenReaderAvailable()
{
  const QByteArray cdx = QByteArray::fromBase64(
    "VmpDRDAxMDAEAwIBAAAAAAAAAAAAAACAAAAAAAMAFQAAAENoZW1EcmF3IDE3LjEuMC4xMDUIAA0AAABldGhhbm9sLmNkeAQCEAAy"
    "7EIA9YjuAACAVgBC3i0BAQkIAABAFwAAAAYAAgkIAABAiAEAQCQCDQgBAAEIBwEAAToEAQABOwQBAABFBAEAATwEAQAASgQBAAAM"
    "BgEAAQ8GAQABDQYBAABCBAEAAEMEAQAARAQBAAAKCAgAAwBgAMgAAwALCAgABAAAAPAAAwAJCAQAM7MCAAgIBAAAAAIABwgEAAAA"
    "AQAGCAQAAAAEAAUIBAAAAB4ABAgCAHgAAwgEAAAAeAAjCAEABQwIAQAAKAgBAAEpCAEAASoIAQABMggBAAArCAEAKCwIAQAKLQgB"
    "AAEuCAEAAAIIEAAAACQAAAAkAAAAJAAAACQAAQMCAAAAAgMCAAEAAAMyAAgA////////AAAAAAAA//8AAAAA/////wAAAAD//wAA"
    "AAD/////AAAAAP////8AAP//AAEkAAAAAgADAOQEBQBBcmlhbAQA5AQPAFRpbWVzIE5ldyBSb21hbgAIeAAAAwAAAlgCWAAAAAAa"
    "zBLF/7L/shsaExMDZwV7A+AAAgAAAlgCWAAAAAAazBLFAAEAAABkAAAAAQABAQEAAgABJw8AAQABAAAAAAAAAAAAAAAAAAIAGQGQ"
    "AAAAAABgAAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAAAC0CwIAAAC1CxQAAABDaGVtaWNhbCBGb3JtdWxhOiC2Cw4AAABFeGFjdCBN"
    "YXNzOiC3CxQAAABNb2xlY3VsYXIgV2VpZ2h0OiC4CwcAAABtL3o6ILkLFgAAAEVsZW1lbnRhbCBBbmFseXNpczogugsRAAAAQm9p"
    "bGluZyBQb2ludDoguwsRAAAATWVsdGluZyBQb2ludDogvAsRAAAAQ3JpdGljYWwgVGVtcDogvQsRAAAAQ3JpdGljYWwgUHJlczog"
    "vgsQAAAAQ3JpdGljYWwgVm9sOiC/CxAAAABHaWJicyBFbmVyZ3k6IMALCQAAAExvZyBQOiDBCwYAAABNUjogwgsPAAAASGVucnkn"
    "cyBMYXc6IMMLEAAAAEhlYXQgb2YgRm9ybTogxAsIAAAAdFBTQTogxQsJAAAAQ0xvZ1A6IMYLBwAAAENNUjogxwsIAAAATG9nUzog"
    "yAsHAAAAcEthOiDJCwIAAADKCwIAAAALDAIAAQAKDAEAAAkMAQAADAwFAAAAKCMpAYBCAAAABAIQAAAAAAAAAAAAhesBA+tRCwIW"
    "CAQAAAAkABgIBAAAACQAGQgAABAIAgABAA8IAgABAAOAPAAAAAQCEAAy7EIA9YjuAACAVgBC3i0BCgACADsABIA7AAAAAAIIAACA"
    "UgD1yO4ACgACADoANwQBAAEAAASAPQAAAAACCAAAgEMACMQIAQoAAgA8ADcEAQABAAAEgD8AAAAAAggAAIBSABy/IgEKAAIAPgAC"
    "BAIACAArBAIAAQBIBAAANwQBAAEGgAAAAAAAAggAZmZWALXYHgEEAhAAEfFNALXYHgEAgFYAQt4tASMIAQAAAgcCAAAABQcBAAEA"
    "Bw4AAQAAAAMAYADIAAAAT0gAAAAABYA+AAAACgACAD0ABAYEADsAAAAFBgQAPQAAAAoGAQABAAAFgEAAAAAKAAIAPwAEBgQAPQAA"
    "AAUGBAA/AAAACgYBAAEAAAAAAAAAAAAA"
  );

  OpenBabel::OBMol firstRead;
  bool firstReaderAvailable = false;
  const bool firstOk = tryReadClipboardPayloadAsFormat(
    firstRead, cdx, QByteArray("cdx"), &firstReaderAvailable);
  if (!firstReaderAvailable)
    QSKIP("Open Babel cdx reader unavailable in this build.");
  QVERIFY(firstOk);
  QVERIFY(firstRead.NumAtoms() > 0);

  OpenBabel::OBMol secondRead;
  bool secondReaderAvailable = false;
  const bool secondOk = tryReadClipboardPayloadAsFormat(
    secondRead, cdx, QByteArray("cdx"), &secondReaderAvailable);
  QVERIFY(secondReaderAvailable);
  QVERIFY(secondOk);
  QCOMPARE(secondRead.NumAtoms(), firstRead.NumAtoms());
}

QTEST_MAIN(ClipboardFormatDetectorTest)
#include "clipboardformatdetectortest.moc"
