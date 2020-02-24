#include "stdafx.h"
#include "matrix.h"

//Matrix3D ---------------------------------------------------------------------------------------------------------------
void Matrix3D::Invert()
{
   int ipvt[4] = { 0, 1, 2, 3 };

   for (int k = 0; k < 4; ++k)
   {
      float temp = 0.f;
      int l = k;
      for (int i = k; i < 4; ++i)
      {
         const float d = fabsf(m[k][i]);
         if (d > temp)
         {
            temp = d;
            l = i;
         }
      }
      if (l != k)
      {
         const int tmp = ipvt[k];
         ipvt[k] = ipvt[l];
         ipvt[l] = tmp;
         for (int j = 0; j < 4; ++j)
         {
            temp = m[j][k];
            m[j][k] = m[j][l];
            m[j][l] = temp;
         }
      }
      const float d = 1.0f / m[k][k];
      for (int j = 0; j < k; ++j)
      {
         const float c = m[j][k] * d;
         for (int i = 0; i < 4; ++i)
            m[j][i] -= m[k][i] * c;
         m[j][k] = c;
      }
      for (int j = k + 1; j < 4; ++j)
      {
         const float c = m[j][k] * d;
         for (int i = 0; i < 4; ++i)
            m[j][i] -= m[k][i] * c;
         m[j][k] = c;
      }
      for (int i = 0; i < 4; ++i)
         m[k][i] = -m[k][i] * d;
      m[k][k] = d;
   }

   Matrix3D mat3D;
   mat3D.m[ipvt[0]][0] = m[0][0]; mat3D.m[ipvt[0]][1] = m[0][1]; mat3D.m[ipvt[0]][2] = m[0][2]; mat3D.m[ipvt[0]][3] = m[0][3];
   mat3D.m[ipvt[1]][0] = m[1][0]; mat3D.m[ipvt[1]][1] = m[1][1]; mat3D.m[ipvt[1]][2] = m[1][2]; mat3D.m[ipvt[1]][3] = m[1][3];
   mat3D.m[ipvt[2]][0] = m[2][0]; mat3D.m[ipvt[2]][1] = m[2][1]; mat3D.m[ipvt[2]][2] = m[2][2]; mat3D.m[ipvt[2]][3] = m[2][3];
   mat3D.m[ipvt[3]][0] = m[3][0]; mat3D.m[ipvt[3]][1] = m[3][1]; mat3D.m[ipvt[3]][2] = m[3][2]; mat3D.m[ipvt[3]][3] = m[3][3];

   m[0][0] = mat3D.m[0][0]; m[0][1] = mat3D.m[0][1]; m[0][2] = mat3D.m[0][2]; m[0][3] = mat3D.m[0][3];
   m[1][0] = mat3D.m[1][0]; m[1][1] = mat3D.m[1][1]; m[1][2] = mat3D.m[1][2]; m[1][3] = mat3D.m[1][3];
   m[2][0] = mat3D.m[2][0]; m[2][1] = mat3D.m[2][1]; m[2][2] = mat3D.m[2][2]; m[2][3] = mat3D.m[2][3];
   m[3][0] = mat3D.m[3][0]; m[3][1] = mat3D.m[3][1]; m[3][2] = mat3D.m[3][2]; m[3][3] = mat3D.m[3][3];
}

void RotateAround(const Vertex3Ds &pvAxis, Vertex3D_NoTex2 * const pvPoint, const int count, const float angle)
{
   Matrix3 mat;
   mat.RotationAroundAxis(pvAxis, angle);

   for (int i = 0; i < count; ++i)
   {
      const float result[3] = {
         mat.m_d[0][0] * pvPoint[i].x + mat.m_d[0][1] * pvPoint[i].y + mat.m_d[0][2] * pvPoint[i].z,
         mat.m_d[1][0] * pvPoint[i].x + mat.m_d[1][1] * pvPoint[i].y + mat.m_d[1][2] * pvPoint[i].z,
         mat.m_d[2][0] * pvPoint[i].x + mat.m_d[2][1] * pvPoint[i].y + mat.m_d[2][2] * pvPoint[i].z };

      pvPoint[i].x = result[0];
      pvPoint[i].y = result[1];
      pvPoint[i].z = result[2];

      const float resultn[3] = {
         mat.m_d[0][0] * pvPoint[i].nx + mat.m_d[0][1] * pvPoint[i].ny + mat.m_d[0][2] * pvPoint[i].nz,
         mat.m_d[1][0] * pvPoint[i].nx + mat.m_d[1][1] * pvPoint[i].ny + mat.m_d[1][2] * pvPoint[i].nz,
         mat.m_d[2][0] * pvPoint[i].nx + mat.m_d[2][1] * pvPoint[i].ny + mat.m_d[2][2] * pvPoint[i].nz };

      pvPoint[i].nx = resultn[0];
      pvPoint[i].ny = resultn[1];
      pvPoint[i].nz = resultn[2];
   }
}

void RotateAround(const Vertex3Ds &pvAxis, Vertex3Ds * const pvPoint, const int count, const float angle)
{
   Matrix3 mat;
   mat.RotationAroundAxis(pvAxis, angle);

   for (int i = 0; i < count; ++i)
   {
      const float result[3] = {
         mat.m_d[0][0] * pvPoint[i].x + mat.m_d[0][1] * pvPoint[i].y + mat.m_d[0][2] * pvPoint[i].z,
         mat.m_d[1][0] * pvPoint[i].x + mat.m_d[1][1] * pvPoint[i].y + mat.m_d[1][2] * pvPoint[i].z,
         mat.m_d[2][0] * pvPoint[i].x + mat.m_d[2][1] * pvPoint[i].y + mat.m_d[2][2] * pvPoint[i].z };

      pvPoint[i].x = result[0];
      pvPoint[i].y = result[1];
      pvPoint[i].z = result[2];
   }
}

Vertex3Ds RotateAround(const Vertex3Ds &pvAxis, const Vertex2D &pvPoint, const float angle)
{
   const float rsin = sinf(angle);
   const float rcos = cosf(angle);

   // Matrix for rotating around an arbitrary vector

   float matrix[3][2];
   matrix[0][0] = pvAxis.x*pvAxis.x + rcos * (1.0f - pvAxis.x*pvAxis.x);
   matrix[1][0] = pvAxis.x*pvAxis.y*(1.0f - rcos) - pvAxis.z*rsin;
   matrix[2][0] = pvAxis.z*pvAxis.x*(1.0f - rcos) + pvAxis.y*rsin;

   matrix[0][1] = pvAxis.x*pvAxis.y*(1.0f - rcos) + pvAxis.z*rsin;
   matrix[1][1] = pvAxis.y*pvAxis.y + rcos * (1.0f - pvAxis.y*pvAxis.y);
   matrix[2][1] = pvAxis.y*pvAxis.z*(1.0f - rcos) - pvAxis.x*rsin;

   return Vertex3Ds(matrix[0][0] * pvPoint.x + matrix[0][1] * pvPoint.y,
      matrix[1][0] * pvPoint.x + matrix[1][1] * pvPoint.y,
      matrix[2][0] * pvPoint.x + matrix[2][1] * pvPoint.y);
}

Matrix3D::Matrix3D(const float Scale) { 
   SetScaling(Scale, Scale, Scale); 
}

Matrix3D::Matrix3D() {

}

Matrix3D::Matrix3D(float _11, float _12, float _13, float _14, float _21, float _22, float _23, float _24, float _31, float _32, float _33, float _34, float _41, float _42, float _43, float _44) {
   this->_11 = _11; this->_12 = _12; this->_13 = _13; this->_14 = _14;
   this->_21 = _21; this->_22 = _22; this->_23 = _23; this->_24 = _24;
   this->_31 = _31; this->_32 = _32; this->_33 = _33; this->_34 = _34;
   this->_41 = _41; this->_42 = _42; this->_43 = _43; this->_44 = _44;
}

// premultiply the given matrix, i.e., result = mult * (*this)
void Matrix3D::Multiply(const Matrix3D &mult, Matrix3D &result) const
{
   Matrix3D matrixT;
   for (int i = 0; i < 4; ++i)
   {
      for (int l = 0; l < 4; ++l)
      {
         matrixT.m[i][l] = (m[0][l] * mult.m[i][0]) + (m[1][l] * mult.m[i][1]) +
            (m[2][l] * mult.m[i][2]) + (m[3][l] * mult.m[i][3]);
      }
   }
   result = matrixT;
}

Matrix3D Matrix3D::operator+ (const Matrix3D& m) const
{
   return Matrix3D(_11 + m._11, _12 + m._12, _13 + m._13, _14 + m._14,
                   _21 + m._21, _22 + m._22, _23 + m._23, _24 + m._24,
                   _31 + m._31, _32 + m._32, _33 + m._33, _34 + m._34,
                   _41 + m._41, _42 + m._42, _43 + m._43, _44 + m._44);

}

Matrix3D Matrix3D::operator* (const Matrix3D& mult) const
{
   Matrix3D matrixT;
   for (int i = 0; i < 4; ++i)
   {
      for (int l = 0; l < 4; ++l)
      {
         matrixT.m[i][l] = (mult.m[0][l] * m[i][0]) + (mult.m[1][l] * m[i][1]) +
            (mult.m[2][l] * m[i][2]) + (mult.m[3][l] * m[i][3]);
      }
   }
   return matrixT;
   /*
   return Matrix3D(
      (this->m[0][0] * m.m[0][0]) + (this->m[1][0] * m.m[0][1]) + (this->m[2][0] * m.m[0][2]) + (this->m[3][0] * m.m[0][3]),
      (this->m[0][0] * m.m[1][0]) + (this->m[1][0] * m.m[1][1]) + (this->m[2][0] * m.m[1][2]) + (this->m[3][0] * m.m[1][3]),
      (this->m[0][0] * m.m[2][0]) + (this->m[1][0] * m.m[2][1]) + (this->m[2][0] * m.m[2][2]) + (this->m[3][0] * m.m[2][3]),
      (this->m[0][0] * m.m[3][0]) + (this->m[1][0] * m.m[3][1]) + (this->m[2][0] * m.m[3][2]) + (this->m[3][0] * m.m[3][3]),
      (this->m[0][1] * m.m[0][0]) + (this->m[1][1] * m.m[0][1]) + (this->m[2][1] * m.m[0][2]) + (this->m[3][1] * m.m[0][3]),
      (this->m[0][1] * m.m[1][0]) + (this->m[1][1] * m.m[1][1]) + (this->m[2][1] * m.m[1][2]) + (this->m[3][1] * m.m[1][3]),
      (this->m[0][1] * m.m[2][0]) + (this->m[1][1] * m.m[2][1]) + (this->m[2][1] * m.m[2][2]) + (this->m[3][1] * m.m[2][3]),
      (this->m[0][1] * m.m[3][0]) + (this->m[1][1] * m.m[3][1]) + (this->m[2][1] * m.m[3][2]) + (this->m[3][1] * m.m[3][3]),
      (this->m[0][2] * m.m[0][0]) + (this->m[1][2] * m.m[0][1]) + (this->m[2][2] * m.m[0][2]) + (this->m[3][2] * m.m[0][3]),
      (this->m[0][2] * m.m[1][0]) + (this->m[1][2] * m.m[1][1]) + (this->m[2][2] * m.m[1][2]) + (this->m[3][2] * m.m[1][3]),
      (this->m[0][2] * m.m[2][0]) + (this->m[1][2] * m.m[2][1]) + (this->m[2][2] * m.m[2][2]) + (this->m[3][2] * m.m[2][3]),
      (this->m[0][2] * m.m[3][0]) + (this->m[1][2] * m.m[3][1]) + (this->m[2][2] * m.m[3][2]) + (this->m[3][2] * m.m[3][3]),
      (this->m[0][3] * m.m[0][0]) + (this->m[1][3] * m.m[0][1]) + (this->m[2][3] * m.m[0][2]) + (this->m[3][3] * m.m[0][3]),
      (this->m[0][3] * m.m[1][0]) + (this->m[1][3] * m.m[1][1]) + (this->m[2][3] * m.m[1][2]) + (this->m[3][3] * m.m[1][3]),
      (this->m[0][3] * m.m[2][0]) + (this->m[1][3] * m.m[2][1]) + (this->m[2][3] * m.m[2][2]) + (this->m[3][3] * m.m[2][3]),
      (this->m[0][3] * m.m[3][0]) + (this->m[1][3] * m.m[3][1]) + (this->m[2][3] * m.m[3][2]) + (this->m[3][3] * m.m[3][3])
      );*/
}

void Matrix3D::SetTranslation(const float tx, const float ty, const float tz)
{
   SetIdentity();
   _41 = tx;
   _42 = ty;
   _43 = tz;
}
void Matrix3D::SetTranslation(const Vertex3Ds& t)
{
   SetTranslation(t.x, t.y, t.z);
}

void Matrix3D::SetScaling(const float sx, const float sy, const float sz)
{
   SetIdentity();
   _11 = sx;
   _22 = sy;
   _33 = sz;
}

void Matrix3D::RotateXMatrix(const float x)
{
   SetIdentity();
   _22 = _33 = cosf(x);
   _23 = sinf(x);
   _32 = -_23;
}

void Matrix3D::RotateYMatrix(const float y)
{
   SetIdentity();
   _11 = _33 = cosf(y);
   _31 = sinf(y);
   _13 = -_31;
}

void Matrix3D::RotateZMatrix(const float z)
{
   SetIdentity();
   _11 = _22 = cosf(z);
   _12 = sinf(z);
   _21 = -_12;
}

void Matrix3D::SetIdentity()
{
   _11 = _22 = _33 = _44 = 1.0f;
   _12 = _13 = _14 = _41 =
      _21 = _23 = _24 = _42 =
      _31 = _32 = _34 = _43 = 0.0f;
}
void Matrix3D::Scale(const float x, const float y, const float z)
{
   _11 *= x; _12 *= x; _13 *= x;
   _21 *= y; _22 *= y; _23 *= y;
   _31 *= z; _32 *= z; _33 *= z;
}

// extract the matrix corresponding to the 3x3 rotation part
void Matrix3D::GetRotationPart(Matrix3D& rot)
{
   rot._11 = _11; rot._12 = _12; rot._13 = _13; rot._14 = 0.0f;
   rot._21 = _21; rot._22 = _22; rot._23 = _23; rot._24 = 0.0f;
   rot._31 = _31; rot._32 = _32; rot._33 = _33; rot._34 = 0.0f;
   rot._41 = rot._42 = rot._43 = 0.0f; rot._44 = 1.0f;
}

Vertex3Ds Matrix3D::MulVector(const Vertex3Ds &v) const
{
   // Transform it through the current matrix set
   const float xp = _11 * v.x + _21 * v.y + _31 * v.z + _41;
   const float yp = _12 * v.x + _22 * v.y + _32 * v.z + _42;
   const float zp = _13 * v.x + _23 * v.y + _33 * v.z + _43;
   const float wp = _14 * v.x + _24 * v.y + _34 * v.z + _44;

   const float inv_wp = 1.0f / wp;
   return Vertex3Ds(xp*inv_wp, yp*inv_wp, zp*inv_wp);
}

Vertex3Ds Matrix3D::MultiplyVectorNoTranslate(const Vertex3Ds &v) const
{
   // Transform it through the current matrix set
   const float xp = _11 * v.x + _21 * v.y + _31 * v.z;
   const float yp = _12 * v.x + _22 * v.y + _32 * v.z;
   const float zp = _13 * v.x + _23 * v.y + _33 * v.z;

   return Vertex3Ds(xp, yp, zp);
}

void Matrix3D::Transpose()
{
   Matrix3D tmp;
   for (int i = 0; i < 4; ++i)
   {
      tmp.m[0][i] = m[i][0];
      tmp.m[1][i] = m[i][1];
      tmp.m[2][i] = m[i][2];
      tmp.m[3][i] = m[i][3];
   }
   *this = tmp;
}

Matrix3D Matrix3D::MatrixLookAtLH(vec3 &eye, vec3 &at, vec3 &up) {
#ifdef ENABLE_SDL
   vec3 zaxis = vec3::normal(at - eye);
   vec3 xaxis = vec3::normal(vec3::cross(up, zaxis));
   vec3 yaxis = vec3::cross(zaxis, xaxis);
   float dotX = vec3::dot(xaxis, eye);
   float dotY = vec3::dot(yaxis, eye);
   float dotZ = vec3::dot(zaxis, eye);
#else
   vec3 xaxis, yaxis, zaxis;
   vec3 a_e = vec3(at - eye);
   D3DXVec3Normalize(&zaxis, &a_e);
   D3DXVec3Cross(&xaxis, &up, &zaxis);
   D3DXVec3Normalize(&xaxis, &xaxis);
   D3DXVec3Cross(&yaxis, &zaxis, &zaxis);
   float dotX = D3DXVec3Dot(&xaxis, &eye);
   float dotY = D3DXVec3Dot(&yaxis, &eye);
   float dotZ = D3DXVec3Dot(&zaxis, &eye);
#endif
   return Matrix3D(
      xaxis.x, yaxis.x, zaxis.x, 0,
      xaxis.y, yaxis.y, zaxis.x, 0,
      xaxis.z, yaxis.z, zaxis.z, 0,
      -dotX, -dotY, -dotZ, 1
   );

}

Matrix3D Matrix3D::MatrixPerspectiveFovLH(float fovy, float aspect, float zn, float zf) {
   float yScale = 1.0f / tanf(fovy / 2.0f);
   float xScale = yScale / aspect;
   return Matrix3D(
      xScale, 0.0f, 0.0f, 0.0f,
      0.0f, yScale, 0.0f, 0.0f,
      0.0f, 0.0f, zf / (zf - zn), 1.0f,
      0.0f, 0.0f, -zn * zf / (zf - zn), 0.0f
   );
}

Matrix3D Matrix3D::MatrixPerspectiveOffCenterLH(float l, float r, float b, float t, float zn, float zf) {
   return Matrix3D(
      2.0f * zn / (r - l), 0.0f, 0.0f, 0.0f,
      0.0f, 2.0f * zn / (t - b), 0.0f, 0.0f,
      (l + r) / (l - r), (t + b) / (b - t), zf / (zf - zn), 1.0f,
      0.0f, 0.0f, -zn * zf / (zf - zn), 0.0f
   );
}

Matrix3D Matrix3D::MatrixRotationYawPitchRoll(float yaw, float pitch, float roll) {
   float sr = sin(roll);
   float cr = cos(roll);
   float sp = sin(pitch);
   float cp = cos(pitch);
   float sy = sin(yaw);
   float cy = cos(yaw);
   //This code should be validated!
   return Matrix3D(
      cr*cy, sr, cr*sy, 0.0f,
      -sr * cp*sy - sp * sy, cr*cp, sr*cp*sy + sp * cy, 0.0f,
      - sr * sp*cy - cp * sy, -cr * sp, -sr * sp*sy + cp * cy, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f
   );
}


//Matrix3 ----------------------------------------------------------------------------------------------------------------

Matrix3::Matrix3()
{
}

Matrix3::Matrix3(float _11, float _12, float _13, float _21, float _22, float _23, float _31, float _32, float _33)
{
   this->_11 = _11; this->_12 = _12; this->_13 = _13;
   this->_21 = _21; this->_22 = _22; this->_23 = _23;
   this->_31 = _31; this->_32 = _32; this->_33 = _33;
}


void Matrix3::scaleX(const float factor)
{
   m_d[0][0] *= factor;
}
void Matrix3::scaleY(const float factor)
{
   m_d[1][1] *= factor;
}
void Matrix3::scaleZ(const float factor)
{
   m_d[2][2] *= factor;
}

void Matrix3::SkewSymmetric(const Vertex3Ds &pv3D)
{
   m_d[0][0] = 0; m_d[0][1] = -pv3D.z; m_d[0][2] = pv3D.y;
   m_d[1][0] = pv3D.z; m_d[1][1] = 0; m_d[1][2] = -pv3D.x;
   m_d[2][0] = -pv3D.y; m_d[2][1] = pv3D.x; m_d[2][2] = 0;
}

void Matrix3::MulScalar(const float scalar)
{
   for (int i = 0; i < 3; ++i)
      for (int l = 0; l < 3; ++l)
         m_d[i][l] *= scalar;
}

Matrix3 Matrix3::operator+ (const Matrix3& m) const
{
   return Matrix3(_11 + m._11, _12 + m._12, _13 + m._13,
      _21 + m._21, _22 + m._22, _23 + m._23,
      _31 + m._31, _32 + m._32, _33 + m._33);

}

Matrix3 Matrix3::operator* (const Matrix3& m) const
{
   return Matrix3(
      (this->m_d[0][0] * m.m_d[0][0]) + (this->m_d[1][0] * m.m_d[0][1]) + (this->m_d[2][0] * m.m_d[0][2]),
      (this->m_d[0][0] * m.m_d[1][0]) + (this->m_d[1][0] * m.m_d[1][1]) + (this->m_d[2][0] * m.m_d[1][2]),
      (this->m_d[0][0] * m.m_d[2][0]) + (this->m_d[1][0] * m.m_d[2][1]) + (this->m_d[2][0] * m.m_d[2][2]),
      (this->m_d[0][1] * m.m_d[0][0]) + (this->m_d[1][1] * m.m_d[0][1]) + (this->m_d[2][1] * m.m_d[0][2]),
      (this->m_d[0][1] * m.m_d[1][0]) + (this->m_d[1][1] * m.m_d[1][1]) + (this->m_d[2][1] * m.m_d[1][2]),
      (this->m_d[0][1] * m.m_d[2][0]) + (this->m_d[1][1] * m.m_d[2][1]) + (this->m_d[2][1] * m.m_d[2][2]),
      (this->m_d[0][2] * m.m_d[0][0]) + (this->m_d[1][2] * m.m_d[0][1]) + (this->m_d[2][2] * m.m_d[0][2]),
      (this->m_d[0][2] * m.m_d[1][0]) + (this->m_d[1][2] * m.m_d[1][1]) + (this->m_d[2][2] * m.m_d[1][2]),
      (this->m_d[0][2] * m.m_d[2][0]) + (this->m_d[1][2] * m.m_d[2][1]) + (this->m_d[2][2] * m.m_d[2][2])
   );
}

void Matrix3::MulMatrices(const Matrix3& pmat1, const Matrix3& pmat2)
{
   Matrix3 matans;
   for (int i = 0; i < 3; ++i)
      for (int l = 0; l < 3; ++l)
         matans.m_d[i][l] = pmat1.m_d[i][0] * pmat2.m_d[0][l] +
         pmat1.m_d[i][1] * pmat2.m_d[1][l] +
         pmat1.m_d[i][2] * pmat2.m_d[2][l];
   *this = matans;
}

void Matrix3::MulMatricesAndMulScalar(const Matrix3& pmat1, const Matrix3& pmat2, const float scalar)
{
   Matrix3 matans;
   for (int i = 0; i < 3; ++i)
      for (int l = 0; l < 3; ++l)
         matans.m_d[i][l] = (pmat1.m_d[i][0] * pmat2.m_d[0][l] +
            pmat1.m_d[i][1] * pmat2.m_d[1][l] +
            pmat1.m_d[i][2] * pmat2.m_d[2][l])*scalar;
   *this = matans;
}

void Matrix3::AddMatrix(const Matrix3& pmat)
{
   for (int i = 0; i < 3; ++i)
      for (int l = 0; l < 3; ++l)
         m_d[i][l] += pmat.m_d[i][l];
}

void Matrix3::OrthoNormalize()
{
   Vertex3Ds vX(m_d[0][0], m_d[1][0], m_d[2][0]);
   Vertex3Ds vY(m_d[0][1], m_d[1][1], m_d[2][1]);
   Vertex3Ds vZ = CrossProduct(vX, vY);
   vX.Normalize();
   vZ.Normalize();
   vY = CrossProduct(vZ, vX);
   //vY.Normalize(); // not needed

   m_d[0][0] = vX.x; m_d[0][1] = vY.x; m_d[0][2] = vZ.x;
   m_d[1][0] = vX.y; m_d[1][1] = vY.y; m_d[1][2] = vZ.y;
   m_d[2][0] = vX.z; m_d[2][1] = vY.z; m_d[2][2] = vZ.z;
}

/*void Matrix3::Transpose(Matrix3 * const pmatOut) const
{
   for (int i = 0; i < 3; ++i)
   {
      pmatOut->m_d[0][i] = m_d[i][0];
      pmatOut->m_d[1][i] = m_d[i][1];
      pmatOut->m_d[2][i] = m_d[i][2];
   }
}*/

void Matrix3::Identity(const float value)
{
   m_d[0][0] = m_d[1][1] = m_d[2][2] = value;
   m_d[0][1] = m_d[0][2] =
      m_d[1][0] = m_d[1][2] =
      m_d[2][0] = m_d[2][1] = 0.0f;
}

// Create matrix for rotating around an arbitrary vector
// NB: axis must be normalized
// NB: this actually rotates by -angle in right-handed coordinates
void Matrix3::RotationAroundAxis(const Vertex3Ds& axis, const float angle)
{
   const float rsin = sinf(angle);
   const float rcos = cosf(angle);

   m_d[0][0] = axis.x*axis.x + rcos * (1.0f - axis.x*axis.x);
   m_d[1][0] = axis.x*axis.y*(1.0f - rcos) - axis.z*rsin;
   m_d[2][0] = axis.z*axis.x*(1.0f - rcos) + axis.y*rsin;

   m_d[0][1] = axis.x*axis.y*(1.0f - rcos) + axis.z*rsin;
   m_d[1][1] = axis.y*axis.y + rcos * (1.0f - axis.y*axis.y);
   m_d[2][1] = axis.y*axis.z*(1.0f - rcos) - axis.x*rsin;

   m_d[0][2] = axis.z*axis.x*(1.0f - rcos) - axis.y*rsin;
   m_d[1][2] = axis.y*axis.z*(1.0f - rcos) + axis.x*rsin;
   m_d[2][2] = axis.z*axis.z + rcos * (1.0f - axis.z*axis.z);
}

void Matrix3::RotationAroundAxis(const Vertex3Ds& axis, const float rsin, const float rcos)
{
   m_d[0][0] = axis.x*axis.x + rcos * (1.0f - axis.x*axis.x);
   m_d[1][0] = axis.x*axis.y*(1.0f - rcos) - axis.z*rsin;
   m_d[2][0] = axis.z*axis.x*(1.0f - rcos) + axis.y*rsin;

   m_d[0][1] = axis.x*axis.y*(1.0f - rcos) + axis.z*rsin;
   m_d[1][1] = axis.y*axis.y + rcos * (1.0f - axis.y*axis.y);
   m_d[2][1] = axis.y*axis.z*(1.0f - rcos) - axis.x*rsin;

   m_d[0][2] = axis.z*axis.x*(1.0f - rcos) - axis.y*rsin;
   m_d[1][2] = axis.y*axis.z*(1.0f - rcos) + axis.x*rsin;
   m_d[2][2] = axis.z*axis.z + rcos * (1.0f - axis.z*axis.z);
}

//D3D Matrices ----------------------------------------------------------------------------------------------------------------

#ifdef ENABLE_SDL
D3DXMATRIX::D3DXMATRIX() {
   for (size_t i = 0;i < 4;++i)
      for (size_t j = 0;j < 4;++j)
         m[i][j] = (i == j) ? 1.0f : 0.0f;
}

D3DXMATRIX::D3DXMATRIX(const D3DXMATRIX &input) {
   memcpy(this->m, input.m, sizeof(float) * 16);
}

D3DXMATRIX::D3DXMATRIX(const D3DXMATRIX *input) {
   memcpy(this->m, input->m, sizeof(float) * 16);
}

D3DXMATRIX::D3DXMATRIX(const Matrix3D &input) {
   memcpy(this->m, input.m, sizeof(float) * 16);
}

//Vectors4 ------------------------------------------------------------------------------------------------------------------------
vec4::vec4(float x, float y, float z, float w) {
   v[0] = x;
   v[1] = y;
   v[2] = z;
   v[3] = w;
}

vec4::vec4() {
   x = 0.0; y = 0.0;z = 0.0;w = 1.0;
}

vec4 vec4::normal(vec4 &input) {
   float len = input.x*input.x + input.y*input.y + input.z*input.z + input.w*input.w;
   if (len <= 1.e-10f) {
      return vec4(0.0f, 0.0f, 0.0f, 0.0f);
   }
   len = 1.0f / sqrt(len);
   return vec4(input.x*len, input.y*len, input.z*len, input.w*len);
}

float vec4::dot(vec4 &a, vec4 &b) {
   return a.x * b.x + a.y * a.y + a.z * b.z + a.w * b.w;
}

vec4 vec4::operator+ (const vec4& m) const {
   return vec4(this->x + m.x, this->y + m.y, this->z + m.z, this->w + m.w);
}

vec4 vec4::operator- (const vec4& m) const {
   return vec4(this->x - m.x, this->y - m.y, this->z - m.z, this->w - m.w);
}


//Vectors3 ------------------------------------------------------------------------------------------------------------------------

vec3::vec3(float x, float y, float z) {
   v[0] = x;
   v[1] = y;
   v[2] = z;
}
vec3::vec3() {

}

vec3 vec3::normal(vec3 &input) {
   float len = input.x*input.x + input.y*input.y + input.z*input.z;
   if (len <= 1.e-10f) {
      return vec3(0.0f, 0.0f, 0.0f);
   }
   len = 1.0f / sqrt(len);
   return vec3(input.x*len, input.y*len, input.z*len);
}

vec3 vec3::cross(vec3 &a, vec3 &b) {
   return vec3(a.x - b.x, b.y - a.y, a.z - b.z);
}

float vec3::dot(vec3 &a, vec3 &b) {
   return a.x * b.x + a.y * a.y + a.z * b.z;
}

vec3 vec3::TransformCoord(vec3 vec, Matrix3D mat) {
   float w = (vec.x * mat._14 + vec.y * mat._24 + vec.z * mat._34 + mat._44);
   if (w <= 1.e-10f) {
      return vec3(0.0f, 0.0f, 0.0f);
   }
   w = 1.0f / w;
   return vec3(
      w * (vec.x * mat._11 + vec.y * mat._21 + vec.z * mat._31 + mat._41),
      w * (vec.x * mat._12 + vec.y * mat._22 + vec.z * mat._32 + mat._42),
      w * (vec.x * mat._13 + vec.y * mat._23 + vec.z * mat._33 + mat._43));
}

vec3 vec3::operator+ (const vec3& m) const {
   return vec3(this->x + m.x, this->y + m.y, this->z + m.z);
}

vec3 vec3::operator- (const vec3& m) const {
   return vec3(this->x - m.x, this->y - m.y, this->z - m.z);
}


#endif