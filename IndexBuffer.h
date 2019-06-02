#pragma once

#include "stdafx.h"
#include "typeDefs3D.h"

#ifdef ENABLE_SDL

class IndexBuffer
{
public:
   enum Format {
      FMT_INDEX16 = 16,
      FMT_INDEX32 = 32
   };

   void lock(const unsigned int offsetToLock, const unsigned int sizeToLock, void **dataBuffer, const DWORD flags);
   void unlock(void);
   void release(void);
   void bind();
   static void bindNull() { m_curIndexBuffer = nullptr; }

   static void CreateIndexBuffer(const unsigned int numIndices, const DWORD usage, const IndexBuffer::Format format, IndexBuffer **idxBuffer);

   static IndexBuffer* CreateAndFillIndexBuffer(const unsigned int numIndices, const unsigned int * indices);
   static IndexBuffer* CreateAndFillIndexBuffer(const unsigned int numIndices, const WORD * indices);
   static IndexBuffer* CreateAndFillIndexBuffer(const std::vector<unsigned int>& indices);
   static IndexBuffer* CreateAndFillIndexBuffer(const std::vector<WORD>& indices);

   GLuint getOffset() const { return offset; }
   Format getIndexFormat() const { return indexFormat; }
private:
   GLuint count;
   GLuint offset;
   DWORD usage;

   // CPU memory management
   unsigned int _offsetToLock;
   unsigned int _sizeToLock;
   void *_dataBuffer;

   //GPU memory management
   GLuint Buffer;
   Format indexFormat;

   static IndexBuffer* m_curIndexBuffer; // for caching
};

#else

class IndexBuffer : public IDirect3DIndexBuffer9
{
public:
   enum Format {
      FMT_INDEX16 = D3DFMT_INDEX16,
      FMT_INDEX32 = D3DFMT_INDEX32
   };
   enum LockFlags
   {
      WRITEONLY = 0,                      // in DX9, this is specified during VB creation
      NOOVERWRITE = D3DLOCK_NOOVERWRITE,  // meaning: no recently drawn vertices are overwritten. only works with dynamic VBs.
                                          // it's only needed for VBs which are locked several times per frame
                                          DISCARD = D3DLOCK_DISCARD           // discard previous contents; only works with dynamic VBs
   };

   void lock(const unsigned int offsetToLock, const unsigned int sizeToLock, void **dataBuffer, const DWORD flags);
   void unlock(void);
   void release(void);

   static void CreateIndexBuffer(const unsigned int numIndices, const DWORD usage, const IndexBuffer::Format format, IndexBuffer **idxBuffer);

   static IndexBuffer* CreateAndFillIndexBuffer(const unsigned int numIndices, const unsigned int * indices);
   static IndexBuffer* CreateAndFillIndexBuffer(const unsigned int numIndices, const WORD * indices);
   static IndexBuffer* CreateAndFillIndexBuffer(const std::vector<unsigned int>& indices);
   static IndexBuffer* CreateAndFillIndexBuffer(const std::vector<WORD>& indices);

   static void setD3DDevice(IDirect3DDevice9* pD3DDevice);

private:
   IndexBuffer();      // disable default constructor
   static IDirect3DDevice9* m_pD3DDevice;
};

#endif