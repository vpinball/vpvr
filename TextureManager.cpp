#include "stdafx.h"

#include "TextureManager.h"
#include "RenderDevice.h"
#include "Texture.h"
#include "typedefs3D.h"

D3DTexture* TextureManager::LoadTexture(BaseTexture* memtex, const bool clamptoedge, const bool force_linear_rgb)
{
    const Iter it = m_map.find(memtex);
   if (it == m_map.end())
   {
      TexInfo texinfo;
      texinfo.d3dtex = m_rd.UploadTexture(memtex, &texinfo.texWidth, &texinfo.texHeight, clamptoedge, force_linear_rgb);
      if (!texinfo.d3dtex)
         return 0;
      texinfo.dirty = false;
      m_map[memtex] = texinfo;
      return texinfo.d3dtex;
   }
   else
   {
      if (it->second.dirty)
      {
         m_rd.UpdateTexture(it->second.d3dtex, memtex, force_linear_rgb);
         it->second.dirty = false;
      }
      return it->second.d3dtex;
   }
}

void TextureManager::SetDirty(BaseTexture* memtex)
{
   const Iter it = m_map.find(memtex);
   if (it != m_map.end())
      it->second.dirty = true;
}

void TextureManager::UnloadTexture(BaseTexture* memtex)
{
   const Iter it = m_map.find(memtex);
   if (it != m_map.end())
   {
       SAFE_RELEASE_TEXTURE(it->second.d3dtex);
      m_map.erase(it);
   }
}

void TextureManager::UnloadAll()
{
   for (Iter it = m_map.begin(); it != m_map.end(); ++it)
       SAFE_RELEASE_TEXTURE(it->second.d3dtex);

   m_map.clear();
}
