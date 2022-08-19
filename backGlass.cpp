#include "stdafx.h"

#include <rapidxml_utils.hpp>

#include "backGlass.h"
#include "RenderDevice.h"
#include "Shader.h"
#include "captureExt.h"

//#define WRITE_BACKGLASS_IMAGES
#ifdef WRITE_BACKGLASS_IMAGES
 #include <iostream>
 #include <fstream>
#endif

//XML helpers

inline char nextChar(size_t &inPos, const size_t inSize, const char* const inChars, const char* const outChars, const char* const inData) {
   char c = (inPos >= inSize) ? '=' : inData[inPos];
   while (outChars[c] < 0) {
      inPos++;
      c = (inPos >= inSize) ? '=' : inData[inPos];
   }
   inPos++;
   return c;
}

/*
returns actual data size if successful or -1 if something went wrong.
*/
static int decode_base64(const char* const inData, char* const outData, const size_t inSize, const size_t outSize) {
   static constexpr char inChars[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   static char* outChars = nullptr;
   //Create decode table from encode table
   if (!outChars) {
      outChars = new char[256];
      for (size_t i = 0;i < 256;++i) outChars[i] = 0;
      for (char i = 0;i < 64;++i) outChars[inChars[i]] = i;
      //Hack for fast skipping
      outChars['&'] = -1;
      outChars[10] = -1;
      outChars[13] = -1;
   }

   size_t inPos = 0;
   size_t outPos = 0;
   bool done = false;
   unsigned int padding = 0;
   while ((inPos < inSize) && (outPos < outSize) && !done) {
      char b1 = nextChar(inPos, inSize, inChars, outChars, inData);
      char b2 = nextChar(inPos, inSize, inChars, outChars, inData);
      char b3 = nextChar(inPos, inSize, inChars, outChars, inData);
      char b4 = nextChar(inPos, inSize, inChars, outChars, inData);

      done = (b1 == '=' || b2 == '=' || b3 == '=' || b4 == '=');
      if (done) {
         if (b1 == '=') padding += 3;
         else if (b2 == '=') padding += 3;
         else if (b3 == '=') padding += 2;
         else if (b4 == '=') padding++;
      }
      b1 = outChars[b1];
      b2 = outChars[b2];
      b3 = outChars[b3];
      b4 = outChars[b4];
      outData[outPos] = (b1 << 2) | (b2 >> 4);
      if (outPos + 1 < outSize) outData[outPos + 1] = (b2 << 4) | (b3 >> 2);
      if (outPos + 2 < outSize) outData[outPos + 2] = (b3 << 6) | (b4);
      outPos += 3;
   }
   return min(outPos - padding, outSize);
}

//Actual Backglass code

BackGlass::BackGlass(RenderDevice* const pd3dDevice, Texture * backgroundFallback) :
   m_pd3dDevice(pd3dDevice), m_backgroundFallback(backgroundFallback)
{
#ifdef ENABLE_VR
   //Check for a directb2s and try to use its backglass data
   string b2sFileName = g_pplayer->m_ptable->m_szFileName;
   b2sFileName = b2sFileName.substr(0, b2sFileName.find_last_of('.'));
   b2sFileName.append(".directb2s");
   m_backglass_dmd = int2(0,0);
   m_backglass_dmd_width = 0;
   m_backglass_dmd_height = 0;
   m_backglass_grill_height = 0;
   m_backglass_width = 0;
   m_backglass_height = 0;
   m_dmd_width = 0;
   m_dmd_height = 0;
   m_dmd = Vertex2D(0.f,0.f);

   char* data = nullptr;

   try {
      rapidxml::file<> b2sFile(b2sFileName.c_str());
      rapidxml::xml_document<> b2sTree;
      b2sTree.parse<0>(b2sFile.data());
      auto rootNode = b2sTree.first_node("DirectB2SData");
      if (!rootNode) {
         return;
      }
      size_t data_len = 0;
      auto currentNode = rootNode->first_node();
      while (currentNode) {//Iterate all Nodes within DirectB2SData
         char* nodeName = currentNode->name();
         if (strcmp(nodeName, "VRDMDLocation") == 0) {
            auto attrib = currentNode->first_attribute("LocX");
            if (attrib) m_backglass_dmd.x = atoi(attrib->value());
            attrib = currentNode->first_attribute("LocY");
            if (attrib) m_backglass_dmd.y = atoi(attrib->value());
            attrib = currentNode->first_attribute("Width");
            if (attrib) m_backglass_dmd_width = atoi(attrib->value());
            attrib = currentNode->first_attribute("Height");
            if (attrib) m_backglass_dmd_height = atoi(attrib->value());
         }
         else if (strcmp(nodeName, "GrillHeight") == 0) {
            auto attrib = currentNode->first_attribute("Value");
            if (attrib) m_backglass_grill_height = atoi(attrib->value());
         }
         else if (strcmp(nodeName, "Illumination") == 0) {
            auto illuminationNode = currentNode->first_node();
            int bulb = 1;
            while (illuminationNode) {//Iterate all Nodes within Illumination
               auto attrib = illuminationNode->first_attribute("Image");
               if (attrib) {
                  if (data_len < attrib->value_size() * 3 / 4 + 1) {
                     delete [] data;
                     data_len = attrib->value_size() * 3 / 4 + 1;
                     data = new char[data_len];
                  }
                  int size = decode_base64(attrib->value(), data, attrib->value_size(), data_len);
#ifdef WRITE_BACKGLASS_IMAGES
                  if (WRITE_BACKGLASS_IMAGES > 0 && size > 0) {//Write Image to disk. Also check if the base64 decoder is working...
                     string imageFileName = b2sFileName;
                     imageFileName.append(illuminationNode->name()).append(".bulb").append(std::to_string(bulb)).append(".png");//if it is not a png just rename it...
                     std::ofstream imageFile(imageFileName, std::ios::out | std::ios::binary | std::ios::trunc);
                     if (imageFile.is_open()) {
                        imageFile.write(data, size);
                        imageFile.close();
                     }
                  }
#endif
                  if (size > 0) {
                     //Handle Bulb light images
                  }
               }
               illuminationNode = illuminationNode->next_sibling();
               bulb++;
            }
         }
         else if (strcmp(nodeName, "Images") == 0) {
            auto imagesNode = currentNode->first_node();
            while (imagesNode) {//Iterate all Nodes within Images
               auto attrib = imagesNode->first_attribute("Value");
               if (attrib) {
                  if (data_len < attrib->value_size() * 3 / 4 + 1) {
                     delete [] data;
                     data_len = attrib->value_size() * 3 / 4 + 1;
                     data = new char[data_len];
                  }
                  int size = decode_base64(attrib->value(), data, attrib->value_size(), data_len);
                  if ((size > 0) && (strcmp(imagesNode->name(), "BackglassImage") == 0)) {
                     m_backgroundTexture = m_pd3dDevice->m_texMan.LoadTexture(BaseTexture::CreateFromData(data, size), TextureFilter::TEXTURE_MODE_TRILINEAR, true, true, false);
                     m_backglass_width = m_backgroundTexture->width;
                     m_backglass_height = m_backgroundTexture->height;
                  }
#ifdef WRITE_BACKGLASS_IMAGES
                  if (WRITE_BACKGLASS_IMAGES > 0 && size > 0) {//Write Image to disk. Also useful to check if the base64 decoder is working...
                     string imageFileName = b2sFileName;
                     imageFileName.append(imagesNode->name()).append(".png");//if it is not a png just rename it...
                     std::ofstream imageFile(imageFileName, std::ios::out | std::ios::binary | std::ios::trunc);
                     if (imageFile.is_open()) {
                        imageFile.write(data, size);
                        imageFile.close();
                     }
                  }
#endif
               }
               imagesNode = imagesNode->next_sibling();
            }
         }
         currentNode = currentNode->next_sibling();
      }
   }
   catch (...) {//If file does not exist, or something else goes wrong just disable the Backglass. This is very experimental anyway.
      m_backgroundTexture = nullptr;
   }
   delete [] data;
   float tableWidth, glassHeight;
   g_pplayer->m_ptable->get_Width(&tableWidth);
   g_pplayer->m_ptable->get_GlassHeight(&glassHeight);
   if (m_backglass_width>0 && m_backglass_height>0)
      m_pd3dDevice->DMDShader->SetVector(SHADER_backBoxSize, tableWidth * (0.5f - m_backglass_scale / 2.0f), glassHeight, m_backglass_scale * tableWidth, m_backglass_scale * tableWidth * (float)m_backglass_height / (float)m_backglass_width);
   else
      m_pd3dDevice->DMDShader->SetVector(SHADER_backBoxSize, tableWidth * (0.5f - m_backglass_scale / 2.0f), glassHeight, m_backglass_scale * tableWidth, m_backglass_scale * tableWidth * (float)(9.0 / 16.0));
   if (m_backglass_dmd_width > 0 && m_backglass_dmd_height > 0 && m_backglass_width > 0 && m_backglass_height > 0) {
      m_dmd_width = (float)m_backglass_dmd_width / (float)m_backglass_width;
      m_dmd_height = (float)m_backglass_dmd_height / (float)m_backglass_height;
      m_dmd.x = tableWidth * m_backglass_scale * (float)m_backglass_dmd.x / (float)m_backglass_width;
      m_dmd.y = tableWidth * m_backglass_scale * (1.0f - (float)m_backglass_dmd.y / (float)m_backglass_height - m_dmd_height);
   }
#endif
}

BackGlass::~BackGlass()
{
}

void BackGlass::Render()
{
   if (g_pplayer->m_capPUP && capturePUP())
   {
      m_backgroundTexture = m_pd3dDevice->m_texMan.LoadTexture(g_pplayer->m_texPUP, TextureFilter::TEXTURE_MODE_TRILINEAR, true, true, false);
      m_backglass_width = g_pplayer->m_texPUP->width();
      m_backglass_height = g_pplayer->m_texPUP->height();
      float tableWidth, glassHeight;
      g_pplayer->m_ptable->get_Width(&tableWidth);
      g_pplayer->m_ptable->get_GlassHeight(&glassHeight);
      if (g_pplayer->m_texdmd)
      {
         // If we expect a DMD the captured image is probably missing a grill in 3scr mode
         // 3scr mode preferable to support VR rooms, so better to just drop the grills in this experimental mode.
         const int dmdheightoff = (int)(m_backglass_scale * tableWidth * (float)(9.0 / 16.0) * .3f);
         const int dmdheightextra = (int)(tableWidth * .05f);
         glassHeight += (float)(dmdheightoff + dmdheightextra);

         m_pd3dDevice->DMDShader->SetVector(SHADER_backBoxSize, tableWidth * (0.5f - m_backglass_scale / 2.0f), glassHeight, m_backglass_scale * tableWidth, m_backglass_scale * tableWidth * (float)(9.0 / 16.0));

         // We lost the grille, so make a nice big DMD.
         m_dmd_width = 0.8f;
         m_dmd_height = m_dmd_width / 4.0f;
         m_dmd.x = tableWidth * (0.5f - m_dmd_width / 2.0f);
         m_dmd.y = (float)(-dmdheightoff + dmdheightextra / 2);
      }
      else
         m_pd3dDevice->DMDShader->SetVector(SHADER_backBoxSize, tableWidth * (0.5f - m_backglass_scale / 2.0f), glassHeight, m_backglass_scale * tableWidth, m_backglass_scale * tableWidth * (float)(3.0 / 4.0));
   }

   if (m_backgroundTexture)
      m_pd3dDevice->DMDShader->SetTexture(SHADER_Texture0, m_backgroundTexture);
   else if (m_backgroundFallback)
      m_pd3dDevice->DMDShader->SetTexture(SHADER_Texture0, m_backgroundFallback, TextureFilter::TEXTURE_MODE_TRILINEAR, true, true, false);
   else return;

   m_pd3dDevice->SetRenderState(RenderDevice::ZWRITEENABLE, RenderDevice::RS_FALSE);
   m_pd3dDevice->SetRenderState(RenderDevice::ZENABLE, RenderDevice::RS_FALSE);
   m_pd3dDevice->SetRenderStateCulling(RenderDevice::CULL_NONE);

   m_pd3dDevice->SetRenderState(RenderDevice::ALPHABLENDENABLE, RenderDevice::RS_FALSE);

   m_pd3dDevice->DMDShader->SetTechnique(SHADER_TECHNIQUE_basic_noDMD);

   m_pd3dDevice->DMDShader->SetVector(SHADER_vColor_Intensity, 1.0f, 1.0f, 1.0f, 1.0f);

   static constexpr float Verts[4 * 5] =
   {
       1.0f,  1.0f, 0.0f, 1.0f, 0.0f,
      -1.0f,  1.0f, 0.0f, 0.0f, 0.0f,
       1.0f, -1.0f, 0.0f, 1.0f, 1.0f,
      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f
   };

   m_pd3dDevice->DMDShader->Begin(0);
   m_pd3dDevice->DrawTexturedQuad((Vertex3D_TexelOnly*)Verts);
   m_pd3dDevice->DMDShader->End();

   //m_pd3dDevice->SetRenderStateCulling(RenderDevice::CULL_CCW); // not necessary anymore
   m_pd3dDevice->SetRenderState(RenderDevice::ZENABLE, RenderDevice::RS_TRUE);
   m_pd3dDevice->SetRenderState(RenderDevice::ZWRITEENABLE, RenderDevice::RS_TRUE);
}

void BackGlass::DMDdraw(float DMDposx, float DMDposy, float DMDwidth, float DMDheight, const COLORREF DMDcolor, const float intensity)
{
   if (g_pplayer->m_texdmd || captureExternalDMD()) // If DMD capture is enabled check if external DMD exists (for capturing UltraDMD+P-ROC DMD)
   {
      //const float width = g_pplayer->m_pin3d.m_useAA ? 2.0f*(float)m_width : (float)m_width;
      m_pd3dDevice->DMDShader->SetTechnique(SHADER_TECHNIQUE_basic_DMD); //!! DMD_UPSCALE ?? -> should just work

      const vec4 c = convertColor(DMDcolor, intensity);
      m_pd3dDevice->DMDShader->SetVector(SHADER_vColor_Intensity, &c);
#ifdef DMD_UPSCALE
      const vec4 r((float)(m_dmd.x * 3), (float)(m_dmd.y * 3), 1.f, (float)(g_pplayer->m_overall_frames % 2048));
#else
      const vec4 r((float)g_pplayer->m_dmd.x, (float)g_pplayer->m_dmd.y, 1.f, (float)(g_pplayer->m_overall_frames % 2048));
#endif
      m_pd3dDevice->DMDShader->SetVector(SHADER_vRes_Alpha_time, &r);

      // If we're capturing Freezy DMD switch to ext technique to avoid incorrect colorization
      if (captureExternalDMD())
         m_pd3dDevice->DMDShader->SetTechnique(SHADER_TECHNIQUE_basic_DMD_ext);

      if (g_pplayer->m_texdmd != nullptr)
         m_pd3dDevice->DMDShader->SetTexture(SHADER_Texture0, m_pd3dDevice->m_texMan.LoadTexture(g_pplayer->m_texdmd, TextureFilter::TEXTURE_MODE_NONE, true, true, false));
      //      m_pd3dPrimaryDevice->DMDShader->SetVector(SHADER_quadOffsetScale, 0.0f, -1.0f, backglass_scale, backglass_scale*(float)backglass_height / (float)backglass_width);
      bool zDisabled = false;
      m_pd3dDevice->SetRenderStateCulling(RenderDevice::CULL_NONE);
      if (m_backgroundTexture) { //For VR, place it in the backglass
         if (m_dmd_width == 0.0f || m_dmd_height == 0.0f) {//If file contains no valid VRDMD position
            if (m_backglass_grill_height > 0) {
               //DMD is centered in the Grill of the backglass
               constexpr float scale = 0.5f;// 0.5 => use 50% of the height of the grill.
               float tableWidth;
               g_pplayer->m_ptable->get_Width(&tableWidth);
               tableWidth *= m_backglass_scale;
               m_dmd_height = m_backglass_scale * scale * (float)m_backglass_grill_height / (float)m_backglass_width;
               m_dmd_width = m_dmd_height / (float)g_pplayer->m_texdmd->height() * (float)g_pplayer->m_texdmd->width();
               m_dmd.x = tableWidth * (0.5f - m_dmd_width / 2.0f);
               m_dmd.y = tableWidth * (float)m_backglass_grill_height*(0.5f - scale / 2.0f) / (float)m_backglass_width;
            }
         }
         m_pd3dDevice->SetRenderState(RenderDevice::ZENABLE, RenderDevice::RS_FALSE);
         m_pd3dDevice->SetRenderState(RenderDevice::ZWRITEENABLE, RenderDevice::RS_FALSE);
         zDisabled = true;
         DMDposx = (float)m_dmd.x;
         DMDposy = (float)m_dmd.y;
         DMDwidth = (float)m_dmd_width;
         DMDheight = (float)m_dmd_height;
      }

      float DMDVerts[4 * 5] =
      {
         1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
         0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
         1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
         0.0f, 0.0f, 0.0f, 0.0f, 0.0f
      };

      for (unsigned int i = 0; i < 4; ++i)
      {
         DMDVerts[i * 5    ] =        (DMDVerts[i * 5    ] * DMDwidth  + DMDposx)*2.0f - 1.0f;
         DMDVerts[i * 5 + 1] = 1.0f - (DMDVerts[i * 5 + 1] * DMDheight + DMDposy)*2.0f;
      }

      m_pd3dDevice->DMDShader->Begin(0);
      m_pd3dDevice->DrawTexturedQuad((Vertex3D_TexelOnly*)DMDVerts);
      m_pd3dDevice->DMDShader->End();

      if (zDisabled)
      {
         m_pd3dDevice->SetRenderState(RenderDevice::ZWRITEENABLE, RenderDevice::RS_TRUE);
         m_pd3dDevice->SetRenderState(RenderDevice::ZENABLE, RenderDevice::RS_TRUE);
      }
   }
}
