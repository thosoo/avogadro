// fix-orbital-visualization task 1.3 (design D1): Mesh::valid() must be
// lock-free so a thread that already holds the mesh read lock (e.g. a
// SurfaceEngine render pass reached through SceneBuilder::drawMesh) can call
// it without self-deadlocking on the non-recursive QReadWriteLock.
//
// Pre-fix, Mesh::valid() took a QWriteLocker on the same mesh, which blocks
// forever when the calling thread already holds that mesh's read lock (the
// read-then-write self-deadlock that froze the modern-tier paint path). This
// test holds a read lock and calls valid() on the same thread; pre-fix that
// deadlocks the test run, post-fix it returns.

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

class MeshLockTest : public QObject {
  Q_OBJECT
private Q_SLOTS:
  void validReturnsUnderHeldReadLock();
  void validRejectsMismatchedSizes();
  void validRejectsBadColorCount();
  void writeLockAcquirableAfterReadReleased();
};

// The regression: hold the mesh read lock on this thread, then call valid().
// Pre-fix this deadlocks (valid() takes a write lock on a mesh this thread
// already reads). Post-fix it returns true for a consistent mesh.
void MeshLockTest::validReturnsUnderHeldReadLock()
{
  Mesh mesh;
  mesh.setVertices(tri());
  mesh.setNormals(tri());
  mesh.setColors(triColors()); // 3 colors == N, so the mesh is valid

  mesh.lock()->lockForRead();
  // This call must return promptly (it deadlocked pre-fix).
  const bool valid = mesh.valid();
  mesh.lock()->unlock();

  QVERIFY(valid);
}

void MeshLockTest::validRejectsMismatchedSizes()
{
  Mesh mesh;
  mesh.setVertices(tri());       // 3 vertices
  mesh.setNormals(tri());        // 3 normals
  // Add extra vertices so sizes differ (3 != 6).
  mesh.addVertices(tri());

  QVERIFY(!mesh.valid());
}

void MeshLockTest::validRejectsBadColorCount()
{
  Mesh mesh;
  mesh.setVertices(tri());       // 3 vertices
  mesh.setNormals(tri());        // 3 normals
  // A color vector that is neither 1 nor N (3) is invalid.
  mesh.setColors({ Color3f(1, 0, 0), Color3f(0, 1, 0) }); // 2 colors

  QVERIFY(!mesh.valid());
}

void MeshLockTest::writeLockAcquirableAfterReadReleased()
{
  Mesh mesh;
  mesh.setVertices(tri());
  mesh.setNormals(tri());

  mesh.lock()->lockForRead();
  mesh.lock()->unlock();

  // Sanity: no read lock leaked from this test (or from valid()), so a writer
  // can acquire the mesh write lock immediately.
  QVERIFY(mesh.lock()->tryLockForWrite());
  mesh.lock()->unlock();
}

QTEST_MAIN(MeshLockTest)
#include "meshlocktest.moc"
