# gpu-rendering-pipeline Specification

## Purpose

Renders molecules through a tiered GPU pipeline whose default tier targets 60 fps on
protein-scale scenes while keeping the GUI thread responsive, with defined compatibility
tiers for old hardware and a frozen legacy tier, so rendering performance and GUI
responsiveness no longer degrade with scene size.

## Requirements

### Requirement: Tiered rendering backends
The application SHALL provide three rendering tiers — `modern` (shader/instanced, GL 3.3+
compatibility profile), `compat` (batched fixed-function, GL 2.0+), and `legacy` (the
pre-existing immediate-mode renderer) — and SHALL select the highest tier that is compiled
in and supported by the runtime GL context, unless overridden. The selected tier SHALL be
logged at startup with the resolved GL renderer string.

#### Scenario: Capable hardware defaults to modern
- **WHEN** the application starts on a system whose GL context supports GL 3.3 or better
- **THEN** the modern tier is selected and logged without any user action

#### Scenario: Below-floor context falls back
- **WHEN** the runtime context cannot provide GL 3.3 but provides GL 2.0
- **THEN** the compat tier is selected; if GL 2.0 is also unavailable, the legacy tier is
  selected, and rendering still succeeds

#### Scenario: User override
- **WHEN** the environment override `AVOGADRO_RENDERER` is set to `modern`, `compat`, or
  `legacy`
- **THEN** the requested tier is used (or startup fails with a clear diagnostic if it is
  not compiled in or not supported), and the override takes precedence over probing

#### Scenario: macOS uses legacy
- **WHEN** the application is built for macOS
- **THEN** only the compat and legacy tiers are compiled in and the legacy tier is
  selected by default

### Requirement: Modern-tier projection transform correctness
The modern tier's model-view-projection matrix (`GLWidget::currentModelViewProjection()`)
SHALL equal the standard `projection · modelview` product of the live GL stack,
so the modern tier projects geometry to exactly the same clip space as the
native fixed-function (compat / legacy) path. The matrix SHALL NOT be
assembled in a way that transposes or reorders the projection and modelview —
in particular, feeding GL column-major matrices into Qt's `QMatrix4x4`
raw-array constructor without correcting for its row-major read / column-major
store convention, which maps the scene through `(modelview · projection)ᵀ`
and distorts it non-uniformly (oval atoms, over-stretched bonds). The modern
tier SHALL render the same scene identically to the compat / legacy tiers,
with correct aspect (round atoms, correct bond lengths), at every
`devicePixelRatio`, including `devicePixelRatio = 1`.

#### Scenario: Modern tier projects like the native stack
- **WHEN** a scene is rendered on the modern tier
- **THEN** every world point projects to the same clip-space position as
  `gluProject` reports for the same point on the live `GL_PROJECTION ·
  GL_MODELVIEW` stack

#### Scenario: Modern tier is not distorted at standard density
- **WHEN** a scene is rendered at `devicePixelRatio = 1`
- **THEN** atoms render as round discs and bond lengths match the compat /
  legacy tier output for the same scene and camera

#### Scenario: Modern tier is not distorted at HiDPI
- **WHEN** a scene is rendered at `devicePixelRatio != 1`
- **THEN** atoms render as round discs and bond lengths match the compat /
  legacy tier output, with no non-uniform distortion

#### Scenario: Modern and compat agree on the same scene
- **WHEN** the same molecule is rendered on the modern tier and on the compat
  tier
- **THEN** the projected atom positions and bond lengths are equivalent (the
  modern tier's `uMVP` matches the compat tier's native `projection · modelview`)

### Requirement: Modern-tier frame performance
On the modern tier, rendering SHALL sustain the following frame rates during continuous
camera rotation: at least 60 fps for a ~4,000-atom molecule on integrated-graphics-class
hardware; at least 60 fps on discrete and 30 fps on integrated hardware for a ~50,000-atom
assembly; and at least 30 fps on discrete hardware for a ~1,000,000-atom scene viewed
statically or rotated. Submission of an unchanged scene SHALL not issue per-primitive work
proportional to scene size.

#### Scenario: Monomer on integrated GPU
- **WHEN** a ~4,000-atom protein is rotated continuously on integrated-graphics-class
  hardware with the modern tier
- **THEN** the average frame rate is at least 60 fps

#### Scenario: Large assembly
- **WHEN** a ~50,000-atom assembly is rotated continuously
- **THEN** the average frame rate is at least 60 fps on discrete and 30 fps on integrated
  hardware

#### Scenario: Capsid-scale scene
- **WHEN** a ~1,000,000-atom scene is displayed or rotated on discrete hardware
- **THEN** the average frame rate is at least 30 fps

#### Scenario: Static scene submission is bounded
- **WHEN** the scene is unchanged and frames are repeatedly rendered
- **THEN** the number of GL draw calls per frame is bounded by a small constant
  independent of scene size

### Requirement: GUI thread is not blocked by the viewport
On the modern tier, scene tessellation, cache rebuilds, and buffer preparation SHALL run
off the GUI thread; the GUI thread's per-frame work SHALL be limited to submitting ready
GPU buffers plus time-sliced uploads bounded to a small per-frame budget. Scene edits and
rebuilds SHALL NOT block input processing beyond that budget.

#### Scenario: Rotation during heavy scene change
- **WHEN** a large scene is being rebuilt (loaded, edited, or LOD-band changed) while the
  user rotates the view
- **THEN** the GUI keeps responding to input and frames continue to be presented from the
  last ready state during the rebuild

#### Scenario: Editing a large molecule
- **WHEN** the user adds or moves atoms in a large scene with the modern tier
- **THEN** only the affected chunks are rebuilt and interaction stays responsive

### Requirement: Picking correctness at scale
Picking SHALL return the primitive nearest the cursor (or the set within a rubber band)
without silently dropping hits on large scenes. The legacy GL_SELECT path, where still
used (compat and legacy tiers), SHALL detect selection-buffer overflow instead of
truncating silently.

#### Scenario: Click in a large scene
- **WHEN** the user clicks on an atom in a scene far larger than the legacy selection
  buffer capacity
- **THEN** the topmost primitive under the cursor is selected

#### Scenario: Rubber band exceeding legacy capacity
- **WHEN** a rubber-band selection covers more hits than the legacy selection buffer holds
  (on tiers that use GL_SELECT)
- **THEN** the overflow is detected and reported rather than silently mis-selecting

### Requirement: Selection state scalability
Testing whether a primitive is selected SHALL take constant time independent of the
selection size, and rendering SHALL early-out when nothing is selected, so that selecting
the entire scene does not change per-frame rendering cost asymptotically.

#### Scenario: Select-all on a large scene
- **WHEN** the user selects all atoms of a large scene with the modern tier
- **THEN** the frame rate does not degrade relative to having nothing selected

### Requirement: Level-of-detail stability
LOD selection SHALL be tied to discrete zoom bands with hysteresis and SHALL NOT
re-tessellate geometry per frame; at typical viewing distances the rendered detail SHALL
be at least the quality of the pre-existing dynamic-scaling minimum, and LOD band changes
SHOULD be prepared off the GUI thread.

#### Scenario: No LOD churn while rotating
- **WHEN** the user rotates the view without zooming across the LOD hysteresis threshold
- **THEN** no geometry is re-tessellated and the frame rate is unaffected by LOD logic

### Requirement: Visual parity for ported engines
An engine ported to the new emitter API SHALL produce visually equivalent output to its
legacy rendering (same primitives, colors including per-atom custom colors and selection
highlighting, and transparency behavior), and engines that are not yet ported SHALL
continue to render through the compatibility bridge while the modern tier is active. Text
primitives emitted by engines (atom labels, measurements, annotations) SHALL render on
every tier with the same content, position (screen-space or projected world-space), and
color as the legacy tier. Tier-exclusive overlay features — the unit-cell axes overlay and
the debug/text overlays — SHALL function on the modern and compat tiers, not only on the
legacy tier. A Painter implementation SHALL NOT accept a primitive type and silently
discard it: every accepted draw call either renders the primitive or reports the
omission (one-time diagnostic), and the engines affected are documented.

#### Scenario: Default ball-and-stick parity
- **WHEN** the same molecule is rendered by the default engine on the modern tier and on
  the legacy tier
- **THEN** atoms, bonds (including multi-bond splitting), and selection halos are visually
  equivalent

#### Scenario: Unported engine still renders
- **WHEN** an engine that has not been ported to the emitter API is enabled while the
  modern tier is active
- **THEN** it renders through the compatibility bridge without crashing and without
  corrupting the modern-tier passes

#### Scenario: Atom labels on the modern tier
- **WHEN** the LabelEngine is enabled and a molecule is displayed on the modern tier
- **THEN** atom labels render with the same text, position, and color as on the legacy
  tier, and follow their atoms during rotation

#### Scenario: Axes overlay on the modern tier
- **WHEN** "Render Axes" is enabled on the modern tier
- **THEN** the axes overlay renders as on the legacy tier

#### Scenario: Debug HUD on the modern tier
- **WHEN** the debug overlay is enabled on the modern tier
- **THEN** the HUD renders — including the modern-tier draw-call and frame counters that
  the legacy-only path could never display

#### Scenario: Extension text overlay on the modern tier
- **WHEN** an extension adds a text overlay label on the modern tier
- **THEN** the label renders

#### Scenario: No silent primitive loss
- **WHEN** an engine emits a primitive type through the modern-tier emitter that the
  emitter does not implement
- **THEN** the omission is reported once (diagnostic log) and the affected engine is
  documented, instead of the primitive vanishing without trace

#### Scenario: Legacy tier unchanged
- **WHEN** the legacy tier is active
- **THEN** text, axes, and overlays render exactly as before, without double-drawing

### Requirement: Frozen low tiers
The compat and legacy tiers SHALL provide functional parity for the basics — molecules
render, tools operate, picking works — and SHALL receive bug fixes only; new rendering
features SHALL NOT be added to them, and the legacy tier's pre-existing performance
pathologies are accepted as documented rather than fixed.

#### Scenario: Compat tier basics
- **WHEN** the compat tier is forced on capable hardware via the override
- **THEN** molecules render correctly, tools work, and picking returns correct results

#### Scenario: Legacy tier unchanged
- **WHEN** the legacy tier is active
- **THEN** behavior matches the pre-pipeline renderer, including its known limitations

### Requirement: Modern-tier GL error isolation
The modern tier's draw-path GL error inspection SHALL report only errors generated by
the modern tier's own draw calls. Stale GL errors set on the shared context by
initialization unrelated to the modern tier SHALL NOT surface or be attributed to a
sphere, cylinder, or line draw. Shared GL context initialization performed for the
modern tier SHALL NOT introduce invalid-enumeration (`GL_INVALID_ENUM`) errors on the
resolved GL 3.3 context.

#### Scenario: Stale init error not attributed to the cylinder draw
- **WHEN** the shared GL context is left in an error state (for example `GL_INVALID_ENUM`)
  by context initialization unrelated to the modern tier
- **THEN** the modern tier's draw does NOT emit a `CYLINDER draw GL error` (or equivalent)
  warning for that stale error

#### Scenario: Shared init does not set invalid-enumeration errors
- **WHEN** `GLWidget` initializes the shared GL context for the modern tier
- **THEN** the initialization does NOT issue `glEnable`/`glLightModeli` with enums
  unsupported by the resolved context, so no `GL_INVALID_ENUM` is generated

#### Scenario: Genuine draw error is still reported
- **WHEN** a modern-tier draw call itself produces a GL error
- **THEN** that error is still reported by the modern backend's draw-path error inspection

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
### Requirement: Pick affordance parity with the legacy pick scene
The modern tier's picking pipeline SHALL offer click affordances at least as forgiving as
the frozen legacy pick scene: the effective pick geometry of a bond SHALL be no smaller
than the legacy pick scene's bond representation (bond radius inflated by the legacy pick
slop, drawn across the bond's full extent), and atoms SHALL keep their legacy slop. A
click that selected a bond on the legacy tier from the same position SHALL NOT resolve to
an unrelated atom on the modern tier; in particular, a click whose nearest surface is an
atom silhouette adjacent to a bond SHALL still allow the user to reach the bond by moving
the cursor within the bond's visual extent.

#### Scenario: Click near an atom end of a bond
- **WHEN** the user clicks within the bond's visual extent, close to an endpoint atom's
  silhouette, on the modern tier
- **THEN** the bond is picked (the hit resolves to the bond, not to the atom), matching
  legacy-tier behavior from the same position

#### Scenario: Click the middle of a bond
- **WHEN** the user clicks the middle of a bond on the modern tier
- **THEN** the bond is picked

### Requirement: Pick hits identify the clicked primitive
A pick hit returned by the modern tier SHALL carry the rendered primitive's own
index as its name, so tools resolve exactly the primitive the user clicked. The
name SHALL NOT depend on how many instances or segments the primitive was drawn
as: a bond drawn as several cylinder half-segments SHALL resolve to that bond's
index from a click on any of its segments, and this SHALL hold regardless of
which path produced the frame (synchronous engine emission or worker snapshot
tessellation). Atoms SHALL keep their current click behavior (an atom hit
resolves the clicked atom).

#### Scenario: Clicking the middle bond of a multi-bond molecule
- **WHEN** a molecule with at least three bonds is rendered on the modern tier
  and the user clicks the middle of the second bond
- **THEN** the pick hit's name is that bond's index (not 0)

#### Scenario: Clicking the first and last bonds
- **WHEN** the same multi-bond molecule is rendered and the user clicks the
  first and then the last bond
- **THEN** the pick hits' names are respectively the first and the last
  bond's indices

#### Scenario: Every segment of a split bond resolves to its bond
- **WHEN** a double bond is rendered as two offset half-segments per end and
  the user clicks each visible segment in turn
- **THEN** every hit resolves to the same bond's index

#### Scenario: Worker-rendered frames pick the same bonds
- **WHEN** the scene was produced by the worker snapshot tessellation path and
  the user clicks a non-first bond
- **THEN** the pick hit's name is that bond's index, matching the
  synchronous path for the same scene

### Requirement: Legacy-visible mutations reach the render cache
Every molecule mutation that the legacy tier re-renders implicitly because it redraws from
live data each frame — including bond order changes and element changes — SHALL cause the
modern tier to rebuild or re-emit the affected scene content within one frame of the
mutation's notification. A mutation that changes rendered appearance on the legacy tier
SHALL NOT require a coincidental second mutation (such as unrelated atom add/remove
traffic) to become visible on the modern tier.

#### Scenario: Bond order click with hydrogen adjustment disabled
- **WHEN** the user clicks a bond to cycle its order while "adjust hydrogens" is disabled
  and no optimization session is running
- **THEN** the bond renders with its new order within one frame of the click, without any
  other molecule change occurring

#### Scenario: Element recolor
- **WHEN** the user clicks an atom with a different element selected (element alchemy)
- **THEN** the atom re-renders with the new element's color and radius within one frame

#### Scenario: Drag-over-bond order preview
- **WHEN** the user drags a stroke across an existing bond, temporarily changing its order
- **THEN** the preview order change is visible during the drag and reverts at release

### Requirement: View-space lighting correctness
On the modern tier, fragment lighting SHALL be evaluated with the surface
normal and the light directions expressed in the same coordinate space (view
space). The lit appearance of a fragment SHALL depend on the live camera in
exactly the fixed-function way established by the legacy tier's eye-space
lights: the highlight SHALL stay fixed relative to the viewer while the scene
rotates under the camera (headlamp parity), and the unlit region of each
primitive SHALL correspondingly stay on the side facing away from the viewer,
never swinging onto geometry facing the viewer. There SHALL BE NO camera pose
at which an atom or bond renders as a fully unlit (ambient-only, near-black)
disc or band.

#### Scenario: Highlight is stable under rotation
- **WHEN** the camera is rotated continuously around a lit molecule on the
  modern tier
- **THEN** the specular highlight and the bright/dark shading pattern stay
  fixed relative to the viewer (the same screen direction throughout the
  rotation), matching the compat/legacy tiers' behavior for the same camera
  path

#### Scenario: No atom renders fully black at any angle
- **WHEN** the camera is swept through a full rotation (both axes) around any
  molecule of ordinary organic elements on the modern tier
- **THEN** no atom is presented as a near-black disc; every atom's visible
  face carries diffuse shading at all camera poses

#### Scenario: Parity with the fixed-function tiers
- **WHEN** the same molecule is rendered by the modern tier and the legacy
  tier from the same camera pose
- **THEN** the shading is visually equivalent: same lit hemisphere direction,
  same highlight placement, same ambient-only regions

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

### Requirement: Modern tier restores GL program and attribute state after drawing
The modern tier's per-frame draws SHALL restore the GL context state they bind:
shader programs SHALL be released after their draw calls, the generic vertex
attributes used for instancing SHALL be disabled with their instance divisors
reset after their last use in the frame, and the GL buffer bindings the tier
binds (including `GL_ARRAY_BUFFER`) SHALL be restored to their prior values —
in particular, no modern-tier instance buffer SHALL remain bound after the
frame's draw work or after a pick pass. Fixed-function draws that run later in
the same frame — the axes navigation glyph, the debug and extension text
overlays, and the active tool's overlay paint — SHALL render at their intended
positions and colors, unaffected by the modern tier's programs, instance
attributes, or buffer bindings — and unaffected by the fixed-function lighting
enable state: the modern tier's draw and pick passes SHALL NOT leave
`GL_LIGHTING` disabled into later fixed-function draws whose colors are
carried by materials (with lighting disabled, material colors are ignored and
the geometry renders unlit in whatever raster color is leftover).

#### Scenario: Axes glyph renders in its corner after scene content exists
- **WHEN** the scene contains atoms (so the modern tier's instanced draws run)
  and the axes overlay is enabled
- **THEN** the axes glyph renders in its designated corner with its red, green,
  and blue arrows — not displaced to the scene center and not black — and the
  axes' cylinder shafts are visible, not only the sphere and cone tips

#### Scenario: Debug HUD text persists once the scene is non-empty
- **WHEN** the first atom is added to the previously empty scene
- **THEN** the debug/extension text overlays remain visible in their intended
  positions on subsequent frames

#### Scenario: Tool overlay unaffected by instanced draws
- **WHEN** the active tool paints an overlay in the same frame as modern-tier
  instanced draws
- **THEN** the overlay renders at its intended position and colors, not
  transformed by the modern tier's uniforms or shaded by the modern tier's program

#### Scenario: No instance buffer stays bound after a frame or a pick
- **WHEN** a modern-tier frame has been drawn (including instance uploads) or a
  pick pass has completed
- **THEN** `GL_ARRAY_BUFFER_BINDING` is 0 on the current context, so later
  fixed-function or display-list draws cannot misinterpret client pointers as
  buffer offsets

#### Scenario: Axes colors stay correct after a pick
- **WHEN** the user clicks in the viewport (so the modern tier's pick pass
  runs) and a later frame draws the axes overlay
- **THEN** the axes glyph still renders its red, green, and blue arrows with
  material shading — never white and never an unrelated leftover raster color
  such as yellow
