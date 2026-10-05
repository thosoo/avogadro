# bundled-openbabel delta spec

## Purpose

Defines how Avogadro acquires, pins, and validates its bundled OpenBabel dependency (the permanent `thosoo/openbabel` companion fork), so that builds are reproducible from a single-sourced pin and never depend on post-checkout source mutation or duplicated pin constants.

## ADDED Requirements

### Requirement: Submodule-sourced OpenBabel pin
The bundled OpenBabel source SHALL be acquired exclusively from a git submodule in this repository, pinned to a single commit of `thosoo/openbabel`. The submodule gitlink SHALL be the only place the pin is recorded; no build script or workflow file SHALL independently hardcode an OpenBabel commit, archive URL, or source hash.

#### Scenario: Fresh recursive clone configures and builds
- **WHEN** a developer clones with `git clone --recurse-submodules` and configures the project
- **THEN** the bundled OpenBabel build uses the submodule source at the pinned commit, with no additional source download

#### Scenario: Pin bump touches only the gitlink
- **WHEN** the OpenBabel pin is updated in a commit
- **THEN** the change is expressed as the submodule gitlink (plus, at most, docs), with no duplicated commit constants left in CMake or workflow files

### Requirement: Fail-loud initialization guard
When configuring a checkout whose submodule content is missing or empty, the build system SHALL fail configuration with an error instructing the user to run `git submodule update --init`, and SHALL NOT download OpenBabel source from the network at configure or build time.

#### Scenario: Non-recursive clone fails with guidance
- **WHEN** a developer clones without submodules and configures the project
- **THEN** configuration stops with an actionable error naming the init command, and no OpenBabel download is attempted

#### Scenario: Source-archive checkout cannot silently degrade
- **WHEN** the project is configured from a source archive that cannot contain submodule content
- **THEN** configuration fails with the same guidance rather than silently fetching a different source

### Requirement: No post-checkout source mutation
The build SHALL NOT modify OpenBabel source files after checkout. All required OpenBabel changes (including the MSVC TLS fix) SHALL be present natively in the pinned fork commit.

#### Scenario: MSVC build needs no patch step
- **WHEN** the bundled OpenBabel is built with MSVC
- **THEN** the build succeeds from the pristine submodule source with no patching stage

#### Scenario: Build leaves the submodule clean
- **WHEN** a full configure-and-build completes
- **THEN** `git status` inside `extern/openbabel` reports a clean working tree

### Requirement: CI cache key derived from the pin
CI SHALL derive its bundled-OpenBabel build cache key from the submodule gitlink at the checked-out HEAD.

#### Scenario: Pin change rekeys the cache
- **WHEN** a commit changes the submodule pin and CI runs
- **THEN** the cache key reflects the new gitlink, so the pinned source is built rather than reusing a stale cached build

### Requirement: Bundled build stays isolated
The bundled OpenBabel SHALL be built and installed into its own prefix under the build directory (as today, `build/openbabel-install`), and Avogadro SHALL locate and link OpenBabel from that prefix. CI caching of the built prefix MAY continue.

#### Scenario: Avogadro links against the bundled prefix
- **WHEN** the project is configured and built from a clean build directory
- **THEN** OpenBabel libraries and headers are installed under the bundled prefix and Avogadro builds and tests against them
