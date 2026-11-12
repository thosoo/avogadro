#include "config.h"
#include <QtTest>

#include "xtb/lbfgs.h"

#include <cmath>
#include <vector>

using Avogadro::Xtb::LBfgs;

class LbfgsTest : public QObject
{
  Q_OBJECT
private slots:
  void quadraticConverges();
  void rosenbrockConverges();
  void maxEvaluationsStops();
  void historyResetWorks();
};

// f = sum((x-3)^2), grad = 2(x-3). Two "atoms" -> four coordinates.
void LbfgsTest::quadraticConverges()
{
  std::vector<double> coords = { 0.0, 0.0, 0.0, 0.0 };
  auto evaluate = [](const std::vector<double>& c, std::vector<double>& g) -> double {
    double f = 0.0;
    for (size_t i = 0; i < c.size(); ++i) {
      double d = c[i] - 3.0;
      f += d * d;
      g[i] = 2.0 * d;
    }
    return f;
  };
  auto shouldStop = [](int evals) -> bool { return evals >= 100; };

  LBfgs lbfgs;
  LBfgs::Result r = lbfgs.minimize(coords, evaluate, shouldStop);
  QVERIFY(r.finalGradNorm < 1e-6);
  QVERIFY(r.evaluations <= 100);
}

// Rosenbrock a=1 b=100 from (-1.2, 1.0).
void LbfgsTest::rosenbrockConverges()
{
  std::vector<double> coords = { -1.2, 1.0 };
  auto evaluate = [](const std::vector<double>& c, std::vector<double>& g) -> double {
    const double a = 1.0;
    const double b = 100.0;
    double x = c[0];
    double y = c[1];
    double f = (a - x) * (a - x) + b * (y - x * x) * (y - x * x);
    g[0] = -2.0 * (a - x) - 4.0 * b * x * (y - x * x);
    g[1] = 2.0 * b * (y - x * x);
    return f;
  };
  auto shouldStop = [](int evals) -> bool { return evals >= 1000; };

  LBfgs lbfgs;
  LBfgs::Result r = lbfgs.minimize(coords, evaluate, shouldStop);
  QVERIFY(std::fabs(coords[0] - 1.0) < 1e-2);
  QVERIFY(std::fabs(coords[1] - 1.0) < 1e-2);
  QVERIFY(r.evaluations <= 1000);
}

// Same Rosenbrock but the caller stops after 10 evaluations.
void LbfgsTest::maxEvaluationsStops()
{
  std::vector<double> coords = { -1.2, 1.0 };
  auto evaluate = [](const std::vector<double>& c, std::vector<double>& g) -> double {
    const double a = 1.0;
    const double b = 100.0;
    double x = c[0];
    double y = c[1];
    double f = (a - x) * (a - x) + b * (y - x * x) * (y - x * x);
    g[0] = -2.0 * (a - x) - 4.0 * b * x * (y - x * x);
    g[1] = 2.0 * b * (y - x * x);
    return f;
  };
  auto shouldStop = [](int evals) -> bool { return evals >= 10; };

  LBfgs lbfgs;
  LBfgs::Result r = lbfgs.minimize(coords, evaluate, shouldStop);
  QVERIFY(r.evaluations <= 11);
}

// Run to partial convergence, reset history, run again: still converges.
void LbfgsTest::historyResetWorks()
{
  std::vector<double> coords = { -1.2, 1.0 };
  auto evaluate = [](const std::vector<double>& c, std::vector<double>& g) -> double {
    const double a = 1.0;
    const double b = 100.0;
    double x = c[0];
    double y = c[1];
    double f = (a - x) * (a - x) + b * (y - x * x) * (y - x * x);
    g[0] = -2.0 * (a - x) - 4.0 * b * x * (y - x * x);
    g[1] = 2.0 * b * (y - x * x);
    return f;
  };
  auto shouldStop = [](int evals) -> bool { return evals >= 20; };

  LBfgs lbfgs;
  LBfgs::Result r1 = lbfgs.minimize(coords, evaluate, shouldStop);
  QVERIFY(r1.evaluations <= 21);

  lbfgs.resetHistory();
  auto shouldStop2 = [](int evals) -> bool { return evals >= 1000; };
  LBfgs::Result r2 = lbfgs.minimize(coords, evaluate, shouldStop2);
  QVERIFY(std::fabs(coords[0] - 1.0) < 1e-2);
  QVERIFY(std::fabs(coords[1] - 1.0) < 1e-2);
  QVERIFY(r2.evaluations <= 1000);
}

QTEST_MAIN(LbfgsTest)
#include "lbfgstest.moc"
