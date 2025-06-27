#include "hairengine.h"

#include <avogadro/atom.h>
#include <avogadro/molecule.h>
#include <avogadro/painterdevice.h>
#include <avogadro/color.h>
#include <Eigen/Geometry>

#include <QRandomGenerator>
#include <QtMath>
#ifdef ENABLE_GLSL
#  include <GL/glew.h>
#endif

namespace Avogadro {

HairEngine::HairEngine(QObject *parent)
  : Engine(parent), m_hairLength(1.0)
{
  m_timer.start();
#ifdef ENABLE_GLSL
  const char *vertSrc =
    "attribute vec3 attrDir;\n"
    "attribute float attrPhase;\n"
    "attribute float attrTip;\n"
    "uniform float time;\n"
    "uniform float hairLength;\n"
    "void main() {\n"
    "  vec3 dir = normalize(attrDir);\n"
    "  vec3 swing = normalize(cross(dir, vec3(0.0,0.0,1.0)));\n"
    "  dir = normalize(dir + swing * 0.2 * sin(time + attrPhase));\n"
    "  vec3 pos = gl_Vertex.xyz;\n"
    "  if (attrTip > 0.5) pos += dir * hairLength;\n"
    "  gl_Position = gl_ModelViewProjectionMatrix * vec4(pos, 1.0);\n"
    "  gl_FrontColor = gl_Color;\n"
    "}\n";

  const char *fragSrc =
    "void main() {\n"
    "  gl_FragColor = gl_Color;\n"
    "}\n";

  m_vertShader = glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB);
  glShaderSourceARB(m_vertShader, 1, &vertSrc, 0);
  glCompileShaderARB(m_vertShader);

  m_fragShader = glCreateShaderObjectARB(GL_FRAGMENT_SHADER_ARB);
  glShaderSourceARB(m_fragShader, 1, &fragSrc, 0);
  glCompileShaderARB(m_fragShader);

  m_program = glCreateProgramObjectARB();
  glAttachObjectARB(m_program, m_vertShader);
  glAttachObjectARB(m_program, m_fragShader);

  glBindAttribLocationARB(m_program, 1, "attrDir");
  glBindAttribLocationARB(m_program, 2, "attrPhase");
  glBindAttribLocationARB(m_program, 3, "attrTip");

  glLinkProgramARB(m_program);

  m_timeLoc = glGetUniformLocationARB(m_program, "time");
  m_lengthLoc = glGetUniformLocationARB(m_program, "hairLength");

  m_dirAttr = 1;
  m_phaseAttr = 2;
  m_tipAttr = 3;
#endif
}

Engine *HairEngine::clone() const
{
  HairEngine *engine = new HairEngine(parent());
  engine->setAlias(alias());
  engine->setEnabled(isEnabled());
  return engine;
}

#ifdef ENABLE_GLSL
HairEngine::~HairEngine()
{
  glDeleteObjectARB(m_program);
  glDeleteObjectARB(m_vertShader);
  glDeleteObjectARB(m_fragShader);
}
#else
HairEngine::~HairEngine()
{
}
#endif

void HairEngine::setMolecule(Molecule *mol)
{
  Engine::setMolecule(mol);
  m_hair.clear();
  if (!mol)
    return;
  foreach(const Atom *a, mol->atoms()) {
    QVector<HairStrand> hairs;
    for (int i = 0; i < 8; ++i) {
      HairStrand h;
      h.dir = Eigen::Vector3d::Random().normalized();
      h.phase = QRandomGenerator::global()->generateDouble() * 2.0 * M_PI;
      hairs.append(h);
    }
    m_hair.insert(a->id(), hairs);
  }
}

bool HairEngine::renderOpaque(PainterDevice *pd)
{
  if (!pd->molecule())
    return false;

  double t = m_timer.elapsed() / 1000.0;
#ifdef ENABLE_GLSL
  glUseProgramObjectARB(m_program);
  glUniform1fARB(m_timeLoc, static_cast<float>(t));
  glUniform1fARB(m_lengthLoc, static_cast<float>(m_hairLength));
#endif
  Color *map = colorMap();
  if (!map)
    map = pd->colorMap();

  foreach(const Atom *a, pd->molecule()->atoms()) {
    QVector<HairStrand> hairs = m_hair.value(a->id());
    Eigen::Vector3d base = *a->pos();
    map->setFromPrimitive(a);
    pd->painter()->setColor(map);

    foreach(const HairStrand &h, hairs) {
#ifdef ENABLE_GLSL
      glBegin(GL_LINES);
      glVertexAttrib3dvARB(m_dirAttr, h.dir.data());
      glVertexAttrib1fARB(m_phaseAttr, static_cast<float>(h.phase));
      glVertexAttrib1fARB(m_tipAttr, 0.0f);
      glVertex3dv(base.data());
      glVertexAttrib3dvARB(m_dirAttr, h.dir.data());
      glVertexAttrib1fARB(m_phaseAttr, static_cast<float>(h.phase));
      glVertexAttrib1fARB(m_tipAttr, 1.0f);
      glVertex3dv(base.data());
      glEnd();
#else
      Eigen::Vector3d swing = h.dir.cross(Eigen::Vector3d::UnitZ()).normalized();
      Eigen::Vector3d dir = h.dir + swing * 0.2 * qSin(t + h.phase);
      Eigen::Vector3d tip = base + dir.normalized() * m_hairLength;
      pd->painter()->drawLine(base, tip, 1.0);
#endif
    }
  }
#ifdef ENABLE_GLSL
  glUseProgramObjectARB(0);
#endif
  return true;
}

} // namespace Avogadro

