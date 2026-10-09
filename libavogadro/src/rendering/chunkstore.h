/**********************************************************************
  ChunkStore - tier-agnostic chunked store of per-instance records.

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Avogadro is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 *****************************************************************/

#ifndef AVOGADRO_RENDERING_CHUNKSTORE_H
#define AVOGADRO_RENDERING_CHUNKSTORE_H

#include <QList>
#include <QVector>
#include <QString>

namespace Avogadro {
namespace rendering {

//! A contiguous [start, start + count) span of instance indices within a
//! ChunkStore. Ranges are coalesced by ChunkStore::dirtyRanges().
struct DirtyRange {
  unsigned int start = 0;
  unsigned int count = 0;

  bool isEmpty() const { return count == 0; }
  unsigned int end() const { return start + count; }
};

//! A chunked store of per-instance records (atoms, bond-halves, lines, meshes).
//!
//! The store keeps records in a contiguous vector and tracks which indices have
//! been modified since the last ChunkStore::acknowledge() call. The submit
//! stage (modern/compat backends) re-uploads only the dirty ranges instead of
//! the whole instance buffer every frame, so submitting an unchanged scene does
//! not issue per-primitive work proportional to the scene size.
//!
//! T is the per-instance record type (e.g. a sphere instance or a bond half).
//! The store is a plain CPU container; it owns no GPU resources so it is safe
//! to build on worker threads and hand the result to the GUI thread.
template <typename T>
class ChunkStore {
public:
  ChunkStore() = default;

  //! Append a record and mark it dirty.
  bool append(const T &item) {
    m_data.push_back(item);
    if (static_cast<unsigned int>(m_dirty.size()) < m_data.size())
      m_dirty.resize(m_data.size(), 0);
    m_dirty[m_data.size() - 1] = 1;
    return true;
  }

  //! Pre-allocate capacity without touching dirty tracking.
  void reserve(unsigned int n) {
    m_data.reserve(n);
  }

  unsigned int size() const {
    return static_cast<unsigned int>(m_data.size());
  }

  bool isEmpty() const {
    return m_data.isEmpty();
  }

  void clear() {
    m_data.clear();
    m_dirty.clear();
  }

  const T &at(unsigned int i) const {
    return m_data.at(i);
  }

  const T &operator[](unsigned int i) const {
    return m_data[i];
  }

  T &operator[](unsigned int i) {
    return m_data[i];
  }

  T &back() {
    return m_data.back();
  }

  const T &back() const {
    return m_data.back();
  }

  //! Mark an in-place modification of a single record dirty.
  void modify(unsigned int i) {
    markDirty(i, 1);
  }

  //! Mark a contiguous range of records dirty. A range past the end is clamped.
  void markDirty(unsigned int start, unsigned int count) {
    if (count == 0 || start >= m_data.size())
      return;
    unsigned int end = qMin(start + count,
                            static_cast<unsigned int>(m_data.size()));
    if (static_cast<unsigned int>(m_dirty.size()) < m_data.size())
      m_dirty.resize(m_data.size(), 0);
    for (unsigned int i = start; i < end; ++i)
      m_dirty[i] = 1;
  }

  //! True if at least one record is marked dirty since the last acknowledge().
  bool hasDirty() const {
    for (char flag : m_dirty)
      if (flag)
        return true;
    return false;
  }

  //! Coalesced contiguous dirty ranges since the last acknowledge().
  QList<DirtyRange> dirtyRanges() const {
    QList<DirtyRange> ranges;
    unsigned int runStart = 0;
    unsigned int runLen = 0;
    bool inRun = false;
    for (unsigned int i = 0; i < m_data.size(); ++i) {
      const bool dirty = i < m_dirty.size() && m_dirty[i];
      if (dirty) {
        if (!inRun) {
          runStart = i;
          inRun = true;
          runLen = 0;
        }
        ++runLen;
      } else if (inRun) {
        ranges.append(DirtyRange{runStart, runLen});
        inRun = false;
      }
    }
    if (inRun)
      ranges.append(DirtyRange{runStart, runLen});
    return ranges;
  }

  //! Clear all dirty flags after the submit stage has uploaded the ranges.
  void acknowledge() {
    m_dirty.fill(0);
  }

  //! Replace the whole record vector and mark the replacement dirty.
  void setAll(QVector<T> items) {
    m_data = std::move(items);
    m_dirty = QVector<char>(m_data.size(), 1);
  }

private:
  QVector<T> m_data;
  QVector<char> m_dirty; // parallel to m_data; 1 == dirty since acknowledge()
};

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_CHUNKSTORE_H
