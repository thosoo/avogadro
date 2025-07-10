#include "config.h"
#include <QtTest>
#include <openbabel/obconversion.h>
#include <openbabel/format.h>
#include <openbabel/mol.h>
#include <openbabel/plugin.h>
#include <QTemporaryFile>
#include <QDir>

using OpenBabel::OBConversion;
using OpenBabel::OBFormat;
using OpenBabel::OBMol;
using OpenBabel::OBPlugin;

class CdxFormatTest : public QObject
{
  Q_OBJECT
private slots:
  void readCdx();
};

void CdxFormatTest::readCdx()
{
  // Binary ChemDraw sample (ethanol) encoded as base64 so no external file
  const char *b64 =
      "VmpDRDAxMDAEAwIBAAAAAAAAAAAAAACAAAAAAAMAFQAAAENoZW1EcmF3IDE3LjEuMC4xMDUIAA0A"
      "AABldGhhbm9sLmNkeAQCEAAy7EIA9YjuAACAVgBC3i0BAQkIAABAFwAAAAYAAgkIAABAiAEAQCQC"
      "DQgBAAEIBwEAAToEAQABOwQBAABFBAEAATwEAQAASgQBAAAMBgEAAQ8GAQABDQYBAABCBAEAAEME"
      "AQAARAQBAAAKCAgAAwBgAMgAAwALCAgABAAAAPAAAwAJCAQAM7MCAAgIBAAAAAIABwgEAAAAAQAG"
      "CAQAAAAEAAUIBAAAAB4ABAgCAHgAAwgEAAAAeAAjCAEABQwIAQAAKAgBAAEpCAEAASoIAQABMggB"
      "AAArCAEAKCwIAQAKLQgBAAEuCAEAAAIIEAAAACQAAAAkAAAAJAAAACQAAQMCAAAAAgMCAAEAAAMy"
      "AAgA////////AAAAAAAA//8AAAAA/////wAAAAD//wAAAAD/////AAAAAP////8AAP//AAEkAAAA"
      "AgADAOQEBQBBcmlhbAQA5AQPAFRpbWVzIE5ldyBSb21hbgAIeAAAAwAAAlgCWAAAAAAazBLF/7L/"
      "shsaExMDZwV7A+AAAgAAAlgCWAAAAAAazBLFAAEAAABkAAAAAQABAQEAAgABJw8AAQABAAAAAAAA"
      "AAAAAAAAAAIAGQGQAAAAAABgAAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAAAC0CwIAAAC1CxQAAABD"
      "aGVtaWNhbCBGb3JtdWxhOiC2Cw4AAABFeGFjdCBNYXNzOiC3CxQAAABNb2xlY3VsYXIgV2VpZ2h0"
      "OiC4CwcAAABtL3o6ILkLFgAAAEVsZW1lbnRhbCBBbmFseXNpczogugsRAAAAQm9pbGluZyBQb2lu"
      "dDoguwsRAAAATWVsdGluZyBQb2ludDogvAsRAAAAQ3JpdGljYWwgVGVtcDogvQsRAAAAQ3JpdGlj"
      "YWwgUHJlczogvgsQAAAAQ3JpdGljYWwgVm9sOiC/CxAAAABHaWJicyBFbmVyZ3k6IMALCQAAAExv"
      "ZyBQOiDBCwYAAABNUjogwgsPAAAASGVucnkncyBMYXc6IMMLEAAAAEhlYXQgb2YgRm9ybTogxAsI"
      "AAAAdFBTQTogxQsJAAAAQ0xvZ1A6IMYLBwAAAENNUjogxwsIAAAATG9nUzogyAsHAAAAcEthOiDJ"
      "CwIAAADKCwIAAAALDAIAAQAKDAEAAAkMAQAADAwFAAAAKCMpAYBCAAAABAIQAAAAAAAAAAAAhesB"
      "A+tRCwIWCAQAAAAkABgIBAAAACQAGQgAABAIAgABAA8IAgABAAOAPAAAAAQCEAAy7EIA9YjuAACA"
      "VgBC3i0BCgACADsABIA7AAAAAAIIAACAUgD1yO4ACgACADoANwQBAAEAAASAPQAAAAACCAAAgEMA"
      "CMQIAQoAAgA8ADcEAQABAAAEgD8AAAAAAggAAIBSABy/IgEKAAIAPgACBAIACAArBAIAAQBIBAAA"
      "NwQBAAEGgAAAAAAAAggAZmZWALXYHgEEAhAAEfFNALXYHgEAgFYAQt4tASMIAQAAAgcCAAAABQcB"
      "AAEABw4AAQAAAAMAYADIAAAAT0gAAAAABYA+AAAACgACAD0ABAYEADsAAAAFBgQAPQAAAAoGAQAB"
      "AAAFgEAAAAAKAAIAPwAEBgQAPQAAAAUGBAA/AAAACgYBAAEAAAAAAAAAAAAA";

  QByteArray data = QByteArray::fromBase64(b64);
  if(qEnvironmentVariableIsEmpty("BABEL_LIBDIR")) {
    const char *dirs[] = {
      BABEL_LIBDIR,
      "/usr/lib/openbabel/3.1.1",
      "/usr/lib/x86_64-linux-gnu/openbabel/3.1.1",
      "/usr/lib/x86_64-linux-gnu/openbabel/3",
      nullptr};
    for (int i = 0; dirs[i]; ++i) {
      if (QDir(dirs[i]).exists()) {
        qputenv("BABEL_LIBDIR", dirs[i]);
        break;
      }
    }
  }
  OBPlugin::LoadAllPlugins();
  OBConversion conv;
  OBFormat* fmt = conv.FindFormat("cdx");
  QVERIFY(fmt != nullptr);
  conv.SetInFormat(fmt);
  QTemporaryFile tmp;
  QVERIFY(tmp.open());
  tmp.write(data);
  tmp.flush();
  OBMol mol;
  QVERIFY(conv.ReadFile(&mol, tmp.fileName().toLatin1().constData()));
  QVERIFY(mol.NumAtoms() >= 0);
}

QTEST_MAIN(CdxFormatTest)
#include "moc_cdxformattest.cpp"
