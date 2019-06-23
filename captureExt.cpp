#include "stdafx.h"
#include "captureExt.h"

HBITMAP m_BitMap_DMD;
bool dmdCaptureRunning = false;
bool dmdBitMapProcessing = false;
bool dmdSuccess = false;

HBITMAP m_BitMap_PUP;
bool PUPCaptureRunning = false;
bool PUPBitMapProcessing = false;
bool PUPSuccess = false;

ThreadPool threadPool (4);

// Capture external DMD from Freezy, UltraDMD or P-ROC (CCC Reloaded) and replace the DMD texture
bool captureExternalDMD()
{
   if (g_pplayer->m_capExtDMD)
   {
      HWND target = FindWindowA(NULL, "Virtual DMD"); // Freezys and UltraDMD
      if (target == NULL)
         target = FindWindowA("pygame", NULL); // P-ROC DMD (CCC Reloaded)

      if (target != NULL)
      {
         // Get target window width and height
         RECT rt;
         GetWindowRect(target, &rt);
         int w = rt.right - rt.left;
         int h = rt.bottom - rt.top;

         if (!dmdCaptureRunning)
         {
            threadPool.enqueue(std::bind(captureDMDWindow, w, h, rt.left, rt.top));
         }
         if (!dmdBitMapProcessing && m_BitMap_DMD != NULL)
         {
            threadPool.enqueue(std::bind(processdmdBitMap, w, h));
         }
      }
   }
   return dmdSuccess;
}

void captureDMDWindow(int w, int h, int offsetLeft, int offsetTop)
{
   dmdCaptureRunning = true;

   HDC dcTarget = GetDC(HWND_DESKTOP); // Freezy is WPF so need to capture the window through desktop
   HDC dcTemp = CreateCompatibleDC(NULL);
   if (g_pplayer->m_texdmd != NULL && (g_pplayer->m_texdmd->width() != w || g_pplayer->m_texdmd->height() != h))
      m_BitMap_DMD = NULL;
   if (m_BitMap_DMD == NULL)
      m_BitMap_DMD = CreateCompatibleBitmap(dcTarget, w, h);
   HANDLE hOld = SelectObject(dcTemp, m_BitMap_DMD);
   // BitBlt the desktop can be a pretty expensive operation, is there a faster way to capture WPF window from c++?
   BitBlt(dcTemp, 0, 0, w, h, dcTarget, offsetLeft, offsetTop, SRCCOPY);
   SelectObject(dcTemp, hOld);
   DeleteObject(hOld);
   DeleteDC(dcTemp);
   ReleaseDC(HWND_DESKTOP, dcTarget);

   dmdCaptureRunning = false;
}

void processdmdBitMap(int w, int h)
{
   if (g_pplayer->m_texdmd == NULL || (g_pplayer->m_texdmd->width() != w || g_pplayer->m_texdmd->height() != h))
   {
      if (g_pplayer->m_texdmd != NULL)
      {
         g_pplayer->m_pin3d.m_pd3dPrimaryDevice->m_texMan.UnloadTexture(g_pplayer->m_texdmd);
         delete g_pplayer->m_texdmd;
      }
      g_pplayer->m_texdmd = g_pplayer->m_texdmd->CreateFromHBitmap(m_BitMap_DMD);
   }
   else
   {
      BaseTexture* dmdTex = g_pplayer->m_texdmd->CreateFromHBitmap(m_BitMap_DMD);
      memcpy(g_pplayer->m_texdmd->data(), dmdTex->data(), g_pplayer->m_texdmd->m_data.size());
      g_pplayer->m_pin3d.m_pd3dPrimaryDevice->m_texMan.SetDirty(g_pplayer->m_texdmd);
      delete dmdTex;
      dmdTex = NULL;
   }
   dmdSuccess = true;
}

// Capture PUP Player window if present and display it as backglass
bool capturePUP()
{
   if (g_pplayer->m_capPUP)
   {
      HWND target = FindWindowA(NULL, "PUPOVERLAY2"); // PUP Window

      if (target != NULL)
      {
         // Get target window width and height
         RECT rt;
         GetWindowRect(target, &rt);
         int w = rt.right - rt.left;
         int h = rt.bottom - rt.top;

         if (!PUPCaptureRunning)
         {
            threadPool.enqueue(std::bind(capturePUPWindow, w, h, rt.left, rt.top));
         }

         if (!PUPBitMapProcessing && m_BitMap_PUP != NULL)
         {
            threadPool.enqueue(std::bind(processPUPBitMap, w, h));
         }
      }
   }
   return PUPSuccess;
}

void capturePUPWindow(int w, int h, int offsetLeft, int offsetTop)
{
   PUPCaptureRunning = true;

   HDC dcTarget = GetDC(HWND_DESKTOP); // PUP Player has alot of layered windows so need to capture the window through desktop
   HDC dcTemp = CreateCompatibleDC(NULL);
   if (g_pplayer->m_texPUP != NULL && (g_pplayer->m_texPUP->width() != w || g_pplayer->m_texPUP->height() != h))
      m_BitMap_PUP = NULL;
   if (m_BitMap_PUP == NULL)
      m_BitMap_PUP = CreateCompatibleBitmap(dcTarget, w, h);
   HANDLE hOld = SelectObject(dcTemp, m_BitMap_PUP);
   // BitBlt the desktop can be a pretty expensive operation, is there a faster way from c++?
   BitBlt(dcTemp, 0, 0, w, h, dcTarget, offsetLeft, offsetTop, SRCCOPY);
   SelectObject(dcTemp, hOld);
   DeleteObject(hOld);
   DeleteDC(dcTemp);
   ReleaseDC(HWND_DESKTOP, dcTarget);

   PUPCaptureRunning = false;
}

void processPUPBitMap(int w, int h)
{
   if (g_pplayer->m_texPUP == NULL || (g_pplayer->m_texPUP->width() != w || g_pplayer->m_texPUP->height() != h))
   {
      if (g_pplayer->m_texPUP != NULL)
      {
         g_pplayer->m_pin3d.m_pd3dPrimaryDevice->m_texMan.UnloadTexture(g_pplayer->m_texPUP);
         delete g_pplayer->m_texPUP;
      }
      g_pplayer->m_texPUP = g_pplayer->m_texPUP->CreateFromHBitmap(m_BitMap_PUP);
   }
   else
   {
      BaseTexture* PUPTex = g_pplayer->m_texPUP->CreateFromHBitmap(m_BitMap_PUP);
      memcpy(g_pplayer->m_texPUP->data(), PUPTex->data(), g_pplayer->m_texPUP->m_data.size());
      g_pplayer->m_pin3d.m_pd3dPrimaryDevice->m_texMan.SetDirty(g_pplayer->m_texPUP);
      delete PUPTex;
      PUPTex = NULL;
   }
   PUPSuccess = true;
}