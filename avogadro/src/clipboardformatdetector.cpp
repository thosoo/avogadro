#include "clipboardformatdetector.h"

#include <algorithm>
#include <QMimeData>
#include <QString>
#include <QSet>

namespace Avogadro {

namespace {
const char kChemDrawCDXSignature[] = "VjCD0100";
const int kChemDrawCDXSignatureLength = 8;
const int kMinimumCDXPayloadLength = 24;

bool isWindowsCustomMime(const QString &format)
{
  return format.startsWith(QLatin1String("application/x-qt-windows-mime;value=\""));
}

bool formatNameSuggestsChemDraw(const QString &format)
{
  return format.contains(QLatin1String("chemdraw"), Qt::CaseInsensitive)
      || format.contains(QLatin1String("cdx"), Qt::CaseInsensitive)
      || format.contains(QLatin1String("cdxml"), Qt::CaseInsensitive);
}

int candidatePriority(const ChemDrawCandidate &candidate)
{
  if (candidate.source == "explicit-cdx-header"
      || candidate.source == "explicit-cdx-embedded")
    return 0;
  if (candidate.source == "explicit-cdxml")
    return 1;
  if (candidate.source == "windows-interchange-cdx")
    return 2;
  if (candidate.source == "direct-cdxml-text")
    return 3;
  if (candidate.source == "native-embedded" || candidate.source == "native-cdxml"
      || candidate.source == "windows-wrapper-embedded")
    return 5;
  return 6;
}

bool candidateHasHigherPriority(const ChemDrawCandidate &a,
                                const ChemDrawCandidate &b)
{
  return candidatePriority(a) < candidatePriority(b);
}
}

bool isChemDrawCDX(const QByteArray &payload)
{
  return payload.startsWith(kChemDrawCDXSignature);
}

int findEmbeddedCDX(const QByteArray &payload)
{
  int offset = payload.indexOf(kChemDrawCDXSignature);
  while (offset >= 0) {
    const int bytesAfterSignature = payload.size() - offset - kChemDrawCDXSignatureLength;
    if (bytesAfterSignature >= kMinimumCDXPayloadLength)
      return offset;

    offset = payload.indexOf(kChemDrawCDXSignature, offset + 1);
  }
  return -1;
}

bool hasPlausibleChemDrawCDX(const QByteArray &payload)
{
  return isChemDrawCDX(payload)
      && (payload.size() - kChemDrawCDXSignatureLength) >= kMinimumCDXPayloadLength;
}

bool looksLikeCDXML(const QByteArray &payload)
{
  const QString text = QString::fromUtf8(payload).trimmed();
  return text.contains(QLatin1String("<CDXML"), Qt::CaseInsensitive)
      || text.contains(QLatin1String("-//CambridgeSoft//DTD ChemDraw"), Qt::CaseInsensitive)
      || (text.contains(QLatin1String("http://www.cambridgesoft.com/xml/cdxml"), Qt::CaseInsensitive)
          && text.contains(QLatin1String("<"), Qt::CaseInsensitive));
}

bool looksLikeMolfileText(const QByteArray &payload)
{
  const QString text = QString::fromLatin1(payload).trimmed();
  return text.contains(QLatin1String("V2000"))
      || text.contains(QLatin1String("V3000"))
      || text.contains(QLatin1String("M  END"));
}

QList<ChemDrawCandidate> detectChemDrawClipboardCandidates(const QMimeData *mimeData)
{
  QList<ChemDrawCandidate> candidates;
  if (!mimeData)
    return candidates;

  if (mimeData->hasFormat("chemical/x-cdx")) {
    const QByteArray cdxData = mimeData->data("chemical/x-cdx");
    if (hasPlausibleChemDrawCDX(cdxData)) {
      ChemDrawCandidate candidate;
      candidate.payload = cdxData;
      candidate.formatId = "cdx";
      candidate.source = "explicit-cdx-header";
      candidate.strength = DetectionStrong;
      candidates.append(candidate);
    } else {
      const int offset = findEmbeddedCDX(cdxData);
      if (offset >= 0) {
        ChemDrawCandidate candidate;
        candidate.payload = cdxData.mid(offset);
        candidate.formatId = "cdx";
        candidate.source = "explicit-cdx-embedded";
        candidate.strength = DetectionStrong;
        candidates.append(candidate);
      }
    }
  }

  if (mimeData->hasFormat("chemical/x-cdxml")) {
    const QByteArray payload = mimeData->data("chemical/x-cdxml");
    if (looksLikeCDXML(payload)) {
      ChemDrawCandidate candidate;
      candidate.payload = payload;
      candidate.formatId = "cdxml";
      candidate.source = "explicit-cdxml";
      candidate.strength = DetectionStrong;
      candidates.append(candidate);
    }
  }

  const QString nativeMime =
    QLatin1String("application/x-qt-windows-mime;value=\"Native\"");
  const QString windowsInterchangeMime =
    QLatin1String("application/x-qt-windows-mime;value=\"ChemDraw Interchange Format\"");
  if (mimeData->hasFormat(windowsInterchangeMime)) {
    const QByteArray interchangeData = mimeData->data(windowsInterchangeMime);
    if (hasPlausibleChemDrawCDX(interchangeData)) {
      ChemDrawCandidate candidate;
      candidate.payload = interchangeData;
      candidate.formatId = "cdx";
      candidate.source = "windows-interchange-cdx";
      candidate.strength = DetectionStrong;
      candidates.append(candidate);
    } else {
      const int offset = findEmbeddedCDX(interchangeData);
      if (offset >= 0) {
        ChemDrawCandidate candidate;
        candidate.payload = interchangeData.mid(offset);
        candidate.formatId = "cdx";
        candidate.source = "windows-interchange-cdx";
        candidate.strength = DetectionStrong;
        candidates.append(candidate);
      }
    }
  }

  if (mimeData->hasFormat(nativeMime)) {
    const QByteArray nativeData = mimeData->data(nativeMime);
    const int offset = findEmbeddedCDX(nativeData);
    if (offset >= 0) {
      ChemDrawCandidate candidate;
      candidate.payload = nativeData.mid(offset);
      candidate.formatId = "cdx";
      candidate.source = "native-embedded";
      candidate.strength = DetectionStrong;
      candidates.append(candidate);
    } else if (looksLikeCDXML(nativeData)) {
      ChemDrawCandidate candidate;
      candidate.payload = nativeData;
      candidate.formatId = "cdxml";
      candidate.source = "native-cdxml";
      candidate.strength = DetectionStrong;
      candidates.append(candidate);
    }
  }

  // Native clipboard producers on Windows often expose app-specific MIME keys.
  // Keep these as weak hints: only treat as ChemDraw if parse actually succeeds.
  const QStringList formats = mimeData->formats();
  QSet<QByteArray> seenPayloads;
  foreach (const ChemDrawCandidate &candidate, candidates) {
    seenPayloads.insert(candidate.formatId + '\n' + candidate.payload);
  }

  foreach (const QString &format, formats) {
    const QByteArray blob = mimeData->data(format);
    if (blob.isEmpty())
      continue;

    const bool isWindowsMime = isWindowsCustomMime(format);
    const bool formatHint = isWindowsMime && formatNameSuggestsChemDraw(format);
    Q_UNUSED(formatHint);
    const DetectionStrength strength = DetectionWeak;

    const int offset = findEmbeddedCDX(blob);
    if (offset >= 0) {
      ChemDrawCandidate candidate;
      candidate.payload = blob.mid(offset);
      candidate.formatId = "cdx";
      candidate.source =
        (isWindowsMime && format.contains(QLatin1String("Embed Source"), Qt::CaseInsensitive))
          ? "windows-wrapper-embedded"
          : "weak-generic";
      candidate.strength = strength;
      const QByteArray key = candidate.formatId + '\n' + candidate.payload;
      if (!seenPayloads.contains(key)) {
        candidates.append(candidate);
        seenPayloads.insert(key);
      }
      continue;
    }

    if (looksLikeCDXML(blob)) {
      ChemDrawCandidate candidate;
      candidate.payload = blob;
      candidate.formatId = "cdxml";
      candidate.source = QString::fromUtf8(blob).trimmed().startsWith(QLatin1String("<"))
        ? "direct-cdxml-text"
        : "weak-generic";
      candidate.strength = strength;
      const QByteArray key = candidate.formatId + '\n' + candidate.payload;
      if (!seenPayloads.contains(key)) {
        candidates.append(candidate);
        seenPayloads.insert(key);
      }
    }
  }

  std::stable_sort(candidates.begin(), candidates.end(), candidateHasHigherPriority);
  return candidates;
}

ChemDrawHandlingDecision classifyChemDrawHandling(const ChemDrawCandidate &candidate,
                                                  bool readerAvailable,
                                                  bool parseSucceeded)
{
  if (!candidate.isValid())
    return NotHandled;

  if (candidate.strength == DetectionStrong) {
    if (!readerAvailable || !parseSucceeded)
      return HardFailure;
    return Handled;
  }

  if (candidate.strength == DetectionWeak && readerAvailable && parseSucceeded)
    return Handled;

  return NotHandled;
}

} // namespace Avogadro
