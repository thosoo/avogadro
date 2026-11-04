# auto-optimize-interaction Delta

## ADDED Requirements

### Requirement: AutoOpt tool interactions are safe in every tool-lifetime state
The AutoOpt tool SHALL NOT dereference view or force-field state that has not been
established yet. Starting, stopping, painting, and mouse interaction SHALL behave as
defined below regardless of the order in which the tool is activated, the canvas is
touched, the first frame is presented, and a force field becomes available. On
renderers that degrade honestly by skipping the tool's overlay pass, the tool SHALL
remain fully operable: the overlay pass is a presentation detail and SHALL NOT be a
prerequisite for starting or running a session. When no force field implementation is
available, the tool SHALL be inert rather than crashing: canvas interaction and
repaints SHALL NOT dereference absent force-field state.

#### Scenario: Start pressed before the first painted frame
- **WHEN** the user activates the AutoOpt tool and presses Start without ever moving
  or clicking the pointer over the molecule view
- **THEN** the process does not crash: the tool either starts the session normally or
  declines with a visible/logged diagnostic, and a subsequent attempt after touching
  the view behaves as a normal session

#### Scenario: Tool works on renderers that skip the tool overlay
- **WHEN** the application runs on a context where the renderer degrades honestly by
  skipping the active tool's overlay pass (for example a Core-profile-only GL stack)
- **THEN** activating AutoOpt and starting, running, and stopping a session work with
  no crash; only the tool's on-screen overlay (energy readout, halos) is absent

#### Scenario: Mouse events over the view with no force field available
- **WHEN** no force field implementation can be loaded and the user activates the
  AutoOpt tool and presses or releases the mouse over the molecule view
- **THEN** the process does not crash and the tool performs no force-field bookkeeping

#### Scenario: Repaint with no force field available
- **WHEN** no force field implementation can be loaded and the view repaints while the
  AutoOpt tool is active
- **THEN** the frame presents without a crash and without force-field reporting

#### Scenario: Stop is safe when no session ever started
- **WHEN** the user stops (or the tool is deactivated) without a session having been
  started
- **THEN** the tool tears down without a crash and leaves no force-field markers behind
