#pragma once

#include "typedefs3D.h"
class RenderDevice;

enum SamplerFilter
{
   SF_UNDEFINED, // Used for undefined default values
   SF_NONE, // No filtering at all. DX: MIPFILTER = NONE; MAGFILTER = POINT; MINFILTER = POINT; / OpenGL Nearest/Nearest
   SF_POINT, // Point sampled (aka nearest mipmap) texture filtering.
   SF_BILINEAR, // Bilinar texture filtering (linear min/mag, no mipmapping). DX: MIPFILTER = NONE; MAGFILTER = LINEAR; MINFILTER = LINEAR;
   SF_TRILINEAR, // Trilinar texture filtering (linear min/mag, with mipmapping). DX: MIPFILTER = LINEAR; MAGFILTER = LINEAR; MINFILTER = LINEAR;
   SF_ANISOTROPIC // Anisotropic texture filtering.
};

enum SamplerAddressMode
{
   SA_UNDEFINED, // Used for undefined default values
#ifdef ENABLE_SDL
   SA_REPEAT = GL_REPEAT,
   SA_CLAMP = GL_CLAMP_TO_EDGE,
   SA_MIRROR = GL_MIRRORED_REPEAT
#else
   SA_REPEAT,
   SA_CLAMP,
   SA_MIRROR
#endif
};

class Sampler
{
public:
   Sampler(RenderDevice* rd, BaseTexture* const surf, const bool force_linear_rgb, const SamplerAddressMode clampu = SA_CLAMP, const SamplerAddressMode clampv = SA_CLAMP, const SamplerFilter filter = SF_NONE);
#ifdef ENABLE_SDL
   Sampler(RenderDevice* rd, GLuint glTexture, bool ownTexture, bool isMSAA, bool force_linear_rgb, const SamplerAddressMode clampu = SA_CLAMP, const SamplerAddressMode clampv = SA_CLAMP, const SamplerFilter filter = SF_NONE);
   GLuint GetCoreTexture() const { return m_texture; }
#else
   Sampler(RenderDevice* rd, IDirect3DTexture9* dx9Texture, bool ownTexture, bool force_linear_rgb, const SamplerAddressMode clampu = SA_CLAMP, const SamplerAddressMode clampv = SA_CLAMP, const SamplerFilter filter = SF_NONE);
   IDirect3DTexture9* GetCoreTexture() { return m_texture;  }
#endif
   ~Sampler();

   void UpdateTexture(BaseTexture* const surf, const bool force_linear_rgb);
   void SetClamp(const SamplerAddressMode clampu, const SamplerAddressMode clampv);
   void SetFilter(const SamplerFilter filter);

   bool IsLinear() const { return m_isLinear; }
   bool IsMSAA() const { return m_isMSAA; }
   int GetWidth() const { return m_width; }
   int GetHeight() const { return m_height; }
   SamplerFilter GetFilter() const { return m_filter; }
   SamplerAddressMode GetClampU() const { return m_clampu; }
   SamplerAddressMode GetClampV() const { return m_clampv; }

public:
   bool m_dirty;

private:
   bool m_ownTexture;
   bool m_isLinear;
   bool m_isMSAA;
   RenderDevice* m_rd;
   int m_width;
   int m_height;
   SamplerAddressMode m_clampu;
   SamplerAddressMode m_clampv;
   SamplerFilter m_filter;
#ifdef ENABLE_SDL
   GLuint m_texture = 0;
   GLuint CreateTexture(UINT Width, UINT Height, UINT Levels, colorFormat Format, void* data, int stereo);
#else
   IDirect3DTexture9* m_texture;
   IDirect3DTexture9* CreateSystemTexture(BaseTexture* const surf, const bool force_linear_rgb, colorFormat& texformat);
#endif
};
