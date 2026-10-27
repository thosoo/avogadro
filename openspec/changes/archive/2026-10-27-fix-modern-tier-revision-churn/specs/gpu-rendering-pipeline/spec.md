# gpu-rendering-pipeline Delta

## MODIFIED Requirements

### Requirement: No blank frames during scene rebuild
While a scene rebuild is in flight on the modern tier, the widget SHALL
continue to present the last ready scene: every frame SHALL either present
newly adopted geometry or re-present the previously presented geometry. A
cleared (background-only) frame SHALL NOT be presented as a result of a
rebuild being pending, a worker result being superseded by a newer molecule
revision, or a molecule read lock being unavailable. The retained scene SHALL
continue to track camera rotation while it is re-presented. Under continuous
scene change (sustained editing, or an auto-optimization ticking), the
display SHALL remain continuously visible; latency (stale geometry for a few
frames) is acceptable, alternation between scene and blank is not. Under
sustained revision churn of any rate, worker results SHALL be adopted by
forward progress — a finished result newer than the currently presented
content SHALL be presented, and presentation latency SHALL be bounded by one
build — so that the display SHALL NOT fall permanently behind the molecule
state while the churn continues.

#### Scenario: AutoOpt ticks do not blink the display
- **WHEN** an auto-optimization session updates the molecule at its regular
  tick rate for a prolonged period
- **THEN** every presented frame contains the molecule; no cleared frames are
  interleaved with rendered frames

#### Scenario: Continuous drawing stroke stays visible
- **WHEN** the user drags a drawing stroke that mutates the molecule on every
  mouse move
- **THEN** the molecule remains visible for the whole stroke (possibly
  lagging the cursor), and does not disappear or blink during the stroke

#### Scenario: Rebuild in flight tracks camera rotation
- **WHEN** the camera is rotated while a scene rebuild is pending or a worker
  result is in flight
- **THEN** the re-presented last-ready scene rotates with the camera (it is
  drawn with the current camera transform, not frozen at an older one)

#### Scenario: Molecule read lock unavailable
- **WHEN** the render path cannot acquire the molecule read lock for a frame
- **THEN** the frame presents the last ready scene rather than a cleared frame

#### Scenario: Bond order change during an optimization session
- **WHEN** the user clicks a bond to cycle its order while an
  auto-optimization session is ticking
- **THEN** the re-ordered bond is presented within a bounded latency (one
  build), even though ticks continue to bump the molecule revision

#### Scenario: Sustained drag under churn
- **WHEN** the user drags an atom while an auto-optimization session ticks
- **THEN** the presented atom position trails the true position by no more
  than one build, and the trail does not grow over time
