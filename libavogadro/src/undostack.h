/**********************************************************************
  undostack.h
  Avogadro::UndoStack — a QUndoStack whose push() serializes command
  application against the molecule write lock (fix-autoopt-push-lock-race).

  Copyright (C) 2007-2009 by the Avogadro developers.

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation version 2 of the License.
 **********************************************************************/

#ifndef AVOGADRO_UNDOSTACK_H
#define AVOGADRO_UNDOSTACK_H

#include <QUndoStack>

#include <avogadro/global.h>

namespace Avogadro {

class Molecule;

//! Molecule-aware undo stack.
//!
//! Every push() wraps the base QUndoStack::push() in the guarded molecule's
//! write lock, so command redo()/undo() bodies (add/remove atoms, hydrogen
//! adjustment storms) can never mutate the live molecule while the AutoOpt
//! worker reads it under the read lock. All 14 push sites in the application
//! route through the single shared UndoStack instance (distributed to GLWidget
//! and extensions via undoStack()), so one override covers every caller.
//!
//! The guarded molecule is set at construction and updated whenever the active
//! document's molecule changes (MainWindow::setMolecule); a null molecule
//! falls back to an unlocked push so the stack is usable before the first
//! molecule exists.
class A_EXPORT UndoStack : public QUndoStack
{
  Q_OBJECT

public:
  explicit UndoStack(Molecule *molecule, QObject *parent = nullptr);
  ~UndoStack() override;

  //! Update the molecule whose write lock guards command application.
  void setMolecule(Molecule *molecule);

  //! Molecule currently guarded by this stack (may be null).
  Molecule *molecule() const { return m_molecule; }

  // NB: QUndoStack::push() is non-virtual in Qt, so this is a name-hiding
  // declaration (not an override). Every call site holding an UndoStack*
  // resolves push() to this locked variant; the base call is made explicitly
  // via QUndoStack::push() in undostack.cpp.
  void push(QUndoCommand *command);

private:
  Molecule *m_molecule;
};

} // namespace Avogadro

#endif // AVOGADRO_UNDOSTACK_H
