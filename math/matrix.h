#pragma once

#include "vector.h"

#ifdef ENABLE_SDL
class Matrix3D;
class D3DXMATRIX {
public:
   union {
      struct {
         float        _11, _12, _13, _14;
         float        _21, _22, _23, _24;
         float        _31, _32, _33, _34;
         float        _41, _42, _43, _44;

      };
      float m[4][4];
   };
   D3DXMATRIX();
   D3DXMATRIX(const D3DXMATRIX &input);
   D3DXMATRIX(const D3DXMATRIX *input);
   D3DXMATRIX(const Matrix3D &input);
};

#define D3DMATRIX D3DXMATRIX
class vec4 {
public:
   union {
      struct {
         float        x, y, z, w;
      };
      struct {
         float        r, g, b, a;
      };
      float v[4];
   };
   vec4(float x, float y, float z, float w);
   vec4();
   static vec4 normal(vec4 &input);
   static float dot(vec4 &a, vec4 &b);
   vec4 operator+ (const vec4& m) const;
   vec4 operator- (const vec4& m) const;
};

typedef vec4 D3DXPLANE;

class vec3 {
public:
   union {
      struct {
         float        x, y, z;
      };
      struct {
         float        r, g, b;
      };
      float v[3];
   };

   vec3(float x, float y, float z);
   vec3();
   static vec3 normal(vec3 &input);
   static vec3 cross(vec3 &a, vec3 &b);
   static float dot(vec3 &a, vec3 &b);
   static vec3 TransformCoord(vec3 vec, Matrix3D mat);
   vec3 operator+ (const vec3& m) const;
   vec3 operator- (const vec3& m) const;

};

#else
typedef class D3DXVECTOR4 vec4;
typedef class D3DXVECTOR3 vec3;
#endif

// 3x3 matrix for representing linear transformation of 3D vectors
class Matrix3
{
public:
   union {
      struct {
         float _11, _12, _13;
         float _21, _22, _23;
         float _31, _32, _33;
      };
      float m_d[3][3];
   };

   Matrix3();
   Matrix3(float _11, float _12, float _13, float _21, float _22, float _23,  float _31, float _32, float _33);
   void scaleX(const float factor);
   void scaleY(const float factor);
   void scaleZ(const float factor);
   void CreateSkewSymmetric(const Vertex3Ds &pv3D);
   void MultiplyScalar(const float scalar);
   Matrix3 Matrix3::operator* (const Matrix3& v) const;
   Matrix3 Matrix3::operator+ (const Matrix3& v) const;
   template <class VecType>
   Vertex3Ds operator* (const VecType& v) const
   {
      return Vertex3Ds(
         m_d[0][0] * v.x + m_d[0][1] * v.y + m_d[0][2] * v.z,
         m_d[1][0] * v.x + m_d[1][1] * v.y + m_d[1][2] * v.z,
         m_d[2][0] * v.x + m_d[2][1] * v.y + m_d[2][2] * v.z);
   }

   template <class VecType>
   Vertex3Ds MultiplyVector(const VecType& v) const
   {
      return (*this) * v;
   }

   // multiply vector with matrix transpose
   template <class VecType>
   Vertex3Ds MultiplyVectorT(const VecType& v) const
   {
      return Vertex3Ds(
         m_d[0][0] * v.x + m_d[1][0] * v.y + m_d[2][0] * v.z,
         m_d[0][1] * v.x + m_d[1][1] * v.y + m_d[2][1] * v.z,
         m_d[0][2] * v.x + m_d[1][2] * v.y + m_d[2][2] * v.z);
   }
   void MultiplyMatrix(const Matrix3 * const pmat1, const Matrix3 * const pmat2);
   void AddMatrix(const Matrix3 * const pmat1, const Matrix3 * const pmat2);
   void OrthoNormalize();
   void Transpose(Matrix3 * const pmatOut) const;
   void Identity(const float value = 1.0f);
   void RotationAroundAxis(const Vertex3Ds& axis, const float angle);
   void RotationAroundAxis(const Vertex3Ds& axis, const float rsin, const float rcos);
};


////////////////////////////////////////////////////////////////////////////////


// 4x4 matrix for representing affine transformations of 3D vectors
class Matrix3D : public D3DMATRIX
{
public:
   Matrix3D();
   Matrix3D(const float Scale);
   Matrix3D(float _11, float _12, float _13, float _14, float _21, float _22, float _23, float _24, float _31, float _32, float _33, float _34, float _41, float _42, float _43, float _44);

   // premultiply the given matrix, i.e., result = mult * (*this)
   void Multiply(const Matrix3D &mult, Matrix3D &result) const;
   void SetTranslation(const float tx, const float ty, const float tz);
   void SetTranslation(const Vertex3Ds& t);
   void SetScaling(const float sx, const float sy, const float sz);
   void RotateXMatrix(const float x);
   void RotateYMatrix(const float y);
   void RotateZMatrix(const float z);
   void SetIdentity();
   void Scale(const float x, const float y, const float z);
   // extract the matrix corresponding to the 3x3 rotation part
   void GetRotationPart(Matrix3D& rot);
   // generic multiply function for everything that has .x, .y and .z
   template <class VecIn, class VecOut>
   void MultiplyVector(const VecIn& vIn, VecOut& vOut) const
   {
      // Transform it through the current matrix set
      const float xp = _11 * vIn.x + _21 * vIn.y + _31 * vIn.z + _41;
      const float yp = _12 * vIn.x + _22 * vIn.y + _32 * vIn.z + _42;
      const float zp = _13 * vIn.x + _23 * vIn.y + _33 * vIn.z + _43;
      const float wp = _14 * vIn.x + _24 * vIn.y + _34 * vIn.z + _44;

      const float inv_wp = 1.0f / wp;
      vOut.x = xp * inv_wp;
      vOut.y = yp * inv_wp;
      vOut.z = zp * inv_wp;
   }
   Vertex3Ds MultiplyVector(const Vertex3Ds &v) const;
   Vertex3Ds MultiplyVectorNoTranslate(const Vertex3Ds &v) const;
   template <class VecIn, class VecOut>
   void MultiplyVectorNoTranslate(const VecIn& vIn, VecOut& vOut) const
   {
      // Transform it through the current matrix set
      const float xp = _11 * vIn.x + _21 * vIn.y + _31 * vIn.z;
      const float yp = _12 * vIn.x + _22 * vIn.y + _32 * vIn.z;
      const float zp = _13 * vIn.x + _23 * vIn.y + _33 * vIn.z;

      vOut.x = xp;
      vOut.y = yp;
      vOut.z = zp;
   }

   template <class VecIn, class VecOut>
   void MultiplyVectorNoTranslateNormal(const VecIn& vIn, VecOut& vOut) const
   {
      // Transform it through the current matrix set
      const float xp = _11 * vIn.nx + _21 * vIn.ny + _31 * vIn.nz;
      const float yp = _12 * vIn.nx + _22 * vIn.ny + _32 * vIn.nz;
      const float zp = _13 * vIn.nx + _23 * vIn.ny + _33 * vIn.nz;

      vOut.x = xp;
      vOut.y = yp;
      vOut.z = zp;
   }
   void Invert();
   void Transpose();
   Matrix3D operator+ (const Matrix3D& m) const;
   Matrix3D operator* (const Matrix3D& m) const;

   static Matrix3D MatrixLookAtLH(vec3 &eye,vec3 &at,vec3 &up);
   static Matrix3D MatrixPerspectiveFovLH(float fovy, float aspect, float zn, float zf);
   static Matrix3D MatrixPerspectiveOffCenterLH(float l, float r, float b, float t, float zn, float zf);
   static Matrix3D MatrixRotationYawPitchRoll(float yaw, float pitch, float roll);
};
