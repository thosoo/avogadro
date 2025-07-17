/**********************************************************************
  HairEngine - Display hair/fur on atoms

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Avogadro is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.
 **********************************************************************/

#ifndef HAIRENGINE_H
#define HAIRENGINE_H

#include <avogadro/global.h>
#include <avogadro/engine.h>

#include <QtCore/QElapsedTimer>
#include <QtCore/QHash>
#include <Eigen/Geometry>
#include <vector>


// Forward declare the generated UI form to avoid requiring the header in
// consumers of this engine.
namespace Ui {
class HairSettingsWidget;
}

namespace Avogadro {

class HairSettingsWidget;
class Atom;

/**
 * \brief Available inertia models for hair animation.
 */
enum InertiaModel {
  VectorEMA,  /**< Exponential moving average smoothing. */
  SpringLag,  /**< Critically damped torsional spring.   */
  MassSpring  /**< Full mass–spring strands.             */
};

/**
 * \brief Runtime settings controlling the hair dynamics.
 */
struct Settings
{
  InertiaModel model;     ///< Selected inertia model.
  double alpha;           ///< EMA coefficient for VectorEMA.
  double lagFactor;       ///< Amplitude scale for Spring-Lag.
  double naturalFreq;     ///< Natural spring frequency in rad/s.
  int    nodesPerStrand;  ///< Number of nodes in Mass-Spring mode.
  double stiffness;       ///< Constraint stiffness for Mass-Spring.
  double drag;            ///< Velocity drag for Mass-Spring.
  int    constraintIters; ///< Constraint solver iterations.
};
class HairEngine : public Engine
{
  Q_OBJECT
  AVOGADRO_ENGINE("Hair", tr("Hair"), tr("Renders hair on atoms"))

public:
  HairEngine(QObject *parent = 0);
  ~HairEngine();

  Engine *clone() const;

  bool renderOpaque(PainterDevice *pd);

  QWidget *settingsWidget();
  bool hasSettings() { return true; }

  double radius(const PainterDevice *, const Primitive *p = 0) const;
  Engine::Layers layers() const;

  void writeSettings(QSettings &settings) const;
  void readSettings(QSettings &settings);

public slots:
  /// Set the hair length in Angstroms.
  void setLength(double value);
  /// Set the number of hairs drawn per atom.
  void setCount(int value);
  /// Change the inertia model.
  void setInertiaModel(int value);
  /// Set EMA alpha parameter.
  void setAlpha(double value);
  /// Set torsional spring lag factor.
  void setLagFactor(double value);
  /// Set torsional spring natural frequency.
  void setNaturalFreq(double value);
  /// Set nodes per strand for mass-spring mode.
  void setNodes(int value);
  /// Set mass-spring stiffness.
  void setStiffness(double value);
  /// Set mass-spring drag.
  void setDrag(double value);
  /// Set mass-spring constraint iterations.
  void setConstraintIters(int value);

private slots:
  void settingsWidgetDestroyed();

private:
  HairSettingsWidget *m_settingsWidget;
  void clearState();
  Settings m_settings;
  double m_length;
  int m_count;
  QElapsedTimer m_timer;       ///< Timer used for animation phases.
  QElapsedTimer m_frameTimer;  ///< Tracks per-frame time step.
  unsigned int m_frameCount;   ///< Incremented each frame for RNG.
  QHash<unsigned int, Eigen::Vector3d> m_prevPos;
  QHash<unsigned int, Eigen::Vector3d> m_prevVel;
  QHash<unsigned int, Eigen::Vector3d> m_smoothVel;
  struct SpringState { Eigen::Vector3d baseDir; Eigen::Vector3d dir; Eigen::Vector3d omega; };
  struct NodeState { Eigen::Vector3d pos; Eigen::Vector3d prev; };
  struct Strand { Eigen::Vector3d baseDir; std::vector<NodeState> nodes; };
  QHash<unsigned int, std::vector<SpringState>> m_springStrands;
  QHash<unsigned int, std::vector<Strand>> m_massStrands;
  Eigen::Projective3d m_prevModelView;
  bool m_hasPrevModelView;
};


class HairEngineFactory : public QObject, public PluginFactory
{
  Q_OBJECT
  Q_INTERFACES(Avogadro::PluginFactory)
  Q_PLUGIN_METADATA(IID "net.sourceforge.avogadro.pluginfactory/1.5")
  AVOGADRO_ENGINE_FACTORY(HairEngine)
};

} // End namespace Avogadro

#endif
