# windows-installer-runtime Delta

## ADDED Requirements

### Requirement: Bundled xTB runtime resolves on a clean machine
The Windows installer SHALL bundle the xTB shared library together with its complete
transitive DLL dependency closure, the GFN-FF parameter data file, and the license
texts of every bundled component, installed inside the application directory. The
bundle SHALL be pinned: the workflow SHALL record the exact upstream source and
artifact filename of every downloaded component together with its sha256 digest, and
the packaging step SHALL fail when a downloaded artifact's digest does not match the
recorded digest. The existing dependency-closure and smoke checks SHALL treat the
bundle as first-class packaged runtime: the packaged application SHALL resolve the
bundled library at runtime (no external installation required), and the smoke run
SHALL verify that the bundled library loads and exports the C API entry points the
application calls.

#### Scenario: Clean machine loads the bundled library
- **WHEN** the installer is installed on a clean Windows machine (no conda, no Intel
  runtime, no xTB installed system-wide) and the application starts
- **THEN** the optimization tool's xTB backends are usable, and the smoke run observes
  that the bundled library loaded and exports the expected C API entry point

#### Scenario: Digest mismatch fails the build
- **WHEN** a pinned upstream artifact changes upstream (new upload at the same
  location) and the packaging step downloads it
- **THEN** the installer build fails naming the artifact whose digest no longer
  matches, and no installer artifact is produced

#### Scenario: Closure covers transitive dependencies
- **WHEN** the dependency-closure inspection or the smoke run walks the bundled
  library's imports
- **THEN** every non-system DLL the bundled library imports is present in the package
  (including transitive dependencies of the library's own components), so loading
  never fails with a missing module

#### Scenario: GFN-FF parameter data ships with the bundle
- **WHEN** the user selects GFN-FF on a clean Windows install
- **THEN** the GFN-FF parameter file is found next to the application and the session
  starts, instead of failing on missing parameter data

#### Scenario: License texts ship with the bundle
- **WHEN** the installer package is inspected
- **THEN** the license text of every bundled third-party component (the xTB library
  and each of its runtime dependencies) is present in the package
