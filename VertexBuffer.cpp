#include "stdafx.h"
#include "VertexBuffer.h"
#include "RenderDevice.h"

static unsigned int fvfToSize(const DWORD fvf)
{
   switch (fvf)
   {
   case MY_D3DFVF_NOTEX2_VERTEX:
   case MY_D3DTRANSFORMED_NOTEX2_VERTEX:
      return sizeof(Vertex3D_NoTex2);
   case MY_D3DFVF_TEX:
      return sizeof(Vertex3D_TexelOnly);
   default:
      return 0;
   }
}

#ifdef ENABLE_SDL
//TODO get all this stuff from here, RenderDevice, def.h and FileIO to one place. And reduce calls for creating/updating meshes to one for all objects (e.g. bumper).
void setVertexAttribPointer(const DWORD fvf) {
   uintptr_t offset = 0;
   switch (fvf)
   {
   case MY_D3DFVF_NOTEX2_VERTEX:             //VertexNormalTexelElement and Vertex3D_NoTex2
   case MY_D3DTRANSFORMED_NOTEX2_VERTEX:     //VertexTrafoTexelElement and Vertex3D_NoTex2
      CHECKD3D(glEnableVertexAttribArray(0));//Position
      CHECKD3D(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (const void *)offset));//Position
      offset += 3;
      CHECKD3D(glEnableVertexAttribArray(1));//Normals
      CHECKD3D(glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, (const void *)offset));//Normals
      offset += 3;
      CHECKD3D(glEnableVertexAttribArray(2));//Texture
      CHECKD3D(glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, (const void *)offset));//Texture
      CHECKD3D(glEnableVertexAttribArray(0));//Position
      CHECKD3D(glEnableVertexAttribArray(1));//Normals
      CHECKD3D(glDisableVertexAttribArray(2));//Texture
      break;
   case MY_D3DFVF_TEX:     //VertexTexelElement and Vertex3D_TexelOnly
      CHECKD3D(glEnableVertexAttribArray(0));//Position
      CHECKD3D(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (const void *)offset));//Position
      offset += 3;
      CHECKD3D(glEnableVertexAttribArray(2));//Texture
      CHECKD3D(glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, (const void *)offset));//Texture
      CHECKD3D(glEnableVertexAttribArray(0));//Position
      CHECKD3D(glEnableVertexAttribArray(2));//Texture
      break;
   default:
      ShowError("Cannot set unknown Vertex Attribute Pointer.");
   }
}

#endif

void VertexBuffer::CreateVertexBuffer(const unsigned int vertexCount, const DWORD usage, const DWORD fvf, VertexBuffer **vBuffer)
{
#ifdef ENABLE_SDL
   VertexBuffer* vb = new VertexBuffer();
   glGenVertexArrays(1, &(vb->Array));
   glGenBuffers(1, &(vb->Buffer));
   vb->count = vertexCount;
   vb->sizePerVertex = fvfToSize(fvf);
   vb->usage = usage ? usage : USAGE_STATIC;
   vb->fvf = fvf;
   *vBuffer = vb;
#else
   // NB: We always specify WRITEONLY since MSDN states,
   // "Buffers created with D3DPOOL_DEFAULT that do not specify D3DUSAGE_WRITEONLY may suffer a severe performance penalty."
   // This means we cannot read from vertex buffers, but I don't think we need to.
   HRESULT hr;
   hr = m_pD3DDevice->CreateVertexBuffer(vertexCount * fvfToSize(fvf), D3DUSAGE_WRITEONLY | usage, 0,
      D3DPOOL_DEFAULT, (IDirect3DVertexBuffer9**)vBuffer, NULL);
   if (FAILED(hr))
      ReportError("Fatal Error: unable to create vertex buffer!", hr, __FILE__, __LINE__);
#endif
}

void VertexBuffer::lock(const unsigned int offsetToLock, const unsigned int sizeToLock, void **dataBuffer, const DWORD flags)
{
#ifdef ENABLE_SDL
   if (sizeToLock == 0) {
      _sizeToLock = sizePerVertex * count;
   }
   else {
      _sizeToLock = sizeToLock;
   }
   if (offsetToLock < sizePerVertex*count) {
      *dataBuffer = malloc(_sizeToLock);
      _dataBuffer = *dataBuffer;
      _offsetToLock = offsetToLock;
   }
   else {
      *dataBuffer = NULL;
      _dataBuffer = NULL;
      _sizeToLock = 0;
   }
#else
   CHECKD3D(this->Lock(offsetToLock, sizeToLock, dataBuffer, flags));
#endif
}
void VertexBuffer::unlock()
{
#ifdef ENABLE_SDL
   if (!_dataBuffer)
      return;
   CHECKD3D(glBindVertexArray(this->Array));
   CHECKD3D(glBindBuffer(GL_ARRAY_BUFFER, this->Buffer));
   if ((_offsetToLock != 0) || (_sizeToLock != sizePerVertex * count)) {
      if (sizePerVertex*count - _offsetToLock > 0) {
         CHECKD3D(glBufferSubData(GL_ARRAY_BUFFER, _offsetToLock, min(_sizeToLock, sizePerVertex*count - _offsetToLock), _dataBuffer));
      }
   }
   else {
      CHECKD3D(glBufferData(GL_ARRAY_BUFFER, sizePerVertex*count, _dataBuffer, usage));
   }
//   setVertexAttribPointer(fvf);
   CHECKD3D(glBindBuffer(GL_ARRAY_BUFFER, 0));
   CHECKD3D(glBindVertexArray(0));
   free(_dataBuffer);
#else
   CHECKD3D(this->Unlock());
#endif
}

void VertexBuffer::release()
{
#ifdef ENABLE_SDL
   CHECKD3D(glDeleteBuffers(1, &this->Buffer));
   this->Buffer = 0;
   this->sizePerVertex = 0;
   this->offset = 0;
   this->count = 0;
#else
   SAFE_RELEASE_NO_CHECK_NO_SET(this);
#endif
}

#ifndef ENABLE_SDL
IDirect3DDevice9* VertexBuffer::m_pD3DDevice= NULL;

void VertexBuffer::setD3DDevice(IDirect3DDevice9* pD3DDevice) {
   m_pD3DDevice = pD3DDevice;
}
#endif