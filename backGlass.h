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
   int m_backglass_dmd_x;
   int m_backglass_dmd_y;
   int m_backglass_dmd_width;
   int m_backglass_dmd_height;
   float m_dmd_height;
   float m_dmd_width;
   float m_dmd_x;
   float m_dmd_y;
   int m_backglass_grill_height;
   int m_backglass_width;
   int m_backglass_height;
   float m_backglass_scale;
};
