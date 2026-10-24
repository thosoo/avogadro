# gpu-rendering-pipeline Delta

## ADDED Requirements

### Requirement: GL initialization runs only with a current context
GLWidget initialization — including fixed-function state setup and renderer-backend
creation — SHALL execute only while a GL context is current on the calling thread.
The widget's resize and paint entry points SHALL treat a failed context
initialization as "no work possible": no GL call SHALL be issued without a current
context, initialization SHALL NOT be marked complete when it did not run, and
initialization SHALL be retried on the first subsequent frame that does have a
current context, after which the widget renders normally (including through the
modern tier's existing lazy first-frame recovery). Startup and fallback diagnostics
SHALL report the presentation path actually in use after a fallback decision, not
the tier that was requested.

#### Scenario: Second view created while hidden
- **WHEN** a GLWidget is created for a document view that is not yet exposed, its
  resize event fires without a current context, and the widget is later shown
- **THEN** no GL call is issued during the contextless event; initialization is
  deferred; and the first exposed frame initializes the widget and presents the
  scene correctly

#### Scenario: Contextless initialization does not mark itself complete
- **WHEN** initialization bails out because no context could be made current
- **THEN** the widget does not record initialization as done, and the next frame
  with a current context performs full initialization instead of skipping it

#### Scenario: Fallback is reported accurately
- **WHEN** the modern renderer backend cannot initialize and the frame falls back
  to the fixed-function presentation path
- **THEN** the startup log names the fixed-function path as the presentation path
  actually in use, rather than claiming the modern tier is in use

#### Scenario: No undefined driver dispatch is observable
- **WHEN** the application starts with multiple GLWidgets (the second created while
  hidden) on any OpenGL vendor
- **THEN** the log never shows GL state queries returning live data while no
  context is current, and the process survives first paint without a driver-module
  access violation
