/**********************************************************************
  Cylinder - OpenGL Cylinder drawing class.

  Copyright (C) 2006,2007 Benoit Jacob

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

#include "config.h"

#include "cylinder_p.h"

#include <QOpenGLContext>

#ifdef ENABLE_GLSL
  #include <GL/glew.h>
#endif

#include <QOpenGLWidget>

#include <Eigen/Geometry>

// Win32 build (19/05/08)
#include <math.h> 

// The bundled Mesa GL/gl.h only declares the GL 1.1 entry points this file
// already uses (glVertexPointer, glNewList, ...). The GL 1.5 buffer-binding
// call used by the defensive GL_ARRAY_BUFFER unbind below is not a static
// symbol in libGL, so load it through QOpenGLFunctions_1_5 (the same mechanism
// glpainter_p.cpp uses). A transient object is fine: initialize() runs from
// GLPainterPrivate::isValid(), i.e. during a draw with a context current.
#include <QtOpenGL/QOpenGLFunctions_1_5>
#ifndef M_PI
  #define M_PI 3.1415926535897932384626433832795 
#endif

using namespace Eigen;

namespace Avogadro {

  class CylinderPrivate {
    public:
      CylinderPrivate() : vertexBuffer(0), normalBuffer(0), displayList(0), isValid(false) {}

      /** Pointer to the buffer storing the vertex array */
      Eigen::Vector3f *vertexBuffer;
      /** Pointer to the buffer storing the normal array */
      Eigen::Vector3f *normalBuffer;
      /** The id of the OpenGL display list */
      GLuint displayList;
      /** Equals true if the vertex array has been correctly initialized */
      bool isValid;

      /** the number of faces of the cylinder. This only
       * includes the lateral faces, as the base and top faces (the
       * two discs) are not rendered. */
      int faces;
  };

  Cylinder::Cylinder(int faces) : d(new CylinderPrivate)
  {
    setup(faces);
  }

  Cylinder::~Cylinder()
  {
    freeBuffers();
    if( d->displayList ) {
      glDeleteLists( d->displayList, 1 );
    }
    delete d;
  }

  void Cylinder::freeBuffers()
  {
    if( d->normalBuffer )
    {
      delete [] d->normalBuffer;
      d->normalBuffer = 0;
    }
    if( d->vertexBuffer )
    {
      delete [] d->vertexBuffer;
      d->vertexBuffer = 0;
    }
  }

  void Cylinder::setup( int faces )
  {
    if( d->isValid && faces == d->faces ) return;
    d->faces = faces;
    initialize();
  }

  void Cylinder::initialize()
  {
    // Regenerate the geometry if it is missing or needs a different
    // tessellation. The vertex/normal buffers are RETAINED (not freed after
    // capture) so the immediate-mode draw path reads live memory rather than a
    // freed buffer. This removes the two defects that used to make the
    // axes-shaft vanish: (1) the display list captured glDrawArrays reading
    // d->vertexBuffer as client memory at EXECUTION time, then the buffers were
    // freed (use-after-free), and (2) the display list itself was created with
    // glGenLists, which is rejected (GL_INVALID_OPERATION) on a forward-
    // compatible Core profile. Immediate-mode emission needs neither.
    if( d->isValid && d->faces >= 0 )
      freeBuffers();
    d->isValid = false;
    if( d->faces < 0 ) return;

    if( d->faces < 3 )
    {
      // Degenerate cylinder: a single line segment from 0 to 1 along +Z.
      freeBuffers();
      d->vertexBuffer = new Vector3f[2];
      if( ! d->vertexBuffer ) return;
      d->vertexBuffer[0] = Vector3f( 0.0f, 0.0f, 0.0f );
      d->vertexBuffer[1] = Vector3f( 0.0f, 0.0f, 1.0f );
    }
    else
    {
      int vertexCount = 2 * d->faces + 2;
      freeBuffers();
      d->vertexBuffer = new Vector3f[vertexCount];
      if( ! d->vertexBuffer ) return;
      d->normalBuffer = new Vector3f[vertexCount];
      if( ! d->normalBuffer ) return;

      float baseAngle = 2 * M_PI / d->faces;
      // build vertex and normal buffers
      for( int i = 0; i <= d->faces; i++ )
      {
        float angle = baseAngle * i;
        Vector3f v( cosf(angle), sinf(angle), 0.0f );
        d->normalBuffer[ 2 * i ] = v;
        d->normalBuffer[ 2 * i + 1 ] = v;
        d->vertexBuffer[ 2 * i ] = v;
        d->vertexBuffer[ 2 * i + 1 ] = v;
        d->vertexBuffer[ 2 * i ].z() = 1.0f;
      }
    }
    d->isValid = true;
  }

  void Cylinder::drawImmediate() const
  {
    if( ! d->vertexBuffer )
      return;

    if( d->faces < 3 )
    {
      // Degenerate cylinder: a single line segment from 0 to 1 along +Z.
      glBegin( GL_LINES );
      glVertex3d( 0.0, 0.0, 0.0 );
      glVertex3d( 0.0, 0.0, 1.0 );
      glEnd();
      return;
    }

    glBegin( GL_QUAD_STRIP );
    for( int i = 0; i <= d->faces; ++i )
    {
      const Eigen::Vector3f &n0 = d->normalBuffer[ 2 * i ];
      const Eigen::Vector3f &v0 = d->vertexBuffer[ 2 * i ];
      const Eigen::Vector3f &n1 = d->normalBuffer[ 2 * i + 1 ];
      const Eigen::Vector3f &v1 = d->vertexBuffer[ 2 * i + 1 ];
      glNormal3f( n0.x(), n0.y(), n0.z() );
      glVertex3d( v0.x(), v0.y(), v0.z() );
      glNormal3f( n1.x(), n1.y(), n1.z() );
      glVertex3d( v1.x(), v1.y(), v1.z() );
    }
    glEnd();
  }

  void Cylinder::draw( const Eigen::Vector3d &end1, const Eigen::Vector3d &end2,
      double radius ) const
  {
    // Ensure the geometry buffers are available (see initialize()). draw() is
    // const (the painter calls it on a const Cylinder), so cast away const for
    // the one-time lazy setup; the buffers are only written here, never during
    // the draw itself.
    if( ! d->isValid )
      const_cast<Cylinder*>(this)->initialize();

    // construct the 4D transformation matrix
    Eigen::Matrix4d matrix;
    matrix.row(3) << 0, 0, 0, 1;
    matrix.block<3,1>(0,2) = end2 - end1; // the axis

    // construct an orthogonal basis whose first vector is the axis, and whose other vectors
    // have norm equal to 'radius'.
    Vector3d axisNormalized = matrix.block<3,1>(0,2).normalized();
    matrix.block<3,1>(0,0) = axisNormalized.unitOrthogonal() * radius;
    matrix.block<3,1>(0,1) = axisNormalized.cross(matrix.block<3,1>(0,0));
    matrix.block<3,1>(0,3) = end1;

    // Emit the lateral surface in immediate mode (works on every GL profile,
    // including forward-compatible Core where glGenLists is rejected).
    glPushMatrix();
    glMultMatrixd( matrix.data() );
    drawImmediate();
    glPopMatrix();
  }

  void Cylinder::drawMulti( const Eigen::Vector3d &end1, const Eigen::Vector3d &end2,
      double radius, int order, double shift,
      const Eigen::Vector3d &planeNormalVector ) const
  {
    // Ensure the geometry buffers are available (see initialize()).
    if( ! d->isValid )
      const_cast<Cylinder*>(this)->initialize();

    // construct the 4D transformation matrix
    Eigen::Matrix4d matrix;
    matrix.row(3) << 0,0,0,1;
    matrix.block<3,1>(0,3) = end1;
    matrix.block<3,1>(0,2) = end2 - end1; // the "axis vector" of the line
    // Now we want to construct an orthonormal basis whose third
    // vector is axis.normalized(). The first vector in this
    // basis, which we call ortho1, should be approximately lying in the
    // z=0 plane if possible. This is to ensure double bonds don't look
    // like single bonds from the default point of view.
    Eigen::Vector3d axisNormalized = matrix.block<3,1>(0,2).normalized();
    Eigen::Block<Eigen::Matrix4d, 3, 1> ortho1(matrix, 0, 0);
    ortho1 = axisNormalized.cross(planeNormalVector);
    double ortho1Norm = ortho1.norm();
    if( ortho1Norm > 0.001 ) ortho1 = ortho1.normalized() * radius;
    else ortho1 = axisNormalized.unitOrthogonal() * radius;
    matrix.block<3,1>(0,1) = axisNormalized.cross(ortho1);

    // now the matrix is entirely filled, so we can do the actual drawing !
    glPushMatrix();
    glMultMatrixd( matrix.data() );
    if( order == 1 )
    {
      drawImmediate();
    }
    else
    {
      double angleOffset = 0.0;
      if( order >= 3 )
      {
        if( order == 3 ) angleOffset = 90.0;
        else angleOffset = 22.5;
      }

      double displacementFactor = shift / radius;
      for( int i = 0; i < order; i++)
      {
        glPushMatrix();
        glRotated( angleOffset + 360.0 * i / order,
            0.0, 0.0, 1.0 );
        glTranslated( displacementFactor, 0.0, 0.0 );
        drawImmediate();
        glPopMatrix();
      }
    }
    glPopMatrix();
  }

}
