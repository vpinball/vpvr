#include "stdafx.h"
#include <thread>
#include "captureExt.h"

// The capture thread will do most of the capture work, it must:
// 1. Find DMD or PUP windows, if enabled.
// 2. Prepare DXGI capture interfaces
// 3. Signal that capture is ready, texture size is now available.
// 4. Wait for main thread to create texture.
// 5. Fill texture data periodically.


enum ecStage { ecSearching, ecTexture, ecFailure, ecCapturing, ecUninitialized };
ExtCapture ecDMD, ecPUP;
ecStage ecDMDStage = ecUninitialized, ecPUPStage = ecUninitialized;
bool StopCapture;
std::thread threadCap;


// Call from VP's rendering loop.   Prepares textures once the sizes are detected by the 
// capture thread. 

void captureCheckTextures()
{
   if (ecDMDStage == ecTexture)
   {
      if (g_pplayer->m_texdmd != NULL)
      {
         g_pplayer->m_pin3d.m_pd3dPrimaryDevice->m_texMan.UnloadTexture(g_pplayer->m_texdmd);
         delete g_pplayer->m_texdmd;
      }
      // Sleaze alert! - ec creates a HBitmap, but we hijack ec's data pointer to dump its data directly into VP's texture
      g_pplayer->m_texdmd = g_pplayer->m_texdmd->CreateFromHBitmap(ecDMD.m_HBitmap);
      ecDMD.m_pData = g_pplayer->m_texdmd->data();
      ecDMDStage = ecCapturing;
   }
   if (ecPUPStage == ecTexture)
   {
      if (g_pplayer->m_texPUP != NULL)
      {
         g_pplayer->m_pin3d.m_pd3dPrimaryDevice->m_texMan.UnloadTexture(g_pplayer->m_texPUP);
         delete g_pplayer->m_texPUP;
      }
      g_pplayer->m_texPUP = g_pplayer->m_texPUP->CreateFromHBitmap(ecPUP.m_HBitmap);
      ecPUP.m_pData = g_pplayer->m_texPUP->data();
      ecPUPStage = ecCapturing;
   }

}

void captureFindPUP()
{
   HWND target = FindWindowA(NULL, "PUPSCREEN2"); // PUP Window

   if (target != NULL)
   {
      RECT r;
      GetWindowRect(target, &r);
      if (ecPUP.SetupCapture(r))
      {
         ecPUPStage = ecTexture;
      }
      else
      {
         ecPUPStage = ecFailure;
      }
   }
}

clock_t dmddelay = 0;

void captureFindDMD()
{
   HWND target = FindWindowA(NULL, "Virtual DMD"); // Freezys and UltraDMD
   if (target == NULL)
      target = FindWindowA("pygame", NULL); // P-ROC DMD (CCC Reloaded)
   if (target == NULL)
      target = FindWindowA(NULL, "PUPSCREEN1"); // PupDMD
   if (target != NULL)
   {
      if (dmddelay == 0)
      {
         dmddelay = clock() + 1 * CLOCKS_PER_SEC;
      }
      if (clock() < dmddelay)
         return;
      RECT r;
      GetWindowRect(target, &r);
      if (ecDMD.SetupCapture(r))
      {
         ecDMDStage = ecTexture;
      }
      else
      {
         ecDMDStage = ecFailure;
      }
   }
}

void captureThread()
{
   SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);

   while (!StopCapture)
   {
      if (ecDMDStage == ecSearching)
         captureFindDMD();
      if (ecDMDStage == ecCapturing)
      {
         ecDMD.GetFrame();
         g_pplayer->m_pin3d.m_pd3dPrimaryDevice->m_texMan.SetDirty(g_pplayer->m_texdmd);
      }

      if (ecPUPStage == ecSearching)
         captureFindPUP();
      if (ecPUPStage == ecCapturing)
      {
         ecPUP.GetFrame();
         g_pplayer->m_pin3d.m_pd3dPrimaryDevice->m_texMan.SetDirty(g_pplayer->m_texPUP);
      }
      Sleep(5);
   }
}

void captureStartup()
{
   ecDMDStage = g_pplayer->m_capExtDMD ? ecSearching : ecFailure;
   ecPUPStage = g_pplayer->m_capPUP ? ecSearching : ecFailure;
   StopCapture = false;
   std::thread t(captureThread);
   threadCap = move(t);
}

void captureStop()
{
   StopCapture = true;
   if (threadCap.joinable())
      threadCap.join();
   ExtCapture::Dispose();
   ecDMDStage = ecPUPStage = ecUninitialized;
}

bool capturePUP()
{
   if (ecPUPStage == ecUninitialized)
   {
      captureStartup();
      return false;
   }
   if (ecPUPStage == ecFailure)
      return false;
   if (ecPUPStage == ecCapturing)
      return true;
   captureCheckTextures();
   return false;
}

bool captureExternalDMD()
{
   if (ecPUPStage == ecUninitialized)
   {
      captureStartup();
      return false;
   }
   if (ecDMDStage == ecFailure)
      return false;
   if (ecDMDStage == ecCapturing)
      return true;
   captureCheckTextures();
   return false;
}

outputmaptype ExtCapture::m_duplicatormap;

bool ExtCapture::SetupCapture(RECT inputRect)
{
   memcpy(&m_Rect, &inputRect, sizeof(RECT));

   m_Width = m_Rect.right - m_Rect.left;
   m_Height = m_Rect.bottom - m_Rect.top;

   if (m_Adapter)
      m_Adapter->Release();
   if (m_Output)
      m_Output->Release();

   /* Retrieve a IDXGIFactory that can enumerate the adapters. */
   IDXGIFactory1* factory = NULL;
   HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&factory));

   if (FAILED(hr))
      return false;

   /* Enumerate the adapters.*/
   UINT i = 0, dx = 0;
   std::vector<IDXGIAdapter1*> adapters; /* Needs to be Released(). */

   POINT pt;
   pt.x = m_Rect.left;
   pt.y = m_Rect.top;


   bool found = false;
   while (!found && DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(i, &m_Adapter)) {
      ++i;
      if (m_Adapter)
      {
         while (!found && DXGI_ERROR_NOT_FOUND != m_Adapter->EnumOutputs(dx, &m_Output)) {
            ++dx;
            if (m_Output)
            {
               hr = m_Output->GetDesc(&m_outputdesc);
               if (PtInRect(&m_outputdesc.DesktopCoordinates, pt))
               {
                  found = true;
                  m_DispLeft = pt.x - m_outputdesc.DesktopCoordinates.left;
                  m_DispTop = pt.y - m_outputdesc.DesktopCoordinates.top;
               }
               else
               {
                  m_Output->Release();
               }
            }
         }
         if (!found)
         {
            m_Adapter->Release();
         }
      }
   }
   if (!found)
      return false;

   std::tuple<int, int> idx = std::make_tuple(dx, i);
   outputmaptype::iterator it;
   it = m_duplicatormap.find(idx);
   if (it != m_duplicatormap.end())
   {
      m_CapOut = it->second;
   }
   else
   {
      hr = D3D11CreateDevice(m_Adapter,              /* Adapter: The adapter (video card) we want to use. We may use NULL to pick the default adapter. */
         D3D_DRIVER_TYPE_UNKNOWN,  /* DriverType: We use the GPU as backing device. */
         NULL,                     /* Software: we're using a D3D_DRIVER_TYPE_HARDWARE so it's not applicaple. */
         NULL,                     /* Flags: maybe we need to use D3D11_CREATE_DEVICE_BGRA_SUPPORT because desktop duplication is using this. */
         NULL,                     /* Feature Levels (ptr to array):  what version to use. */
         0,                        /* Number of feature levels. */
         D3D11_SDK_VERSION,        /* The SDK version, use D3D11_SDK_VERSION */
         &m_CapOut.d3d_device,              /* OUT: the ID3D11Device object. */
         &d3d_feature_level,       /* OUT: the selected feature level. */
         &m_CapOut.d3d_context);            /* OUT: the ID3D11DeviceContext that represents the above features. */

      if (S_OK != hr) {
         printf("Error: failed to create the D3D11 Device.\n");
         if (E_INVALIDARG == hr) {
            printf("Got INVALID arg passed into D3D11CreateDevice. Did you pass a adapter + a driver which is not the UNKNOWN driver?.\n");
         }
         return false;
      }

      hr = m_Output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&m_Output1);
      if (S_OK != hr) {
         printf("Error: failed to query the IDXGIOutput1 interface.\n");
         return false;
      }

      hr = m_Output1->DuplicateOutput(m_CapOut.d3d_device, &m_CapOut.m_duplication);
      if (S_OK != hr) {
         printf("Error: failed to create the duplication output.\n");
         return false;
      }

      if (NULL == &m_CapOut.m_duplication) {
         printf("Error: okay, we shouldn't arrive here but the duplication var is NULL.\n");
         return false;
      }
      m_duplicatormap[idx] = m_CapOut;
   }

   /* Create the staging texture that we need to download the pixels from gpu. */
   D3D11_TEXTURE2D_DESC tex_desc;
   tex_desc.Width = m_outputdesc.DesktopCoordinates.right - m_outputdesc.DesktopCoordinates.left;
   tex_desc.Height = m_outputdesc.DesktopCoordinates.bottom - m_outputdesc.DesktopCoordinates.top;
   tex_desc.MipLevels = 1;
   tex_desc.ArraySize = 1; /* When using a texture array. */
   tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; /* This is the default data when using desktop duplication, see https://msdn.microsoft.com/en-us/library/windows/desktop/hh404611(v=vs.85).aspx */
   tex_desc.SampleDesc.Count = 1; /* MultiSampling, we can use 1 as we're just downloading an existing one. */
   tex_desc.SampleDesc.Quality = 0; /* "" */
   tex_desc.Usage = D3D11_USAGE_STAGING;
   tex_desc.BindFlags = 0;
   tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
   tex_desc.MiscFlags = 0;


   hr = m_CapOut.d3d_device->CreateTexture2D(&tex_desc, NULL, &staging_tex);
   if (E_INVALIDARG == hr) {
      printf("Error: received E_INVALIDARG when trying to create the texture.\n");
      return false;
   }
   else if (S_OK != hr) {
      printf("Error: failed to create the 2D texture, error: %d.\n", hr);
      return false;
   }

   // duplication->GetDesc(&m_duplication_desc);

   HDC all_screen = GetDC(NULL);
   int BitsPerPixel = GetDeviceCaps(all_screen, BITSPIXEL);
   HDC hdc2 = CreateCompatibleDC(all_screen);

   BITMAPINFO info;
   info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
   info.bmiHeader.biWidth = m_Width;
   info.bmiHeader.biHeight = -m_Height;
   info.bmiHeader.biPlanes = 1;
   info.bmiHeader.biBitCount = (WORD)BitsPerPixel;
   info.bmiHeader.biCompression = BI_RGB;

   m_HBitmap = CreateDIBSection(hdc2, &info, DIB_RGB_COLORS, (void**)&m_pData, 0, 0);
   return true;
}

void ExtCapture::GetFrame()
{
   HRESULT hr;
   int pitch = 0;
   unsigned char* data = nullptr;
   DXGI_OUTDUPL_FRAME_INFO frame_info;
   IDXGIResource* desktop_resource = NULL;
   ID3D11Texture2D* tex = NULL;
   DXGI_MAPPED_RECT mapped_rect;

   hr = m_CapOut.m_duplication->AcquireNextFrame(2500, &frame_info, &desktop_resource);

   if (DXGI_ERROR_ACCESS_LOST == hr) {
      printf("Received a DXGI_ERROR_ACCESS_LOST.\n");
   }
   else if (DXGI_ERROR_WAIT_TIMEOUT == hr) {
      printf("Received a DXGI_ERROR_WAIT_TIMEOUT.\n");
   }
   else if (DXGI_ERROR_INVALID_CALL == hr) {
      printf("Received a DXGI_ERROR_INVALID_CALL.\n");
   }
   else if (S_OK == hr) {
      //printf("Yay we got a frame.\n");
      hr = desktop_resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
      if (S_OK != hr) {
         printf("Error: failed to query the ID3D11Texture2D interface on the IDXGIResource we got.\n");
         exit(EXIT_FAILURE);
      }
      hr = m_CapOut.m_duplication->MapDesktopSurface(&mapped_rect);
      if (S_OK == hr) {
         printf("We got acess to the desktop surface\n");
         hr = m_CapOut.m_duplication->UnMapDesktopSurface();
         if (S_OK != hr) {
            printf("Error: failed to unmap the desktop surface after successfully mapping it.\n");
         }
      }
      else if (DXGI_ERROR_UNSUPPORTED == hr) {
         m_CapOut.d3d_context->CopyResource(staging_tex, tex);

         D3D11_MAPPED_SUBRESOURCE map;
         HRESULT map_result = m_CapOut.d3d_context->Map(staging_tex,          /* Resource */
            0,                    /* Subresource */
            D3D11_MAP_READ,       /* Map type. */
            0,                    /* Map flags. */
            &map);

         if (S_OK == map_result) {
            data = (unsigned char*)map.pData;
            //printf("Mapped the staging tex; we can access the data now.\n");
            //printf("RowPitch: %u, DepthPitch: %u, %02X, %02X, %02X\n", map.RowPitch, map.DepthPitch, data[0], data[1], data[2]);
            pitch = map.RowPitch;

         }
         else {
            printf("Error: failed to map the staging tex. Cannot access the pixels.\n");
         }

         m_CapOut.d3d_context->Unmap(staging_tex, 0);
      }
      else if (DXGI_ERROR_INVALID_CALL == hr) {
         printf("MapDesktopSurface returned DXGI_ERROR_INVALID_CALL.\n");
      }
      else if (DXGI_ERROR_ACCESS_LOST == hr) {
         printf("MapDesktopSurface returned DXGI_ERROR_ACCESS_LOST.\n");
      }
      else if (E_INVALIDARG == hr) {
         printf("MapDesktopSurface returned E_INVALIDARG.\n");
      }
      else {
         printf("MapDesktopSurface returned an unknown error.\n");
      }
   }

   /* Clean up */
   {

      if (NULL != tex) {
         tex->Release();
         tex = NULL;
      }

      if (NULL != desktop_resource) {
         desktop_resource->Release();
         desktop_resource = NULL;
      }

      /* We must release the frame. */
      hr = m_CapOut.m_duplication->ReleaseFrame();
      if (S_OK != hr) {
         return;
         // std::cout << "FAILED TO RELEASE " << hr << std::endl;
      }
   }

   if (!data) {
      return;
   }

   uint8_t* sptr = reinterpret_cast<uint8_t*>(data) + pitch * m_DispTop;
   uint8_t* ddptr = (uint8_t *)m_pData;

   for (size_t h = 0; h < m_Height; ++h)
   {
      memcpy_s(ddptr, m_Width * 4, sptr + (m_DispLeft * 4), m_Width * 4);
      sptr += pitch;
      ddptr += m_Width * 4;
   }

}

void ExtCapture::Dispose()
{
   for (auto it = m_duplicatormap.begin(); it != m_duplicatormap.end(); it++)
   {
      it->second.m_duplication->Release();
      it->second.d3d_context->Release();
      it->second.d3d_device->Release();
   }
   m_duplicatormap.clear();
}