#include "stdafx.h"

#include "TextureManager.h"
#include "RenderDevice.h"
#include "Texture.h"
#include "typedefs3D.h"

Sampler* TextureManager::LoadTexture(BaseTexture* memtex, const SamplerFilter filter, const SamplerAddressMode clampU, const SamplerAddressMode clampV, const bool force_linear_rgb)
{
   SamplerFilter sa_filter;
   switch (filter)
   {
   case TEXTURE_MODE_NONE:
      sa_filter = SF_NONE;
      break;
   case TEXTURE_MODE_POINT:
      sa_filter = SF_POINT;
      break;
   case TEXTURE_MODE_BILINEAR:
      sa_filter = SF_BILINEAR;
      break;
   case TEXTURE_MODE_TRILINEAR:
      sa_filter = SF_TRILINEAR;
      break;
   case TEXTURE_MODE_ANISOTROPIC:
      sa_filter = SF_ANISOTROPIC;
      break;
   }
   const Iter it = m_map.find(memtex);
   if (it == m_map.end())
   {
      Sampler* sampler = new Sampler(&m_rd, memtex, force_linear_rgb, clampU ? SA_CLAMP : SA_REPEAT, clampV ? SA_CLAMP : SA_REPEAT, sa_filter);
      sampler->m_dirty = false;
      m_map[memtex] = sampler;
      return sampler;
   }
   else
   {
      Sampler* sampler = it->second;
      if (sampler->m_dirty)
      {
         sampler->UpdateTexture(memtex, force_linear_rgb);
         sampler->m_dirty = false;
      }
      sampler->SetClamp(clampU ? SA_CLAMP : SA_REPEAT, clampV ? SA_CLAMP : SA_REPEAT);
      sampler->SetFilter(sa_filter);
      return sampler;
   }
}

void TextureManager::SetDirty(BaseTexture* memtex)
{
   const Iter it = m_map.find(memtex);
   if (it != m_map.end())
      it->second->m_dirty = true;
}

void TextureManager::UnloadTexture(BaseTexture* memtex)
{
   const Iter it = m_map.find(memtex);
   if (it != m_map.end())
   {
      delete it->second;
      m_map.erase(it);
   }
}

void TextureManager::UnloadAll()
{
   for (Iter it = m_map.begin(); it != m_map.end(); ++it)
      delete it->second;
   m_map.clear();
}
