#include "stdafx.h"
#include <thread>
#include "captureExt.h"

// The capture thread will do most of the capture work, it must:
// 1. Find DMD or PUP windows, if enabled.
// 2. Prepare DXGI capture interfaces
// 3. Signal that capture is ready, texture size is now available.
// 4. Wait for main thread to create texture.
// 5. Fill texture data periodically.

ExtCapture ecDMD, ecPUP;
//std::map<void*, ExtCapture> ecDyn;
bool StopCapture;
std::thread threadCap;

// Call from VP's rendering loop.   Prepares textures once the sizes are detected by the 
// capture thread. 

void captureCheckTextures()
{
   if (ecDMD.ecStage == ecTexture)
   {
      if (g_pplayer->m_texdmd != nullptr)
      {
         g_pplayer->m_pin3d.m_pd3dPrimaryDevice->m_texMan.UnloadTexture(g_pplayer->m_texdmd);
         delete g_pplayer->m_texdmd;
      }
      // Sleaze alert! - ec creates a HBitmap, but we hijack ec's data pointer to dump its data directly into VP's texture
      g_pplayer->m_texdmd = g_pplayer->m_texdmd->CreateFromHBitmap(ecDMD.m_HBitmap);
      ecDMD.m_pData = g_pplayer->m_texdmd->data();
      ecDMD.ecStage = ecCapturing;
   }
   if (ecPUP.ecStage == ecTexture)
   {
      if (g_pplayer->m_texPUP != nullptr)
      {
         g_pplayer->m_pin3d.m_pd3dPrimaryDevice->m_texMan.UnloadTexture(g_pplayer->m_texPUP);
         delete g_pplayer->m_texPUP;
      }
      g_pplayer->m_texPUP = g_pplayer->m_texPUP->CreateFromHBitmap(ecPUP.m_HBitmap);
      ecPUP.m_pData = g_pplayer->m_texPUP->data();
      ecPUP.ecStage = ecCapturing;
   }

}

std::mutex mtx;
std::condition_variable cv;

void captureThread()
{
   SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);

   while (!StopCapture)
   {
      for (auto it = ExtCapture::m_duplicatormap.begin(); it != ExtCapture::m_duplicatormap.end(); ++it)
      {
         it->second->AcquireFrame();
      }
      if (ecDMD.ecStage == ecSearching)
         ecDMD.SearchWindow();
      if (ecDMD.ecStage == ecCapturing)
      {
         if (ecDMD.GetFrame())
            g_pplayer->m_pin3d.m_pd3dPrimaryDevice->m_texMan.SetDirty(g_pplayer->m_texdmd);

      }

      if (ecPUP.ecStage == ecSearching)
      {
         ecPUP.SearchWindow();
      }
      if (ecPUP.ecStage == ecCapturing)
      {
         if (ecPUP.GetFrame())
            g_pplayer->m_pin3d.m_pd3dPrimaryDevice->m_texMan.SetDirty(g_pplayer->m_texPUP);
      }
      std::unique_lock<std::mutex> lck(mtx);

      cv.wait(lck);
      Sleep(16);

   }
}

void captureStartup()
{
   const std::list<string> dmdlist = { "Virtual DMD", "pygame", "PUPSCREEN1", "formDMD", "PUPSCREEN5" };
   ecDMD.Setup(dmdlist);
   const std::list<string> puplist = { "PUPSCREEN2", "Form1" };
   ecPUP.Setup(puplist);
   ecDMD.ecStage = g_pplayer->m_capExtDMD ? ecSearching : ecFailure;
   ecPUP.ecStage = g_pplayer->m_capPUP ? ecSearching : ecFailure;
   StopCapture = false;
   std::thread t(captureThread);
   threadCap = move(t);
}

void captureStop()
{
   StopCapture = true;
   cv.notify_one();
   if (threadCap.joinable())
      threadCap.join();
   ExtCapture::Dispose();
   ecDMD.ecStage = ecPUP.ecStage = ecUninitialized;
}

bool capturePUP()
{
   if (ecPUP.ecStage == ecUninitialized)
   {
      captureStartup();
      return false;
   }
   if (ecPUP.ecStage == ecFailure)
      return false;
   captureCheckTextures();
   cv.notify_one();
   if (ecPUP.ecStage == ecCapturing)
      return true;

   return false;
}

bool captureExternalDMD()
{
   if (ecDMD.ecStage == ecUninitialized)
   {
      captureStartup();
      return false;
   }
   if (ecDMD.ecStage == ecFailure)
      return false;
   captureCheckTextures();
   cv.notify_one();
   if (ecDMD.ecStage == ecCapturing)
      return true;

   return false;
}

outputmaptype ExtCapture::m_duplicatormap;
capturelisttype ExtCapture::m_allCaptures;

void ExtCapture::SearchWindow()
{
   HWND target = nullptr;

   for (const string& windowtext : m_searchWindows)
   {
      target = FindWindowA(nullptr, windowtext.c_str());
      if (target == nullptr)
         target = FindWindowA(windowtext.c_str(), nullptr);
      if (target != nullptr)
         break;
   }
   if (target != nullptr)
   {
      if (m_delay == 0)
      {
         m_delay = clock() + 1 * CLOCKS_PER_SEC;
      }
      if (clock() < m_delay)
         return;
      RECT r;
      GetWindowRect(target, &r);
      if (SetupCapture(r))
      {
         ecStage = ecTexture;
      }
      else
      {
         ecStage = ecFailure;
      }
   }
}

void ExtCapture::Setup(std::list<string> windowlist)
{
   ecStage = ecUninitialized;
   m_delay = 0;
   m_searchWindows = windowlist;
   m_pData = nullptr;
}

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
   IDXGIFactory1* factory = nullptr;
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
         dx = 0;
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

   const std::tuple<int, int> idx = std::make_tuple(dx, i);
   outputmaptype::iterator it = m_duplicatormap.find(idx);
   if (it != m_duplicatormap.end())
   {
      m_pCapOut = it->second;
   }
   else
   {
      m_pCapOut = new ExtCaptureOutput();

      hr = D3D11CreateDevice(m_Adapter,              /* Adapter: The adapter (video card) we want to use. We may use NULL to pick the default adapter. */
         D3D_DRIVER_TYPE_UNKNOWN,  /* DriverType: We use the GPU as backing device. */
         NULL,                     /* Software: we're using a D3D_DRIVER_TYPE_HARDWARE so it's not applicaple. */
         NULL,                     /* Flags: maybe we need to use D3D11_CREATE_DEVICE_BGRA_SUPPORT because desktop duplication is using this. */
         NULL,                     /* Feature Levels (ptr to array):  what version to use. */
         0,                        /* Number of feature levels. */
         D3D11_SDK_VERSION,        /* The SDK version, use D3D11_SDK_VERSION */
         &m_pCapOut->d3d_device,              /* OUT: the ID3D11Device object. */
         &d3d_feature_level,       /* OUT: the selected feature level. */
         &m_pCapOut->d3d_context);            /* OUT: the ID3D11DeviceContext that represents the above features. */

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

      hr = m_Output1->DuplicateOutput(m_pCapOut->d3d_device, &m_pCapOut->m_duplication);
      if (S_OK != hr) {
         printf("Error: failed to create the duplication output.\n");
         return false;
      }

      if (nullptr == &m_pCapOut->m_duplication) {
         printf("Error: okay, we shouldn't arrive here but the duplication var is nullptr.\n");
         return false;
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

      hr = m_pCapOut->d3d_device->CreateTexture2D(&tex_desc, nullptr, &m_pCapOut->staging_tex);
      if (E_INVALIDARG == hr) {
         printf("Error: received E_INVALIDARG when trying to create the texture.\n");
         return false;
      }
      else if (S_OK != hr) {
         printf("Error: failed to create the 2D texture, error: %d.\n", hr);
         return false;
      }
      m_duplicatormap[idx] = m_pCapOut;
   }
   m_allCaptures.push_back(this);

   // duplication->GetDesc(&m_duplication_desc);

   const HDC all_screen = GetDC(nullptr);
   const int BitsPerPixel = GetDeviceCaps(all_screen, BITSPIXEL);
   const HDC hdc2 = CreateCompatibleDC(all_screen);

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


void ExtCaptureOutput::AcquireFrame()
{
   srcdata = nullptr;
   DXGI_OUTDUPL_FRAME_INFO frame_info;
   IDXGIResource* desktop_resource = nullptr;
   ID3D11Texture2D* tex = nullptr;
   DXGI_MAPPED_RECT mapped_rect;

   HRESULT hr = m_duplication->AcquireNextFrame(2500, &frame_info, &desktop_resource);

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
      hr = m_duplication->MapDesktopSurface(&mapped_rect);
      if (S_OK == hr) {
         printf("We got acess to the desktop surface\n");
         hr = m_duplication->UnMapDesktopSurface();
         if (S_OK != hr) {
            printf("Error: failed to unmap the desktop surface after successfully mapping it.\n");
         }
      }
      else if (DXGI_ERROR_UNSUPPORTED == hr) {
         d3d_context->CopyResource(staging_tex, tex);

         D3D11_MAPPED_SUBRESOURCE map;
         const HRESULT map_result = d3d_context->Map(staging_tex,          /* Resource */
            0,                    /* Subresource */
            D3D11_MAP_READ,       /* Map type. */
            0,                    /* Map flags. */
            &map);

         if (S_OK == map_result) {
            srcdata = (unsigned char*)map.pData;
            //printf("Mapped the staging tex; we can access the data now.\n");
            //printf("RowPitch: %u, DepthPitch: %u, %02X, %02X, %02X\n", map.RowPitch, map.DepthPitch, data[0], data[1], data[2]);
            m_pitch = map.RowPitch;

         }
         else {
            printf("Error: failed to map the staging tex. Cannot access the pixels.\n");
         }

         d3d_context->Unmap(staging_tex, 0);
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
   UINT BufSize = frame_info.TotalMetadataBufferSize;
   if (m_MetaDataBufferSize < BufSize)
   {
      delete m_MetaDataBuffer;
      m_MetaDataBuffer = new char[BufSize];
   }

   // Get move rectangles


   hr = m_duplication->GetFrameMoveRects(BufSize, reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(m_MetaDataBuffer), &BufSize);
   if (SUCCEEDED(hr))
   {
      DXGI_OUTDUPL_MOVE_RECT* pmr = (DXGI_OUTDUPL_MOVE_RECT*)m_MetaDataBuffer;
      for (size_t i = 0;i < BufSize / sizeof(DXGI_OUTDUPL_MOVE_RECT);i++, pmr++)
      {
         for (auto it = ExtCapture::m_allCaptures.begin(); it != ExtCapture::m_allCaptures.end(); ++it)
         {
            const int capleft = (*it)->m_DispLeft;
            const int captop = (*it)->m_DispTop;
            const int capright = (*it)->m_DispLeft + (*it)->m_Width;
            const int capbottom = (*it)->m_DispTop + (*it)->m_Height;

            if (pmr->DestinationRect.left < capright && pmr->DestinationRect.right > capleft &&
               pmr->DestinationRect.top < capbottom && pmr->DestinationRect.bottom > captop)
               (*it)->m_bDirty = true;
         }
      }
   }

   BufSize = frame_info.TotalMetadataBufferSize;

   // Get dirty rectangles
   hr = m_duplication->GetFrameDirtyRects(BufSize, reinterpret_cast<RECT*>(m_MetaDataBuffer), &BufSize);
   if (SUCCEEDED(hr))
   {
      RECT* r = (RECT*)m_MetaDataBuffer;
      for (size_t i = 0;i < BufSize / sizeof(RECT);i++, r++)
      {
         for (auto it = ExtCapture::m_allCaptures.begin(); it != ExtCapture::m_allCaptures.end(); ++it)
         {
            const int capleft = (*it)->m_DispLeft;
            const int captop = (*it)->m_DispTop;
            const int capright = (*it)->m_DispLeft + (*it)->m_Width;
            const int capbottom = (*it)->m_DispTop + (*it)->m_Height;

            if (r->left < capright && r->right > capleft &&
               r->top < capbottom && r->bottom > captop)
               (*it)->m_bDirty = true;
         }
      }
   }
   /* Clean up */
   {

      if (nullptr != tex) {
         tex->Release();
         tex = nullptr;
      }

      if (nullptr != desktop_resource) {
         desktop_resource->Release();
         desktop_resource = nullptr;
      }

      /* We must release the frame. */
      hr = m_duplication->ReleaseFrame();
      if (S_OK != hr) {
         return;
         // std::cout << "FAILED TO RELEASE " << hr << std::endl;
      }
   }
}

bool ExtCapture::GetFrame()
{
   if (!m_bDirty)
      return false;

   m_bDirty = false;

   const uint8_t* sptr = reinterpret_cast<uint8_t*>(m_pCapOut->srcdata) + m_pCapOut->m_pitch * m_DispTop;
   uint8_t* ddptr = (uint8_t*)m_pData;

   for (int h = 0; h < m_Height; ++h)
   {
      memcpy_s(ddptr, m_Width * 4, sptr + (m_DispLeft * 4), m_Width * 4);
      sptr += m_pCapOut->m_pitch;
      ddptr += m_Width * 4;
      Sleep(0);
   }
   return true;
}

void ExtCapture::Dispose()
{
   for (auto it = m_duplicatormap.begin(); it != m_duplicatormap.end(); ++it)
   {
      it->second->m_duplication->Release();
      it->second->d3d_context->Release();
      it->second->d3d_device->Release();
      delete it->second;
   }
   m_duplicatormap.clear();
   m_allCaptures.clear();
}
