## ADDED Requirements

### Requirement: Force-field access is thread-confined
While an auto-optimization is running, only the worker thread SHALL call into
the live force-field instance. Any state the GUI thread needs from the
optimization (energy, fixed/ignored atoms, fix/ignore markers) SHALL be
published by the worker as an immutable snapshot and read by the GUI thread
only through that snapshot, under a lock whose hold time is independent of
the optimization work. Tool shutdown or document teardown SHALL quiesce the
worker (stop requesting ticks and wait for the in-flight tick to finish)
before mutating or clearing shared force-field state. Worker-side reads of
the live molecule (snapshot copy, topology scan) SHALL hold the molecule read
lock for the duration of the read. No camera-frame or paint path SHALL block
on the optimizer's per-tick work.

#### Scenario: Idle-running HUD does not crash
- **WHEN** an auto-optimization runs unattended (HUD and per-atom overlay
  visible, regular repaints) for a prolonged period
- **THEN** the process does not crash and no heap corruption is reported;
  the GUI reads only worker-published snapshots

#### Scenario: Repaint during an in-flight optimization tick
- **WHEN** a repaint reads the published snapshot while the worker is inside
  `Setup`/stepper work on the live force field
- **THEN** the reader either sees the previous consistent snapshot or the
  new one, never torn data, and the repaint does not wait for the tick to
  finish

#### Scenario: Tool switch or document close while running
- **WHEN** the tool is disabled or the molecule is destroyed while an
  optimization tick is in flight
- **THEN** teardown waits for the in-flight tick before touching
  force-field state, and no mutation of constraint state overlaps worker
  execution

#### Scenario: Drawing or deleting during a run
- **WHEN** the user draws a bond or deletes an atom while the optimizer
  runs
- **THEN** the worker's molecule snapshot and topology scan hold the
  molecule read lock, so no concurrent read/write on live molecule storage
  occurs
