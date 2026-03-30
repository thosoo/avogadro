#ifndef AVOGADRO_CLIPBOARDFORMATDETECTOR_H
#define AVOGADRO_CLIPBOARDFORMATDETECTOR_H

#include <QByteArray>
#include <QList>

class QMimeData;

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

} // namespace Avogadro

#endif // AVOGADRO_CLIPBOARDFORMATDETECTOR_H
