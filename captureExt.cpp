#include "stdafx.h"
#include "captureExt.h"

// Experimental function to capture external DMD from Freezy, UltraDMD or P-ROC (CCC)
bool captureExternalDMD()
{
   bool success = false;

   if (g_pplayer->m_capExtDMD)
   {
      HWND target = FindWindowA(NULL, "Virtual DMD"); // Freezys and UltraDMD
      if (target == NULL)
         target = FindWindowA("pygame", NULL); // P-ROC DMD (CCC Reloaded)

      if (target != NULL)
      {
         HDC dcTarget;

         // Get target window width and height
         RECT rt;
         GetWindowRect(target, &rt);
         int w = rt.right - rt.left;
         int h = rt.bottom - rt.top;
         dcTarget = GetDC(HWND_DESKTOP); // Freezy is WPF so need to capture the window through desktop

         HBITMAP dmdBitMap = CreateCompatibleBitmap(dcTarget, w, h);
         HDC dcTemp = CreateCompatibleDC(NULL);
         HGDIOBJ holdBitmap = SelectObject(dcTemp, dmdBitMap);
         // BitBlt the desktop can be a pretty expensive operation, is there a faster way to capture WPF window from c++?
         BitBlt(dcTemp, 0, 0, w, h, dcTarget, rt.left, rt.top, SRCCOPY);
         SelectObject(dcTemp, holdBitmap);
         DeleteObject(holdBitmap);
         DeleteDC(dcTemp);

         if (g_pplayer->m_texdmd == NULL || (g_pplayer->m_texdmd->width() != w && g_pplayer->m_texdmd->height() != h))
         {
            if (g_pplayer->m_texdmd != NULL)
            {
               g_pplayer->m_pin3d.m_pd3dPrimaryDevice->m_texMan.UnloadTexture(g_pplayer->m_texdmd);
               delete g_pplayer->m_texdmd;
            }
            g_pplayer->m_texdmd = g_pplayer->m_texdmd->CreateFromHBitmap(dmdBitMap);
         }
         else
         {
            BaseTexture* dmdTex = g_pplayer->m_texdmd->CreateFromHBitmap(dmdBitMap);
            memcpy(g_pplayer->m_texdmd->data(), dmdTex->data(), sizeof(g_pplayer->m_texdmd->m_data));
            g_pplayer->m_pin3d.m_pd3dPrimaryDevice->m_texMan.SetDirty(g_pplayer->m_texdmd);
            delete dmdTex;
         }
         DeleteObject(dmdBitMap);
         ReleaseDC(HWND_DESKTOP, dcTarget);
         success = true;
      }
      DeleteObject(target);
   }

   return success;
}