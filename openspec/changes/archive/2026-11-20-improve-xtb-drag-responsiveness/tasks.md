## 1. Pointer-driven tick

- [x] 1.1 Add a pointer-driven tick to `AutoOptTool::mouseMoveEvent`: when an xTB session is active,
  call `m_xtbEngine->requestTick()` so a pointer-authored movement registers with the worker on the
  next loop pass instead of waiting the pacing timer; verify by build + code inspection against design
  D1
- [x] 1.2 Confirm the collapse semantics still hold under the pointer-driven tick: `requestTick()`
  sets only `m_tickPending` and wakes the worker, so the extra call cannot queue work or overrun the
  worker; verify by build + code inspection against design D3

## 2. Regression guard

- [x] 2.1 Run `ctest -R "xtbengineTest|autooptreproTest" --output-on-failure`; all green
  (autooptreproTest 26.5s pass, xtbengineTest 30.6s pass) — in particular the
  converged-stops-evaluations, cap, drag-deferral, release-recovery, and pacing-rate tests (they pin
  the idle/latch behavior the pointer-driven tick must not disturb)
