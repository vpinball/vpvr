#pragma once
#include "typeDefs3D.h"

class RenderDevice;

class BackGlass
{
public:
   BackGlass(RenderDevice* const device, Texture * backgroundFallback);
   ~BackGlass();
   void Render();
   void DMDdraw(const float DMDposx, const float DMDposy, const float DMDwidth, const float DMDheight, const COLORREF DMDcolor, const float intensity);

private:
   RenderDevice* m_pd3dDevice;
   Texture* m_backgroundFallback;
   D3DTexture* m_backgroundTexture;
   int2 m_backglass_dmd;
   int m_backglass_dmd_width;
   int m_backglass_dmd_height;
   float m_dmd_height;
   float m_dmd_width;
   Vertex2D m_dmd;
   int m_backglass_grill_height;
   int m_backglass_width;
   int m_backglass_height;

   static constexpr float m_backglass_scale = 1.2f;
};
