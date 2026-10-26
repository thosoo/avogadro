# Design

## Context

`fix-windows-first-run-crash` verified the installer scope and the GL-init guard
on the reporting machine, but the crash persists — tier-independently (modern
AND compat), with Threaded Optimization off, and only at the second view's first
real paint (adjacent to `GLPainterPrivate::isValid`'s `createObjects()` qDebug;
fault site `nvoglv64!DrvPresentBuffers`, driver worker thread). Driver rollback
is rejected by the user. Code investigation + oracle validation (2026-10-26)
root-caused the crash to undefined GL-name use across unshared contexts:

- `main.cpp` never sets `Qt::AA_ShareOpenGLContexts` → every `QOpenGLWidget`
  context is independent (own share group).
- `MainWindow::newGLWidget` (mainwindow.cpp:3905) + `GLWidget::constructor`
  (glwidget.cpp:528-535) share one `GLPainter` between widget 1 and widget 2.
- `Sphere`/`Cylinder`/`TextRenderer`/mesh-VBO-cache objects hold raw GL names
  created under whichever context drew first; the other widget uses them under a
  context where they are undefined.
- `~GLWidgetPrivate` (glwidget.cpp:235-241) issues `glDeleteLists` with no
  context current.

Oracle verdict (2026-10-26): hypothesis confirmed; discriminating test = one
fix-build cycle on the reporting machine run outside the debugger with Event
Viewer clean.

## Goals / Non-Goals

**Goals:**
- Views sharing GL objects actually share a GL share group.
- GL object deletion is context-safe.
- One push, one CI cycle; artifact for the user-machine discriminating test.

**Non-Goals:**
- Per-context painter ownership / context-tagged regeneration (larger surgery;
  unnecessary once the share group is real).
- Changes to the rendering tiers, painter internals, or engines.
- Re-opening the verified installer scope of `fix-windows-first-run-crash`.

## Decisions

1. **`Qt::AA_ShareOpenGLContexts` before `Application` construction**
   (`avogadro/src/main.cpp`, immediately before `Application app(argc, argv);`).
   Qt then places every `QOpenGLWidget` context in one share group, which is the
   design the painter-sharing code already assumes. Display lists, textures, and
   VBOs are share-group objects, so cross-view painter use becomes defined
   behavior. VAOs are not share-group objects, but they are owned per widget
   (each `ModernRendererBackend` builds its own), so nothing regresses.
   Attribute must be set before the `QApplication` is constructed — hence its
   position, and nowhere else.

2. **Context-safe teardown in `GLWidget::~GLWidget`** (`glwidget.cpp`): wrap the
   GL-object-owning destruction (shared-painter release/last-owner delete,
   engines, `delete d` which runs the `GLWidgetPrivate` display-list deletion) in
   `makeCurrent()` / `doneCurrent()`, guarded on `context() && context()->isValid()`
   so headless/never-shown widgets skip the GL calls cleanly (same guard shape as
   the event-path checks landed in `fix-windows-first-run-crash`). `makeCurrent`/
   `doneCurrent` are public on `QOpenGLWidget` in Qt 6, and the derived destructor
   body runs before the `QOpenGLWidget` base is torn down, so the context is still
   available.

3. **Not in this change:** regenerating objects on context re-creation. With the
   attribute set, Qt's global share context keeps the group stable across
   `QOpenGLWidget` hide/show context churn, so cached names stay valid. If a
   future non-Qt context source appears, context-tagging becomes that change.

## Risks / Trade-offs

- [Sharing changes GL resource lifetime semantics] → with one share group,
  objects live until explicitly deleted (same as today's painter ownership);
  the last-owner delete now runs with a context current, so deletions actually
  happen instead of silently dropping.
- [Headless CI behavior change] → the global share context is created at
  startup; on GDI-generic/offscreen it is inert. The existing tier-resolution
  qFatal and headless guards are untouched.
- [MSVC parity] → both edits are plain calls; no aggregate init, no const
  mutex, no new threading primitives (AGENTS.md MSVC rules respected).

## Migration Plan

Single commit; no data migration. Push, let CI build both legs, install the
artifact on the reporting machine, run outside the debugger twice (fresh +
reused settings), check Event Viewer. Rollback = revert the commit.

## Open Questions

- None pending for implementation. The user-machine run decides UB-vs-driver
  attribution: AV gone → confirmed; AV persists with clean GL usage → the
  driver-regression alternative takes over with clean attribution (and a driver
  bug report).
