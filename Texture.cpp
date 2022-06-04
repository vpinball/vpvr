#include "stdafx.h"
#include "Texture.h"

#include "freeimage.h"

#include "math/math.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG // only use the SSE2-JPG path from stbi, as all others are not faster than FreeImage //!! can remove stbi again if at some point FreeImage incorporates libjpeg-turbo or something similar
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"

BaseTexture* BaseTexture::CreateFromFreeImage(FIBITMAP* dib)
{
   // check if Textures exceed the maximum texture dimension
   int maxTexDim = LoadValueIntWithDefault("Player", "MaxTexDimension", 0); // default: Don't resize textures
   if (maxTexDim <= 0)
      maxTexDim = 65536;

   const int pictureWidth = FreeImage_GetWidth(dib);
   const int pictureHeight = FreeImage_GetHeight(dib);

   FIBITMAP* dibResized = dib;
   FIBITMAP* dibConv = dib;
   BaseTexture* tex = nullptr;

   // do loading in a loop, in case memory runs out and we need to scale the texture down due to this
   bool success = false;
   while(!success)
   {
      // the mem is so low that the texture won't even be able to be rescaled -> return
      if (maxTexDim <= 0)
      {
         FreeImage_Unload(dib);
         return nullptr;
      }

      if ((pictureHeight > maxTexDim) || (pictureWidth > maxTexDim))
      {
         int newWidth = max(min(pictureWidth, maxTexDim), MIN_TEXTURE_SIZE);
         int newHeight = max(min(pictureHeight, maxTexDim), MIN_TEXTURE_SIZE);
         /*
          * The following code tries to maintain the aspect ratio while resizing.
          */
         if (pictureWidth - newWidth > pictureHeight - newHeight)
             newHeight = min(pictureHeight * newWidth / pictureWidth, maxTexDim);
         else
             newWidth = min(pictureWidth * newHeight / pictureHeight, maxTexDim);
         dibResized = FreeImage_Rescale(dib, newWidth, newHeight, FILTER_BILINEAR); //!! use a better filter in case scale ratio is pretty high?
      }
      else if (pictureWidth < MIN_TEXTURE_SIZE || pictureHeight < MIN_TEXTURE_SIZE)
      {
         // some drivers seem to choke on small (1x1) textures, so be safe by scaling them up
         const int newWidth = max(pictureWidth, MIN_TEXTURE_SIZE);
         const int newHeight = max(pictureHeight, MIN_TEXTURE_SIZE);
         dibResized = FreeImage_Rescale(dib, newWidth, newHeight, FILTER_BOX);
      }

      // failed to get mem?
      if (!dibResized)
      {
         maxTexDim /= 2;
         while ((maxTexDim > pictureHeight) && (maxTexDim > pictureWidth))
             maxTexDim /= 2;

         continue;
      }

      const FREE_IMAGE_TYPE img_type = FreeImage_GetImageType(dibResized);
      const bool rgbf = (img_type == FIT_FLOAT) || (img_type == FIT_DOUBLE) || (img_type == FIT_RGBF) || (img_type == FIT_RGBAF); //(FreeImage_GetBPP(dibResized) > 32);
      const bool has_alpha = !rgbf && FreeImage_IsTransparent(dibResized);
      // already in correct format?
      if(((img_type == FIT_BITMAP) && (FreeImage_GetBPP(dibResized) == (has_alpha ? 32 : 24))) || (img_type == FIT_RGBF))
         dibConv = dibResized;
      else
      {
         dibConv = rgbf ? FreeImage_ConvertToRGBF(dibResized) : has_alpha ? FreeImage_ConvertTo32Bits(dibResized) : FreeImage_ConvertTo24Bits(dibResized);
         if (dibResized != dib) // did we allocate a rescaled copy?
            FreeImage_Unload(dibResized);

         // failed to get mem?
         if (!dibConv)
         {
            maxTexDim /= 2;
            while ((maxTexDim > pictureHeight) && (maxTexDim > pictureWidth))
               maxTexDim /= 2;

            continue;
         }
      }

      Format format;
      const unsigned int tex_w = FreeImage_GetWidth(dibConv);
      const unsigned int tex_h = FreeImage_GetHeight(dibConv);
      if (rgbf)
      {
          float maxval = 0.f;
          BYTE* bits = (BYTE*)FreeImage_GetBits(dibConv);
          int pitch = FreeImage_GetPitch(dibConv);
          for (unsigned int y = 0; y < tex_h; y++) {
              float* pixel = (float*)bits;
              for (unsigned int x = 0; x < tex_w * 3; x++) {
                  maxval = max(maxval, pixel[x]);
              }
              bits += pitch;
          }
          format = (maxval <= 65504.f) ? RGB_FP16 : RGB_FP32;
      }
      else
          format = has_alpha ? SRGBA : SRGB;

      try
      {
         tex = new BaseTexture(tex_w, tex_h, format);
         success = true;
      }
      // failed to get mem?
      catch(...)
      {
         if (tex)
            delete tex;

         if (dibConv != dibResized) // did we allocate a copy from conversion?
            FreeImage_Unload(dibConv);
         else if (dibResized != dib) // did we allocate a rescaled copy?
            FreeImage_Unload(dibResized);

         maxTexDim /= 2;
         while ((maxTexDim > pictureHeight) && (maxTexDim > pictureWidth))
            maxTexDim /= 2;
      }
   }

   tex->m_realWidth = pictureWidth;
   tex->m_realHeight = pictureHeight;

   const int pitchdst = tex->pitch(), pitchsrc = FreeImage_GetPitch(dibConv), pitch = min(pitchsrc, pitchdst);
   // Copy, applying channel and data format conversion as well as flipping upside down
   // Note that free image use RGB for float image, and the FI_RGBA_xxx for others
   if (tex->m_format == RGB_FP16)
   {
      const BYTE* __restrict bits = FreeImage_GetBits(dibConv);
      unsigned pitch = FreeImage_GetPitch(dibConv);
      unsigned short* const __restrict pdst = (unsigned short*)tex->data();
      for (int y = 0; y < tex->m_height; ++y)
      {
         float* pixel = (float*)bits;
         for (int x = 0; x < tex->m_width; ++x)
         {
            int o = 3 * (x + (tex->m_height - y - 1) * tex->m_width);
            pdst[o + 0] = float2half(pixel[0]);
            pdst[o + 1] = float2half(pixel[1]);
            pdst[o + 2] = float2half(pixel[2]);
            pixel += 3;
         }
         bits += pitch;
      }
   }
   else if (tex->m_format == RGB_FP32)
   {
      const BYTE* __restrict bits = FreeImage_GetBits(dibConv);
      unsigned pitch = FreeImage_GetPitch(dibConv);
      float* const __restrict pdst = (float*)tex->data();
      for (int y = 0; y < tex->m_height; ++y)
      {
         float* pixel = (float*)bits;
         for (int x = 0; x < tex->m_width; ++x)
         {
            int o = 3 * (x + (tex->m_height - y - 1) * tex->m_width);
            pdst[o + 0] = pixel[0];
            pdst[o + 1] = pixel[1];
            pdst[o + 2] = pixel[2];
            pixel += 3;
         }
         bits += pitch;
      }
   }
   else
   {
      const BYTE* __restrict bits = FreeImage_GetBits(dibConv);
      unsigned pitch = FreeImage_GetPitch(dibConv);
      BYTE* const __restrict pdst = tex->data();
      bool has_alpha = (tex->m_format == RGBA) || (tex->m_format == SRGBA);
      for (int y = 0; y < tex->m_height; ++y)
      {
         BYTE* pixel = (BYTE*)bits;
         for (int x = 0; x < tex->m_width; ++x)
         {
            if (has_alpha)
            { 
               int o = 4 * (x + (tex->m_height - y - 1) * tex->m_width);
               pdst[o + 0] = pixel[FI_RGBA_RED];
               pdst[o + 1] = pixel[FI_RGBA_GREEN];
               pdst[o + 2] = pixel[FI_RGBA_BLUE];
               pdst[o + 3] = pixel[FI_RGBA_ALPHA];
               pixel += 4;
            }
            else
            {
               int o = 3 * (x + (tex->m_height - y - 1) * tex->m_width);
               pdst[o + 0] = pixel[FI_RGBA_RED];
               pdst[o + 1] = pixel[FI_RGBA_GREEN];
               pdst[o + 2] = pixel[FI_RGBA_BLUE];
               pixel += 3;
            }
         }
         bits += pitch;
      }
   }

   if (dibConv != dibResized) // did we allocate a copy from conversion?
      FreeImage_Unload(dibConv);
   else if (dibResized != dib) // did we allocate a rescaled copy?
      FreeImage_Unload(dibResized);
   FreeImage_Unload(dib);

   return tex;
}

BaseTexture* BaseTexture::CreateFromFile(const string& szfile)
{
   if (szfile.empty())
      return nullptr;

   FREE_IMAGE_FORMAT fif;

   // check the file signature and deduce its format
   fif = FreeImage_GetFileType(szfile.c_str(), 0);
   if (fif == FIF_UNKNOWN) {
      // try to guess the file format from the file extension
      fif = FreeImage_GetFIFFromFilename(szfile.c_str());
   }

   // check that the plugin has reading capabilities ...
   if ((fif != FIF_UNKNOWN) && FreeImage_FIFSupportsReading(fif)) {
      // ok, let's load the file
      FIBITMAP * const dib = FreeImage_Load(fif, szfile.c_str(), 0);
      if (!dib)
         return nullptr;
      
      BaseTexture* const mySurface = BaseTexture::CreateFromFreeImage(dib);

      //if (bitsPerPixel == 24)
      //   mySurface->SetOpaque();

      return mySurface;
   }
   else
      return nullptr;
}

BaseTexture* BaseTexture::CreateFromData(const void *data, const size_t size)
{
   // check the file signature and deduce its format
   FIMEMORY * const dataHandle = FreeImage_OpenMemory((BYTE*)data, (DWORD)size);
   if (!dataHandle)
      return nullptr;
   const FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeFromMemory(dataHandle, (int)size);

   // check that the plugin has reading capabilities ...
   if ((fif != FIF_UNKNOWN) && FreeImage_FIFSupportsReading(fif)) {
      // ok, let's load the file
      FIBITMAP * const dib = FreeImage_LoadFromMemory(fif, dataHandle, 0);
      FreeImage_CloseMemory(dataHandle);
      if (!dib)
         return nullptr;
      return BaseTexture::CreateFromFreeImage(dib);
   }
   else
   {
      FreeImage_CloseMemory(dataHandle);
      return nullptr;
   }
}

// from the FreeImage FAQ page
static FIBITMAP* HBitmapToFreeImage(HBITMAP hbmp)
{
   BITMAP bm;
   GetObject(hbmp, sizeof(BITMAP), &bm);
   FIBITMAP* dib = FreeImage_Allocate(bm.bmWidth, bm.bmHeight, bm.bmBitsPixel);
   if (!dib)
      return nullptr;
   // The GetDIBits function clears the biClrUsed and biClrImportant BITMAPINFO members (dont't know why)
   // So we save these infos below. This is needed for palettized images only.
   const int nColors = FreeImage_GetColorsUsed(dib);
   const HDC dc = GetDC(nullptr);
   /*const int Success =*/ GetDIBits(dc, hbmp, 0, FreeImage_GetHeight(dib),
      FreeImage_GetBits(dib), FreeImage_GetInfo(dib), DIB_RGB_COLORS);
   ReleaseDC(nullptr, dc);
   // restore BITMAPINFO members
   FreeImage_GetInfoHeader(dib)->biClrUsed = nColors;
   FreeImage_GetInfoHeader(dib)->biClrImportant = nColors;
   return dib;
}

BaseTexture* BaseTexture::CreateFromHBitmap(const HBITMAP hbm, bool with_alpha)
{
   FIBITMAP* dib = HBitmapToFreeImage(hbm);
   if (!dib)
      return nullptr;
   if (with_alpha && FreeImage_GetBPP(dib) == 24)
   {
      FIBITMAP*  dibConv = FreeImage_ConvertTo32Bits(dib);
      FreeImage_Unload(dib);
      dib = dibConv;
      if (!dib)
         return nullptr;
   }
   BaseTexture* const pdds = BaseTexture::CreateFromFreeImage(dib);
   return pdds;
}


////////////////////////////////////////////////////////////////////////////////


Texture::Texture()
{
   m_pdsBuffer = nullptr;
   m_hbmGDIVersion = nullptr;
   m_ppb = nullptr;
   m_alphaTestValue = 1.0f;
}

Texture::Texture(BaseTexture * const base)
{
   m_pdsBuffer = base;
   SetSizeFrom(base);

   m_hbmGDIVersion = nullptr;
   m_ppb = nullptr;
   m_alphaTestValue = 1.0f;
}

Texture::~Texture()
{
   FreeStuff();
}

HRESULT Texture::SaveToStream(IStream *pstream, const PinTable *pt)
{
   BiffWriter bw(pstream, 0);

   bw.WriteString(FID(NAME), m_szName);
   bw.WriteString(FID(PATH), m_szPath);
   bw.WriteInt(FID(WDTH), m_width);
   bw.WriteInt(FID(HGHT), m_height);

   if (!m_ppb)
   {
      bw.WriteTag(FID(BITS));

      // 32-bit picture
      LZWWriter lzwwriter(pstream, (int *)m_pdsBuffer->data(), m_width * 4, m_height, m_pdsBuffer->pitch());
      lzwwriter.CompressBits(8 + 1);
   }
   else // JPEG (or other binary format)
   {
      if (!pt->GetImageLink(this))
      {
         bw.WriteTag(FID(JPEG));
         m_ppb->SaveToStream(pstream);
      }
      else
         bw.WriteInt(FID(LINK), 1);
   }
   bw.WriteFloat(FID(ALTV), m_alphaTestValue);
   bw.WriteTag(FID(ENDB));

   return S_OK;
}

HRESULT Texture::LoadFromStream(IStream *pstream, int version, PinTable *pt)
{
   BiffReader br(pstream, this, pt, version, 0, 0);

   br.Load();

   return ((m_pdsBuffer != nullptr) ? S_OK : E_FAIL);
}


bool Texture::LoadFromMemory(BYTE * const data, const DWORD size)
{
   if (m_pdsBuffer)
      FreeStuff();

   const int maxTexDim = LoadValueIntWithDefault("Player", "MaxTexDimension", 0); // default: Don't resize textures
   if(maxTexDim <= 0) // only use fast JPG path via stbi if no texture resize must be triggered
   {
      int ok, x, y, channels_in_file;
      ok = stbi_info_from_memory(data, size, &x, &y, &channels_in_file); // Request stbi to convert image to SRGB or SRGBA
      unsigned char * const __restrict stbi_data = stbi_load_from_memory(data, size, &x, &y, &channels_in_file, (ok && channels_in_file <= 3) ? 3 : 4);
      if (stbi_data) // will only enter this path for JPG files
      {
         assert(channels_in_file == 3 || channels_in_file == 4);
         BaseTexture* tex = nullptr;
         try
         {
            tex = new BaseTexture(x, y, channels_in_file == 4 ? BaseTexture::SRGBA : BaseTexture::SRGB);
         }
         // failed to get mem?
         catch(...)
         {
            if(tex)
               delete tex;

            goto freeimage_fallback;
         }

         BYTE* const __restrict pdst = (BYTE*)tex->data();
         const BYTE* const __restrict psrc = (BYTE*)stbi_data;
         memcpy(pdst, psrc, x * y * channels_in_file);
         stbi_image_free(stbi_data);

         tex->m_realWidth = x;
         tex->m_realHeight = y;

         m_pdsBuffer = tex;
         SetSizeFrom(m_pdsBuffer);

         return true;
      }
   }

freeimage_fallback:

   FIMEMORY * const hmem = FreeImage_OpenMemory(data, size);
   if (!hmem)
      return false;
   const FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeFromMemory(hmem, 0);
   FIBITMAP * const dib = FreeImage_LoadFromMemory(fif, hmem, 0);
   FreeImage_CloseMemory(hmem);
   if (!dib)
      return false;

   m_pdsBuffer = BaseTexture::CreateFromFreeImage(dib);
   SetSizeFrom(m_pdsBuffer);

   return true;
}


bool Texture::LoadToken(const int id, BiffReader * const pbr)
{
   switch(id)
   {
   case FID(NAME): pbr->GetString(m_szName); break;
   case FID(PATH): pbr->GetString(m_szPath); break;
   case FID(WDTH): pbr->GetInt(m_width); break;
   case FID(HGHT): pbr->GetInt(m_height); break;
   case FID(ALTV): pbr->GetFloat(m_alphaTestValue); break;
   case FID(BITS):
   {
      if (m_pdsBuffer)
         FreeStuff();

      // BMP stored as a 32-bit SBGRA picture
      BYTE* tmp = new BYTE[m_width * m_height * 4];
      LZWReader lzwreader(pbr->m_pistream, (int *)tmp, m_width * 4, m_height, m_width * 4);
      lzwreader.Decoder();

      // Find out if all alpha values are 0x00 or 0xFF
      bool has_alpha = false;
      for (int i = 0; i < m_height && !has_alpha; i++)
      {
         unsigned int o = i * m_width * 4 + 3;
         for (int l = 0; l < m_width && !has_alpha; l++,o+=4)
         {
            if (tmp[o] != 0 && tmp[o] != 255)
               has_alpha = true;
         }
      }

      m_pdsBuffer = new BaseTexture(m_width, m_height, has_alpha ? BaseTexture::SRGBA : BaseTexture::SRGB);

      // copy, converting from SBGR to SRGB, and eventually dropping the alpha channel
      BYTE* const __restrict pdst = m_pdsBuffer->data();
      unsigned int o1 = 0, o2 = 0, o2_step = has_alpha ? 4 : 3;
      for (int yo = 0; yo < m_height; yo++)
         for (int xo = 0; xo < m_width; xo++, o1+=4, o2+=o2_step)
         {
            pdst[o2    ] = tmp[o1 + 2];
            pdst[o2 + 1] = tmp[o1 + 1];
            pdst[o2 + 2] = tmp[o1    ];
            if (has_alpha) pdst[o2 + 3] = tmp[o1 + 3];
         }
      
      delete[] tmp;
      SetSizeFrom(m_pdsBuffer);

      break;
   }
   case FID(JPEG):
   {
      m_ppb = new PinBinary();
      m_ppb->LoadFromStream(pbr->m_pistream, pbr->m_version);
      // m_ppb->m_szPath has the original filename
      // m_ppb->m_pdata() is the buffer
      // m_ppb->m_cdata() is the filesize
      return LoadFromMemory((BYTE*)m_ppb->m_pdata, m_ppb->m_cdata);
      //break;
   }
   case FID(LINK):
   {
      int linkid;
      pbr->GetInt(linkid);
      PinTable * const pt = (PinTable *)pbr->m_pdata;
      m_ppb = pt->GetImageLinkBinary(linkid);
      return LoadFromMemory((BYTE*)m_ppb->m_pdata, m_ppb->m_cdata);
      //break;
   }
   }
   return true;
}

void Texture::FreeStuff()
{
   delete m_pdsBuffer;
   m_pdsBuffer = nullptr;
   if (m_hbmGDIVersion)
   {
      if(m_hbmGDIVersion != g_pvp->m_hbmInPlayMode)
          DeleteObject(m_hbmGDIVersion);
      m_hbmGDIVersion = nullptr;
   }
   if (m_ppb)
   {
      delete m_ppb;
      m_ppb = nullptr;
   }
}

void Texture::CreateGDIVersion()
{
   if (m_hbmGDIVersion)
      return;

   if (g_pvp->m_table_played_via_command_line || g_pvp->m_table_played_via_SelectTableOnStart) // only do anything in here (and waste memory/time on it) if UI needed (i.e. if not just -Play via command line is triggered or selected on VPX start with the file popup!)
   {
      m_hbmGDIVersion = g_pvp->m_hbmInPlayMode;
      return;
   }

   const HDC hdcScreen = GetDC(nullptr);
   m_hbmGDIVersion = CreateCompatibleBitmap(hdcScreen, m_width, m_height);
   const HDC hdcNew = CreateCompatibleDC(hdcScreen);
   const HBITMAP hbmOld = (HBITMAP)SelectObject(hdcNew, m_hbmGDIVersion);

   BITMAPINFO bmi = {};
   bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
   bmi.bmiHeader.biWidth = m_width;
   bmi.bmiHeader.biHeight = -m_height;
   bmi.bmiHeader.biPlanes = 1;
   bmi.bmiHeader.biBitCount = 32;
   bmi.bmiHeader.biCompression = BI_RGB;
   bmi.bmiHeader.biSizeImage = 0;

   BYTE* tmp = new BYTE[m_width * m_height * 4];
   if (m_pdsBuffer->m_format == BaseTexture::RGB_FP32) // Tonemap for 8bpc-Display
   {
      const float* const __restrict src = (float*)m_pdsBuffer->data();
      unsigned int o = 0;
      for (int j = 0; j < m_pdsBuffer->height(); ++j)
         for (int i = 0; i < m_pdsBuffer->width(); ++i, ++o)
         {
            const float r = src[o * 3 + 0];
            const float g = src[o * 3 + 1];
            const float b = src[o * 3 + 2];
            const float l = r * 0.176204f + g * 0.812985f + b * 0.0108109f;
            const float n = (l * (float)(255. * 0.25) + 255.0f) / (l + 1.0f); // simple tonemap and scale by 255, overflow is handled by clamp below
            tmp[o * 4 + 0] = (int)clamp(b * n, 0.f, 255.f);
            tmp[o * 4 + 1] = (int)clamp(g * n, 0.f, 255.f);
            tmp[o * 4 + 2] = (int)clamp(r * n, 0.f, 255.f);
            tmp[o * 4 + 3] = 255;
         }
   }
   else if (m_pdsBuffer->m_format == BaseTexture::RGB_FP16) // Tonemap for 8bpc-Display
   {
      const unsigned short* const __restrict src = (unsigned short*)m_pdsBuffer->data();
      unsigned int o = 0;
      for (int j = 0; j < m_pdsBuffer->height(); ++j)
         for (int i = 0; i < m_pdsBuffer->width(); ++i, ++o)
         {
            const float r = half2float(src[o * 3 + 0]);
            const float g = half2float(src[o * 3 + 1]);
            const float b = half2float(src[o * 3 + 2]);
            const float l = r * 0.176204f + g * 0.812985f + b * 0.0108109f;
            const float n = (l * (float)(255. * 0.25) + 255.0f) / (l + 1.0f); // simple tonemap and scale by 255, overflow is handled by clamp below
            tmp[o * 4 + 0] = (int)clamp(b * n, 0.f, 255.f);
            tmp[o * 4 + 1] = (int)clamp(g * n, 0.f, 255.f);
            tmp[o * 4 + 2] = (int)clamp(r * n, 0.f, 255.f);
            tmp[o * 4 + 3] = 255;
         }
   }
   else if (m_pdsBuffer->m_format == BaseTexture::RGB || m_pdsBuffer->m_format == BaseTexture::SRGB)
   {
      BYTE* const __restrict src = m_pdsBuffer->data();
      unsigned int o = 0;
      for (int j = 0; j < m_pdsBuffer->height(); ++j)
         for (int i = 0; i < m_pdsBuffer->width(); ++i, ++o)
         {
            tmp[o * 4 + 0] = src[o * 3 + 2]; // B
            tmp[o * 4 + 1] = src[o * 3 + 1]; // G
            tmp[o * 4 + 2] = src[o * 3 + 0]; // R
            tmp[o * 4 + 3] = 255; // A
         }
   }
   else if (m_pdsBuffer->m_format == BaseTexture::RGBA || m_pdsBuffer->m_format == BaseTexture::SRGBA)
   {
      const BYTE* const __restrict psrc = m_pdsBuffer->data();
      unsigned int o = 0;
      bool isWinXP = GetWinVersion() < 2600;
      for (int j = 0; j < m_pdsBuffer->height(); ++j)
      {
         for (int i = 0; i < m_pdsBuffer->width(); ++i, ++o)
         {
            int r = psrc[o * 4 + 0];
            int g = psrc[o * 4 + 1];
            int b = psrc[o * 4 + 2];
            int alpha = psrc[o * 4 + 3];
            if (!isWinXP) // For everything newer than Windows XP: use the alpha in the bitmap, thus RGB needs to be premultiplied with alpha, due to how AlphaBlend() works
            {
               if (alpha == 0) // adds a checkerboard where completely transparent (for the image manager display)
               {
                  r = g = b = ((((i >> 4) ^ (j >> 4)) & 1) << 7) + 127;
               }
               else if (alpha != 255) // premultiply alpha for win32 AlphaBlend()
               {
                  r = r * alpha >> 8;
                  g = g * alpha >> 8;
                  b = b * alpha >> 8;
               }
            }
            else
            {
               if (alpha != 255)
               {
                  const unsigned int c = (((((i >> 4) ^ (j >> 4)) & 1) << 7) + 127) * (255 - alpha);
                  r = (r * alpha + c) >> 8;
                  g = (g * alpha + c) >> 8;
                  b = (b * alpha + c) >> 8;
               }
            }
            tmp[o * 4 + 0] = b;
            tmp[o * 4 + 1] = g;
            tmp[o * 4 + 2] = r;
            tmp[o * 4 + 3] = alpha;
         }
      }
   }

   SetStretchBltMode(hdcNew, COLORONCOLOR);
   StretchDIBits(hdcNew,
      0, 0, m_width, m_height,
      0, 0, m_width, m_height,
      tmp, &bmi, DIB_RGB_COLORS, SRCCOPY);

   SelectObject(hdcNew, hbmOld);
   DeleteDC(hdcNew);
   ReleaseDC(nullptr, hdcScreen);
}

void Texture::GetTextureDC(HDC *pdc)
{
   CreateGDIVersion();
   *pdc = CreateCompatibleDC(nullptr);
   m_oldHBM = (HBITMAP)SelectObject(*pdc, m_hbmGDIVersion);
}

void Texture::ReleaseTextureDC(HDC dc)
{
   SelectObject(dc, m_oldHBM);
   DeleteDC(dc);
}

void Texture::CreateFromResource(const int id)
{
   const HBITMAP hbm = (HBITMAP)LoadImage(g_pvp->theInstance, MAKEINTRESOURCE(id), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);

   if (m_pdsBuffer)
      FreeStuff();

   if (hbm == nullptr)
      return;

   m_pdsBuffer = CreateFromHBitmap(hbm);
}

BaseTexture* Texture::CreateFromHBitmap(const HBITMAP hbm, bool with_alpha)
{
   BaseTexture* const pdds = BaseTexture::CreateFromHBitmap(hbm, with_alpha);
   SetSizeFrom(pdds);

   return pdds;
}
