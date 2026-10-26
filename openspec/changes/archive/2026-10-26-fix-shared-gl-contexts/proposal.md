# Why

The residual Windows first-run crash (after `fix-windows-first-run-crash` landed
its verified installer + GL-hygiene scope) is root-caused to undefined behavior in
GL resource usage, not to the NVIDIA driver: GL object names (display lists,
textures, mesh VBOs) owned by one view's `GLPainter` are used from a second view's
GL context that shares nothing with it.

Chain (all verified in-repo, oracle-validated):

1. `avogadro/src/main.cpp` never sets `Qt::AA_ShareOpenGLContexts`, so every
   `QOpenGLWidget` gets an independent, unshared GL context (share groups differ).
2. `MainWindow::newGLWidget` (mainwindow.cpp:3905) creates the second view as
   `GLWidget(format, this, d->glWidget)`, and `GLWidget::constructor`
   (glwidget.cpp:528-535) shares the first widget's `GLPainter` with it.
3. `GLPainter`'s objects hold raw GL names — `Sphere`/`Cylinder` display lists
   (sphere_p.cpp:92-117, cylinder_p.cpp:92-226), `TextRenderer`'s quad display
   list and font textures (textrenderer_p.cpp:103-340), the mesh VBO cache
   (glpainter_p.cpp). The second view calls the first view's names under its own
   context where they are undefined — undefined behavior that corrupts driver
   state and faults in `nvoglv64!DrvPresentBuffers` on a driver worker thread.
4. Same-class hazard: `~GLWidgetPrivate` (glwidget.cpp:235-241) deletes display
   lists with no context current.

This explains the observed tier-independence (GLPainter is both the compat tier's
painter and the modern tier's text/overlay/bridge painter), the crash adjacency to
our own `createObjects()` qDebug, and why the contextless-call guard and both
driver-side experiments (Threaded Optimization off, compat tier) had no effect.

# What Changes

- **All GLWidgets render with contexts in a single share group.** `main.cpp` sets
  `Qt::AA_ShareOpenGLContexts` before `Application` (a `QApplication`) is
  constructed, making the existing painter-sharing design legal: display lists,
  textures, and buffer objects are genuinely shared between views.
- **GL objects are deleted only with a context current.** `GLWidget::~GLWidget`
  wraps the GL-object-owning teardown (shared painter release, engines,
  `GLWidgetPrivate` display lists) in `makeCurrent()` / `doneCurrent()`.

# Capabilities

### Modified Capabilities

- `gpu-rendering-pipeline`: new requirement — GL resources shared between views
  SHALL only be used from contexts within one share group, and GL object
  deletion SHALL occur only with an appropriate context current.

# Impact

- `avogadro/src/main.cpp` — one attribute set before `Application app(argc, argv);`.
- `libavogadro/src/glwidget.cpp` — destructor teardown wrapped in
  `makeCurrent()`/`doneCurrent()`.
- No rendering-tier changes; no painter/engine changes; the Linux legs and the
  headless test guards are unaffected (makeCurrent failure paths already exist).

# Verification

- Linux: full build + ctest green (compile under GCC/Clang/ASan legs).
- Windows: installer workflow green; artifact installed on the reporting machine
  (Quadro P3200 / 573.22) and run **outside** the debugger — Event Viewer shows no
  `nvoglv64` access violation, window presents and survives. This is the
  discriminating test between the UB root cause and the (rejected) pure-driver-
  regression alternative.
