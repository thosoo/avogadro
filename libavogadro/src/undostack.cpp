/**********************************************************************
  undostack.cpp
  Implementation of Avogadro::UndoStack (fix-autoopt-push-lock-race).

  Copyright (C) 2007-2009 by the Avogadro developers.

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation version 2 of the License.
 **********************************************************************/

#include "undostack.h"

#include <avogadro/molecule.h>

#include <QWriteLocker>

namespace Avogadro {

//! Re-entrancy guard for push().
//!
//! Detects a re-entrant push(): if a command's redo()/undo() were to call
//! push() again on the same thread, we would already be holding the molecule
//! write lock, and the non-recursive QReadWriteLock would hang forever. The
//! guard converts that into a loud debug failure instead of an indefinite
//! deadlock.
//!
//! It is a thread-local flag set for the duration of one push() (RAII, so it
//! always clears even on an early exit). It is intentionally NOT a
//! tryLockForWrite probe: the AutoOpt worker legitimately holds the molecule
//! read lock while pushing nothing, and a probe would false-fire on it and
//! abort debug builds during normal runs.
namespace {
inline bool &pushHoldingLock()
{
  static thread_local bool holding = false;
  return holding;
}

class PushReentrancyGuard
{
public:
  PushReentrancyGuard() { pushHoldingLock() = true; }
  ~PushReentrancyGuard() { pushHoldingLock() = false; }
  PushReentrancyGuard(const PushReentrancyGuard &) = delete;
  PushReentrancyGuard &operator=(const PushReentrancyGuard &) = delete;
};
} // namespace

UndoStack::UndoStack(Molecule *molecule, QObject *parent)
  : QUndoStack(parent), m_molecule(molecule)
{
}

UndoStack::~UndoStack()
{
}

void UndoStack::setMolecule(Molecule *molecule)
{
  m_molecule = molecule;
}

void UndoStack::push(QUndoCommand *command)
{
  if (!command)
    return;

#ifdef QT_DEBUG
  // Invariant: the calling thread must not already hold the molecule lock.
  // pushHoldingLock() is true only while an enclosing push() holds the write
  // lock, so this fires exactly on a re-entrant push (self-deadlock).
  if (pushHoldingLock()) {
    qCritical("Avogadro::UndoStack::push: re-entrant push while holding the"
              " molecule write lock (self-deadlock)");
  }
#endif

  PushReentrancyGuard guard;

  if (m_molecule) {
    // Serialize command application against worker reads: hold the molecule
    // write lock for the whole push -> redo()/undo() window. Blocking (not
    // try) lock: a worker tick holding the read lock is brief and quiesced on
    // shutdown, so this cannot deadlock (see fix-autoopt-push-lock-race D4).
    QWriteLocker locker(m_molecule->lock());
    Q_UNUSED(locker);
    QUndoStack::push(command);
  } else {
    // No molecule guarded yet (stack exists before the first molecule): fall
    // back to the unlocked base push rather than dereferencing a null lock.
    QUndoStack::push(command);
  }
}

} // namespace Avogadro
