#include "StdAfx.h"
#include "RenderDevice.h"
#include "Shader.h"
#include "math\math.h"
#include "inc\ThreadPool.h"
#ifdef ENABLE_BAM
#include "BAM\BAM_ViewPortSetup.h"
#include "BAM\BAM_Tracker.h"
#endif

extern int logicalNumberOfProcessors;

int NumVideoBytes = 0;

Pin3D::Pin3D()
{
   m_pddsBackBuffer = nullptr;
   m_pddsAOBackBuffer = nullptr;
   m_pddsAOBackTmpBuffer = nullptr;
   m_pddsZBuffer = nullptr;
   m_pdds3DZBuffer = nullptr;
   m_pd3dPrimaryDevice = nullptr;
   m_pd3dSecondaryDevice = nullptr;
   m_envRadianceTexture = nullptr;
   m_tableVBuffer = nullptr;
   m_backGlass = nullptr;

   m_cam.x = 0.f;
   m_cam.y = 0.f;
   m_cam.z = 0.f;
   m_inc  = 0.f;
}

Pin3D::~Pin3D()
{
   m_gpu_profiler.Shutdown();

   m_pd3dPrimaryDevice->UnSetZBuffer();
   m_pd3dPrimaryDevice->FreeShader();

   m_pinballEnvTexture.FreeStuff();

   m_builtinEnvTexture.FreeStuff();

   m_aoDitherTexture.FreeStuff();

   if (m_envRadianceTexture)
   {
      m_pd3dPrimaryDevice->m_texMan.UnloadTexture(m_envRadianceTexture);
      delete m_envRadianceTexture;
      m_envRadianceTexture = nullptr;
   }

   SAFE_BUFFER_RELEASE(m_tableVBuffer);

   SAFE_RELEASE_RENDER_TARGET(m_pddsAOBackBuffer);
   SAFE_RELEASE_RENDER_TARGET(m_pddsAOBackTmpBuffer);
#ifndef ENABLE_SDL
   if (!m_pd3dPrimaryDevice->m_useNvidiaApi && m_pd3dPrimaryDevice->m_INTZ_support)
   {
      SAFE_RELEASE_NO_SET((D3DTexture*)m_pddsStaticZ);
      SAFE_RELEASE_NO_SET((D3DTexture*)m_pddsZBuffer);
   }
   else
   {
      SAFE_RELEASE_NO_SET((RenderTarget*)m_pddsStaticZ);
      SAFE_RELEASE_NO_SET((RenderTarget*)m_pddsZBuffer);
   }
   m_pddsStaticZ = nullptr;
   SAFE_RELEASE(m_pddsStatic);
#else
   assert(m_pddsZBuffer == nullptr);
#endif
   m_pddsZBuffer = nullptr;
   SAFE_RELEASE(m_pdds3DZBuffer);
   SAFE_RELEASE_NO_RCC(m_pddsBackBuffer);

   SAFE_BUFFER_RELEASE(RenderDevice::m_quadVertexBuffer);
   //SAFE_BUFFER_RELEASE(RenderDevice::m_quadDynVertexBuffer);

   if (m_pd3dSecondaryDevice && (m_pd3dSecondaryDevice != m_pd3dPrimaryDevice))
      delete m_pd3dSecondaryDevice;
   delete m_pd3dPrimaryDevice;
   m_pd3dPrimaryDevice = nullptr;
   m_pd3dSecondaryDevice = nullptr;

   if (m_backGlass) {
      delete m_backGlass;
      m_backGlass = nullptr;
   }
}

void Pin3D::TransformVertices(const Vertex3D_NoTex2 * const __restrict rgv, const WORD * const __restrict rgi, const int count, Vertex2D * const __restrict rgvout) const
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

BaseTexture* EnvmapPrecalc(const Texture* envTex, const unsigned int rad_env_xres, const unsigned int rad_env_yres)
{
   g_pvp->ProfileLog("EnvmapPrecalc Start");
   const void* __restrict envmap = envTex->m_pdsBuffer->data();
   const unsigned int env_xres = envTex->m_pdsBuffer->width();
   const unsigned int env_yres = envTex->m_pdsBuffer->height();
   BaseTexture::Format env_format = envTex->m_pdsBuffer->m_format;
   const BaseTexture::Format rad_format = (env_format == BaseTexture::RGB_FP16 || env_format == BaseTexture::RGB_FP32) ? env_format : BaseTexture::SRGB;
   BaseTexture* radTex = new BaseTexture(rad_env_xres, rad_env_yres, rad_format);
   BYTE* const __restrict rad_envmap = radTex->data();
   bool free_envmap = false;

#define PREFILTER_ENVMAP_DIFFUSE
#ifdef PREFILTER_ENVMAP_DIFFUSE
   // pre-filter envmap with a gauss (separable/two passes: x and y)
   //!!! not correct to pre-filter like this, but barely visible in the end, and helps to keep number of samples low (otherwise one would have to use >64k samples instead of 4k!)
   if ((env_format == BaseTexture::RGB_FP16 || env_format == BaseTexture::RGB_FP32) && env_xres > 64)
   {
      const float scale_factor = (float)env_xres*(float)(1.0 / 64.);
      const int xs = (int)(scale_factor*0.5f + 0.5f);
      const void* const __restrict envmap2 = malloc(env_xres * env_yres * 3 * 4);
      const void* const __restrict envmap3 = malloc(env_xres * env_yres * 3 * 4);
      const float sigma = (scale_factor - 1.f)*0.25f;
      float* const __restrict weights = (float*)malloc((xs * 2 + 1) * 4);
      for (int x = 0; x < (xs * 2 + 1); ++x)
         weights[x] = (1.f / sqrtf((float)(2.*M_PI)*sigma*sigma))*expf(-(float)((x - xs)*(x - xs)) / (2.f*sigma*sigma));

      // x-pass:

      for (int y = 0; y < (int)env_yres; ++y)
      {
         const int yoffs = y * env_xres * 3;
         for (int x = 0; x < (int)env_xres; ++x)
         {
            float sum_r = 0.f, sum_g = 0.f, sum_b = 0.f, sum_w = 0.f;
            for (int xt2 = 0; xt2 <= xs * 2; ++xt2)
            {
               int xt = xt2 + (x - xs);
               if (xt < 0)
                  xt += env_xres;
               else if (xt >= (int)env_xres)
                  xt -= env_xres;
               const float w = weights[xt2];
               const unsigned int offs = xt * 3 + yoffs;
               if (env_format == BaseTexture::RGB_FP16)
               {
                  sum_r += half2float(((unsigned short*)envmap)[offs    ]) * w;
                  sum_g += half2float(((unsigned short*)envmap)[offs + 1]) * w;
                  sum_b += half2float(((unsigned short*)envmap)[offs + 2]) * w;
               }
               else
               {
                  sum_r += ((float*)envmap)[offs    ] * w;
                  sum_g += ((float*)envmap)[offs + 1] * w;
                  sum_b += ((float*)envmap)[offs + 2] * w;
               }
               sum_w += w;
            }

            const unsigned int offs = (x + y * env_xres) * 3;
            const float inv_sum = 1.0f / sum_w;
            ((float*)envmap2)[offs    ] = sum_r * inv_sum;
            ((float*)envmap2)[offs + 1] = sum_g * inv_sum;
            ((float*)envmap2)[offs + 2] = sum_b * inv_sum;
         }
      }
      // y-pass:

      for (int y = 0; y < (int)env_yres; ++y)
         for (int x = 0; x < (int)env_xres; ++x)
         {
            float sum_r = 0.f, sum_g = 0.f, sum_b = 0.f, sum_w = 0.f;
            const int yt_end = min(y + xs, (int)env_yres - 1) - (y - xs);
            int offs = x * 3 + max(y - xs, 0)*(env_xres * 3);
            for (int yt = max(y - xs, 0) - (y - xs); yt <= yt_end; ++yt, offs += env_xres * 3)
            {
               const float w = weights[yt];
               sum_r += ((float*)envmap2)[offs] * w;
               sum_g += ((float*)envmap2)[offs + 1] * w;
               sum_b += ((float*)envmap2)[offs + 2] * w;
               sum_w += w;
            }

            offs = (x + y * env_xres) * 3;
            const float inv_sum = 1.0f / sum_w;
            ((float*)envmap3)[offs]     = sum_r * inv_sum;
            ((float*)envmap3)[offs + 1] = sum_g * inv_sum;
            ((float*)envmap3)[offs + 2] = sum_b * inv_sum;
         }

      envmap = envmap3;
      env_format = BaseTexture::RGB_FP32;
      free((void*)envmap2);
      free(weights);
      free_envmap = true;
   }
#endif

   // brute force sampling over hemisphere for each normal direction of the to-be-(ir)radiance-baked environment
   // not the fastest solution, could do a "cosine convolution" over the picture instead (where also just 1024 or x samples could be used per pixel)
   //!! (note though that even 4096 samples can be too low if very bright spots (i.e. sun) in the image! see Delta_2k.hdr -> thus pre-filter enabled above!)
   // but with this implementation one can also have custom maps/LUTs for glossy, etc. later-on
   {
      ThreadPool pool(g_pvp->m_logicalNumberOfProcessors);

      for (unsigned int y = 0; y < rad_env_yres; ++y) {
         pool.enqueue([y, rad_envmap, rad_format, rad_env_xres, rad_env_yres, envmap, env_format, env_xres, env_yres] {
            for (unsigned int x = 0; x < rad_env_xres; ++x)
            {
               // transfo from envmap to normal direction
               const float phi = (float)x / (float)rad_env_xres * (float)(2.0*M_PI) + (float)M_PI;
               const float theta = (float)y / (float)rad_env_yres * (float)M_PI;
               const Vertex3Ds n(sinf(theta) * cosf(phi), sinf(theta) * sinf(phi), cosf(theta));

               // draw x samples over hemisphere and collect cosine weighted environment map samples

               float sum_r = 0.f, sum_g = 0.f, sum_b = 0.f;

               constexpr unsigned int num_samples = 4096;
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
                  // transfo from light direction to envmap
                  // approximations seem to be good enough!
                  const float u = atan2_approx_div2PI(l.y, l.x) + 0.5f; //atan2f(l.y, l.x) * (float)(0.5 / M_PI) + 0.5f;
                  const float v = acos_approx_divPI(l.z); //acosf(l.z) * (float)(1.0 / M_PI);

                  float r, g, b;
                  unsigned int offs = (int)(u*(float)env_xres) + (int)(v*(float)env_yres)*env_xres;
                  if (offs >= env_yres * env_xres)
                     offs = 0;
                  if (env_format == BaseTexture::RGB_FP16)
                  {
                     r = half2float(((unsigned short*)envmap)[offs*3  ]);
                     g = half2float(((unsigned short*)envmap)[offs*3+1]);
                     b = half2float(((unsigned short*)envmap)[offs*3+2]);
                  }
                  else if (env_format == BaseTexture::RGB_FP32)
                  {
                     r = ((float*)envmap)[offs*3  ];
                     g = ((float*)envmap)[offs*3+1];
                     b = ((float*)envmap)[offs*3+2];
                  }
                  else if (env_format == BaseTexture::RGB)
                  {
                     r = ((BYTE*)envmap)[offs*3  ] * (float)(1.0 / 255.0);
                     g = ((BYTE*)envmap)[offs*3+1] * (float)(1.0 / 255.0);
                     b = ((BYTE*)envmap)[offs*3+2] * (float)(1.0 / 255.0);
                  }
                  else if (env_format == BaseTexture::RGBA)
                  {
                     const DWORD rgb = ((DWORD*)envmap)[offs];
                     r = (float)(rgb & 0x00FF0000) * (float)(1.0 / 16711680.0);
                     g = (float)(rgb & 0x0000FF00) * (float)(1.0 /    65280.0);
                     b = (float)(rgb & 0x000000FF) * (float)(1.0 /      255.0);
                  }
                  else if (env_format == BaseTexture::SRGB)
                  {
                     r = invGammaApprox(((BYTE*)envmap)[offs*3  ] * (float)(1.0 / 255.0));
                     g = invGammaApprox(((BYTE*)envmap)[offs*3+1] * (float)(1.0 / 255.0));
                     b = invGammaApprox(((BYTE*)envmap)[offs*3+2] * (float)(1.0 / 255.0));
                  }
                  else if (env_format == BaseTexture::SRGBA)
                  {
                     const DWORD rgb = ((DWORD*)envmap)[offs];
                     r = invGammaApprox((float)(rgb & 0x00FF0000) * (float)(1.0 / 16711680.0));
                     g = invGammaApprox((float)(rgb & 0x0000FF00) * (float)(1.0 /    65280.0));
                     b = invGammaApprox((float)(rgb & 0x000000FF) * (float)(1.0 /      255.0));
                  }
                  else
                     assert(!"unknown format");
#ifndef USE_ENVMAP_PRECALC_COSINE
                  sum_r += r * NdotL;
                  sum_g += g * NdotL;
                  sum_b += b * NdotL;
#else
                  sum_r += r;
                  sum_g += g;
                  sum_b += b;
#endif
               }


               // average all samples
#ifndef USE_ENVMAP_PRECALC_COSINE
               sum_r *= (float)(2.0 / num_samples); // pre-divides by PI for final radiance/color lookup in shader
               sum_g *= (float)(2.0 / num_samples);
               sum_b *= (float)(2.0 / num_samples);
#else
               sum_r *= (float)(1.0 / num_samples); // pre-divides by PI for final radiance/color lookup in shader
               sum_g *= (float)(1.0 / num_samples);
               sum_b *= (float)(1.0 / num_samples);
#endif
               const unsigned int offs = (y*rad_env_xres + x) * 3;
               if (rad_format == BaseTexture::RGB_FP16)
               {
                  ((unsigned short*)rad_envmap)[offs  ] = float2half(sum_r);
                  ((unsigned short*)rad_envmap)[offs+1] = float2half(sum_g);
                  ((unsigned short*)rad_envmap)[offs+2] = float2half(sum_b);
               }
               else if (rad_format == BaseTexture::RGB_FP32)
               {
                  ((float*)rad_envmap)[offs  ] = sum_r;
                  ((float*)rad_envmap)[offs+1] = sum_g;
                  ((float*)rad_envmap)[offs+2] = sum_b;
               }
               else if (rad_format == BaseTexture::SRGB)
               {
                  rad_envmap[offs  ] = (int)clamp(gammaApprox(sum_r) * 255.f, 0.f, 255.f);
                  rad_envmap[offs+1] = (int)clamp(gammaApprox(sum_g) * 255.f, 0.f, 255.f);
                  rad_envmap[offs+2] = (int)clamp(gammaApprox(sum_b) * 255.f, 0.f, 255.f);
               }
            }
         });
      }
   }

   /* ///!!! QA-test above multithreading implementation.
   //!! this is exactly the same code as above, so can be deleted at some point, as it only checks the multithreaded results with a singlethreaded implementation!
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
            Vertex3Ds l = sphere_sample((float)s*(float)(1.0 / num_samples), radical_inverse(s)); // QMC hammersley point set
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
            // approximations seem to be good enough!
            const float u = atan2_approx_div2PI(l.y, l.x) + 0.5f; //atan2f(l.y, l.x) * (float)(0.5 / M_PI) + 0.5f;
            const float v = acos_approx_divPI(l.z); //acosf(l.z) * (float)(1.0 / M_PI);

            float r, g, b;
            if (isHDR)
            {
               unsigned int offs = ((int)(u*(float)env_xres) + (int)(v*(float)env_yres)*env_xres) * 3;
               if (offs >= env_yres * env_xres * 3)
                  offs = 0;
               r = ((float*)envmap)[offs];
               g = ((float*)envmap)[offs + 1];
               b = ((float*)envmap)[offs + 2];
            }
            else
            {
               unsigned int offs = (int)(u*(float)env_xres) + (int)(v*(float)env_yres)*env_xres;
               if (offs >= env_yres * env_xres)
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
         sum[0] *= (float)(2.0 / num_samples); // pre-divides by PI for final radiance/color lookup in shader
         sum[1] *= (float)(2.0 / num_samples);
         sum[2] *= (float)(2.0 / num_samples);
#else
         sum[0] *= (float)(1.0 / num_samples); // pre-divides by PI for final radiance/color lookup in shader
         sum[1] *= (float)(1.0 / num_samples);
         sum[2] *= (float)(1.0 / num_samples);
#endif
         if (isHDR)
         {
            const unsigned int offs = (y*rad_env_xres + x) * 3;
            if (((float*)rad_envmap)[offs] != sum[0] ||
                ((float*)rad_envmap)[offs + 1] != sum[1] ||
                ((float*)rad_envmap)[offs + 2] != sum[2])
            {
               char tmp[911];
               sprintf(tmp, "%d %d %f=%f %f=%f %f=%f ", x, y, ((float*)rad_envmap)[offs], sum[0], ((float*)rad_envmap)[offs + 1], sum[1], ((float*)rad_envmap)[offs + 2], sum[2]);
               ::OutputDebugString(tmp);
            }
         }
         else
         {
            sum[0] = gammaApprox(sum[0]);
            sum[1] = gammaApprox(sum[1]);
            sum[2] = gammaApprox(sum[2]);
            if (
                ((DWORD*)rad_envmap)[y*rad_env_xres + x] != ((int)(sum[0] * 255.0f)) | (((int)(sum[1] * 255.0f)) << 8) | (((int)(sum[2] * 255.0f)) << 16))
                g_pvp->MessageBox("Not OK", "Not OK", MB_OK);
         }
      }

   ///!!! */

#ifdef PREFILTER_ENVMAP_DIFFUSE
   if (free_envmap)
      free((void*)envmap);
#endif

   g_pvp->ProfileLog("EnvmapPrecalc End");
   
   return radTex;
}

HRESULT Pin3D::InitPrimary(const bool fullScreen, const int colordepth, int &refreshrate, const int VSync, const float AAfactor, const int stereo3D, const unsigned int FXAA, const bool useAO, const bool ss_refl)
{
   const unsigned int display = LoadValueIntWithDefault(stereo3D == STEREO_VR ? "PlayerVR" : "Player", "Display", 0);

   std::vector<DisplayConfig> displays;
   getDisplayList(displays);
   int adapter = 0;
   for (std::vector<DisplayConfig>::iterator dispConf = displays.begin(); dispConf != displays.end(); ++dispConf)
      if (display == dispConf->display)
         adapter = dispConf->adapter;

   m_pd3dPrimaryDevice = new RenderDevice(m_viewPort.Width, m_viewPort.Height, fullScreen, colordepth, VSync, AAfactor, stereo3D, FXAA, ss_refl, g_pplayer->m_useNvidiaApi, g_pplayer->m_disableDWM, g_pplayer->m_BWrendering);
   try {
      m_pd3dPrimaryDevice->CreateDevice(refreshrate, adapter);
   }
   catch (...) {
      return E_FAIL;
   }

   if (!m_pd3dPrimaryDevice->LoadShaders())
      return E_FAIL;

   const bool forceAniso = (stereo3D == STEREO_VR) ? true : LoadValueBoolWithDefault("Player", "ForceAnisotropicFiltering", true);
   m_pd3dPrimaryDevice->ForceAnisotropicFiltering(forceAniso);

   const bool compressTextures = (stereo3D == STEREO_VR) ? false : LoadValueBoolWithDefault("Player", "CompressTextures", false);
   m_pd3dPrimaryDevice->CompressTextures(compressTextures);

   m_pd3dPrimaryDevice->SetViewport(&m_viewPort);

   m_pddsZBuffer = m_pd3dPrimaryDevice->AttachZBufferTo(m_pd3dPrimaryDevice->GetBackBufferTexture());

#ifdef ENABLE_SDL
   m_pddsBackBuffer = m_pd3dPrimaryDevice->GetBackBufferTexture();
#else
   m_pd3dPrimaryDevice->GetBackBufferTexture()->GetSurfaceLevel(0, &m_pddsBackBuffer);

   m_pddsStatic = m_pd3dPrimaryDevice->DuplicateRenderTarget(m_pddsBackBuffer);
   if(!m_pddsStatic)
      return E_FAIL;

   m_pddsZBuffer = m_pd3dPrimaryDevice->AttachZBufferTo(m_pddsBackBuffer);
   m_pddsStaticZ = m_pd3dPrimaryDevice->AttachZBufferTo(m_pddsStatic);
   if (!m_pddsZBuffer || !m_pddsStaticZ)
      return E_FAIL;

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
      m_pddsAOBackTmpBuffer = m_pd3dPrimaryDevice->CreateTexture(m_pd3dPrimaryDevice->getBufwidth(), m_pd3dPrimaryDevice->getBufheight(), 1, RENDERTARGET, colorFormat::GREY, nullptr, stereo3D, TextureFilter::TEXTURE_MODE_BILINEAR, false, false);

      m_pddsAOBackBuffer = m_pd3dPrimaryDevice->CreateTexture(m_pd3dPrimaryDevice->getBufwidth(), m_pd3dPrimaryDevice->getBufheight(), 1, RENDERTARGET, colorFormat::GREY, nullptr, stereo3D, TextureFilter::TEXTURE_MODE_BILINEAR, false, false);

       if (!m_pddsAOBackBuffer || !m_pddsAOBackTmpBuffer)
          return E_FAIL;
   }

   return S_OK;
}

HRESULT Pin3D::InitRenderDevice(const bool fullScreen, const int width, const int height, const int colordepth, int &refreshrate, const int VSync, const float AAfactor, const int stereo3D, const unsigned int FXAA, const bool useAO, const bool ss_refl)
{
   m_proj.m_stereo3D = m_stereo3D = stereo3D;
   m_AAfactor = AAfactor;

   // set the viewport for the newly created device
   m_viewPort.X = 0;
   m_viewPort.Y = 0;
   m_viewPort.Width = width;
   m_viewPort.Height = height;
   m_viewPort.MinZ = 0.0f;
   m_viewPort.MaxZ = 1.0f;

   const HRESULT res = InitPrimary(fullScreen, colordepth, refreshrate, VSync, AAfactor, stereo3D, FXAA, useAO, ss_refl);
   if (FAILED(res))
      return res;

   m_pd3dSecondaryDevice = m_pd3dPrimaryDevice; //!! for now, there is no secondary device :/
   return S_OK;
}

HRESULT Pin3D::InitPin3D()
{
   m_backGlass = new BackGlass(m_pd3dSecondaryDevice, g_pplayer->m_ptable->GetDecalsEnabled()
      ? g_pplayer->m_ptable->GetImage(g_pplayer->m_ptable->m_BG_image[g_pplayer->m_ptable->m_BG_current_set].c_str()) : nullptr);
#ifndef ENABLE_SDL
   VertexBuffer::setD3DDevice(m_pd3dPrimaryDevice->GetCoreDevice(), m_pd3dSecondaryDevice->GetCoreDevice());
   IndexBuffer::setD3DDevice(m_pd3dPrimaryDevice->GetCoreDevice(), m_pd3dSecondaryDevice->GetCoreDevice());
#endif
   VertexBuffer::bindNull();
   IndexBuffer::bindNull();

   if (RenderDevice::m_quadVertexBuffer == nullptr)
   {
      VertexBuffer::CreateVertexBuffer(4, 0, MY_D3DFVF_TEX, &RenderDevice::m_quadVertexBuffer, PRIMARY_DEVICE); //!! have 2 for both devices?
      Vertex3D_TexelOnly* bufvb;
      RenderDevice::m_quadVertexBuffer->lock(0, 0, (void**)&bufvb, VertexBuffer::WRITEONLY);
      static constexpr float verts[4 * 5] =
      {
          1.0f,  1.0f, 0.0f, 1.0f, 0.0f,
         -1.0f,  1.0f, 0.0f, 0.0f, 0.0f,
          1.0f, -1.0f, 0.0f, 1.0f, 1.0f,
         -1.0f, -1.0f, 0.0f, 0.0f, 1.0f
      };
      memcpy(bufvb, verts, 4*sizeof(Vertex3D_TexelOnly));
      RenderDevice::m_quadVertexBuffer->unlock();
   }

   //m_quadDynVertexBuffer = nullptr;
   //CreateVertexBuffer(4, USAGE_DYNAMIC, MY_D3DFVF_TEX, &RenderDevice::m_quadDynVertexBuffer);

   //

   // Create the "static" color buffer.
   // This will hold a pre-rendered image of the table and any non-changing elements (ie ramps, decals, etc).

   m_pinballEnvTexture.CreateFromResource(IDB_BALL);
   m_aoDitherTexture.CreateFromResource(IDB_AO_DITHER);

   m_envTexture = g_pplayer->m_ptable->GetImage(g_pplayer->m_ptable->m_envImage);
   m_builtinEnvTexture.CreateFromResource(IDB_ENV);

   const Texture * const envTex = m_envTexture ? m_envTexture : &m_builtinEnvTexture;

   const unsigned int envTexHeight = min(envTex->m_pdsBuffer->height(),256u) / 8;
   const unsigned int envTexWidth = envTexHeight*2;

   m_envRadianceTexture = EnvmapPrecalc(envTex, envTexWidth, envTexHeight);

   m_pd3dPrimaryDevice->m_texMan.SetDirty(m_envRadianceTexture);

   //

   InitPrimaryRenderState();

   if (m_pddsBackBuffer)
      SetPrimaryRenderTarget(m_pddsBackBuffer, m_pddsZBuffer);
   return S_OK;
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

   pd3dDevice->SetRenderState(RenderDevice::LIGHTING, RenderDevice::RS_FALSE);

   pd3dDevice->SetRenderState(RenderDevice::ZENABLE, RenderDevice::RS_TRUE);
   pd3dDevice->SetRenderState(RenderDevice::ZWRITEENABLE, RenderDevice::RS_TRUE);
   pd3dDevice->SetRenderStateCulling(RenderDevice::CULL_CCW);

   pd3dDevice->SetRenderState(RenderDevice::CLIPPING, RenderDevice::RS_FALSE);
   pd3dDevice->SetRenderStateClipPlane0(false);

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

   const PinTable * const ptable = g_pplayer->m_ptable;
   Texture * const pin = ptable->GetDecalsEnabled()
      ? ptable->GetImage(ptable->m_BG_image[ptable->m_BG_current_set])
      : nullptr;
   if (pin)
   {
      m_pd3dPrimaryDevice->Clear(ZBUFFER, 0, 1.0f, 0L);

      m_pd3dPrimaryDevice->SetRenderState(RenderDevice::ZWRITEENABLE, RenderDevice::RS_FALSE);
      m_pd3dPrimaryDevice->SetRenderState(RenderDevice::ZENABLE, RenderDevice::RS_FALSE);

      if (g_pplayer->m_ptable->m_tblMirrorEnabled^g_pplayer->m_ptable->m_reflectionEnabled)
         m_pd3dPrimaryDevice->SetRenderStateCulling(RenderDevice::CULL_NONE);

      m_pd3dPrimaryDevice->SetRenderState(RenderDevice::ALPHABLENDENABLE, RenderDevice::RS_FALSE);

      g_pplayer->Spritedraw(0.f, 0.f, 1.f, 1.f, 0xFFFFFFFF, pin, ptable->m_ImageBackdropNightDay ? sqrtf(g_pplayer->m_globalEmissionScale) : 1.0f);

      if (g_pplayer->m_ptable->m_tblMirrorEnabled^g_pplayer->m_ptable->m_reflectionEnabled)
         m_pd3dPrimaryDevice->SetRenderStateCulling(RenderDevice::CULL_CCW);

      m_pd3dPrimaryDevice->SetRenderState(RenderDevice::ZENABLE, RenderDevice::RS_TRUE);
      m_pd3dPrimaryDevice->SetRenderState(RenderDevice::ZWRITEENABLE, RenderDevice::RS_TRUE);
   }
   else
   {
      const D3DCOLOR d3dcolor = COLORREF_to_D3DCOLOR(ptable->m_colorbackdrop);
      m_pd3dPrimaryDevice->Clear(TARGET | ZBUFFER, d3dcolor, 1.0f, 0L);
   }
}

void Pin3D::InitLights()
{
   //m_pd3dPrimaryDevice->basicShader->SetInt("iLightPointNum",MAX_LIGHT_SOURCES);
#ifdef SEPARATE_CLASSICLIGHTSHADER
   //m_pd3dPrimaryDevice->classicLightShader->SetInt("iLightPointNum",MAX_LIGHT_SOURCES);
#endif

   g_pplayer->m_ptable->m_Light[0].pos.x = g_pplayer->m_ptable->m_right * 0.5f;
   g_pplayer->m_ptable->m_Light[1].pos.x = g_pplayer->m_ptable->m_right * 0.5f;
   g_pplayer->m_ptable->m_Light[0].pos.y = g_pplayer->m_ptable->m_bottom * (float)(1.0 / 3.0);
   g_pplayer->m_ptable->m_Light[1].pos.y = g_pplayer->m_ptable->m_bottom * (float)(2.0 / 3.0);
   g_pplayer->m_ptable->m_Light[0].pos.z = g_pplayer->m_ptable->m_lightHeight;
   g_pplayer->m_ptable->m_Light[1].pos.z = g_pplayer->m_ptable->m_lightHeight;

   vec4 emission = convertColor(g_pplayer->m_ptable->m_Light[0].emission);
   emission.x *= g_pplayer->m_ptable->m_lightEmissionScale * g_pplayer->m_globalEmissionScale;
   emission.y *= g_pplayer->m_ptable->m_lightEmissionScale * g_pplayer->m_globalEmissionScale;
   emission.z *= g_pplayer->m_ptable->m_lightEmissionScale * g_pplayer->m_globalEmissionScale;

   float lightPos[MAX_LIGHT_SOURCES][4] = { 0.f };
   float lightEmission[MAX_LIGHT_SOURCES][4] = { 0.f };

   for (unsigned int i = 0; i < MAX_LIGHT_SOURCES; i++)
   {
       memcpy(&lightPos[i], &g_pplayer->m_ptable->m_Light[i].pos, sizeof(float) * 3);
       memcpy(&lightEmission[i], &emission, sizeof(float) * 3);
   }

   m_pd3dPrimaryDevice->basicShader->SetFloatArray(SHADER_lightPos, (float *)lightPos, 4 * MAX_LIGHT_SOURCES);
   m_pd3dPrimaryDevice->basicShader->SetFloatArray(SHADER_lightEmission, (float *)lightEmission, 4 * MAX_LIGHT_SOURCES);

   vec4 amb_lr = convertColor(g_pplayer->m_ptable->m_lightAmbient, g_pplayer->m_ptable->m_lightRange);
   amb_lr.x *= g_pplayer->m_globalEmissionScale;
   amb_lr.y *= g_pplayer->m_globalEmissionScale;
   amb_lr.z *= g_pplayer->m_globalEmissionScale;
   m_pd3dPrimaryDevice->basicShader->SetVector(SHADER_cAmbient_LightRange, &amb_lr);
#ifdef SEPARATE_CLASSICLIGHTSHADER
   m_pd3dPrimaryDevice->classicLightShader->SetVector(SHADER_cAmbient_LightRange, &amb_lr);
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
   constexpr float inclination = 0.0f;// ANGTORAD(g_pplayer->m_ptable->m_BG_inclination[g_pplayer->m_ptable->m_BG_current_set]);
   //const float FOV = (g_pplayer->m_ptable->m_BG_FOV[g_pplayer->m_ptable->m_BG_current_set] < 1.0f) ? 1.0f : g_pplayer->m_ptable->m_BG_FOV[g_pplayer->m_ptable->m_BG_current_set];

   std::vector<Vertex3Ds> vvertex3D;
   for (size_t i = 0; i < g_pplayer->m_ptable->m_vedit.size(); ++i)
      g_pplayer->m_ptable->m_vedit[i]->GetBoundingVertices(vvertex3D);

   m_proj.m_rcviewport.left = 0;
   m_proj.m_rcviewport.top = 0;
   m_proj.m_rcviewport.right = m_viewPort.Width;
   m_proj.m_rcviewport.bottom = m_viewPort.Height;

   //const float aspect = (float)m_viewPort.Width / (float)m_viewPort.Height; //(float)(4.0/3.0);

   //m_proj.FitCameraToVerticesFS(vvertex3D, aspect, rotation, inclination, FOV, g_pplayer->m_ptable->m_BG_xlatez[g_pplayer->m_ptable->m_BG_current_set], g_pplayer->m_ptable->m_BG_layback[g_pplayer->m_ptable->m_BG_current_set]);
   const float yof = g_pplayer->m_ptable->m_bottom*0.5f + g_pplayer->m_ptable->m_BG_xlatey[g_pplayer->m_ptable->m_BG_current_set];
   constexpr float camx = 0.0f;
   const float camy = g_pplayer->m_ptable->m_bottom*0.5f + g_pplayer->m_ptable->m_BG_xlatex[g_pplayer->m_ptable->m_BG_current_set];
   const float camz = g_pplayer->m_ptable->m_bottom + g_pplayer->m_ptable->m_BG_xlatez[g_pplayer->m_ptable->m_BG_current_set];
   m_proj.m_matWorld.SetIdentity();
   vec3 eye(camx, camy, camz);
   vec3 at(0.0f, yof, 1.0f);
   const vec3 up(0.0f, -1.0f, 0.0f);

   const Matrix3D rotationMat = Matrix3D::MatrixRotationYawPitchRoll(inclination, 0.0f, rotation);
#ifdef ENABLE_SDL
   eye = vec3::TransformCoord(eye, rotationMat);
   at = vec3::TransformCoord(at, rotationMat);
#else
   D3DXVec3TransformCoord(&eye, &eye, (const D3DXMATRIX*)&rotationMat);
   D3DXVec3TransformCoord(&at, &at, (const D3DXMATRIX*)&rotationMat);
#endif
   //D3DXVec3TransformCoord(&up, &up, &rotationMat);
   //at=eye+at;

   const Matrix3D mView = Matrix3D::MatrixLookAtLH(eye, at, up);
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
   constexpr float monitorPixel = 1.0f;// 25.4f * 23.3f / sqrt(1920.0f*1920.0f + 1080.0f*1080.0f);
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
#ifdef ENABLE_SDL
   if (m_stereo3D != STEREO_OFF) {
      constexpr float stereoOffset = 0.03f; //!!
      proj = Matrix3D::MatrixPerspectiveOffCenterLH(left - stereoOffset, right - stereoOffset, bottom, top, m_proj.m_rznear, m_proj.m_rzfar);
      memcpy(m_proj.m_matProj[0].m, proj.m, sizeof(float) * 4 * 4);
      proj = Matrix3D::MatrixPerspectiveOffCenterLH(left + stereoOffset, right + stereoOffset, bottom, top, m_proj.m_rznear, m_proj.m_rzfar);
      memcpy(m_proj.m_matProj[1].m, proj.m, sizeof(float) * 4 * 4);
   }
   else
#endif
   {
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
   const float camx = m_cam.x;
   const float camy = m_cam.y + (FSS_mode ? 500.0f : 0.f);
         float camz = m_cam.z;
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
   if (g_pplayer->m_cameraMode && (g_pplayer->m_ptable->m_BG_current_set == 0 || g_pplayer->m_ptable->m_BG_current_set == 2)) // DT & FSS
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
   if (piEdit == nullptr || piEdit->GetItemType() != ItemTypeEnum::eItemPrimitive)
   {
      assert(m_tableVBuffer == nullptr);
      VertexBuffer::CreateVertexBuffer(4, 0, MY_D3DFVF_NOTEX2_VERTEX, &m_tableVBuffer, PRIMARY_DEVICE);

      Vertex3D_NoTex2 *buffer;
      m_tableVBuffer->lock(0, 0, (void**)&buffer, VertexBuffer::WRITEONLY);

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

      m_tableVBuffer->unlock();
   }
   else
      g_pplayer->m_meshAsPlayfield = true;
}

void Pin3D::RenderPlayfieldGraphics(const bool depth_only)
{
   TRACE_FUNCTION();

   const Material * const mat = g_pplayer->m_ptable->GetMaterial(g_pplayer->m_ptable->m_playfieldMaterial);
   Texture * const pin = (depth_only && !mat->m_bOpacityActive) ? nullptr : g_pplayer->m_ptable->GetImage(g_pplayer->m_ptable->m_image);

   if (depth_only)
   {
       m_pd3dPrimaryDevice->SetRenderState(RenderDevice::COLORWRITEENABLE, 0); //m_pin3d.m_pd3dPrimaryDevice->SetPrimaryRenderTarget(nullptr); // disable color writes
       // even with depth-only rendering we have to take care of alpha textures (stencil playfield to see underlying objects)
       if (pin)
       {
           SetPrimaryTextureFilter(0, TEXTURE_MODE_ANISOTROPIC);
           m_pd3dPrimaryDevice->basicShader->SetTechniqueMetal(SHADER_TECHNIQUE_basic_depth_only_with_texture, mat->m_bIsMetal);
           m_pd3dPrimaryDevice->basicShader->SetTexture(SHADER_Texture0, pin, TextureFilter::TEXTURE_MODE_TRILINEAR, false, false, false);
           m_pd3dPrimaryDevice->basicShader->SetAlphaTestValue(pin->m_alphaTestValue * (float)(1.0 / 255.0));
       }
       else // No image by that name
           m_pd3dPrimaryDevice->basicShader->SetTechniqueMetal(SHADER_TECHNIQUE_basic_depth_only_without_texture, mat->m_bIsMetal);
   }
   else
   {
       m_pd3dPrimaryDevice->basicShader->SetMaterial(mat);

       m_pd3dPrimaryDevice->basicShader->SetDisableLighting(vec4(0.f, 1.f, 0.f, 0.f));

       if (pin)
       {
           SetPrimaryTextureFilter(0, TEXTURE_MODE_ANISOTROPIC);
           m_pd3dPrimaryDevice->basicShader->SetTechniqueMetal(SHADER_TECHNIQUE_basic_with_texture, mat->m_bIsMetal);
           m_pd3dPrimaryDevice->basicShader->SetTexture(SHADER_Texture0, pin, TextureFilter::TEXTURE_MODE_TRILINEAR, false, false, false);
           m_pd3dPrimaryDevice->basicShader->SetAlphaTestValue(pin->m_alphaTestValue * (float)(1.0 / 255.0));
       }
       else // No image by that name
           m_pd3dPrimaryDevice->basicShader->SetTechniqueMetal(SHADER_TECHNIQUE_basic_without_texture, mat->m_bIsMetal);
   }

   if (!g_pplayer->m_meshAsPlayfield)
   {
      assert(m_tableVBuffer != nullptr);
      m_pd3dPrimaryDevice->basicShader->Begin(0);
      m_pd3dPrimaryDevice->DrawPrimitiveVB(RenderDevice::TRIANGLESTRIP, MY_D3DFVF_NOTEX2_VERTEX, m_tableVBuffer, 0, 4, true);
      m_pd3dPrimaryDevice->basicShader->End();
   }
   else
   {
      const IEditable* piEdit = nullptr;
      for (size_t i = 0; i < g_pplayer->m_ptable->m_vedit.size(); ++i)
         if (g_pplayer->m_ptable->m_vedit[i]->GetItemType() == ItemTypeEnum::eItemPrimitive && strcmp(g_pplayer->m_ptable->m_vedit[i]->GetName(), "playfield_mesh") == 0)
         {
            if (piEdit == nullptr || ((Primitive*)piEdit)->m_d.m_toy || !((Primitive*)piEdit)->m_d.m_collidable) // either the first playfield mesh OR a toy/not-collidable (i.e. only used for visuals)?
               piEdit = g_pplayer->m_ptable->m_vedit[i];
         }

      Primitive * const pPrim = (Primitive *)piEdit;
      pPrim->m_d.m_visible = true;  // temporary enable the otherwise invisible playfield
      pPrim->RenderObject();
      pPrim->m_d.m_visible = false; // restore
   }

   if (pin)
   {
      //m_pd3dPrimaryDevice->basicShader->SetTextureNull(SHADER_Texture0);
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
   m_pd3dPrimaryDevice->SetRenderStateAlphaTestFunction(alphaRefValue, RenderDevice::Z_GREATEREQUAL, true);
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

void Pin3D::Flip(const bool vsync)
{
   m_pd3dPrimaryDevice->Flip(vsync);
}

Vertex3Ds Pin3D::Unproject(const Vertex3Ds& point)
{
   m_proj.CacheTransform(); // compute m_matrixTotal

   Matrix3D m2 = m_proj.m_matrixTotal[0]; // = world * view * proj
   m2.Invert();
   const Vertex3Ds p(
       2.0f * (point.x - (float)m_viewPort.X) / (float)m_viewPort.Width - 1.0f,
       1.0f - 2.0f * (point.y - (float)m_viewPort.Y) / (float)m_viewPort.Height,
       (point.z - m_viewPort.MinZ) / (m_viewPort.MaxZ - m_viewPort.MinZ));
   const Vertex3Ds p3 = m2.MultiplyVector(p);
   return p3;
}

Vertex3Ds Pin3D::Get3DPointFrom2D(const POINT& p)
{
   const Vertex3Ds pNear((float)p.x,(float)p.y,m_viewPort.MinZ);
   const Vertex3Ds pFar ((float)p.x,(float)p.y,m_viewPort.MaxZ);
   const Vertex3Ds p1 = Unproject(pNear);
   const Vertex3Ds p2 = Unproject(pFar);
   const float wz = g_pplayer->m_ptable->m_tableheight;
   const float wx = ((wz - p1.z)*(p2.x - p1.x)) / (p2.z - p1.z) + p1.x;
   const float wy = ((wz - p1.z)*(p2.y - p1.y)) / (p2.z - p1.z) + p1.y;
   const Vertex3Ds vertex(wx, wy, wz);
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

void PinProjection::FitCameraToVerticesFS(const std::vector<Vertex3Ds>& pvvertex3D, float aspect, float rotation, float inclination, float FOV, float xlatez, float layback)
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

   //const Matrix3D laybackTrans = ComputeLaybackTransform(layback);

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

void PinProjection::FitCameraToVertices(const std::vector<Vertex3Ds>& pvvertex3D, float aspect, float rotation, float inclination, float FOV, float xlatez, float layback)
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

void PinProjection::ComputeNearFarPlane(const std::vector<Vertex3Ds>& verts)
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

#ifdef ENABLE_BAM
// #ravarcade: All code below will add BAM view and BAM head tracking.
// Most of it is copy of
//-----------------------------------------------------------------------

BAM_Tracker::BAM_Tracker_Client BAM;

void Mat4Mul(float * const __restrict O, const float * const __restrict A, const float * const __restrict B)
{
   O[0] = A[0] * B[0] + A[1] * B[4] + A[2] * B[8] + A[3] * B[12];
   O[1] = A[0] * B[1] + A[1] * B[5] + A[2] * B[9] + A[3] * B[13];
   O[2] = A[0] * B[2] + A[1] * B[6] + A[2] * B[10] + A[3] * B[14];
   O[3] = A[0] * B[3] + A[1] * B[7] + A[2] * B[11] + A[3] * B[15];

   O[4] = A[4] * B[0] + A[5] * B[4] + A[6] * B[8] + A[7] * B[12];
   O[5] = A[4] * B[1] + A[5] * B[5] + A[6] * B[9] + A[7] * B[13];
   O[6] = A[4] * B[2] + A[5] * B[6] + A[6] * B[10] + A[7] * B[14];
   O[7] = A[4] * B[3] + A[5] * B[7] + A[6] * B[11] + A[7] * B[15];

   O[8] = A[8] * B[0] + A[9] * B[4] + A[10] * B[8] + A[11] * B[12];
   O[9] = A[8] * B[1] + A[9] * B[5] + A[10] * B[9] + A[11] * B[13];
   O[10] = A[8] * B[2] + A[9] * B[6] + A[10] * B[10] + A[11] * B[14];
   O[11] = A[8] * B[3] + A[9] * B[7] + A[10] * B[11] + A[11] * B[15];

   O[12] = A[12] * B[0] + A[13] * B[4] + A[14] * B[8] + A[15] * B[12];
   O[13] = A[12] * B[1] + A[13] * B[5] + A[14] * B[9] + A[15] * B[13];
   O[14] = A[12] * B[2] + A[13] * B[6] + A[14] * B[10] + A[15] * B[14];
   O[15] = A[12] * B[3] + A[13] * B[7] + A[14] * B[11] + A[15] * B[15];
}

void Mat4Mul(float * const __restrict OA, const float * const __restrict B)
{
   float A[16];
   memcpy_s(A, sizeof(A), OA, sizeof(A));
   Mat4Mul(OA, A, B);
}

void CreateProjectionAndViewMatrix(float * const __restrict P, float * const __restrict V)
{
   const float degToRad = 0.01745329251f;

   // VPX stuffs
   const PinTable* const t = g_pplayer->m_ptable;
   const int resolutionWidth = g_pplayer->m_pin3d.m_viewPort.Width;
   const int resolutionHeight = g_pplayer->m_pin3d.m_viewPort.Height;
   const int rotation = static_cast<int>(g_pplayer->m_ptable->m_BG_rotation[g_pplayer->m_ptable->m_BG_current_set] / 90.0f);
   //const bool stereo3D = g_pplayer->m_stereo3D;
   const float tableLength = t->m_bottom;
   const float tableWidth = t->m_right;
   const float tableGlass = t->m_glassheight;
   const float minSlope = (t->m_overridePhysics ? t->m_fOverrideMinSlope : t->m_angletiltMin);
   const float maxSlope = (t->m_overridePhysics ? t->m_fOverrideMaxSlope : t->m_angletiltMax);
   const float slope = minSlope + (maxSlope - minSlope) * t->m_globalDifficulty;
   const float angle = -slope * degToRad;

   // Data from config file (Settings):
   float DisplaySize;
   float DisplayNativeWidth;
   float DisplayNativeHeight;

   // Data from head tracking
   float ViewerPositionX, ViewerPositionY, ViewerPositionZ;

   // Get data from BAM Tracker
   // we use Screen Width & Height as Native Resolution. Only aspect ration is important
   DisplayNativeWidth = (float)BAM.GetScreenWidth(); // [mm]
   DisplayNativeHeight = (float)BAM.GetScreenHeight(); // [mm]

   double x, y, z;
   BAM.GetPosition(x, y, z);

   ViewerPositionX = (float)x;
   ViewerPositionY = (float)y;
   ViewerPositionZ = (float)z;

   const double w = DisplayNativeWidth, h = DisplayNativeHeight;
   DisplaySize = (float)(sqrt(w*w + h * h) / 25.4); // [mm] -> [inchs]

                                                    // constant params for this project
   constexpr float AboveScreen = 200.0f; // 0.2m
   constexpr float InsideScreen = 2000.0f; // 2.0m

   // Data build projection matrix
   BuildProjectionMatrix(P,
      DisplaySize,
      DisplayNativeWidth, DisplayNativeHeight,
      (float)resolutionWidth, (float)resolutionHeight,
      0.0f, 0.0f,
      (float)resolutionWidth, (float)resolutionHeight,
      ViewerPositionX, ViewerPositionY, ViewerPositionZ,
      -AboveScreen, InsideScreen,
      rotation);

   // Build View matrix from parts: Translation, Scale, Rotation
   // .. but first View Matrix has camera position
   const float VT[16] = {
      1, 0, 0, 0,
      0, 1, 0, 0,
      0, 0, 1, 0,
      -ViewerPositionX, -ViewerPositionY, -ViewerPositionZ, 1
   };

   // --- Scale, ... some math
   const float pixelsToMillimeters = (float)(25.4*DisplaySize / sqrt(DisplayNativeWidth*DisplayNativeWidth + DisplayNativeHeight * DisplayNativeHeight));
   const float pixelsToMillimetersX = pixelsToMillimeters * DisplayNativeWidth / resolutionWidth;
   const float pixelsToMillimetersY = pixelsToMillimeters * DisplayNativeHeight / resolutionHeight;
   const float ptm = rotation & 1 ? pixelsToMillimetersX : pixelsToMillimetersY;
   const float tableLengthInMillimeters = ptm * tableLength;
   const float displayLengthInMillimeters = ptm * (rotation & 1 ? pixelsToMillimeters * DisplayNativeWidth : pixelsToMillimeters * DisplayNativeHeight);

   // --- Scale world to fit in screen
   const float scale = displayLengthInMillimeters / tableLengthInMillimeters; // calc here scale
   const float S[16] = {
      scale, 0, 0, 0,
      0, scale, 0, 0,
      0, 0, scale, 0,
      0, 0, 0, 1.f
   };
   /// ===

   // --- Translation to desired world element (playfield center or glass center)
   const float _S = sinf(angle);
   const float _C = cosf(angle);
   const float T[16] = {
      1, 0, 0, 0,
      0, -1, 0, 0,
      0, 0, 1, 0,
      scale*(-tableWidth * 0.5f),
      scale*(tableLength * 0.5f - tableGlass * _S),
      scale*(-tableGlass * _C), 1
   };
   /// ===

   // --- Rotate world to make playfield or glass parallel to screen
   const float R[16] = {
      1, 0, 0, 0,
      0, _C, -_S, 0,
      0, _S, _C, 0,
      0, 0, 0, 1
   };
   /// ===

   // combine all to one matrix
   Mat4Mul(V, S, R);
   Mat4Mul(V, T);
   Mat4Mul(V, VT);
}

void Pin3D::UpdateBAMHeadTracking()
{
   // If BAM tracker is not running, we will not do anything.
   if (BAM.IsBAMTrackerPresent())
      CreateProjectionAndViewMatrix(&m_proj.m_matProj[0]._11, &m_proj.m_matView._11);
}
#endif
