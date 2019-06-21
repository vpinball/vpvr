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
   Texture * m_backgroundFallback;
   D3DTexture* m_backgroundTexture;
   int backglass_dmd_x;
   int backglass_dmd_y;
   int backglass_dmd_width;
   int backglass_dmd_height;
   float dmd_height;
   float dmd_width;
   float dmd_x;
   float dmd_y;
   int backglass_grill_height;
   int backglass_width;
   int backglass_height;
   float backglass_scale;
};

