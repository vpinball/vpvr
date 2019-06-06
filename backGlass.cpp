#include "stdafx.h"

#include "rapidxml/rapidxml_utils.hpp"
#include <iostream>
#include <fstream>

#include "backGlass.h"
#include "RenderDevice.h"
#include "Shader.h"

//#define WRITE_BACKGLASS_IMAGES 1
//XML helpers

inline char nextChar(size_t &inPos, size_t inSize, const char* inChars, const char* outChars, const char* inData) {
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
int decode_base64(const char* inData, char* outData, size_t inSize, size_t outSize) {
   static const char inChars[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   static char* outChars = NULL;
   //Create decode table from encode table
   if (!outChars) {
      outChars = (char*)malloc(256);
      for (size_t i = 0;i < 256;++i) outChars[i] = 0;
      for (char i = 0;i < 64;++i) outChars[inChars[i]] = i;
   }
   //Hack for fast skipping
   outChars['&'] = -1;
   outChars[10] = -1;
   outChars[13] = -1;
   size_t inPos = 0;
   size_t outPos = 0;
   bool done = false;
   int padding = 0;
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

BackGlass::BackGlass(RenderDevice* const pd3dDevice,Texture * backgroundFallback) : 
   m_pd3dDevice(pd3dDevice), m_backgroundFallback(backgroundFallback)
{
#ifdef ENABLE_VR
   //Check for a directb2s and try to use its backglass data
   std::string b2sFileName = string(g_pplayer->m_ptable->m_szFileName);
   b2sFileName = b2sFileName.substr(0, b2sFileName.find_last_of("."));
   b2sFileName.append(".directb2s");
   void* data = NULL;
   size_t data_len = 0;
   backglass_dmd_x = 0;
   backglass_dmd_y = 0;
   backglass_grill_height = 0;
   backglass_width = 0;
   backglass_height = 0;
   backglass_scale = 1.2f;
   try {
      rapidxml::file<> b2sFile(b2sFileName.c_str());
      rapidxml::xml_document<> b2sTree;
      b2sTree.parse<0>(b2sFile.data());
      auto rootNode = b2sTree.first_node("DirectB2SData");
      if (!rootNode) {
         return;
      }
      auto currentNode = rootNode->first_node();
      while (currentNode) {//Iterate all Nodes within DirectB2SData
         char* nodeName = currentNode->name();
         if (strcmp(nodeName, "DMDDefaultLocation") == 0) {
            auto attrib = currentNode->first_attribute("LocX");
            if (attrib) backglass_dmd_x = atoi(attrib->value());
            attrib = currentNode->first_attribute("LocY");
            if (attrib) backglass_dmd_y = atoi(attrib->value());
         }
         else if (strcmp(nodeName, "GrillHeight") == 0) {
            auto attrib = currentNode->first_attribute("Value");
            if (attrib) backglass_grill_height = atoi(attrib->value());
         }
         else if (strcmp(nodeName, "Illumination") == 0) {
            auto illuminationNode = currentNode->first_node();
            int bulb = 1;
            while (illuminationNode) {//Iterate all Nodes within Illumination
               auto attrib = illuminationNode->first_attribute("Image");
               if (attrib) {
                  if (data_len < attrib->value_size() * 3 / 4 + 1) {
                     if (data) free(data);
                     data_len = attrib->value_size() * 3 / 4 + 1;
                     data = malloc(data_len);
                  }
                  int size = decode_base64(attrib->value(), (char*)data, attrib->value_size(), data_len);
#ifdef WRITE_BACKGLASS_IMAGES
                  if (WRITE_BACKGLASS_IMAGES > 0 && size > 0) {//Write Image to disk. Also check if the base64 decoder is working...
                     std::string imageFileName = b2sFileName;
                     imageFileName.append(illuminationNode->name()).append(".bulb").append(std::to_string(bulb)).append(".png");//if it is not a png just rename it...
                     std::ofstream imageFile(imageFileName, std::ios::out | std::ios::binary | std::ios::trunc);
                     if (imageFile.is_open()) {
                        imageFile.write((char*)data, size);
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
                     if (data) free(data);
                     data_len = attrib->value_size() * 3 / 4 + 1;
                     data = malloc(data_len);
                  }
                  int size = decode_base64(attrib->value(), (char*)data, attrib->value_size(), data_len);
                  if ((size > 0) && (strcmp(imagesNode->name(), "BackglassImage") == 0)) {
                     m_backgroundTexture = m_pd3dDevice->m_texMan.LoadTexture(BaseTexture::CreateFromData(data, size), true);
                     backglass_width = m_backgroundTexture->width;
                     backglass_height = m_backgroundTexture->height;
                  }
#ifdef WRITE_BACKGLASS_IMAGES
                  if (WRITE_BACKGLASS_IMAGES > 0 && size > 0) {//Write Image to disk. Also useful to check if the base64 decoder is working...
                     std::string imageFileName = b2sFileName;
                     imageFileName.append(imagesNode->name()).append(".png");//if it is not a png just rename it...
                     std::ofstream imageFile(imageFileName, std::ios::out | std::ios::binary | std::ios::trunc);
                     if (imageFile.is_open()) {
                        imageFile.write((char*)data, size);
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
      m_backgroundTexture = NULL;
   }
   if (data) free(data);
   float tableWidth, glassHeight;
   g_pplayer->m_ptable->get_Width(&tableWidth);
   g_pplayer->m_ptable->get_GlassHeight(&glassHeight);
   if (backglass_width>0 && backglass_height>0)
      m_pd3dDevice->DMDShader->SetVector("backBoxSize", tableWidth * (0.5f - backglass_scale / 2.0f), glassHeight, backglass_scale * tableWidth, backglass_scale * tableWidth / (float)backglass_width*(float)backglass_height);
   else
      m_pd3dDevice->DMDShader->SetVector("backBoxSize", tableWidth * (0.5f - backglass_scale / 2.0f), glassHeight, backglass_scale * tableWidth, backglass_scale * tableWidth / 16.0f*9.0f);
#endif
}

BackGlass::~BackGlass()
{
}

void BackGlass::Render()
{
   PinTable * const ptable = g_pplayer->m_ptable;

   if (m_backgroundTexture)
      m_pd3dDevice->DMDShader->SetTexture("Texture0", m_backgroundTexture, false);
   else if (m_backgroundFallback)
      m_pd3dDevice->DMDShader->SetTexture("Texture0", m_backgroundFallback, false);
   else return;

   m_pd3dDevice->SetRenderState(RenderDevice::ZWRITEENABLE, RenderDevice::RS_FALSE);
   m_pd3dDevice->SetRenderState(RenderDevice::ZENABLE, FALSE);
   m_pd3dDevice->SetRenderStateCulling(RenderDevice::CULL_NONE);

   m_pd3dDevice->SetRenderState(RenderDevice::ALPHABLENDENABLE, RenderDevice::RS_FALSE);

   m_pd3dDevice->DMDShader->SetTechnique("basic_noDMD");

   m_pd3dDevice->DMDShader->SetVector("vColor_Intensity", 1.0, 1.0, 1.0, 1.0);

   m_pd3dDevice->DMDShader->Begin(0);
   m_pd3dDevice->DrawTexturedQuad();
   m_pd3dDevice->DMDShader->End();

   m_pd3dDevice->DMDShader->SetVector("quadOffsetScale", 0.0f, 0.0f, 1.0f, 1.0f);

   m_pd3dDevice->SetRenderStateCulling(RenderDevice::CULL_CCW);

   m_pd3dDevice->SetRenderState(RenderDevice::ZENABLE, TRUE);
   m_pd3dDevice->SetRenderState(RenderDevice::ZWRITEENABLE, RenderDevice::RS_TRUE);
}

void BackGlass::DMDdraw(const float DMDposx, const float DMDposy, const float DMDwidth, const float DMDheight, const COLORREF DMDcolor, const float intensity)
{
   if (g_pplayer->m_texdmd)
   {
      //const float width = g_pplayer->m_pin3d.m_useAA ? 2.0f*(float)m_width : (float)m_width;
      m_pd3dDevice->DMDShader->SetTechnique("basic_DMD"); //!! DMD_UPSCALE ?? -> should just work

      const vec4 c = convertColor(DMDcolor, intensity);
      m_pd3dDevice->DMDShader->SetVector("vColor_Intensity", &c);
#ifdef DMD_UPSCALE
      const vec4 r((float)(m_dmdx * 3), (float)(m_dmdy * 3), 1.f, 0.f);
#else
      const vec4 r((float)g_pplayer->m_dmdx, (float)g_pplayer->m_dmdy, 1.f, 0.f);
#endif
      m_pd3dDevice->DMDShader->SetVector("vRes_Alpha", &r);

      m_pd3dDevice->DMDShader->SetTexture("Texture0", m_pd3dDevice->m_texMan.LoadTexture(g_pplayer->m_texdmd, false), false);
      //      m_pd3dPrimaryDevice->DMDShader->SetVector("quadOffsetScale", 0.0f, -1.0f, backglass_scale, backglass_scale*(float)backglass_height / (float)backglass_width);
      bool zDisabled = false;
      float tableWidth;
      g_pplayer->m_ptable->get_Width(&tableWidth);
      tableWidth *= backglass_scale;
      float scale = 0.5f;// 0.5 => use 50% of the height of the grill.
      if (m_backgroundTexture && backglass_grill_height > 0) {
         //DMD is centered in the Grill of the backglass
         float dmd_height = backglass_scale * scale * (float)backglass_grill_height / (float)backglass_width;
         float dmd_width = dmd_height / (float)(g_pplayer->m_texdmd->height()) * (float)(g_pplayer->m_texdmd->width());
         float dmd_x = tableWidth * (0.5f - dmd_width / 2.0f);
         float dmd_y = (tableWidth * (float)backglass_grill_height*(0.5f - scale / 2.0f) / (float)backglass_width);
         m_pd3dDevice->DMDShader->SetVector("quadOffsetScale", dmd_x, dmd_y, dmd_width, dmd_height);
         m_pd3dDevice->SetRenderState(RenderDevice::ZENABLE, FALSE);
         m_pd3dDevice->SetRenderStateCulling(RenderDevice::CULL_NONE);
         zDisabled = true;
      }
      else if (m_pd3dDevice->m_stereo3D == STEREO_VR) {//Place it somewhere at the bottom
         m_pd3dDevice->SetRenderState(RenderDevice::ZENABLE, FALSE);
         zDisabled = true;
         m_pd3dDevice->DMDShader->SetVector("quadOffsetScale", (scale / 2.0f)*tableWidth, (scale / 3.0f)*tableWidth, scale, scale*DMDheight / DMDwidth);
      }
      else//No VR, so place it where it was intended
         m_pd3dDevice->DMDShader->SetVector("quadOffsetScale", DMDposx, DMDposy, DMDwidth, DMDheight);

      m_pd3dDevice->DMDShader->Begin(0);
      m_pd3dDevice->DrawTexturedQuad();
      m_pd3dDevice->DMDShader->End();

      if (zDisabled)
         m_pd3dDevice->SetRenderState(RenderDevice::ZENABLE, TRUE);

      m_pd3dDevice->DMDShader->SetVector("quadOffsetScale", 0.0f, 0.0f, 1.0f, 1.0f);
   }
}
