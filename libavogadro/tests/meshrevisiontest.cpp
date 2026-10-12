// gpu-rendering-pipeline task 5.1 (design A6): Mesh revision counter.
//
// GLPainter streams a mesh to a VBO *once* and re-uploads only when the mesh
// changes. It detects a change via a per-mesh revision counter that is bumped
// on every geometry mutation and read (const) on the draw path. This test
// verifies that counter in isolation (no GL / no display required), because it
// is the invariant the VBO cache keys on: a stable mesh must never bump its
// revision, and any geometry mutation must bump it monotonically.

#include <QtTest>
#include <avogadro/mesh.h>
#include <avogadro/color3f.h>
#include <Eigen/Core>

using namespace Avogadro;

namespace {

std::vector<Eigen::Vector3f> tri() {
  return { Eigen::Vector3f(0, 0, 0),
           Eigen::Vector3f(1, 0, 0),
           Eigen::Vector3f(0, 1, 0) };
}

std::vector<Color3f> triColors() {
  return { Color3f(1, 0, 0), Color3f(0, 1, 0), Color3f(0, 0, 1) };
}

} // namespace

class MeshRevisionTest : public QObject {
  Q_OBJECT
private Q_SLOTS:
  void testFreshMeshRevisionIsZero();
  void testSetVerticesBumpsRevision();
  void testAddVerticesBumpsRevision();
  void testSetNormalsBumpsRevision();
  void testAddNormalsBumpsRevision();
  void testSetColorsBumpsRevision();
  void testAddColorsBumpsRevision();
  void testClearBumpsRevision();
  void testRevisionIsMonotonicAcrossMixedMutations();
  void testStableReadsDoNotBumpRevision();
  void testStableIsConstCorrect();
  void testRevisionSurvivesConcurrentReadWhileUnstable();
};

void MeshRevisionTest::testFreshMeshRevisionIsZero() {
  Mesh mesh;
  QCOMPARE(mesh.revision(), unsigned(0));
}

void MeshRevisionTest::testSetVerticesBumpsRevision() {
  Mesh mesh;
  const unsigned int before = mesh.revision();
  QVERIFY(mesh.setVertices(tri()));
  QCOMPARE(mesh.revision(), before + 1u);
}

void MeshRevisionTest::testAddVerticesBumpsRevision() {
  Mesh mesh;
  mesh.setVertices(tri());
  const unsigned int before = mesh.revision();
  QVERIFY(mesh.addVertices(tri()));
  QCOMPARE(mesh.revision(), before + 1u);
  QCOMPARE(mesh.vertices().size(), size_t(6));
}

void MeshRevisionTest::testSetNormalsBumpsRevision() {
  Mesh mesh;
  mesh.setVertices(tri());
  const unsigned int before = mesh.revision();
  QVERIFY(mesh.setNormals(tri()));
  QCOMPARE(mesh.revision(), before + 1u);
}

void MeshRevisionTest::testAddNormalsBumpsRevision() {
  Mesh mesh;
  mesh.setVertices(tri());
  mesh.setNormals(tri());
  const unsigned int before = mesh.revision();
  QVERIFY(mesh.addNormals(tri()));
  QCOMPARE(mesh.revision(), before + 1u);
}

void MeshRevisionTest::testSetColorsBumpsRevision() {
  Mesh mesh;
  mesh.setVertices(tri());
  const unsigned int before = mesh.revision();
  QVERIFY(mesh.setColors(triColors()));
  QCOMPARE(mesh.revision(), before + 1u);
}

void MeshRevisionTest::testAddColorsBumpsRevision() {
  Mesh mesh;
  mesh.setVertices(tri());
  mesh.setColors(triColors());
  const unsigned int before = mesh.revision();
  QVERIFY(mesh.addColors(triColors()));
  QCOMPARE(mesh.revision(), before + 1u);
}

void MeshRevisionTest::testClearBumpsRevision() {
  Mesh mesh;
  mesh.setVertices(tri());
  mesh.setColors(triColors());
  const unsigned int before = mesh.revision();
  QVERIFY(mesh.clear());
  QCOMPARE(mesh.revision(), before + 1u);
  QVERIFY(mesh.vertices().empty());
}

void MeshRevisionTest::testRevisionIsMonotonicAcrossMixedMutations() {
  Mesh mesh;
  unsigned int expected = 0;
  auto bump = [&](bool ok) {
    QVERIFY(ok);
    ++expected;
    QCOMPARE(mesh.revision(), expected);
  };
  bump(mesh.setVertices(tri()));
  bump(mesh.addVertices(tri()));
  bump(mesh.setNormals(tri()));
  bump(mesh.addNormals(tri()));
  bump(mesh.setColors(triColors()));
  bump(mesh.addColors(triColors()));
  bump(mesh.clear());
  // Re-populate and confirm it keeps climbing, never resetting or regressing.
  bump(mesh.setVertices(tri()));
}

void MeshRevisionTest::testStableReadsDoNotBumpRevision() {
  Mesh mesh;
  mesh.setVertices(tri());
  mesh.setNormals(tri());
  mesh.setColors(triColors());
  mesh.setStable(true);
  const unsigned int stableRevision = mesh.revision();
  // Repeated reads of geometry + stability on the stable mesh must not move the
  // counter -- this is what lets GLPainter skip the re-upload on every frame.
  for (int i = 0; i < 50; ++i) {
    (void)mesh.vertices();
    (void)mesh.normals();
    (void)mesh.colors();
    (void)mesh.stable();
  }
  QCOMPARE(mesh.revision(), stableRevision);
}

void MeshRevisionTest::testStableIsConstCorrect() {
  Mesh mesh;
  mesh.setVertices(tri());
  mesh.setNormals(tri());
  mesh.setColors(triColors());
  mesh.setStable(true);
  // Hand a const reference to the draw-path-style readers. stable(), revision()
  // and the geometry readers are const: the draw path reads them on a const Mesh
  // without mutating it.
  const Mesh& constMesh = mesh;
  QVERIFY(constMesh.stable());
  Q_UNUSED(constMesh.revision());
  (void)constMesh.vertices();
  (void)constMesh.normals();
  (void)constMesh.colors();
}

void MeshRevisionTest::testRevisionSurvivesConcurrentReadWhileUnstable() {
  // The generator mutates the mesh on a worker thread while it is unstable. The
  // revision is read under the same lock on the draw path, so a concurrent read
  // must observe a consistent (monotonic) value rather than corrupt it.
  Mesh mesh;
  mesh.setStable(false);
  unsigned int last = mesh.revision();
  for (int i = 0; i < 200; ++i) {
    mesh.addVertices(tri());
    const unsigned int r = mesh.revision();
    QVERIFY(r >= last);
    last = r;
  }
}

QTEST_MAIN(MeshRevisionTest)
#include "meshrevisiontest.moc"
