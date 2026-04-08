#ifndef AVOGADRO_CLIPBOARDFORMATDETECTOR_H
#define AVOGADRO_CLIPBOARDFORMATDETECTOR_H

#include <QByteArray>
#include <QList>

class QMimeData;
namespace OpenBabel {
class OBMol;
}

namespace Avogadro {

enum DetectionStrength
{
  DetectionNone = 0,
  DetectionWeak,
  DetectionStrong
};

enum ChemDrawHandlingDecision
{
  NotHandled = 0,
  Handled,
  HardFailure
};

struct ChemDrawCandidate
{
  QByteArray formatId;
  QByteArray payload;
  QByteArray source;
  DetectionStrength strength;

  ChemDrawCandidate() : strength(DetectionNone) {}
  bool isValid() const { return !formatId.isEmpty() && !payload.isEmpty(); }
};

bool isChemDrawCDX(const QByteArray &payload);
int findEmbeddedCDX(const QByteArray &payload);
bool hasPlausibleChemDrawCDX(const QByteArray &payload);
bool looksLikeCDXML(const QByteArray &payload);
bool looksLikeMolfileText(const QByteArray &payload);
QList<ChemDrawCandidate> detectChemDrawClipboardCandidates(const QMimeData *mimeData);
ChemDrawHandlingDecision classifyChemDrawHandling(const ChemDrawCandidate &candidate,
                                                  bool readerAvailable,
                                                  bool parseSucceeded);
// Internal clipboard parse helper used by MainWindow paste paths and unit tests.
bool tryReadClipboardPayloadAsFormat(OpenBabel::OBMol &mol,
                                     const QByteArray &payload,
                                     const QByteArray &formatId,
                                     bool *readerAvailable = 0);

} // namespace Avogadro

#endif // AVOGADRO_CLIPBOARDFORMATDETECTOR_H
