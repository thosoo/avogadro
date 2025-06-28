
/**********************************************************************
  HairEngine - Engine to render animated hair on atoms

  Copyright (C) 2025 OpenAI

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
#include "hairengine.h"

#include <avogadro/atom.h>
#include <avogadro/molecule.h>
#include <avogadro/painterdevice.h>
#include <avogadro/color.h>
#include <Eigen/Geometry>

#include <QRandomGenerator>
#include <QtMath>
#ifdef ENABLE_GLSL
#include <GL/glew.h>
#endif

namespace Avogadro {

HairEngine::HairEngine(QObject *parent)
  : Engine(parent), m_hairLength(1.0)
{
  m_timer.start();
#ifdef ENABLE_GLSL
  static const char *vertSrc =
    "uniform float time;\n"
    "uniform float hairLength;\n"
    "attribute vec3 basePos;\n"
    "attribute vec3 dir;\n"
    "attribute float phase;\n"
    "attribute float role;\n"
    "void main()\n"
    "{\n"
    "  vec3 swing = normalize(cross(dir, vec3(0.0,0.0,1.0)));\n"
    "  vec3 offset = swing * 0.2 * sin(time + phase);\n"
    "  vec3 tip = basePos + normalize(dir + offset) * hairLength;\n"
    "  vec3 pos = mix(basePos, tip, role);\n"
    "  gl_Position = gl_ModelViewProjectionMatrix * vec4(pos,1.0);\n"
    "}\n";

  static const char *fragSrc =
    "void main()\n"
    "{\n"
    "  gl_FragColor = gl_Color;\n"
    "}\n";

  GLuint vs = glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB);
  glShaderSourceARB(vs, 1, &vertSrc, 0);
  glCompileShaderARB(vs);
  GLuint fs = glCreateShaderObjectARB(GL_FRAGMENT_SHADER_ARB);
  glShaderSourceARB(fs, 1, &fragSrc, 0);
  glCompileShaderARB(fs);
  m_shader = glCreateProgramObjectARB();
  glAttachObjectARB(m_shader, vs);
  glAttachObjectARB(m_shader, fs);
  glLinkProgramARB(m_shader);
  glDetachObjectARB(m_shader, vs);
  glDetachObjectARB(m_shader, fs);
  glDeleteObjectARB(vs);
  glDeleteObjectARB(fs);

  m_timeLoc = glGetUniformLocationARB(m_shader, "time");
  m_lengthLoc = glGetUniformLocationARB(m_shader, "hairLength");
  m_baseAttr = glGetAttribLocationARB(m_shader, "basePos");
  m_dirAttr = glGetAttribLocationARB(m_shader, "dir");
  m_phaseAttr = glGetAttribLocationARB(m_shader, "phase");
  m_roleAttr = glGetAttribLocationARB(m_shader, "role");
#endif
}

Engine *HairEngine::clone() const
{
  HairEngine *engine = new HairEngine(parent());
  engine->setAlias(alias());
  engine->setEnabled(isEnabled());
  return engine;
}

HairEngine::~HairEngine()
{
#ifdef ENABLE_GLSL
  if (m_shader)
    glDeleteObjectARB(m_shader);
#endif
}

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
  Color *map = colorMap();
  if (!map)
    map = pd->colorMap();

#ifdef ENABLE_GLSL
  glUseProgramObjectARB(m_shader);
  glUniform1fARB(m_timeLoc, t);
  glUniform1fARB(m_lengthLoc, m_hairLength);
#endif

  foreach(const Atom *a, pd->molecule()->atoms()) {
    QVector<HairStrand> hairs = m_hair.value(a->id());
    Eigen::Vector3d base = *a->pos();
    map->setFromPrimitive(a);
    pd->painter()->setColor(map);

    foreach(const HairStrand &h, hairs) {
#ifdef ENABLE_GLSL
      glBegin(GL_LINES);
      glVertexAttrib3fARB(m_baseAttr, base.x(), base.y(), base.z());
      glVertexAttrib3fARB(m_dirAttr, h.dir.x(), h.dir.y(), h.dir.z());
      glVertexAttrib1fARB(m_phaseAttr, h.phase);
      glVertexAttrib1fARB(m_roleAttr, 0.0f);
      glVertex3f(base.x(), base.y(), base.z());
      glVertexAttrib1fARB(m_roleAttr, 1.0f);
      glVertex3f(base.x(), base.y(), base.z());
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

