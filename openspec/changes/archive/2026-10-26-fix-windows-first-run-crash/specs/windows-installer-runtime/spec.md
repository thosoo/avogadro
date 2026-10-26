# windows-installer-runtime Delta

## Purpose

Defines the contract for the Windows installer package contents: every packaged
binary must be loadable on a clean end-user machine, with no missing dependencies and
no dead debug-runtime payloads, and CI must actually verify the packaged runtime
instead of assuming it.

## ADDED Requirements

### Requirement: Packaged binaries resolve their dependencies
Every executable and DLL packaged by the Windows installer SHALL have all of its
non-system DLL dependencies present inside the package at install time. The
dependency-closure check SHALL cover the full package tree — the application
directory and every plugin directory beneath it — and SHALL fail the build when any
imported DLL is neither present in the package nor on a documented allowlist of
operating-system and VC++-redistributable DLLs. The closure SHALL be computed from
the binaries' own import tables, not from build-time link declarations.

#### Scenario: Fresh machine has every plugin loadable
- **WHEN** the built installer is installed on a clean Windows machine (no Qt, no
  Boost, no build tools installed system-wide) and Avogadro starts
- **THEN** every packaged plugin DLL loads successfully; the log reports no
  "module could not be found" failures

#### Scenario: Build fails on a missing dependency
- **WHEN** a packaged binary imports a non-system DLL that is absent from the
  package (for example a plugin importing a Qt module that the deploy step did not
  copy)
- **THEN** the installer build fails with a report naming the binary and the missing
  DLL, and no installer artifact is produced

#### Scenario: Closure covers plugin directories
- **WHEN** the dependency-closure check runs
- **THEN** DLLs under the packaged plugin trees (colors, engines, extensions, tools)
  are inspected as importers, not only the application directory

### Requirement: Qt modules used only by plugins are deployed
Qt runtime DLLs that are imported by packaged plugins or bundled libraries but not
by the main executable SHALL nevertheless be present in the packaged application
directory. Detection SHALL be import-table-driven so that a plugin picking up a new
Qt module cannot silently regress packaging.

#### Scenario: Plugin-only Qt module is present
- **WHEN** a plugin imports a Qt DLL (for example Qt6Concurrent.dll) that the main
  executable does not import
- **THEN** that Qt DLL is present in the packaged application directory after
  packaging, and the plugin loads

### Requirement: No unloadable debug-runtime payloads
The installer SHALL NOT package binaries that require debug C/C++ runtimes
(debug UCRT, debug MSVCP, debug VCRuntime), because those runtimes do not exist on
end-user machines. Release runtime DLLs actually required by packaged binaries SHALL
be present.

#### Scenario: Debug Boost DLLs are excluded
- **WHEN** the package is assembled from a dependency prefix that contains both
  release and debug variants of the same library
- **THEN** only variants loadable on a machine without Visual Studio are packaged

#### Scenario: Release runtime DLLs actually used are kept
- **WHEN** a packaged binary imports a release third-party runtime DLL (for example
  a Boost library used by a bundled parser)
- **THEN** that DLL is present in the package

### Requirement: CI smoke run exercises the packaged runtime
The Windows installer CI job SHALL launch the packaged application and verify from
its captured output that the process initialized: the startup banner and the
renderer-initialization log SHALL be observed, and the process SHALL NOT exit before
producing them. A process that exits without producing the startup diagnostics SHALL
fail the job. A headless runner whose graphics stack cannot satisfy any compiled-in
rendering tier SHALL produce a clearly reported, documented outcome — never a silent
pass and never an undiagnosed empty log.

#### Scenario: Regression in packaged runtime fails CI
- **WHEN** the packaged application exits before emitting its startup banner (for
  example because a dependency DLL is missing)
- **THEN** the smoke-run step fails the job and prints the captured output

#### Scenario: Healthy start is observed
- **WHEN** the packaged application starts on the runner
- **THEN** the smoke-run step observes the startup banner and renderer log (or a
  documented headless-limitation outcome) and passes with that evidence in the log
