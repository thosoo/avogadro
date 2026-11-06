# Tasks: add-erase-tool

## 1. EraseTool skeleton and gesture state (D1, D8)

- [x] 1.1 Create `libavogadro/src/tools/erasetool.h` / `erasetool.cpp`: `EraseTool : public Tool` with `AVOGADRO_TOOL("Erase", tr("Erase"), …)` metadata, constructor (eraser icon `:/erase/erase.png`, tooltip describing the lasso gesture, no keyboard shortcut), `usefulness()` just below Draw, and a `settingsWidget()` with an "Adjust Hydrogens" checkbox (default checked). Verify the tools plugin builds: `cmake --build build --target erasetool`
- [x] 1.2 Implement `mousePressEvent`: record the press point in device pixels, begin a lasso gesture (clear the path, set dragging), lazily connect `toolActivated` (`Qt::UniqueConnection`) to abandon an in-flight loop on tool switch, accept the event. Verify it builds.
- [x] 1.3 Implement `mouseMoveEvent`: while dragging, append the pointer position (device pixels) to the lasso path; accept. Verify it builds.
- [x] 1.4 Implement `mouseReleaseEvent` release assembly (D3/D4/D6): apply the minimum-path guard (D4 — abort on a tap, no command/undo/mutation), then acquire the molecule write lock with `tryLockForWrite` (D6 — abort cleanly on contention: discard polyline, no commands, no state), and on success construct one `DeleteAtomDrawCommand` per victim inside an `UndoSequence` (text "Lasso Delete") and return it. Verify it builds.

## 2. Screen-space containment (D2)

- [x] 2.1 Implement containment in device pixels: project every atom position through `widget->camera()->project()`, bbox-cull against the loop's bounding box, then even-odd point-in-polygon on the survivors; auto-close an unclosed path with a straight segment from end to start. Add a pure static helper `pointInPolygon` so it is unit-testable. Verify via the containment test in 6.x.

## 3. Managed-hydrogen parity (D5)

- [x] 3.1 In the victim set, when adjust-hydrogens is enabled, exclude hydrogens from the victim set; heavy-atom victims pass `adjustHydrogens=true` to `DeleteAtomDrawCommand`. When disabled, every enclosed atom is a victim. Verify via the managed-hydrogen test in 6.x.

## 4. Interrupted-lasso cleanup and overlay (D7, D8)

- [x] 4.1 Implement `toolActivated(Tool*)` and the null-molecule / changed-document abandon: on tool switch, null molecule, or stale document, clear the lasso path and dragging flag (no command, no mutation); the next gesture starts clean. Verify via the interrupted-lasso test in 6.x.
- [x] 4.2 Implement `paint(GLWidget*)`: while dragging with a non-empty path, unproject the recorded device-pixel points to world space (at the widget-center depth) and draw a closed line loop via `widget->painter()->drawLineLoop(...)`, so the overlay renders on modern/compat/legacy tiers without "painter not active" diagnostics. Verify the overlay renders (run on a real window tier / check no diagnostic spam).

## 5. Registration (D8)

- [x] 5.1 Add the `erasetool` entry to `libavogadro/src/tools/CMakeLists.txt`, create `erasetool.qrc` referencing an eraser icon (`erase.png`), and verify the tool builds and appears in the palette: `cmake --build build --target erasetool`.

## 6. Regression tests (D4)

- [x] 6.1 Add `erasetooltest` to `libavogadro/tests/CMakeLists.txt` following the `selectionsettest`/`drawcommandtest` pattern (compiles `erasetool.cpp` + `drawcommand.cpp` directly, offscreen QPA, registered with ctest); verify it builds: `cmake --build build --target erasetooltest`.
- [x] 6.2 Implement the containment-deletion test: set up a molecule + GLWidget with a known camera, drive a lasso around a methyl group (and an unclosed loop), and assert enclosed atoms/bonds are deleted while outside atoms survive.
- [x] 6.3 Implement the tap no-op test: a press-and-release without significant travel deletes nothing and adds no undo entry.
- [x] 6.4 Implement the single-undo-step test: one undo restores the whole lasso (atoms + bonds), one redo re-deletes.
- [x] 6.5 Implement the managed-hydrogen parity test: with adjust-hydrogens on, looping a carbon deletes it and churns its managed hydrogens (not as independent victims); with it off, hydrogens are deleted as victims.
- [x] 6.6 Implement the interrupted-lasso cleanup test: tool switch mid-lasso (via `toolActivated`) leaves no residue and the next gesture starts clean.
- [x] 6.7 Run the new suite plus neighboring suites: `QT_QPA_PLATFORM=offscreen ctest --test-dir build -R "erase|draw|molecule|selection" --output-on-failure` and confirm all green.

## 7. Review and CI (D9)

- [x] 7.1 Self-review the diff against design D1–D9 (containment space, guard semantics, undo grouping, hydrogen parity, lock discipline, overlay, gesture scoping, MSVC portability) and remove any throwaway artifacts.
- [ ] 7.2 Trigger the Windows installer workflow (`gh workflow run 168745168 --ref <branch> --repo thosoo/avogadro`) and verify the build and test suite pass on MSVC per repo policy before opening a PR.
