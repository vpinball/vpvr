#include "StdAfx.h"
#include "RenderDevice.h"
#include "Shader.h"
#include "rapidxml/rapidxml_utils.hpp"
#include <iostream>
#include <fstream>

int NumVideoBytes = 0;

Pin3D::Pin3D()
{
   m_pddsBackBuffer = NULL;
   m_pddsAOBackBuffer = NULL;
   m_pddsAOBackTmpBuffer = NULL;
   m_pddsZBuffer = NULL;
   m_pdds3DZBuffer = NULL;
   m_pd3dPrimaryDevice = NULL;
   m_pd3dSecondaryDevice = NULL;
   m_envRadianceTexture = NULL;
   tableVBuffer = NULL;

   m_camx = 0.f;
   m_camy = 0.f;
   m_camz = 0.f;
   m_inc  = 0.f;
}

Pin3D::~Pin3D()
{
#ifdef FPS
   m_gpu_profiler.Shutdown();
#endif
   m_pd3dPrimaryDevice->UnSetZBuffer();
   m_pd3dPrimaryDevice->FreeShader();

   pinballEnvTexture.FreeStuff();

   envTexture.FreeStuff();

   aoDitherTexture.FreeStuff();

   if (m_envRadianceTexture)
   {
      m_pd3dPrimaryDevice->m_texMan.UnloadTexture(m_envRadianceTexture);
      delete m_envRadianceTexture;
      m_envRadianceTexture = NULL;
   }

   if (tableVBuffer)
      tableVBuffer->release();

   SAFE_RELEASE(m_pddsAOBackBuffer);
   SAFE_RELEASE(m_pddsAOBackTmpBuffer);
   if (!m_pd3dPrimaryDevice->m_useNvidiaApi && m_pd3dPrimaryDevice->m_INTZ_support)
   {
      SAFE_RELEASE_NO_SET((D3DTexture*)m_pddsZBuffer);
   }
   else
   {
      SAFE_RELEASE_NO_SET((RenderTarget*)m_pddsZBuffer);
   }
   m_pddsZBuffer = NULL;
   SAFE_RELEASE(m_pdds3DZBuffer);

   SAFE_RELEASE_NO_RCC(m_pddsBackBuffer);

   if (m_pd3dSecondaryDevice && (m_pd3dSecondaryDevice != m_pd3dPrimaryDevice)) delete m_pd3dSecondaryDevice;
   delete m_pd3dPrimaryDevice;

   m_pd3dPrimaryDevice = NULL;
   m_pd3dSecondaryDevice = NULL;
}

void Pin3D::TransformVertices(const Vertex3D_NoTex2 * rgv, const WORD * rgi, int count, Vertex2D * rgvout) const
{
   // Get the width and height of the viewport. This is needed to scale the
   // transformed vertices to fit the render window.
   const float rClipWidth = (float)m_viewPort.Width*0.5f;
   const float rClipHeight = (float)m_viewPort.Height*0.5f;
   const int xoffset = m_viewPort.X;
   const int yoffset = m_viewPort.Y;

   // Transform each vertex through the current matrix set
   for (int i = 0; i < count; ++i)
   {
      const int l = rgi ? rgi[i] : i;

      // Get the untransformed vertex position
      const float x = rgv[l].x;
      const float y = rgv[l].y;
      const float z = rgv[l].z;

      // Transform it through the current matrix set
      const float xp = m_proj.m_matrixTotal[0]._11*x + m_proj.m_matrixTotal[0]._21*y + m_proj.m_matrixTotal[0]._31*z + m_proj.m_matrixTotal[0]._41;
      const float yp = m_proj.m_matrixTotal[0]._12*x + m_proj.m_matrixTotal[0]._22*y + m_proj.m_matrixTotal[0]._32*z + m_proj.m_matrixTotal[0]._42;
      const float wp = m_proj.m_matrixTotal[0]._14*x + m_proj.m_matrixTotal[0]._24*y + m_proj.m_matrixTotal[0]._34*z + m_proj.m_matrixTotal[0]._44;

      // Finally, scale the vertices to screen coords. This step first
      // "flattens" the coordinates from 3D space to 2D device coordinates,
      // by dividing each coordinate by the wp value. Then, the x- and
      // y-components are transformed from device coords to screen coords.
      // Note 1: device coords range from -1 to +1 in the viewport.
      const float inv_wp = 1.0f / wp;
      const float vTx = (1.0f + xp*inv_wp) * rClipWidth + xoffset;
      const float vTy = (1.0f - yp*inv_wp) * rClipHeight + yoffset;

      rgvout[l].x = vTx;
      rgvout[l].y = vTy;
   }
}

void EnvmapPrecalc(const void* const __restrict envmap, const DWORD env_xres, const DWORD env_yres, void* const __restrict rad_envmap, const DWORD rad_env_xres, const DWORD rad_env_yres, const bool isHDR)
{
   // brute force sampling over hemisphere for each normal direction of the to-be-(ir)radiance-baked environment
   // not the fastest solution, could do a "cosine convolution" over the picture instead (where also just 1024 or x samples could be used per pixel)
   // but with this implementation one can also have custom maps/LUTs for glossy, etc. later-on
   for (unsigned int y = 0; y < rad_env_yres; ++y)
      for (unsigned int x = 0; x < rad_env_xres; ++x)
      {
         // trafo from envmap to normal direction
         const float phi = (float)x / (float)rad_env_xres * (float)(2.0*M_PI) + (float)M_PI;
         const float theta = (float)y / (float)rad_env_yres * (float)M_PI;
         const Vertex3Ds n(sinf(theta) * cosf(phi), sinf(theta) * sinf(phi), cosf(theta));

         // draw x samples over hemisphere and collect cosine weighted environment map samples
         float sum[3];
         sum[0] = sum[1] = sum[2] = 0.0f;

         const unsigned int num_samples = 4096;
         for (unsigned int s = 0; s < num_samples; ++s)
         {
            //!! discard directions pointing below the playfield?? or give them another "average playfield" color??
#define USE_ENVMAP_PRECALC_COSINE
#ifndef USE_ENVMAP_PRECALC_COSINE
            //!! as we do not use importance sampling on the environment, just not being smart -could- be better for high frequency environments
            Vertex3Ds l = sphere_sample((float)s*(float)(1.0/num_samples), radical_inverse(s)); // QMC hammersley point set
            float NdotL = l.Dot(n);
            if (NdotL < 0.0f) // flip if on backside of hemisphere
            {
               NdotL = -NdotL;
               l = -l;
            }
#else
            //Vertex3Ds cos_hemisphere_sample(const Vertex3Ds &normal, Vertex2D uv) { float theta = (float)(2.*M_PI) * uv.x; uv.y = 2.f * uv.y - 1.f; Vertex3Ds spherePoint(sqrt(1.f - uv.y * uv.y) * Vertex2D(cosf(theta), sinf(theta)), uv.y); return normalize(normal + spherePoint); }
            const Vertex3Ds l = rotate_to_vector_upper(cos_hemisphere_sample((float)s*(float)(1.0 / num_samples), radical_inverse(s)), n); // QMC hammersley point set
#endif
            // trafo from light direction to envmap
            const float u = atan2f(l.y, l.x) * (float)(0.5 / M_PI) + 0.5f;
            const float v = acosf(l.z) * (float)(1.0 / M_PI);

	    float r,g,b;
	    if (isHDR)
	    {
		unsigned int offs = ((int)(u*(float)env_xres) + (int)(v*(float)env_yres)*env_xres)*3;
		if (offs >= env_yres*env_xres*3)
		    offs = 0;
		r = ((float*)envmap)[offs];
		g = ((float*)envmap)[offs+1];
		b = ((float*)envmap)[offs+2];
	    }
	    else
	    {
               unsigned int offs = (int)(u*(float)env_xres) + (int)(v*(float)env_yres)*env_xres;
	       if (offs >= env_yres*env_xres)
		   offs = 0;
               const DWORD rgb = ((DWORD*)envmap)[offs];
               r = invGammaApprox((float)(rgb & 255) * (float)(1.0 / 255.0));
               g = invGammaApprox((float)(rgb & 65280) * (float)(1.0 / 65280.0));
               b = invGammaApprox((float)(rgb & 16711680) * (float)(1.0 / 16711680.0));
	    }
#ifndef USE_ENVMAP_PRECALC_COSINE
            sum[0] += r * NdotL;
            sum[1] += g * NdotL;
            sum[2] += b * NdotL;
#else
            sum[0] += r;
            sum[1] += g;
            sum[2] += b;
#endif
         }

         // average all samples
#ifndef USE_ENVMAP_PRECALC_COSINE
         sum[0] *= (float)(2.0/num_samples); // pre-divides by PI for final radiance/color lookup in shader
         sum[1] *= (float)(2.0/num_samples);
         sum[2] *= (float)(2.0/num_samples);
#else
         sum[0] *= (float)(1.0 / num_samples); // pre-divides by PI for final radiance/color lookup in shader
         sum[1] *= (float)(1.0 / num_samples);
         sum[2] *= (float)(1.0 / num_samples);
#endif
	 if (isHDR)
	 {
            const unsigned int offs = (y*rad_env_xres + x)*3;
            ((float*)rad_envmap)[offs  ] = sum[0];
	    ((float*)rad_envmap)[offs+1] = sum[1];
	    ((float*)rad_envmap)[offs+2] = sum[2];
	 }
	 else
	 {
            sum[0] = gammaApprox(sum[0]);
            sum[1] = gammaApprox(sum[1]);
            sum[2] = gammaApprox(sum[2]);
	    ((DWORD*)rad_envmap)[y*rad_env_xres + x] = ((int)(sum[0] * 255.0f)) | (((int)(sum[1] * 255.0f)) << 8) | (((int)(sum[2] * 255.0f)) << 16);
	 }
      }
}

HRESULT Pin3D::InitPrimary(HWND *hwnd, const bool fullScreen, const int colordepth, int &refreshrate, const int VSync, const int stereo3D, const unsigned int FXAA, const bool useAO, const bool ss_refl)
{
   const unsigned int display = GetRegIntWithDefault("Player", "Display", 0);
#ifdef ENABLE_VR
   if ((stereo3D == STEREO_VR) && !vr::VR_IsHmdPresent()) {
	   MessageBox(m_hwnd,"Please start SteamVR or go to Video Options to disable VR support.", "SteamVR", MB_OK);
	   return E_FAIL;
   }
#endif
   m_pd3dPrimaryDevice = new RenderDevice(hwnd, m_viewPort.Width, m_viewPort.Height, fullScreen, colordepth, VSync, m_useAA, stereo3D, FXAA, ss_refl, g_pplayer->m_useNvidiaApi, g_pplayer->m_disableDWM, g_pplayer->m_BWrendering);
   try {
      m_pd3dPrimaryDevice->CreateDevice(refreshrate, display);
   }
   catch (...) {
      return E_FAIL;
   }

   if (!m_pd3dPrimaryDevice->LoadShaders())
      return E_FAIL;

#ifdef ENABLE_SDL
   *hwnd = m_pd3dPrimaryDevice->getHwnd();
#endif

   const int forceAniso = GetRegIntWithDefault("Player", "ForceAnisotropicFiltering", 1);
   m_pd3dPrimaryDevice->ForceAnisotropicFiltering(!!forceAniso);

   const int compressTextures = GetRegIntWithDefault("Player", "CompressTextures", 0);
   m_pd3dPrimaryDevice->CompressTextures(!!compressTextures);

   m_pd3dPrimaryDevice->SetViewport(&m_viewPort);

   m_pddsZBuffer = m_pd3dPrimaryDevice->AttachZBufferTo(m_pd3dPrimaryDevice->GetBackBufferTexture());

#ifdef ENABLE_SDL
   m_pddsBackBuffer = m_pd3dPrimaryDevice->GetBackBufferTexture();

#else
   m_pd3dPrimaryDevice->GetBackBufferTexture()->GetSurfaceLevel(0, &m_pddsBackBuffer);

   if (m_pd3dPrimaryDevice->DepthBufferReadBackAvailable() && (stereo3D || useAO || ss_refl))
      m_pdds3DZBuffer = !m_pd3dPrimaryDevice->m_useNvidiaApi ? (D3DTexture*)m_pd3dPrimaryDevice->AttachZBufferTo(m_pddsBackBuffer) : m_pd3dPrimaryDevice->DuplicateDepthTexture((RenderTarget*)m_pddsZBuffer);

   if (m_pd3dPrimaryDevice->DepthBufferReadBackAvailable() && ((stereo3D!=STEREO_OFF) || useAO || ss_refl)) {
      m_pd3dPrimaryDevice->GetBackBufferTexture()->GetSurfaceLevel(0, &m_pddsBackBuffer);
      m_pdds3DZBuffer = !m_pd3dPrimaryDevice->m_useNvidiaApi ? (D3DTexture*)m_pd3dPrimaryDevice->AttachZBufferTo(m_pddsBackBuffer) : m_pd3dPrimaryDevice->DuplicateDepthTexture((RenderTarget*)m_pddsZBuffer);
      if (!m_pdds3DZBuffer)
      {
         ShowError("Unable to create depth texture!\r\nTry to (un)set \"Alternative Depth Buffer processing\" in the video options!\r\nOr disable Ambient Occlusion, 3D stereo and/or ScreenSpace Reflections!");
         return E_FAIL;
      }

      if (stereo3D != STEREO_OFF) {
         m_pd3dPrimaryDevice->GetBackBufferTexture()->GetSurfaceLevel(0, &m_pddsBackBuffer);
         m_pdds3DZBuffer = !m_pd3dPrimaryDevice->m_useNvidiaApi ? (D3DTexture*)m_pd3dPrimaryDevice->AttachZBufferTo(m_pddsBackBuffer) : m_pd3dPrimaryDevice->DuplicateDepthTexture((RenderTarget*)m_pddsZBuffer);
         if (!m_pdds3DZBuffer)
         {
            ShowError("Unable to create depth texture!\r\nTry to (un)set \"Alternative Depth Buffer processing\" in the video options!\r\nOr disable Ambient Occlusion and/or 3D stereo!");
            return E_FAIL;
         }
      }
   }
#endif

   if (m_pd3dPrimaryDevice->DepthBufferReadBackAvailable() && useAO) 
   {
      m_pddsAOBackTmpBuffer = m_pd3dPrimaryDevice->CreateTexture(m_viewPort.Width, m_viewPort.Height, 1, RENDERTARGET, colorFormat::GREY, NULL, stereo3D);

      m_pddsAOBackBuffer = m_pd3dPrimaryDevice->CreateTexture(m_viewPort.Width, m_viewPort.Height, 1, RENDERTARGET, colorFormat::GREY, NULL, stereo3D);

       if (!m_pddsAOBackBuffer || !m_pddsAOBackTmpBuffer)
          return E_FAIL;
   }

#ifdef ENABLE_VR
   if (stereo3D == STEREO_VR) {
      InitBackglassVR();//Should be done after the RenderDevice exists and the table is loaded.
   }
#endif
   return S_OK;
}

HRESULT Pin3D::InitPin3D(HWND *hwnd, const bool fullScreen, const int width, const int height, const int colordepth, int &refreshrate, const int VSync, const bool useAA, const int stereo3D, const unsigned int FXAA, const bool useAO, const bool ss_refl)
{
   m_proj.m_stereo3D = m_stereo3D = stereo3D;
   m_useAA = useAA;

   // set the viewport for the newly created device
   m_viewPort.X = 0;
   m_viewPort.Y = 0;
   m_viewPort.Width = width;
   m_viewPort.Height = height;
   m_viewPort.MinZ = 0.0f;
   m_viewPort.MaxZ = 1.0f;

   if ((InitPrimary(hwnd, fullScreen, colordepth, refreshrate, VSync, stereo3D, FXAA, useAO, ss_refl)))
      return E_FAIL;

   m_pd3dSecondaryDevice = m_pd3dPrimaryDevice;

   pinballEnvTexture.CreateFromResource(IDB_BALL);

   aoDitherTexture.CreateFromResource(IDB_AO_DITHER);

   m_envTexture = g_pplayer->m_ptable->GetImage(g_pplayer->m_ptable->m_szEnvImage);
   envTexture.CreateFromResource(IDB_ENV);

   Texture * const envTex = m_envTexture ? m_envTexture : &envTexture;

   const unsigned int envTexHeight = min(envTex->m_pdsBuffer->height(), 256) / 8;
   const unsigned int envTexWidth = envTexHeight * 2;

   m_envRadianceTexture = new BaseTexture(envTexWidth, envTexHeight, envTex->m_pdsBuffer->m_format);

   EnvmapPrecalc(envTex->m_pdsBuffer->data(), envTex->m_pdsBuffer->width(), envTex->m_pdsBuffer->height(),
      m_envRadianceTexture->data(), envTexWidth, envTexHeight, envTex->IsHDR());

   m_pd3dPrimaryDevice->m_texMan.SetDirty(m_envRadianceTexture);

   InitPrimaryRenderState();

   if (m_pddsBackBuffer)
      SetPrimaryRenderTarget(m_pddsBackBuffer, m_pddsZBuffer);

   return S_OK;
}

inline char nextChar(size_t &inPos,size_t inSize, const char* inChars, const char* outChars, const char* inData) {
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
      for (size_t i = 0;i < 64;++i) outChars[inChars[i]] = i;
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
   return min(outPos - padding,outSize);
}

//Set to 1 if you want to debug the backglass textures, 0 otherwise
#define WRITE_BACKGLASS_IMAGES 0

//Move this to its own Backglass class?
void Pin3D::InitBackglassVR()
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
         throw(new std::runtime_error("Root node of Backglass is not DirectB2SData. Probably not a valid backglass."));
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
                  if (WRITE_BACKGLASS_IMAGES > 0 && size > 0) {//Write Image to disk. Also check if the base64 decoder is working...
                     std::string imageFileName = b2sFileName;
                     imageFileName.append(illuminationNode->name()).append(".bulb").append(std::to_string(bulb)).append(".png");//if it is not a png just rename it...
                     std::ofstream imageFile(imageFileName, std::ios::out | std::ios::binary | std::ios::trunc);
                     if (imageFile.is_open()) {
                        imageFile.write((char*)data, size);
                        imageFile.close();
                     }
                  }
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
                     backglass_texture = m_pd3dPrimaryDevice->m_texMan.LoadTexture(BaseTexture::CreateFromData(data, size), true);
                     backglass_width = backglass_texture->width;
                     backglass_height = backglass_texture->height;
                  }
                  if (WRITE_BACKGLASS_IMAGES > 0 && size > 0) {//Write Image to disk. Also useful to check if the base64 decoder is working...
                     std::string imageFileName = b2sFileName;
                     imageFileName.append(imagesNode->name()).append(".png");//if it is not a png just rename it...
                     std::ofstream imageFile(imageFileName, std::ios::out | std::ios::binary | std::ios::trunc);
                     if (imageFile.is_open()) {
                        imageFile.write((char*)data, size);
                        imageFile.close();
                     }
                  }
               }
               imagesNode = imagesNode->next_sibling();
            }
         }
         currentNode = currentNode->next_sibling();
      }
   }
   catch (...) {//If file does not exist, or something else goes wrong just disable the Backglass. This is very experimental anyway.
      backglass_texture = NULL;
   }
   if (data) free(data);
   float tableWidth, glassHeight;
   g_pplayer->m_ptable->get_Width(&tableWidth);
   g_pplayer->m_ptable->get_GlassHeight(&glassHeight);
   if (backglass_width>0 && backglass_height>0)
      m_pd3dPrimaryDevice->DMDShader->SetVector("backBoxSize", tableWidth * (0.5f - backglass_scale / 2.0f), glassHeight, backglass_scale * tableWidth, backglass_scale * tableWidth / (float)backglass_width*(float)backglass_height);
   else
      m_pd3dPrimaryDevice->DMDShader->SetVector("backBoxSize", tableWidth * (0.5f - backglass_scale / 2.0f), glassHeight, backglass_scale * tableWidth, backglass_scale * tableWidth / 16.0f*9.0f);
#endif
}

// Sets the texture filtering state.
void Pin3D::SetTextureFilter(RenderDevice * const pd3dDevice, const int TextureNum, const int Mode) const
{
   pd3dDevice->SetTextureFilter(TextureNum, Mode);
}

void Pin3D::SetPrimaryTextureFilter(const int TextureNum, const int Mode) const
{
   SetTextureFilter(m_pd3dPrimaryDevice, TextureNum, Mode);
}

void Pin3D::SetSecondaryTextureFilter(const int TextureNum, const int Mode) const
{
   SetTextureFilter(m_pd3dSecondaryDevice, TextureNum, Mode);
}

#ifdef ENABLE_SDL
void Pin3D::SetRenderTarget(RenderDevice * const pd3dDevice, RenderTarget* pddsSurface, void* unused) const
{
   pd3dDevice->SetRenderTarget(pddsSurface);
}

void Pin3D::SetPrimaryRenderTarget(RenderTarget* pddsSurface, void* unused) const
{
   SetRenderTarget(m_pd3dPrimaryDevice, pddsSurface, unused);
}

void Pin3D::SetSecondaryRenderTarget(RenderTarget* pddsSurface, void* unused) const
{
   SetRenderTarget(m_pd3dSecondaryDevice, pddsSurface, unused);
}
#else

void Pin3D::SetRenderTarget(RenderDevice * const pd3dDevice, RenderTarget* pddsSurface, void* pddsZ) const
{
   pd3dDevice->SetRenderTarget(pddsSurface);
   if (!pd3dDevice->m_useNvidiaApi && m_pd3dPrimaryDevice->m_INTZ_support)
      pd3dDevice->SetZBuffer((D3DTexture*)pddsZ);
   else
      pd3dDevice->SetZBuffer((RenderTarget*)pddsZ);
}

void Pin3D::SetRenderTarget(RenderDevice * const pd3dDevice, D3DTexture* pddsSurface, void* pddsZ) const
{
   pd3dDevice->SetRenderTarget(pddsSurface);
   if (!pd3dDevice->m_useNvidiaApi && m_pd3dPrimaryDevice->m_INTZ_support)
      pd3dDevice->SetZBuffer((D3DTexture*)pddsZ);
   else
      pd3dDevice->SetZBuffer((RenderTarget*)pddsZ);
}

void Pin3D::SetRenderTarget(RenderDevice * const pd3dDevice, D3DTexture* pddsSurface, RenderTarget* pddsZ) const
{
   m_pd3dPrimaryDevice->SetRenderTarget(pddsSurface);
   m_pd3dPrimaryDevice->SetZBuffer(pddsZ);
}

void Pin3D::SetRenderTarget(RenderDevice * const pd3dDevice, D3DTexture* pddsSurface, D3DTexture* pddsZ) const
{
   m_pd3dPrimaryDevice->SetRenderTarget(pddsSurface);
   m_pd3dPrimaryDevice->SetZBuffer(pddsZ);
}

void Pin3D::SetPrimaryRenderTarget(RenderTarget* pddsSurface, void* pddsZ) const
{
   SetRenderTarget(m_pd3dPrimaryDevice, pddsSurface, pddsZ);
}

void Pin3D::SetPrimaryRenderTarget(D3DTexture* pddsSurface, void* pddsZ) const
{
   SetRenderTarget(m_pd3dPrimaryDevice, pddsSurface, pddsZ);
}

void Pin3D::SetPrimaryRenderTarget(D3DTexture* pddsSurface, RenderTarget* pddsZ) const
{
   SetRenderTarget(m_pd3dPrimaryDevice, pddsSurface, pddsZ);
}

void Pin3D::SetPrimaryRenderTarget(D3DTexture* pddsSurface, D3DTexture* pddsZ) const
{
   SetRenderTarget(m_pd3dPrimaryDevice, pddsSurface, pddsZ);
}

void Pin3D::SetSecondaryRenderTarget(RenderTarget* pddsSurface, void* pddsZ) const
{
   SetRenderTarget(m_pd3dSecondaryDevice, pddsSurface, pddsZ);
}

void Pin3D::SetSecondaryRenderTarget(D3DTexture* pddsSurface, void* pddsZ) const
{
   SetRenderTarget(m_pd3dSecondaryDevice, pddsSurface, pddsZ);
}

void Pin3D::SetSecondaryRenderTarget(D3DTexture* pddsSurface, RenderTarget* pddsZ) const
{
   SetRenderTarget(m_pd3dSecondaryDevice, pddsSurface, pddsZ);
}

void Pin3D::SetSecondaryRenderTarget(D3DTexture* pddsSurface, D3DTexture* pddsZ) const
{
   SetRenderTarget(m_pd3dSecondaryDevice, pddsSurface, pddsZ);
}
#endif

void Pin3D::InitRenderState(RenderDevice * const pd3dDevice)
{
   pd3dDevice->SetRenderState(RenderDevice::ALPHABLENDENABLE, RenderDevice::RS_FALSE);

   pd3dDevice->SetRenderState(RenderDevice::LIGHTING, FALSE);

   pd3dDevice->SetRenderState(RenderDevice::ZENABLE, TRUE);
   pd3dDevice->SetRenderState(RenderDevice::ZWRITEENABLE, RenderDevice::RS_TRUE);
   pd3dDevice->SetRenderStateCulling(RenderDevice::CULL_CCW);

   pd3dDevice->SetRenderState(RenderDevice::CLIPPING, FALSE);
   pd3dDevice->SetRenderState(RenderDevice::CLIPPLANEENABLE, 0);

   // initialize first texture stage
   pd3dDevice->SetTextureAddressMode(0, RenderDevice::TEX_CLAMP/*WRAP*/);
#ifndef ENABLE_SDL
   pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
   pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
   pd3dDevice->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
   pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
   pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
   pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR); // default tfactor: 1,1,1,1
#endif
   SetTextureFilter(pd3dDevice, 0, TEXTURE_MODE_TRILINEAR);
   pd3dDevice->SetTextureAddressMode(4, RenderDevice::TEX_CLAMP/*WRAP*/); // normal maps
   SetTextureFilter(pd3dDevice, 4, TEXTURE_MODE_TRILINEAR);
}

void Pin3D::InitPrimaryRenderState()
{
   InitRenderState(m_pd3dPrimaryDevice);
}

void Pin3D::InitSecondaryRenderState()
{
   InitRenderState(m_pd3dSecondaryDevice);
}

void Pin3D::DrawBackground()
{
   SetPrimaryTextureFilter(0, TEXTURE_MODE_TRILINEAR);

   PinTable * const ptable = g_pplayer->m_ptable;
   Texture * const pin = ptable->GetDecalsEnabled()
      ? ptable->GetImage(ptable->m_BG_szImage[ptable->m_BG_current_set])
      : NULL;
   if (pin)
   {
      m_pd3dPrimaryDevice->Clear(ZBUFFER, 0, 1.0f, 0L);

      m_pd3dPrimaryDevice->SetRenderState(RenderDevice::ZWRITEENABLE, RenderDevice::RS_FALSE);
      m_pd3dPrimaryDevice->SetRenderState(RenderDevice::ZENABLE, FALSE);

      if (g_pplayer->m_ptable->m_tblMirrorEnabled^g_pplayer->m_ptable->m_fReflectionEnabled)
         m_pd3dPrimaryDevice->SetRenderStateCulling(RenderDevice::CULL_NONE);

      m_pd3dPrimaryDevice->SetRenderState(RenderDevice::ALPHABLENDENABLE, RenderDevice::RS_FALSE);

      g_pplayer->Spritedraw(0.f, 0.f, 1.f, 1.f, 0xFFFFFFFF, pin, ptable->m_ImageBackdropNightDay ? sqrtf(g_pplayer->m_globalEmissionScale) : 1.0f);

      if (g_pplayer->m_ptable->m_tblMirrorEnabled^g_pplayer->m_ptable->m_fReflectionEnabled)
         m_pd3dPrimaryDevice->SetRenderStateCulling(RenderDevice::CULL_CCW);

      m_pd3dPrimaryDevice->SetRenderState(RenderDevice::ZENABLE, TRUE);
      m_pd3dPrimaryDevice->SetRenderState(RenderDevice::ZWRITEENABLE, RenderDevice::RS_TRUE);
   }
   else
   {
      const D3DCOLOR d3dcolor = COLORREF_to_D3DCOLOR(ptable->m_colorbackdrop);
      m_pd3dPrimaryDevice->Clear(TARGET | ZBUFFER, d3dcolor, 1.0f, 0L);
   }
}

void Pin3D::DrawBackglass()
{
   SetPrimaryTextureFilter(0, TEXTURE_MODE_TRILINEAR);

   PinTable * const ptable = g_pplayer->m_ptable;
   Texture * const background = ptable->GetDecalsEnabled()
      ? ptable->GetImage(ptable->m_BG_szImage[ptable->m_BG_current_set])
      : NULL;

   if (backglass_texture)
      m_pd3dPrimaryDevice->DMDShader->SetTexture("Texture0", backglass_texture, false);
   else if (background)
      m_pd3dPrimaryDevice->DMDShader->SetTexture("Texture0", background, false);
   else return;

   m_pd3dPrimaryDevice->SetRenderState(RenderDevice::ZWRITEENABLE, RenderDevice::RS_FALSE);
   m_pd3dPrimaryDevice->SetRenderState(RenderDevice::ZENABLE, FALSE);
   m_pd3dPrimaryDevice->SetRenderStateCulling(RenderDevice::CULL_NONE);

   m_pd3dPrimaryDevice->SetRenderState(RenderDevice::ALPHABLENDENABLE, RenderDevice::RS_FALSE);

   m_pd3dPrimaryDevice->DMDShader->SetTechnique("basic_noDMD");

   m_pd3dPrimaryDevice->DMDShader->SetVector("vColor_Intensity", 1.0,1.0,1.0,1.0);

   m_pd3dPrimaryDevice->DMDShader->Begin(0);
   m_pd3dPrimaryDevice->DrawTexturedQuad();
   m_pd3dPrimaryDevice->DMDShader->End();

   m_pd3dPrimaryDevice->DMDShader->SetVector("quadOffsetScale", 0.0f, 0.0f, 1.0f, 1.0f);

   m_pd3dPrimaryDevice->SetRenderStateCulling(RenderDevice::CULL_CCW);

   m_pd3dPrimaryDevice->SetRenderState(RenderDevice::ZENABLE, TRUE);
   m_pd3dPrimaryDevice->SetRenderState(RenderDevice::ZWRITEENABLE, RenderDevice::RS_TRUE);
}

void Pin3D::InitLights()
{
   //m_pd3dPrimaryDevice->basicShader->SetInt("iLightPointNum",MAX_LIGHT_SOURCES);
#ifdef SEPARATE_CLASSICLIGHTSHADER
   //m_pd3dPrimaryDevice->classicLightShader->SetInt("iLightPointNum",MAX_LIGHT_SOURCES);
#endif

   g_pplayer->m_ptable->m_Light[0].pos.x = g_pplayer->m_ptable->m_right*0.5f;
   g_pplayer->m_ptable->m_Light[1].pos.x = g_pplayer->m_ptable->m_right*0.5f;
   g_pplayer->m_ptable->m_Light[0].pos.y = g_pplayer->m_ptable->m_bottom*(float)(1.0 / 3.0);
   g_pplayer->m_ptable->m_Light[1].pos.y = g_pplayer->m_ptable->m_bottom*(float)(2.0 / 3.0);
   g_pplayer->m_ptable->m_Light[0].pos.z = g_pplayer->m_ptable->m_lightHeight;
   g_pplayer->m_ptable->m_Light[1].pos.z = g_pplayer->m_ptable->m_lightHeight;

   vec4 emission = convertColor(g_pplayer->m_ptable->m_Light[0].emission);
   emission.x *= g_pplayer->m_ptable->m_lightEmissionScale*g_pplayer->m_globalEmissionScale;
   emission.y *= g_pplayer->m_ptable->m_lightEmissionScale*g_pplayer->m_globalEmissionScale;
   emission.z *= g_pplayer->m_ptable->m_lightEmissionScale*g_pplayer->m_globalEmissionScale;

   struct CLight
   {
      float vPos[3];
      float vEmission[3];
   };
   CLight l[MAX_LIGHT_SOURCES];
   for (unsigned int i = 0; i < MAX_LIGHT_SOURCES; ++i)
   {
      memcpy(&l[i].vPos, &g_pplayer->m_ptable->m_Light[i].pos, sizeof(float) * 3);
      memcpy(&l[i].vEmission, &emission, sizeof(float) * 3);
   }
   m_pd3dPrimaryDevice->basicShader->SetFloatArray("packedLights", (float*)l, 6*MAX_LIGHT_SOURCES);
#ifdef SEPARATE_CLASSICLIGHTSHADER
   m_pd3dPrimaryDevice->classicLightShader->SetFloatArray("packedLights", (float*)l, 6*MAX_LIGHT_SOURCES);
#endif

   vec4 amb_lr = convertColor(g_pplayer->m_ptable->m_lightAmbient, g_pplayer->m_ptable->m_lightRange);
   amb_lr.x *= g_pplayer->m_globalEmissionScale;
   amb_lr.y *= g_pplayer->m_globalEmissionScale;
   amb_lr.z *= g_pplayer->m_globalEmissionScale;
   m_pd3dPrimaryDevice->basicShader->SetVector("cAmbient_LightRange", &amb_lr);
#ifdef SEPARATE_CLASSICLIGHTSHADER
   m_pd3dPrimaryDevice->classicLightShader->SetVector("cAmbient_LightRange", &amb_lr);
#endif

}

// currently unused
//void LookAt( Matrix3D &mat, D3DVECTOR eye, D3DVECTOR target, D3DVECTOR up )
//{
//   D3DVECTOR zaxis = Normalize(eye - target);
//   D3DVECTOR xaxis = Normalize(CrossProduct(up,zaxis));
//   D3DVECTOR yaxis = CrossProduct(zaxis,xaxis);
//   mat._11 = xaxis.x; mat._12 = yaxis.x; mat._13 = zaxis.x; mat._14=0.0f;
//   mat._21 = xaxis.y; mat._22 = yaxis.y; mat._23 = zaxis.y; mat._24=0.0f;
//   mat._31 = xaxis.z; mat._32 = yaxis.z; mat._33 = zaxis.z; mat._34=0.0f;
//   mat._41 = 0.0f;    mat._42 = 0.0f;    mat._43 = zaxis.x; mat._44=0.0f;
//   Matrix3D trans;
//   trans.SetIdentity();
//   trans._41 = eye.x; trans._42 = eye.y; trans._43=eye.z;
//   mat.Multiply( trans, mat );
//}

Matrix3D ComputeLaybackTransform(const float layback)
{
   // skew the coordinate system from kartesian to non kartesian.
   Matrix3D matTrans;
   matTrans.SetIdentity();
   matTrans._32 = -tanf(0.5f * ANGTORAD(layback));
   return matTrans;
}

void Pin3D::UpdateMatrices()
{
#ifdef ENABLE_VR
   if (m_stereo3D == STEREO_VR) {
      m_pd3dPrimaryDevice->SetTransformVR();
      Shader::GetTransform(TRANSFORMSTATE_PROJECTION, m_proj.m_matProj, 2);
      Shader::GetTransform(TRANSFORMSTATE_VIEW, &m_proj.m_matView, 1);
   } else
#endif
   {
      Shader::SetTransform(TRANSFORMSTATE_PROJECTION, m_proj.m_matProj, m_stereo3D != STEREO_OFF  ? 2 : 1);
      Shader::SetTransform(TRANSFORMSTATE_VIEW, &m_proj.m_matView, 1);
   }
   Shader::SetTransform(TRANSFORMSTATE_WORLD, &m_proj.m_matWorld, 1);

   m_proj.CacheTransform();
}

void Pin3D::InitLayoutFS()
{
   TRACE_FUNCTION();

   const float rotation = ANGTORAD(g_pplayer->m_ptable->m_BG_rotation[g_pplayer->m_ptable->m_BG_current_set]);
   const float inclination = 0.0f;// ANGTORAD(g_pplayer->m_ptable->m_BG_inclination[g_pplayer->m_ptable->m_BG_current_set]);
   //const float FOV = (g_pplayer->m_ptable->m_BG_FOV[g_pplayer->m_ptable->m_BG_current_set] < 1.0f) ? 1.0f : g_pplayer->m_ptable->m_BG_FOV[g_pplayer->m_ptable->m_BG_current_set];

   std::vector<Vertex3Ds> vvertex3D;
   for (size_t i = 0; i < g_pplayer->m_ptable->m_vedit.size(); ++i)
      g_pplayer->m_ptable->m_vedit[i]->GetBoundingVertices(vvertex3D);

   m_proj.m_rcviewport.left = 0;
   m_proj.m_rcviewport.top = 0;
   m_proj.m_rcviewport.right = m_viewPort.Width;
   m_proj.m_rcviewport.bottom = m_viewPort.Height;

   const float aspect = (float)m_viewPort.Width / (float)m_viewPort.Height; //(float)(4.0/3.0);

   //m_proj.FitCameraToVerticesFS(vvertex3D, aspect, rotation, inclination, FOV, g_pplayer->m_ptable->m_BG_xlatez[g_pplayer->m_ptable->m_BG_current_set], g_pplayer->m_ptable->m_BG_layback[g_pplayer->m_ptable->m_BG_current_set]);
   const float yof = g_pplayer->m_ptable->m_bottom*0.5f + g_pplayer->m_ptable->m_BG_xlatey[g_pplayer->m_ptable->m_BG_current_set];
   const float camx = 0.0f;
   const float camy = g_pplayer->m_ptable->m_bottom*0.5f + g_pplayer->m_ptable->m_BG_xlatex[g_pplayer->m_ptable->m_BG_current_set];
   const float camz = g_pplayer->m_ptable->m_bottom + g_pplayer->m_ptable->m_BG_xlatez[g_pplayer->m_ptable->m_BG_current_set];
   m_proj.m_matWorld.SetIdentity();
   vec3 eye(camx, camy, camz);
   vec3 at(0.0f, yof, 1.0f);
   vec3 up(0.0f, -1.0f, 0.0f);

   Matrix3D rotationMat = Matrix3D::MatrixRotationYawPitchRoll(inclination, 0.0f, rotation);
#ifdef ENABLE_SDL
   eye = vec3::TransformCoord(eye, rotationMat);
   at = vec3::TransformCoord(at, rotationMat);
#else
   D3DXVec3TransformCoord(&eye, &eye, (const D3DXMATRIX*)&rotationMat);
   D3DXVec3TransformCoord(&at, &at, (const D3DXMATRIX*)&rotationMat);
#endif
   //D3DXVec3TransformCoord(&up, &up, &rotationMat);
   //at=eye+at;

   Matrix3D mView = Matrix3D::MatrixLookAtLH(eye, at, up);
   memcpy(m_proj.m_matView.m, mView.m, sizeof(float) * 4 * 4);
   m_proj.ScaleView(g_pplayer->m_ptable->m_BG_scalex[g_pplayer->m_ptable->m_BG_current_set], g_pplayer->m_ptable->m_BG_scaley[g_pplayer->m_ptable->m_BG_current_set], 1.0f);
   m_proj.RotateView(0, 0, rotation);
   m_proj.m_matWorld._41 = -g_pplayer->m_ptable->m_right*0.5f;//-m_proj.m_vertexcamera.x;
   m_proj.m_matWorld._42 = -g_pplayer->m_ptable->m_bottom*0.5f;//-m_proj.m_vertexcamera.y*1.0f;
   m_proj.m_matWorld._43 = -g_pplayer->m_ptable->m_glassheight;
   // recompute near and far plane (workaround for VP9 FitCameraToVertices bugs)
   m_proj.ComputeNearFarPlane(vvertex3D);
   Matrix3D proj;
   //proj = Matrix3D::MatrixPerspectiveFovLH(ANGTORAD(FOV), aspect, m_proj.m_rznear, m_proj.m_rzfar);
   //proj = Matrix3D::MatrixPerspectiveFovLH((float)(M_PI / 4.0), aspect, m_proj.m_rznear, m_proj.m_rzfar);
   // in-pixel offset for manual oversampling

   proj.SetIdentity();
   const float monitorPixel = 1.0f;// 25.4f * 23.3f / sqrt(1920.0f*1920.0f + 1080.0f*1080.0f);
   const float viewRight = monitorPixel*(float)m_viewPort.Width *0.5f;
   const float viewTop = monitorPixel*(float)m_viewPort.Height *0.5f;
   //eye.x = g_pplayer->m_ptable->m_bottom*0.4f;
   //eye.z += g_pplayer->m_ptable->m_glassheight;
   eye.z = g_pplayer->m_ptable->m_bottom;

   float right = viewRight - eye.x;
   float left = -viewRight - eye.x;
   float top = viewTop - eye.y;
   float bottom = -viewTop - eye.y;
   const float z_screen = eye.z >= m_proj.m_rznear ? eye.z : m_proj.m_rznear;

   // move edges of frustum from z_screen to z_near
   const float z_near_to_z_screen = m_proj.m_rznear / z_screen; // <=1.0
   right *= z_near_to_z_screen;
   left *= z_near_to_z_screen;
   top *= z_near_to_z_screen;
   bottom *= z_near_to_z_screen;

   //Create Projection Matrix - For Realtime Headtracking this matrix should be updated every frame. VR has its own V and P matrices.
   if (m_stereo3D != STEREO_OFF) {
      float stereoOffset = 0.03f;
      proj = Matrix3D::MatrixPerspectiveOffCenterLH(left - stereoOffset, right - stereoOffset, bottom, top, m_proj.m_rznear, m_proj.m_rzfar);
      memcpy(m_proj.m_matProj[0].m, proj.m, sizeof(float) * 4 * 4);
      proj = Matrix3D::MatrixPerspectiveOffCenterLH(left + stereoOffset, right + stereoOffset, bottom, top, m_proj.m_rznear, m_proj.m_rzfar);
      memcpy(m_proj.m_matProj[1].m, proj.m, sizeof(float) * 4 * 4);
   }
   else {
      proj = Matrix3D::MatrixPerspectiveOffCenterLH(left, right, bottom, top, m_proj.m_rznear, m_proj.m_rzfar);
      memcpy(m_proj.m_matProj[0].m, proj.m, sizeof(float) * 4 * 4);
   }

   //m_proj.m_cameraLength = sqrtf(m_proj.m_vertexcamera.x*m_proj.m_vertexcamera.x + m_proj.m_vertexcamera.y*m_proj.m_vertexcamera.y + m_proj.m_vertexcamera.z*m_proj.m_vertexcamera.z);

   UpdateMatrices();

   // Compute view vector
   /*Matrix3D temp, viewRot;
   temp = m_proj.m_matView;
   temp.Invert();
   temp.GetRotationPart( viewRot );
   viewRot.MultiplyVector(Vertex3Ds(0, 0, 1), m_viewVec);
   m_viewVec.Normalize();*/

   InitLights();
}

// here is where the tables camera / rotation / scale is setup
// flashers are ignored in the calculation of boundaries to center the
// table in the view
void Pin3D::InitLayout(const bool FSS_mode, const float xpixoff, const float ypixoff)
{
   TRACE_FUNCTION();

   const float rotation = ANGTORAD(g_pplayer->m_ptable->m_BG_rotation[g_pplayer->m_ptable->m_BG_current_set]);
   float inclination = ANGTORAD(g_pplayer->m_ptable->m_BG_inclination[g_pplayer->m_ptable->m_BG_current_set]);
   const float FOV = (g_pplayer->m_ptable->m_BG_FOV[g_pplayer->m_ptable->m_BG_current_set] < 1.0f) ? 1.0f : g_pplayer->m_ptable->m_BG_FOV[g_pplayer->m_ptable->m_BG_current_set];

   std::vector<Vertex3Ds> vvertex3D;
   for (size_t i = 0; i < g_pplayer->m_ptable->m_vedit.size(); ++i)
      g_pplayer->m_ptable->m_vedit[i]->GetBoundingVertices(vvertex3D);

   m_proj.m_rcviewport.left = 0;
   m_proj.m_rcviewport.top = 0;
   m_proj.m_rcviewport.right = m_viewPort.Width;
   m_proj.m_rcviewport.bottom = m_viewPort.Height;

   const float aspect = ((float)m_viewPort.Width) / ((float)m_viewPort.Height); //(float)(4.0/3.0);

   // next 4 def values for layout portrait(game vert) in landscape(screen horz)
   // for FSS, force an offset to camy which drops the table down 1/3 of the way.
   // some values to camy have been commented out because I found the default value 
   // better and just modify the camz and keep the table design inclination 
   // within 50-60 deg and 40-50 FOV in editor.
   // these values were tested against all known video modes upto 1920x1080 
   // in landscape and portrait on the display
   const float camx = m_camx;
   const float camy = m_camy + (FSS_mode ? 500.0f : 0.f);
         float camz = m_camz;
   const float inc  = m_inc  + (FSS_mode ? 0.2f : 0.f);

   if (FSS_mode)
   {
   //m_proj.m_rcviewport.right = m_viewPort.Height;
   //m_proj.m_rcviewport.bottom = m_viewPort.Width;
   const int width = GetSystemMetrics(SM_CXSCREEN);
   const int height = GetSystemMetrics(SM_CYSCREEN);

   // layout landscape(game horz) in lanscape(LCD\LED horz)
   if ((m_viewPort.Width > m_viewPort.Height) && (height < width))
   {
      //inc += 0.1f;       // 0.05-best, 0.1-good, 0.2-bad > (0.2 terrible original)
      //camy -= 30.0f;     // 70.0f original // 100
      if (aspect > 1.6f)
          camz -= 1170.0f; // 700
      else if (aspect > 1.5f)
          camz -= 1070.0f; // 650
      else if (aspect > 1.4f)
          camz -= 900.0f;  // 580
      else if (aspect > 1.3f)
          camz -= 820.0f;  // 500 // 600
      else
          camz -= 800.0f;  // 480
   }
   else {
      // layout potrait(game vert) in portrait(LCD\LED vert)
      if (height > width)
      {
         if (aspect > 0.6f) {
            camz += 10.0f;
            //camy += 50.0f;
         }
         else if (aspect > 0.5f) {
            camz += 300.0f;
            //camy += 100.0f;
         }
         else {
            camz += 300.0f;
            //camy += 200.0f;
         }
      }
      // layout landscape(game horz) in portrait(LCD\LED vert), who would but the UI allows for it!
      else {
      }
   }
   }

   inclination += inc; // added this to inclination in radians

   m_proj.FitCameraToVertices(vvertex3D, aspect, rotation, inclination, FOV, g_pplayer->m_ptable->m_BG_xlatez[g_pplayer->m_ptable->m_BG_current_set], g_pplayer->m_ptable->m_BG_layback[g_pplayer->m_ptable->m_BG_current_set]);

   m_proj.m_matWorld.SetIdentity();

   m_proj.m_matView.RotateXMatrix((float)M_PI);  // convert Z=out to Z=in (D3D coordinate system)
   m_proj.ScaleView(g_pplayer->m_ptable->m_BG_scalex[g_pplayer->m_ptable->m_BG_current_set], g_pplayer->m_ptable->m_BG_scaley[g_pplayer->m_ptable->m_BG_current_set], 1.0f);

   //!! FSS: added 500.0f to next line on camera y 
   //!! FSS: m_proj.m_vertexcamera.y += camy;
   //!! FSS: g_pplayer->m_ptable->m_BG_xlatey[g_pplayer->m_ptable->m_BG_current_set] += camy;
   //!! FSS: camy = 0.0f;

   m_proj.TranslateView(g_pplayer->m_ptable->m_BG_xlatex[g_pplayer->m_ptable->m_BG_current_set] - m_proj.m_vertexcamera.x + camx, g_pplayer->m_ptable->m_BG_xlatey[g_pplayer->m_ptable->m_BG_current_set] - m_proj.m_vertexcamera.y + camy, -m_proj.m_vertexcamera.z + camz);
   if (g_pplayer->cameraMode && (g_pplayer->m_ptable->m_BG_current_set == 0 || g_pplayer->m_ptable->m_BG_current_set == 2)) // DT & FSS
      m_proj.RotateView(inclination, 0, rotation);
   else
   {
      m_proj.RotateView(0, 0, rotation);
      m_proj.RotateView(inclination, 0, 0);
   }
   m_proj.MultiplyView(ComputeLaybackTransform(g_pplayer->m_ptable->m_BG_layback[g_pplayer->m_ptable->m_BG_current_set]));

   // recompute near and far plane (workaround for VP9 FitCameraToVertices bugs)
   m_proj.ComputeNearFarPlane(vvertex3D);
   if (fabsf(inclination) < 0.0075f) //!! magic threshold, otherwise kicker holes are missing for inclination ~0
      m_proj.m_rzfar += 10.f;
   Matrix3D proj = Matrix3D::MatrixPerspectiveFovLH(ANGTORAD(FOV), aspect, m_proj.m_rznear, m_proj.m_rzfar);

//   memcpy(m_proj.m_matProj.m, proj.m, sizeof(float) * 4 * 4);
   float top = m_proj.m_rznear * tanf(ANGTORAD(FOV) / 2.0f);
   float bottom = -top;
   float right = top * aspect;
   float left = -right;
   //Create Projection Matrix
   if (m_stereo3D != STEREO_OFF) {
      float stereoOffset = 0.04f*m_proj.m_rznear;
      proj = Matrix3D::MatrixPerspectiveOffCenterLH(left + stereoOffset, right + stereoOffset, bottom, top, m_proj.m_rznear, m_proj.m_rzfar);
      proj._41 += 1.4f * stereoOffset;
      memcpy(m_proj.m_matProj[0].m, proj.m, sizeof(float) * 4 * 4);
      proj = Matrix3D::MatrixPerspectiveOffCenterLH(left - stereoOffset, right - stereoOffset, bottom, top, m_proj.m_rznear, m_proj.m_rzfar);
      proj._41 -= 1.4f * stereoOffset;
      memcpy(m_proj.m_matProj[1].m, proj.m, sizeof(float) * 4 * 4);
   }
   else {
      proj = Matrix3D::MatrixPerspectiveOffCenterLH(left, right, bottom, top, m_proj.m_rznear, m_proj.m_rzfar);
      memcpy(m_proj.m_matProj[0].m, proj.m, sizeof(float) * 4 * 4);
   }
   if (xpixoff != 0.f || ypixoff != 0.f)
   {
      Matrix3D projTrans;
      projTrans.SetTranslation((float)((double)xpixoff / (double)m_viewPort.Width), (float)((double)ypixoff / (double)m_viewPort.Height), 0.f);
      projTrans.Multiply(m_proj.m_matProj[0], m_proj.m_matProj[0]);
      projTrans.Multiply(m_proj.m_matProj[1], m_proj.m_matProj[1]);
   }

   //m_proj.m_cameraLength = sqrtf(m_proj.m_vertexcamera.x*m_proj.m_vertexcamera.x + m_proj.m_vertexcamera.y*m_proj.m_vertexcamera.y + m_proj.m_vertexcamera.z*m_proj.m_vertexcamera.z);
   UpdateMatrices();

   // Compute view vector
   /*Matrix3D temp, viewRot;
   temp = m_proj.m_matView;
   temp.Invert();
   temp.GetRotationPart( viewRot );
   viewRot.MultiplyVector(Vertex3Ds(0, 0, 1), m_viewVec);
   m_viewVec.Normalize();*/

   InitLights();
}

void Pin3D::InitPlayfieldGraphics()
{
   const IEditable * const piEdit = g_pplayer->m_ptable->GetElementByName("playfield_mesh");
   if (piEdit == NULL)
   {
      assert(tableVBuffer == NULL);
      VertexBuffer::CreateVertexBuffer(4, 0, MY_D3DFVF_NOTEX2_VERTEX, &tableVBuffer);

      Vertex3D_NoTex2 *buffer;
      tableVBuffer->lock(0, 0, (void**)&buffer, USAGE_STATIC);

      unsigned int offs = 0;
      for (unsigned int y = 0; y <= 1; ++y)
         for (unsigned int x = 0; x <= 1; ++x, ++offs)
         {
            buffer[offs].x = (x & 1) ? g_pplayer->m_ptable->m_right  : g_pplayer->m_ptable->m_left;
            buffer[offs].y = (y & 1) ? g_pplayer->m_ptable->m_bottom : g_pplayer->m_ptable->m_top;
            buffer[offs].z = g_pplayer->m_ptable->m_tableheight;

            buffer[offs].tu = (x & 1) ? 1.f : 0.f;
            buffer[offs].tv = (y & 1) ? 1.f : 0.f;

            buffer[offs].nx = 0.f;
            buffer[offs].ny = 0.f;
            buffer[offs].nz = 1.f;
         }

      tableVBuffer->unlock();
   }
   else
      g_pplayer->m_fMeshAsPlayfield = true;
}

void Pin3D::RenderPlayfieldGraphics(const bool depth_only)
{
   TRACE_FUNCTION();

   const Material * const mat = g_pplayer->m_ptable->GetMaterial(g_pplayer->m_ptable->m_szPlayfieldMaterial);
   Texture * const pin = (depth_only && (!mat || !mat->m_bOpacityActive)) ? NULL : g_pplayer->m_ptable->GetImage((char *)g_pplayer->m_ptable->m_szImage);

   if (depth_only)
   {
       m_pd3dPrimaryDevice->SetRenderState(RenderDevice::COLORWRITEENABLE, 0); //m_pin3d.m_pd3dPrimaryDevice->SetPrimaryRenderTarget(NULL); // disable color writes
	   // even with depth-only rendering we have to take care of alpha textures (stencil playfield to see underlying objects)
	   if (pin)
	   {
		   SetPrimaryTextureFilter(0, TEXTURE_MODE_ANISOTROPIC);
		   m_pd3dPrimaryDevice->basicShader->SetTechnique("basic_depth_only_with_texture");
		   m_pd3dPrimaryDevice->basicShader->SetTexture("Texture0", pin, false);
		   m_pd3dPrimaryDevice->basicShader->SetAlphaTestValue(pin->m_alphaTestValue * (float)(1.0 / 255.0));
	   }
	   else // No image by that name
		   m_pd3dPrimaryDevice->basicShader->SetTechnique("basic_depth_only_without_texture");
   }
   else
   {
       m_pd3dPrimaryDevice->basicShader->SetMaterial(mat);

       if (pin)
       {
           SetPrimaryTextureFilter(0, TEXTURE_MODE_ANISOTROPIC);
           m_pd3dPrimaryDevice->basicShader->SetTechnique("basic_with_texture");
           m_pd3dPrimaryDevice->basicShader->SetTexture("Texture0", pin, false);
           m_pd3dPrimaryDevice->basicShader->SetAlphaTestValue(pin->m_alphaTestValue * (float)(1.0 / 255.0));
       }
       else // No image by that name
           m_pd3dPrimaryDevice->basicShader->SetTechnique("basic_without_texture");
   }
   m_pd3dPrimaryDevice->basicShader->SetBool("is_metal", mat->m_bIsMetal);

   if (!g_pplayer->m_fMeshAsPlayfield)
   {
      assert(tableVBuffer != NULL);
      m_pd3dPrimaryDevice->basicShader->Begin(0);
      m_pd3dPrimaryDevice->DrawPrimitiveVB(RenderDevice::TRIANGLESTRIP, MY_D3DFVF_NOTEX2_VERTEX, tableVBuffer, 0, 4, true);
      m_pd3dPrimaryDevice->basicShader->End();
   }
   else
   {
      const IEditable * const piEdit = g_pplayer->m_ptable->GetElementByName("playfield_mesh");
      Primitive * const pPrim = (Primitive *)piEdit;
      pPrim->m_d.m_fVisible = true;  // temporary enable the otherwise invisible playfield
      pPrim->RenderObject(m_pd3dPrimaryDevice);
      pPrim->m_d.m_fVisible = false; // restore
   }

   if (pin)
   {
      //m_pd3dPrimaryDevice->basicShader->SetTextureNull("Texture0");
      //m_pd3dPrimaryDevice->m_texMan.UnloadTexture(pin->m_pdsBuffer); //!! is used by ball reflection later-on
      SetPrimaryTextureFilter(0, TEXTURE_MODE_TRILINEAR);
   }

   if (depth_only)
       m_pd3dPrimaryDevice->SetRenderState(RenderDevice::COLORWRITEENABLE, 0x0000000Fu); // reenable color writes with default value

   // Apparently, releasing the vertex buffer here immediately can cause rendering glitches in
   // later rendering steps, so we keep it around for now.
}

void Pin3D::EnableAlphaTestReference(const DWORD alphaRefValue) const
{
   m_pd3dPrimaryDevice->SetRenderStateAlphaTestFunction(RenderDevice::ALPHAFUNC, RenderDevice::Z_GREATEREQUAL, true);
}

void Pin3D::EnableAlphaBlend(const bool additiveBlending, const bool set_dest_blend, const bool set_blend_op) const
{
   m_pd3dPrimaryDevice->SetRenderState(RenderDevice::ALPHABLENDENABLE, RenderDevice::RS_TRUE);
   m_pd3dPrimaryDevice->SetRenderState(RenderDevice::SRCBLEND, RenderDevice::SRC_ALPHA);
   if (set_dest_blend)
      m_pd3dPrimaryDevice->SetRenderState(RenderDevice::DESTBLEND, additiveBlending ? RenderDevice::ONE : RenderDevice::INVSRC_ALPHA);
   if (set_blend_op)
      m_pd3dPrimaryDevice->SetRenderState(RenderDevice::BLENDOP, RenderDevice::BLENDOP_ADD);
}

void Pin3D::Flip(bool vsync)
{
   m_pd3dPrimaryDevice->Flip(vsync);
}

Vertex3Ds Pin3D::Unproject(const Vertex3Ds& point)
{
   m_proj.CacheTransform(); // compute m_matrixTotal

   Matrix3D m2 = m_proj.m_matrixTotal[0]; // = world * view * proj
   m2.Invert();
   Vertex3Ds p, p3;

   p.x = 2.0f * (point.x - (float)m_viewPort.X) / (float)m_viewPort.Width - 1.0f;
   p.y = 1.0f - 2.0f * (point.y - (float)m_viewPort.Y) / (float)m_viewPort.Height;
   p.z = (point.z - m_viewPort.MinZ) / (m_viewPort.MaxZ - m_viewPort.MinZ);
   p3 = m2.MultiplyVector(p);
   return p3;
}

Vertex3Ds Pin3D::Get3DPointFrom2D(const POINT& p)
{
   Vertex3Ds p1, p2, pNear, pFar;
   pNear.x = (float)p.x; pNear.y = (float)p.y; pNear.z = m_viewPort.MinZ;
   pFar.x = (float)p.x; pFar.y = (float)p.y; pFar.z = m_viewPort.MaxZ;
   p1 = Unproject(pNear);
   p2 = Unproject(pFar);
   float wz = g_pplayer->m_ptable->m_tableheight;
   float wx = ((wz - p1.z)*(p2.x - p1.x)) / (p2.z - p1.z) + p1.x;
   float wy = ((wz - p1.z)*(p2.y - p1.y)) / (p2.z - p1.z) + p1.y;
   Vertex3Ds vertex(wx, wy, wz);
   return vertex;
}

void PinProjection::RotateView(float x, float y, float z)
{
   Matrix3D matRotateX, matRotateY, matRotateZ;

   matRotateX.RotateXMatrix(x);
   m_matView.Multiply(matRotateX, m_matView);
   matRotateY.RotateYMatrix(y);
   m_matView.Multiply(matRotateY, m_matView);
   matRotateZ.RotateZMatrix(z);
   m_matView.Multiply(matRotateZ, m_matView);        // matView = rotZ * rotY * rotX * origMatView
}

void PinProjection::TranslateView(const float x, const float y, const float z)
{
   Matrix3D matTrans;
   matTrans.SetTranslation(x, y, z);
   m_matView.Multiply(matTrans, m_matView);
}

void PinProjection::ScaleView(const float x, const float y, const float z)
{
   m_matView.Scale(x, y, z);
}

void PinProjection::MultiplyView(const Matrix3D& mat)
{
   m_matView.Multiply(mat, m_matView);
}

void PinProjection::FitCameraToVerticesFS(std::vector<Vertex3Ds>& pvvertex3D, float aspect, float rotation, float inclination, float FOV, float xlatez, float layback)
{
   // Determine camera distance
   const float rrotsin = sinf(rotation);
   const float rrotcos = cosf(rotation);
   const float rincsin = sinf(inclination);
   const float rinccos = cosf(inclination);

   const float slopey = tanf(0.5f*ANGTORAD(FOV)); // *0.5 because slope is half of FOV - FOV includes top and bottom

   // Field of view along the axis = atan(tan(yFOV)*width/height)
   // So the slope of x simply equals slopey*width/height

   const float slopex = slopey*aspect;

   float maxyintercept = -FLT_MAX;
   float minyintercept = FLT_MAX;
   float maxxintercept = -FLT_MAX;
   float minxintercept = FLT_MAX;

   const Matrix3D laybackTrans = ComputeLaybackTransform(layback);

   for (size_t i = 0; i < pvvertex3D.size(); ++i)
   {
      Vertex3Ds v = pvvertex3D[i];
      float temp;

      //v = laybackTrans.MultiplyVector(v);

      // Rotate vertex about x axis according to incoming inclination
      temp = v.y;
      v.y = rinccos*temp - rincsin*v.z;
      v.z = rincsin*temp + rinccos*v.z;

      // Rotate vertex about z axis according to incoming rotation
      temp = v.x;
      v.x = rrotcos*temp - rrotsin*v.y;
      v.y = rrotsin*temp + rrotcos*v.y;

      // Extend slope lines from point to find camera intersection
      maxyintercept = max(maxyintercept, v.y + slopey*v.z);
      minyintercept = min(minyintercept, v.y - slopey*v.z);
      maxxintercept = max(maxxintercept, v.x + slopex*v.z);
      minxintercept = min(minxintercept, v.x - slopex*v.z);
   }

   slintf("maxy: %f\n", maxyintercept);
   slintf("miny: %f\n", minyintercept);
   slintf("maxx: %f\n", maxxintercept);
   slintf("minx: %f\n", minxintercept);

   // Find camera center in xy plane

   const float ydist = (maxyintercept - minyintercept) / (slopey*2.0f);
   const float xdist = (maxxintercept - minxintercept) / (slopex*2.0f);
   m_vertexcamera.z = (float)(max(ydist, xdist)) + xlatez;
   m_vertexcamera.y = (float)((maxyintercept + minyintercept) * 0.5f);
   m_vertexcamera.x = (float)((maxxintercept + minxintercept) * 0.5f);
}

void PinProjection::FitCameraToVertices(std::vector<Vertex3Ds>& pvvertex3D, float aspect, float rotation, float inclination, float FOV, float xlatez, float layback)
{
   // Determine camera distance
   const float rrotsin = sinf(rotation);
   const float rrotcos = cosf(rotation);
   const float rincsin = sinf(inclination);
   const float rinccos = cosf(inclination);

   const float slopey = tanf(0.5f*ANGTORAD(FOV)); // *0.5 because slope is half of FOV - FOV includes top and bottom

   // Field of view along the axis = atan(tan(yFOV)*width/height)
   // So the slope of x simply equals slopey*width/height

   const float slopex = slopey*aspect;

   float maxyintercept = -FLT_MAX;
   float minyintercept = FLT_MAX;
   float maxxintercept = -FLT_MAX;
   float minxintercept = FLT_MAX;

   Matrix3D laybackTrans = ComputeLaybackTransform(layback);

   for (size_t i = 0; i < pvvertex3D.size(); ++i)
   {
      Vertex3Ds v = laybackTrans.MultiplyVector(pvvertex3D[i]);

      // Rotate vertex about x axis according to incoming inclination
      float temp = v.y;
      v.y = rinccos*temp - rincsin*v.z;
      v.z = rincsin*temp + rinccos*v.z;

      // Rotate vertex about z axis according to incoming rotation
      temp = v.x;
      v.x = rrotcos*temp - rrotsin*v.y;
      v.y = rrotsin*temp + rrotcos*v.y;

      // Extend slope lines from point to find camera intersection
      maxyintercept = max(maxyintercept, v.y + slopey*v.z);
      minyintercept = min(minyintercept, v.y - slopey*v.z);
      maxxintercept = max(maxxintercept, v.x + slopex*v.z);
      minxintercept = min(minxintercept, v.x - slopex*v.z);
   }

   slintf("maxy: %f\n", maxyintercept);
   slintf("miny: %f\n", minyintercept);
   slintf("maxx: %f\n", maxxintercept);
   slintf("minx: %f\n", minxintercept);

   // Find camera center in xy plane

   const float ydist = (maxyintercept - minyintercept) / (slopey*2.0f);
   const float xdist = (maxxintercept - minxintercept) / (slopex*2.0f);
   m_vertexcamera.z = max(ydist, xdist) + xlatez;
   m_vertexcamera.y = (maxyintercept + minyintercept) * 0.5f;
   m_vertexcamera.x = (maxxintercept + minxintercept) * 0.5f;
}

void PinProjection::ComputeNearFarPlane(std::vector<Vertex3Ds>& verts)
{
   m_rznear = FLT_MAX;
   m_rzfar = -FLT_MAX;

   Matrix3D matWorldView;
   m_matView.Multiply(m_matWorld, matWorldView);

   for (size_t i = 0; i < verts.size(); ++i)
   {
      const float tempz = matWorldView.MultiplyVector(verts[i]).z;

      // Extend z-range if necessary
      m_rznear = min(m_rznear, tempz);
      m_rzfar = max(m_rzfar, tempz);
   }

   slintf("m_rznear: %f\n", m_rznear);
   slintf("m_rzfar : %f\n", m_rzfar);

   // beware the div-0 problem
   if (m_rznear < 0.001f)
      m_rznear = 0.001f;
   //m_rznear *= 0.89f; //!! magic, influences also stereo3D code
   m_rzfar *= 1.01f;
}

void PinProjection::CacheTransform()
{
   Matrix3D matT;
   m_matProj[0].Multiply(m_matView, matT);        // matT = matView * matProjLeft
   matT.Multiply(m_matWorld, m_matrixTotal[0]);   // total = matWorld * matView * matProj
   if (m_stereo3D > 0) {
      m_matProj[1].Multiply(m_matView, matT);
      matT.Multiply(m_matWorld, m_matrixTotal[1]);
   }
}

// transforms the backdrop
void PinProjection::TransformVertices(const Vertex3Ds * const rgv, const WORD * const rgi, const int count, Vertex2D * const rgvout) const
{
   const float rClipWidth = (m_rcviewport.right - m_rcviewport.left)*0.5f;
   const float rClipHeight = (m_rcviewport.bottom - m_rcviewport.top)*0.5f;
   const int xoffset = m_rcviewport.left;
   const int yoffset = m_rcviewport.top;

   // Transform each vertex through the current matrix set
   for (int i = 0; i < count; ++i)
   {
      const int l = rgi ? rgi[i] : i;

      // Get the untransformed vertex position
      const float x = rgv[l].x;
      const float y = rgv[l].y;
      const float z = rgv[l].z;

      // Transform it through the current matrix set
      const float xp = m_matrixTotal[0]._11*x + m_matrixTotal[0]._21*y + m_matrixTotal[0]._31*z + m_matrixTotal[0]._41;
      const float yp = m_matrixTotal[0]._12*x + m_matrixTotal[0]._22*y + m_matrixTotal[0]._32*z + m_matrixTotal[0]._42;
      const float wp = m_matrixTotal[0]._14*x + m_matrixTotal[0]._24*y + m_matrixTotal[0]._34*z + m_matrixTotal[0]._44;

      // Finally, scale the vertices to screen coords. This step first
      // "flattens" the coordinates from 3D space to 2D device coordinates,
      // by dividing each coordinate by the wp value. Then, the x- and
      // y-components are transformed from device coords to screen coords.
      // Note 1: device coords range from -1 to +1 in the viewport.
      const float inv_wp = 1.0f / wp;
      const float vTx = (1.0f + xp*inv_wp) * rClipWidth + xoffset;
      const float vTy = (1.0f - yp*inv_wp) * rClipHeight + yoffset;

      rgvout[l].x = vTx;
      rgvout[l].y = vTy;
   }
}
