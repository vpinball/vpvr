#pragma once
#include "typeDefs3D.h"

#include <sal.h>
#include <new>
#include <warning.h>
#include <list>
#include <string>
#include <mutex>
#include <DirectXMath.h>

bool captureExternalDMD();
void captureFindPUP();
bool capturePUP();
void captureStartup();
void captureStop();

enum ecStage { ecSearching, ecFoundWaiting, ecTexture, ecFailure, ecCapturing, ecUninitialized };

class ExtCaptureOutput
{
public:
   ~ExtCaptureOutput() { delete m_MetaDataBuffer; };

   IDXGIOutputDuplication* m_duplication = nullptr;
   ID3D11Device* d3d_device = nullptr;
   ID3D11DeviceContext* d3d_context = nullptr;
   unsigned char* srcdata;
   int m_pitch = 0;
   void* m_MetaDataBuffer = nullptr;
   int m_MetaDataBufferSize = 0;
   ID3D11Texture2D* staging_tex = nullptr;
   ID3D11Texture2D* gdi_tex = nullptr;

   void AcquireFrame();
};

typedef std::map<std::tuple<int, int>, ExtCaptureOutput *> outputmaptype;
typedef std::list<class ExtCapture*> capturelisttype;

class ExtCapture
{
public:
   friend class ExtCaptureOutput;

   static capturelisttype m_allCaptures;
   static outputmaptype m_duplicatormap;
   std::list<string> m_searchWindows;
   int m_delay;

   IDXGIAdapter1* m_Adapter = nullptr;
   IDXGIOutput1* m_Output1 = nullptr;
   IDXGIOutput* m_Output = nullptr;
   RECT m_Rect;
   int m_DispTop, m_DispLeft = 0;
   DXGI_OUTPUT_DESC m_outputdesc;

   D3D_FEATURE_LEVEL d3d_feature_level; /* The selected feature level (D3D version), selected from the Feature Levels array, which is NULL here; when it's NULL the default list is used see:  https://msdn.microsoft.com/en-us/library/windows/desktop/ff476082%28v=vs.85%29.aspx ) */
   ExtCaptureOutput *m_pCapOut;

   bool m_FoundRect = false;
   bool m_CaptureRunning = false;
   bool m_BitMapProcessing = false;
   bool m_Success = false;
   bool m_bDirty = false;

   // public:

   bool SetupCapture(RECT inputRect);

   ecStage ecStage = ecUninitialized;

   void Setup(std::list<string> windowlist);
   void SearchWindow();
   bool GetFrame();
   HBITMAP m_HBitmap;
   void* m_pData;
   int m_Width, m_Height = 0;

   static void Dispose(); // Call when you have deleted all instances.
};
