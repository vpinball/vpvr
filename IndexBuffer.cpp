#include "stdafx.h"
#include "IndexBuffer.h"
#include "RenderDevice.h"

IndexBuffer* IndexBuffer::m_curIndexBuffer = nullptr;

void IndexBuffer::CreateIndexBuffer(const unsigned int numIndices, const DWORD usage, const IndexBuffer::Format format, IndexBuffer **idxBuffer)
{
#ifdef ENABLE_SDL
   IndexBuffer* ib = new IndexBuffer();
   glGenBuffers(1, &(ib->Buffer));
   ib->count = numIndices;
   ib->indexFormat = format;
   ib->usage = usage ? usage : GL_STATIC_DRAW;
   *idxBuffer = ib;
#else
   // NB: We always specify WRITEONLY since MSDN states,
   // "Buffers created with D3DPOOL_DEFAULT that do not specify D3DUSAGE_WRITEONLY may suffer a severe performance penalty."
   HRESULT hr;
   const unsigned idxSize = (format == IndexBuffer::FMT_INDEX16) ? 2 : 4;
   hr = m_pD3DDevice->CreateIndexBuffer(idxSize * numIndices, usage | D3DUSAGE_WRITEONLY, (D3DFORMAT)format,
      (D3DPOOL)memoryPool::DEFAULT, (IDirect3DIndexBuffer9**)idxBuffer, NULL);
   if (FAILED(hr))
      ReportError("Fatal Error: unable to create index buffer!", hr, __FILE__, __LINE__);
#endif
}

IndexBuffer* IndexBuffer::CreateAndFillIndexBuffer(const unsigned int numIndices, const WORD * indices)
{
#ifdef ENABLE_SDL
   IndexBuffer* ib = new IndexBuffer();
   ib->count = numIndices;
   ib->indexFormat = IndexBuffer::FMT_INDEX16;
   ib->usage = GL_STATIC_DRAW;
   CHECKD3D(glGenBuffers(1, &(ib->Buffer)));
   CHECKD3D(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->Buffer));
   CHECKD3D(glBufferData(GL_ELEMENT_ARRAY_BUFFER, numIndices * sizeof(indices[0]), &indices[0], GL_STATIC_DRAW));
   CHECKD3D(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
#else
   IndexBuffer* ib;
   CreateIndexBuffer(numIndices, 0, IndexBuffer::FMT_INDEX16, &ib);

   void* buf;
   ib->lock(0, 0, &buf, 0);
   memcpy(buf, indices, numIndices * sizeof(indices[0]));
   ib->unlock();
#endif
   return ib;
}

IndexBuffer* IndexBuffer::CreateAndFillIndexBuffer(const unsigned int numIndices, const unsigned int * indices)
{
#ifdef ENABLE_SDL
   IndexBuffer* ib = new IndexBuffer();
   ib->count = numIndices;
   ib->indexFormat = IndexBuffer::FMT_INDEX32;
   ib->usage = GL_STATIC_DRAW;
   CHECKD3D(glGenBuffers(1, &(ib->Buffer)));
   CHECKD3D(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->Buffer));
   CHECKD3D(glBufferData(GL_ELEMENT_ARRAY_BUFFER, numIndices * sizeof(indices[0]), &indices[0], GL_STATIC_DRAW));
   CHECKD3D(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
#else
   IndexBuffer* ib;
   CreateIndexBuffer(numIndices, 0, IndexBuffer::FMT_INDEX32, &ib);

   void* buf;
   ib->lock(0, 0, &buf, 0);
   memcpy(buf, indices, numIndices * sizeof(indices[0]));
   ib->unlock();
#endif
   return ib;
}

IndexBuffer* IndexBuffer::CreateAndFillIndexBuffer(const std::vector<WORD>& indices)
{
   return CreateAndFillIndexBuffer((unsigned int)indices.size(), indices.data());
}

IndexBuffer* IndexBuffer::CreateAndFillIndexBuffer(const std::vector<unsigned int>& indices)
{
   return CreateAndFillIndexBuffer((unsigned int)indices.size(), indices.data());
}

void IndexBuffer::lock(const unsigned int offsetToLock, const unsigned int sizeToLock, void **dataBuffer, const DWORD flags)
{
#ifdef ENABLE_SDL
   if (sizeToLock == 0) {
      _sizeToLock = (indexFormat == FMT_INDEX16 ? 2 : 4) * count;
   }
   else {
      _sizeToLock = sizeToLock;
   }
   if (offsetToLock < (indexFormat == FMT_INDEX16 ? 2 : 4) * count) {
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

void IndexBuffer::unlock()
{
#ifdef ENABLE_SDL
   if (!_dataBuffer)
      return;
   CHECKD3D(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->Buffer));
   if ((_offsetToLock != 0) || (_sizeToLock != (count * (indexFormat == FMT_INDEX16 ? 2 : 4)))) {
      if ((count * (indexFormat == FMT_INDEX16 ? 2 : 4))  < _offsetToLock) {
         glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, _offsetToLock, min(_sizeToLock, count * (indexFormat == FMT_INDEX16 ? 2 : 4) - _offsetToLock), _dataBuffer);
      }
   }
   else {
      CHECKD3D(glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * (indexFormat == FMT_INDEX16 ? 2 : 4), _dataBuffer, usage));
   }
   CHECKD3D(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
   free(_dataBuffer);
#else
   CHECKD3D(this->Unlock());
#endif
}

void IndexBuffer::release(void)
{
#ifdef ENABLE_SDL
   CHECKD3D(glDeleteBuffers(1, &this->Buffer));
   this->Buffer = 0;
#else
   SAFE_RELEASE_NO_CHECK_NO_SET(this);
#endif
}

#ifndef ENABLE_SDL
IDirect3DDevice9* IndexBuffer::m_pD3DDevice = NULL;

void IndexBuffer::setD3DDevice(IDirect3DDevice9* pD3DDevice) {
   m_pD3DDevice = pD3DDevice;
}
#endif

void IndexBuffer::bind()
{
#ifdef ENABLE_SDL
   if (m_curIndexBuffer != this)
   {
      CHECKD3D(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->Buffer));
      m_curIndexBuffer = this;
   }
#else
   if (m_curIndexBuffer != ib)   {      CHECKD3D(m_pD3DDevice->SetIndices(ib));      m_curIndexBuffer = ib;   }
#endif
}
