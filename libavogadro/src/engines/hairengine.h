/**********************************************************************
  HairEngine - Engine for rendering hair on atoms

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  This source code is released under the GNU GPL.
**********************************************************************/

#ifndef HAIRENGINE_H
#define HAIRENGINE_H

#include <avogadro/global.h>
#include <avogadro/engine.h>
#include <Eigen/Core>
#include <QElapsedTimer>
#include <QMap>
#include <QVector>
#ifdef ENABLE_GLSL
#  include <GL/glew.h>
#endif

namespace Avogadro {

class Atom;
class Molecule;

class HairEngine : public Engine
{
  Q_OBJECT
  AVOGADRO_ENGINE("Hair", tr("Hair"), tr("Render animated hair on atoms"))

public:
  explicit HairEngine(QObject *parent = 0);
  ~HairEngine();

  Engine *clone() const;

  bool renderOpaque(PainterDevice *pd);
  void setMolecule(Molecule *molecule);

private:
  struct HairStrand {
    Eigen::Vector3d dir;
    double phase;
  };

  QMap<unsigned int, QVector<HairStrand> > m_hair;
  QElapsedTimer m_timer;
  double m_hairLength;
#ifdef ENABLE_GLSL
  GLuint m_vertShader;
  GLuint m_fragShader;
  GLuint m_program;
  GLint m_timeLoc;
  GLint m_lengthLoc;
  GLint m_dirAttr;
  GLint m_phaseAttr;
  GLint m_tipAttr;
#endif
};

class HairEngineFactory : public QObject, public PluginFactory
{
  Q_OBJECT
  Q_INTERFACES(Avogadro::PluginFactory)
  Q_PLUGIN_METADATA(IID "net.sourceforge.avogadro.pluginfactory/1.5")
  AVOGADRO_ENGINE_FACTORY(HairEngine)
};

} // namespace Avogadro

#endif
