# Tasks

## 1. Draw tool state lifetime fix

- [x] 1.1 `drawtool.h`/`drawtool.cpp`: convert `m_beginAtom`, `m_endAtom`,
      `m_prevBond`, `m_bond` from raw pointers to ids; resolve via
      `atomById`/`bondById` at each use with null checks. Verify: compiles
      under GCC/Clang (local build green); MSVC confirmed by the Windows CI
      legs in task 1.4.
- [x] 1.2 Null-guard the crash chains and audit every unresolved deref in
      drawtool.cpp; missing objects reset/abandon the affected state. Also:
      cross-event state is abandoned (not dereferenced) when a held id no
      longer resolves or the molecule was replaced between events; transient
      fix/ignore markers are unset on abandon; `m_beginMolecule` is a
      `QPointer`; `molecule->atom(index)->isHydrogen()` in `mouseMoveEvent`
      (lines ~346/354) is null-checked, since `atom()` returns null for an
      out-of-range or churn-stale index and that path was not part of the
      id-conversion. Verify: cdb on the reporting machine (Quadro P3200) at
      commit 59ab882 faulted again at `cmp [rax+38h],1` inside
      `GLWidget::mouseMoveEvent` -> drawtool `mouseMoveEvent`; the stack
      pinpointed the `existingAtom->isHydrogen()` chain, which this guard
      fixes. Commit bd5705269. A new
      `staleOrReplacedMoleculeAbandonsStroke` test exercises the abandon path.
- [x] 1.3 Local verification: `cmake --build build -j8 && ctest --test-dir
      build --output-on-failure` green (default platform; offscreen artifacts
      documented in the archived A/B). Verify: local run — 34/34 tests pass,
      including the new `autooptreprotest::staleOrReplacedMoleculeAbandonsStroke`.
- [x] 1.4 Windows legs: both workflows green on the fix commit. Verify:
      `gh run list` on the pushed commit — HEAD 6ea9a1794 (drawtool completeness
      fix, includes textrenderer startup-crash fix 36a176191): Qt6 CI
      #40605604103 `completed success` (all Test legs incl. ASan-sensitive
      matrix); Windows Installer #40605604098 `watch --exit-status` = 0 with
      Build + Create installer + OpenBabel runtime verification green (Test leg
      skipped by the known artifact-upload infra hang). Note: commits were
      never pushed initially (origin/master was bd5705269); pushed 460ec10ca,
      36a176191, 6ea9a1794 to master, which triggered these runs.
- [x] 1.5 Oracle exhaustive fix-completeness audit: the id-conversion +
      prepareStrokeState + press/move/release guards are correct, but two
      same-class unchecked derefs remained in the right-click DELETE path of
      `mouseReleaseEvent`. Fixed in 6ea9a1794:
      - `drawtool.cpp` ~690: `molecule->atom(m_hits[0].name())` then
        `atom->isHydrogen()` — `atom()` returns null for a stale pick index
        after churn. Guarded with `atom &&` (same class as 346/354).
      - `drawtool.cpp` ~702: `molecule->bond(m_hits[0].name())` then
        `delBond->beginAtomId()/endAtomId()` — `bond()` returns null for a
        stale index. Guarded the whole block on `delBond` (abandon the delete
        when the bond no longer resolves).
      Verify: local build green; ctest 34/34. CI legs: Qt6 CI #40605604103
      `completed success` (all Test legs incl. ASan-sensitive matrix); Windows
      Installer #40605604098 `watch --exit-status`=0 (Build + Create installer
      + OpenBabel runtime verification green; Test leg skipped by the known
      artifact-upload hang). Both legs green on 6ea9a1794.
- [ ] 1.6 User-machine repro: draw/undo/redraw loop with "adjust hydrogens" on
      — no crash across at least 20 strokes (the previous faulting sequence).
      Verify: user-reported outcome. The first repro (commit 59ab882) crashed
      at a third path (`existingAtom->isHydrogen()` in `mouseMoveEvent`), now
      fixed in bd5705269 and completeness-audited in 6ea9a1794. User must
      reinstall the latest Windows Installer and re-run the repro to confirm no
      crash.

## 3. Startup crash (separate, not a drawtool regression)

- [x] 3.1 Cold-start crash: avogadro GUI appears for a fraction of a second,
      then crashes on a cold start (real crash, not a cdb artifact). Root cause
      located by oracle in `TextRendererPrivate::isGLExtensionSupported`
      (`libavogadro/src/textrenderer_p.cpp:431-437`): `glGetString(GL_EXTENSIONS)`
      returns NULL when no GL context is current on the thread (first
      `TextRenderer::begin` at startup), passed straight to `strstr` with no
      null check -> `strstr(NULL, ...)` -> the exact AV (`VCRUNTIME140!strstr+0x220`,
      null r9 haystack). Confirmed independent of the drawtool crash (different
      module, instruction, trigger; disjoint git deltas). Fix: `if (!extensions) return 0;`
      (falls into the existing GL_TEXTURE_2D 'absent' branch). Audit: `renderertier.cpp`
      `probeContextVersion` already null-checks GL_VERSION; `glwidget.cpp`
      GL_RENDERER/VENDOR/VERSION qDebug logs only when the context is current.
      This is the only unguarded glGetString dereference. Verify: local build
      green; ctest 34/34; CI legs: Qt6 CI #40605604103 + Windows Installer
      #40605604098 both green on 36a176191 (now on master with the drawtool
      fixes).
- [ ] 3.2 User-machine repro: reinstall the latest Windows Installer and
      cold-launch avogadro (no cdb) — GUI should stay up (no fraction-of-a-second
      crash). Also confirm autoopt now starts (it needs a live GL context).

## 2. Bookkeeping

- [x] 2.1 Note the crash attribution in `fix-modern-tier-revision-churn`
      (drawing instability has a second, independent component: this tool-state
      bug) so the churn change keeps its display-stability scope.
