# gpu-rendering-pipeline Delta

## ADDED Requirements

### Requirement: Cross-view GL resources live in one share group
All views that share rendering-side GL objects (the shared `GLPainter` and any
engine-owned textures) SHALL run with GL contexts that belong to a single share
group, or SHALL NOT share GL object names at all. GL object deletion SHALL
happen only while a context that can legally delete the objects is current on
the calling thread. No GL call path SHALL dereference an object name that was
created under a context outside the caller's share group.

#### Scenario: Two views share one painter
- **WHEN** a second `GLWidget` is created sharing the first widget's `GLPainter`,
  and both views render the same scene
- **THEN** both views initialize and present without a driver-module access
  violation, on every rendering tier

#### Scenario: View teardown deletes GL objects safely
- **WHEN** a `GLWidget` is destroyed after having rendered
- **THEN** its owned GL objects are deleted with a context current; when no
  context can be made current, no GL call is issued and the widget still
  destructs cleanly

#### Scenario: Startup with a hidden second view survives first paint
- **WHEN** the application starts with the second view created while hidden and
  both views later become visible
- **THEN** the first presents of both views survive, and the process does not
  fault inside the GL driver's present path
