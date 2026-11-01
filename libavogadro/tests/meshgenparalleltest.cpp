// Regression / correctness test for OpenSpec task 5.3 / D9 parallel marching
// cubes (fix-surface-visualization-performance).
//
// MeshGenerator::run() partitions the outer cell loop into K contiguous slabs,
// marches each slab on a worker into slab-local buffers, then concatenates in
// slab order. The spec allows equivalence-after-ordering between the serial
// (K=1) and parallel (K>1) results: identical triangle counts, identical
// vertex positions as a multisets, all coordinates finite. Two runs with the
// SAME K must be bitwise identical (run-to-run determinism).
//
// K is forced through the MeshGenerator::testThreadCount test hook
// (0 = auto; > 0 substitutes for min(8, idealThreadCount())).

#include <QtTest/QtTest>
#include <QCoreApplication>

#include <avogadro/cube.h>
#include <avogadro/mesh.h>
#include <avogadro/meshgenerator.h>

#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

using namespace Avogadro;
using Vector3f = Eigen::Vector3f;

namespace {

// A synthetic cube holding a sphere distance field: negative inside, positive
// outside, so the iso=0 surface is a closed sphere (well inside the grid).
Cube *makeSphereCube(int dim, double radius)
{
  Cube *cube = new Cube;
  Eigen::Vector3d min(-3.0, -3.0, -3.0);
  Eigen::Vector3d max(3.0, 3.0, 3.0);
  // setLimits(min, max, dim) -> spacing derived from dim-1 intervals
  if (!cube->setLimits(min, max, Eigen::Vector3i(dim, dim, dim))) {
    delete cube;
    return nullptr;
  }
  const Eigen::Vector3d center = (min + max) * 0.5;
  std::vector<float> *data = cube->data();
  data->clear();
  data->reserve(static_cast<std::size_t>(dim) * dim * dim);
  for (int i = 0; i < dim; ++i) {
    for (int j = 0; j < dim; ++j) {
      for (int k = 0; k < dim; ++k) {
        Eigen::Vector3d pos = min + Eigen::Vector3d(i, j, k).cwiseProduct(
                                        (max - min) / (dim - 1));
        double d = (pos - center).norm() - radius;
        data->push_back(static_cast<float>(d));
      }
    }
  }
  return cube;
}

// Round coordinates to 1e-5 and return a sortable key per vertex.
using RoundedKey = std::tuple<long long, long long, long long>;

RoundedKey roundedKey(const Vector3f &v)
{
  auto r = [](float f) {
    return static_cast<long long>(std::llround(static_cast<double>(f) * 1e5));
  };
  return std::make_tuple(r(v.x()), r(v.y()), r(v.z()));
}

std::vector<RoundedKey> sortedVertexKeys(const std::vector<Vector3f> &verts)
{
  std::vector<RoundedKey> keys;
  keys.reserve(verts.size());
  for (const Vector3f &v : verts)
    keys.push_back(roundedKey(v));
  std::sort(keys.begin(), keys.end());
  return keys;
}

bool allFinite(const std::vector<Vector3f> &verts)
{
  for (const Vector3f &v : verts) {
    if (!std::isfinite(v.x()) || !std::isfinite(v.y()) || !std::isfinite(v.z()))
      return false;
  }
  return true;
}

// Generate a mesh for the cube at the given forced slab count K.
// Returns false when generation failed; vertices/normals are filled from the
// mesh afterwards.
bool generate(Cube *cube, int K, std::vector<Vector3f> &verts,
              std::vector<Vector3f> &normals)
{
  Mesh mesh;
  MeshGenerator generator;
  if (!generator.initialize(cube, &mesh, 0.0f)) {
    qWarning("initialize failed");
    return false;
  }
  MeshGenerator::testThreadCount = K;
  generator.run(); // run directly on the calling thread (deterministic order)
  MeshGenerator::testThreadCount = 0;
  verts = mesh.vertices();
  normals = mesh.normals();
  return true;
}

} // namespace

class MeshGenParallelTest : public QObject
{
  Q_OBJECT

private slots:
  void parallelMatchesSerial();
  void deterministicPerK();
};

// (a) identical triangle counts, (b) vertex positions equal as multisets,
// (c) all coordinates finite -- comparing K=1 (serial) and K=4 (parallel).
void MeshGenParallelTest::parallelMatchesSerial()
{
  const int dim = 48;
  Cube *cube = makeSphereCube(dim, 1.5);
  QVERIFY(cube != nullptr);

  std::vector<Vector3f> verts1, norms1, verts4, norms4;
  QVERIFY(generate(cube, 1, verts1, norms1));
  QVERIFY(generate(cube, 4, verts4, norms4));
  delete cube;

  QVERIFY(!verts1.empty());
  QCOMPARE(verts1.size(), norms1.size());
  QCOMPARE(verts4.size(), norms4.size());

  // (c) every coordinate finite in both runs
  QVERIFY(allFinite(verts1));
  QVERIFY(allFinite(verts4));

  // (a) identical triangle counts
  QCOMPARE(verts1.size() / 3, verts4.size() / 3);

  // (b) vertex positions equal as multisets (rounded to 1e-5)
  auto keys1 = sortedVertexKeys(verts1);
  auto keys4 = sortedVertexKeys(verts4);
  QVERIFY2(keys1 == keys4,
           "serial and parallel marching cubes produced different geometry");
}

// (d) two runs with the same K are bitwise identical (determinism).
void MeshGenParallelTest::deterministicPerK()
{
  const int dim = 48;
  Cube *cube = makeSphereCube(dim, 1.5);
  QVERIFY(cube != nullptr);

  std::vector<Vector3f> v1a, n1a, v1b, n1b, v4a, n4a, v4b, n4b;
  QVERIFY(generate(cube, 1, v1a, n1a));
  QVERIFY(generate(cube, 1, v1b, n1b));
  QVERIFY(generate(cube, 4, v4a, n4a));
  QVERIFY(generate(cube, 4, v4b, n4b));
  delete cube;

  QVERIFY(v1a == v1b);
  QVERIFY(n1a == n1b);
  QVERIFY(v4a == v4b);
  QVERIFY(n4a == n4b);
}

QTEST_MAIN(MeshGenParallelTest)
#include "meshgenparalleltest.moc"
