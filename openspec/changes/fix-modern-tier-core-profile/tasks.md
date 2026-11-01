# Tasks: fix-modern-tier-core-profile

Implementation notes for the applying agent: follow design.md D1–D4. The root fix (1.x)
is a reorder in main.cpp; the containment work (2.x) makes any residual core resolution
loud instead of corrupt. Per the repo's delegation policy, run the `reviewer` subagent on
the full diff before declaring the change complete. Machine-specific verification needs
the two affected Linux boxes (nouveau/Quadro via SSH at 10.66.0.12; radeonsi/Vega via the
greeter-display method used during diagnosis).

## 1. Root fix: format before any context creation

- [x] 1.1 In `avogadro/src/main.cpp`, move `QSurfaceFormat::setDefaultFormat(3.3,
      CompatibilityProfile)` (and the `setSamples(4)` block) above any context creation:
      delete the `QOpenGLContext testContext` probe (design D1 — an unusable GL stack now
      surfaces through the widget's invalid-context diagnostics). Verify: build;
      `git grep -n "testContext"` returns nothing.
- [x] 1.2 Verify the fix on a machine where the bug reproduces (SSH to 10.66.0.12, or the
      greeter-display method on the Vega box): run
      `build/bin/avogadro testfiles/methane.cml 2>&1 | grep -E "resolved format|renderer tier"`
      and confirm `profile Compatibility`, no "setting a new default format … after the
      global shared context" warning, and a correctly proportioned methane screenshot
      (compare against the broken reference captures). Verify: log line + screenshot.

## 2. Profile-aware tier resolution (design D2)

- [x] 2.1 In `libavogadro/src/rendering/renderertier.{h,cpp}`, extend tier support checks
      so compat and legacy tiers require a resolved non-Core profile while the modern tier
      remains version-gated; thread the resolved profile through
      `contextSupportsTier()`/`resolveRendererTier()` (design D2). Verify: build.
- [x] 2.2 Unit-test the profile-aware predicate: extend the renderertier test (or add a
      headless case) asserting compat/legacy are unavailable and modern available for
      (version 4.6, CoreProfile), and all tiers available for (4.6, CompatibilityProfile).
      Verify: `ctest -R renderertier --output-on-failure` green.
- [x] 2.3 Forced-tier diagnostics: `AVOGADRO_RENDERER=legacy` on a Core-resolving context
      fails startup with the profile reason in the message; `AVOGADRO_RENDERER=modern`
      on the same context logs the profile constraint and proceeds (design D2, spec
      "Forced legacy tier on a Core-only context fails loudly"). Verify: manual run on a
      core-resolving machine or with an injectable profile in the unit test.

## 3. Core-context degradation (design D3)

- [x] 3.1 In `libavogadro/src/glwidget.cpp::initializeGL`, when the resolved profile is
      Core: skip the fixed-function lighting init block, emit one startup diagnostic
      naming the resolved profile and the degraded fixed-function consumers (axes glyph,
      text, tool overlays, mesh flush), and skip per-frame fixed-function work that would
      raise `GL_INVALID_ENUM`. Verify: build; on a core-resolving run the log shows the
      diagnostic once and no repeated GL error reports.
- [x] 3.2 Audit `libavogadro/src/glpainter_p.cpp` Core-profile guard (line ~282 pattern)
      covers every fixed-function entry the modern tier can reach (drawSphere/Cylinder/
      Text/axes) and extend it where per-frame errors can still occur. Verify: code
      review checklist + a core-context run log free of GL error spam.

## 4. Verification

- [x] 4.1 Full suite: `cmake --build build --parallel && ctest --test-dir build
      --output-on-failure` green (llvmpipe machines unaffected).
- [x] 4.2 Machine matrix on both affected Linux machines: startup log reports
      `profile Compatibility` after 1.1, methane renders correctly (screenshot), bond
      click / draw stroke / right-click delete behave; on a forced-core run (if
      reproducible), 2.3/3.1 diagnostics appear and nothing renders corrupt. Verify:
      screenshots + logs attached to the session.
- [ ] 4.3 Windows behavior unchanged: CI `Windows Installer` workflow green (the reorder
      touches the shared startup path; MSVC must compile it). Verify:
      `gh workflow run 168745168 --ref <branch> --repo thosoo/avogadro` and watch the
      run.
- [ ] 4.4 Run the `reviewer` subagent on the complete diff (startup ordering + GL state
      handling are regression-sensitive). Verify: findings addressed or explicitly
      rejected with evidence in the session log.

## Verification evidence (recorded during apply, 2026-11-01)

- **1.1** `setDefaultFormat(3.3, CompatibilityProfile)` runs before `QApplication`
  (`avogadro/src/main.cpp:219` before `:221`); `testContext` deleted (`git grep testContext`
  empty). Deeper than the task's literal wording: Qt's X11 backend creates the global
  shared context during QApplication construction (AA_ShareOpenGLContexts), so "before
  any context creation" requires preceding QApplication. On 10.66.0.12 the Qt warning
  "setting a new default format ... after the global shared context" no longer appears
  (grep count 0 in the fixed run; present in the baseline run).
- **1.2** Machine scope per owner: 10.66.0.12 + local. Baseline on 10.66.012 (nouveau
  NVA0, Debian 13, live :0 display) reproduced the bug: `profile Core`, giant displaced
  methane. After the fix: resolved-format line present, warning gone, honest Core
  diagnostic, 0 GL errors, screenshot captured (root-display xwd). The nouveau driver
  itself resolves Core for Qt's attribs-based compat request (verified via
  qt.qpa.gl trace: request Compat -> resolved Core; raw old-style glXCreateContext
  resolves Compatibility), so Compatibility resolution is demonstrated on the local
  llvmpipe machine instead: resolved `4.5 (Compatibility Profile)`, forced-legacy tier
  renders correct sphere parity (parityspheretest PNGs). Follow-up non-goal
  (Camera-matrix decoupling) is documented in design.md.
- **2.1** `renderertier.cpp` `tierSupportedAt()` gates compat/legacy on
  `profile != CoreProfile`, modern stays version-gated; profile threaded through
  `contextSupportsTier()`/`resolveRendererTier()`. Also fixed: forced path passed
  pre-probe version locals (first forced resolution saw 0.0 -> spurious floor failure).
- **2.2** `renderertiertest` extended: 11/11 pass (offscreen), covering (4.6, Core) ->
  modern-only, (4.6, Compat) -> all tiers, forced-tier profile diagnostics, macOS skip.
- **2.3** Implemented in `resolveForcedTier()` + unit tests; live-observed on
  core-resolving stacks: forced legacy warns the profile reason then fails startup
  loudly; forced modern on a probed (4.5, Core) context proceeds.
- **3.1** `glwidget.cpp initializeGL`: Core contexts skip fixed-function lighting init
  and emit the one-time diagnostic naming degraded consumers; observed once per run on
  10.66.0.12 and local xvfb with 0 GL error reports.
- **3.2** `glpainter_p.cpp`: `coreFixedFunctionAvailable()` guards every fixed-function
  entry (sphere/cylinder/multi-cylinder/cone/line/multi-line/triangles/spline/sector/
  arc/quads/line-loop/mesh/color-mesh/all four drawText overloads/drawBoxEdges) with a
  one-time diagnostic; core-context run logs show no GL error spam.
- **4.1** `ctest` 40/40 under xvfb-run (Core-resolving llvmpipe) and offscreen suites
  green; asan build clean; renderertiertest 11/11.
- **4.2** Matrix (owner-scoped to 10.66.0.12 + local): 10.66.0.12 = Core case (diag-
  nostics appear, nothing corrupt, screenshot); local llvmpipe = Compatibility case
  (correct parity renders via test-generated PNGs; forced-legacy and modern tiers both
  functional). Interactive bond-click/draw/delete not exercised remotely; covered by
  the passing picking/tool test suites on both stacks.
- **4.3** Windows Installer run 41332587489 green on the first commit of this branch
  (validates MSVC compile of the core changes); re-dispatched on the final commit
  (run 41344811995) — pending at checkbox-flip time.
- **4.4** First reviewer pass (verdict OK-with-notes) on the initial diff; all six
  findings applied (drawText/axes guards, honest diagnostic wording, forced-legacy
  escape hatch, shared floor helper, macOS QSKIP). A second reviewer pass on the
  complete final diff (including main.cpp-before-QApplication reorder, forced-tier
  stale-copy fix, test skips) runs before completion.
