#include "stdafx.h"

#include <DxErr.h>

//#include "Dwmapi.h" // use when we get rid of XP at some point, get rid of the manual dll loads in here then

#ifndef DISABLE_FORCE_NVIDIA_OPTIMUS
#include "nvapi.h"
#endif

#include "typeDefs3D.h"
#ifdef ENABLE_SDL
#include "sdl2/SDL_syswm.h"
#endif
#include "RenderDevice.h"
#include "TextureManager.h"
#include "Shader.h"
#ifndef ENABLE_SDL
#include "Material.h"
#include "BasicShader.h"
#include "BallShader.h"
#include "DMDShader.h"
#include "FBShader.h"
#include "FlasherShader.h"
#include "LightShader.h"
#include "StereoShader.h"
#ifdef SEPARATE_CLASSICLIGHTSHADER
#include "ClassicLightShader.h"
#endif
#endif

// SMAA:
#include "shader/AreaTex.h"
#include "shader/SearchTex.h"

#ifndef ENABLE_SDL
#pragma comment(lib, "d3d9.lib")        // TODO: put into build system
#pragma comment(lib, "d3dx9.lib")       // TODO: put into build system
#if _MSC_VER >= 1900
 #pragma comment(lib, "legacy_stdio_definitions.lib") //dxerr.lib needs this
#endif
#pragma comment(lib, "dxerr.lib")       // TODO: put into build system

static RenderTarget *srcr_cache = nullptr; //!! meh, for nvidia depth read only
static D3DTexture *srct_cache = nullptr;
static D3DTexture* dest_cache = nullptr;

static bool IsWindowsVistaOr7()
{
   OSVERSIONINFOEXW osvi = { sizeof(osvi), 0, 0, 0, 0,{ 0 }, 0, 0 };
   const DWORDLONG dwlConditionMask = //VerSetConditionMask(
      VerSetConditionMask(
         VerSetConditionMask(
            0, VER_MAJORVERSION, VER_EQUAL),
         VER_MINORVERSION, VER_EQUAL)/*,
      VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL)*/;
   osvi.dwMajorVersion = HIBYTE(_WIN32_WINNT_VISTA);
   osvi.dwMinorVersion = LOBYTE(_WIN32_WINNT_VISTA);
   //osvi.wServicePackMajor = 0;

   const bool vista = VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION /*| VER_SERVICEPACKMAJOR*/, dwlConditionMask) != FALSE;

   OSVERSIONINFOEXW osvi2 = { sizeof(osvi), 0, 0, 0, 0,{ 0 }, 0, 0 };
   osvi2.dwMajorVersion = HIBYTE(_WIN32_WINNT_WIN7);
   osvi2.dwMinorVersion = LOBYTE(_WIN32_WINNT_WIN7);
   //osvi2.wServicePackMajor = 0;

   const bool win7 = VerifyVersionInfoW(&osvi2, VER_MAJORVERSION | VER_MINORVERSION /*| VER_SERVICEPACKMAJOR*/, dwlConditionMask) != FALSE;

   return vista || win7;
}
#endif

typedef HRESULT(STDAPICALLTYPE *pRGV)(LPOSVERSIONINFOEXW osi);
static pRGV mRtlGetVersion = nullptr;

bool IsWindows10_1803orAbove()
{
   if (mRtlGetVersion == nullptr)
      mRtlGetVersion = (pRGV)GetProcAddress(GetModuleHandle(TEXT("ntdll")), "RtlGetVersion"); // apparently the only really reliable solution to get the OS version (as of Win10 1803)

   if (mRtlGetVersion != nullptr)
   {
      OSVERSIONINFOEXW osInfo;
      osInfo.dwOSVersionInfoSize = sizeof(osInfo);
      mRtlGetVersion(&osInfo);

      if (osInfo.dwMajorVersion > 10)
         return true;
      if (osInfo.dwMajorVersion == 10 && osInfo.dwMinorVersion > 0)
         return true;
      if (osInfo.dwMajorVersion == 10 && osInfo.dwMinorVersion == 0 && osInfo.dwBuildNumber >= 17134) // which is the more 'common' 1803
         return true;
   }

   return false;
}

#ifdef ENABLE_SDL
//my definition for SDL    GLint size;    GLenum type;    GLboolean normalized;    GLsizei stride;
//D3D definition   WORD Stream;    WORD Offset;    BYTE Type;    BYTE Method;    BYTE Usage;    BYTE UsageIndex;
constexpr VertexElement VertexTexelElement[] =
{
   { 3, GL_FLOAT, GL_FALSE, 0, "POSITION0" },
   { 2, GL_FLOAT, GL_FALSE, 0, "TEXCOORD0" },
   { 0, 0, 0, 0, nullptr}
   /*   { 0, 0 * sizeof(float), D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },  // pos
      { 0, 3 * sizeof(float), D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },  // tex0
      D3DDECL_END()*/
};
VertexDeclaration* RenderDevice::m_pVertexTexelDeclaration = (VertexDeclaration*)&VertexTexelElement;

constexpr VertexElement VertexNormalTexelElement[] =
{
   { 3, GL_FLOAT, GL_FALSE, 0, "POSITION0" },
   { 3, GL_FLOAT, GL_FALSE, 0, "NORMAL0" },
   { 2, GL_FLOAT, GL_FALSE, 0, "TEXCOORD0" },
   { 0, 0, 0, 0, nullptr}
/*
      { 0, 0 * sizeof(float), D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },  // pos
      { 0, 3 * sizeof(float), D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },  // normal
      { 0, 6 * sizeof(float), D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },  // tex0
      D3DDECL_END()*/
};
VertexDeclaration* RenderDevice::m_pVertexNormalTexelDeclaration = (VertexDeclaration*)&VertexNormalTexelElement;

/*constexpr VertexElement VertexNormalTexelTexelElement[] =
{
   { 0, 0  * sizeof(float),D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },  // pos
   { 0, 3  * sizeof(float),D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0 },  // normal
   { 0, 6  * sizeof(float),D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },  // tex0
   { 0, 8  * sizeof(float),D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },  // tex1
   D3DDECL_END()
};

VertexDeclaration* RenderDevice::m_pVertexNormalTexelTexelDeclaration = nullptr;*/

constexpr VertexElement VertexTrafoTexelElement[] =
{
   { 4, GL_FLOAT, GL_FALSE, 0, "POSITION0" },
   { 2, GL_FLOAT, GL_FALSE, 0, nullptr },//legacy?
   { 2, GL_FLOAT, GL_FALSE, 0, "TEXCOORD0" },
   { 0, 0, 0, 0, nullptr }

   /*   { 0, 0 * sizeof(float), D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITIONT, 0 }, // transformed pos
   { 0, 4 * sizeof(float), D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,  1 }, // (mostly, except for classic lights) unused, there to be able to share same code as VertexNormalTexelElement
   { 0, 6 * sizeof(float), D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 }, // tex0
   D3DDECL_END()*/
};
VertexDeclaration* RenderDevice::m_pVertexTrafoTexelDeclaration = (VertexDeclaration*)&VertexTrafoTexelElement;
#else
constexpr VertexElement VertexTexelElement[] =
{
   { 0, 0 * sizeof(float), D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },  // pos
   { 0, 3 * sizeof(float), D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },  // tex0
   D3DDECL_END()
};
VertexDeclaration* RenderDevice::m_pVertexTexelDeclaration = nullptr;

constexpr VertexElement VertexNormalTexelElement[] =
{
   { 0, 0 * sizeof(float), D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },  // pos
   { 0, 3 * sizeof(float), D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0 },  // normal
   { 0, 6 * sizeof(float), D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },  // tex0
   D3DDECL_END()
};
VertexDeclaration* RenderDevice::m_pVertexNormalTexelDeclaration = nullptr;

/*constexpr VertexElement VertexNormalTexelTexelElement[] =
{
   { 0, 0  * sizeof(float),D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },  // pos
   { 0, 3  * sizeof(float),D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0 },  // normal
   { 0, 6  * sizeof(float),D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },  // tex0
   { 0, 8  * sizeof(float),D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },  // tex1
   D3DDECL_END()
};

VertexDeclaration* RenderDevice::m_pVertexNormalTexelTexelDeclaration = nullptr;*/

// pre-transformed, take care that this is a float4 and needs proper w component setup (also see https://docs.microsoft.com/en-us/windows/desktop/direct3d9/mapping-fvf-codes-to-a-directx-9-declaration)
constexpr VertexElement VertexTrafoTexelElement[] =
{
   { 0, 0 * sizeof(float), D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITIONT, 0 }, // transformed pos
   { 0, 4 * sizeof(float), D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,  1 }, // (mostly, except for classic lights) unused, there to be able to share same code as VertexNormalTexelElement
   { 0, 6 * sizeof(float), D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,  0 }, // tex0
   D3DDECL_END()
};
VertexDeclaration* RenderDevice::m_pVertexTrafoTexelDeclaration = nullptr;

static unsigned int fvfToSize(const DWORD fvf)
{
   switch (fvf)
   {
   case MY_D3DFVF_NOTEX2_VERTEX:
   case MY_D3DTRANSFORMED_NOTEX2_VERTEX:
      return sizeof(Vertex3D_NoTex2);
   case MY_D3DFVF_TEX:
      return sizeof(Vertex3D_TexelOnly);
   default:
      assert(!"Unknown FVF type in fvfToSize");
      return 0;
   }
}

static VertexDeclaration* fvfToDecl(const DWORD fvf)
{
   switch (fvf)
   {
   case MY_D3DFVF_NOTEX2_VERTEX:
      return RenderDevice::m_pVertexNormalTexelDeclaration;
   case MY_D3DTRANSFORMED_NOTEX2_VERTEX:
      return RenderDevice::m_pVertexTrafoTexelDeclaration;
   case MY_D3DFVF_TEX:
      return RenderDevice::m_pVertexTexelDeclaration;
   default:
      assert(!"Unknown FVF type in fvfToDecl");
      return nullptr;
   }
}
#endif

static UINT ComputePrimitiveCount(const RenderDevice::PrimitiveTypes type, const int vertexCount)
{
   switch (type)
   {
   case RenderDevice::POINTLIST:
      return vertexCount;
   case RenderDevice::LINELIST:
      return vertexCount / 2;
   case RenderDevice::LINESTRIP:
      return std::max(0, vertexCount - 1);
   case RenderDevice::TRIANGLELIST:
      return vertexCount / 3;
   case RenderDevice::TRIANGLESTRIP:
   case RenderDevice::TRIANGLEFAN:
      return std::max(0, vertexCount - 2);
   default:
      return 0;
   }
}

#ifdef ENABLE_SDL
static const char* glErrorToString(const int error) {
   switch (error) {
   case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
   case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
   case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
   case GL_STACK_OVERFLOW: return "GL_STACK_OVERFLOW";
   case GL_STACK_UNDERFLOW: return "GL_STACK_UNDERFLOW";
   case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
   case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
   default: return "unknown";
   }
}
#endif

void ReportFatalError(const HRESULT hr, const char *file, const int line)
{
   char msg[2048+128];
#ifdef ENABLE_SDL
   sprintf_s(msg, "GL Fatal Error 0x%0002X %s in %s:%d", hr, glErrorToString(hr), file, line);
   ShowError(msg);
#else
   sprintf_s(msg, "Fatal error %s (0x%x: %s) at %s:%d", DXGetErrorString(hr), hr, DXGetErrorDescription(hr), file, line);
   ShowError(msg);
   exit(-1);
#endif
}

void ReportError(const char *errorText, const HRESULT hr, const char *file, const int line)
{
   char msg[2048+128];
#ifdef ENABLE_SDL
   sprintf_s(msg, "GL Error 0x%0002X %s in %s:%d\n%s", hr, glErrorToString(hr), file, line, errorText);
   ShowError(msg);
#else
   sprintf_s(msg, "%s %s (0x%x: %s) at %s:%d", errorText, DXGetErrorString(hr), hr, DXGetErrorDescription(hr), file, line);
   ShowError(msg);
   exit(-1);
#endif
}

#if 0//def ENABLE_SDL //not used anymore
void checkGLErrors(const char *file, const int line) {
   GLenum err;
   unsigned int count = 0;
   while ((err = glGetError()) != GL_NO_ERROR) {
      count++;
      ReportFatalError(err, file, line);
   }
   /*if (count>0) {
      exit(-1);
   }*/
}
#endif

// Callback function for printing debug statements
#ifdef _DEBUG
void APIENTRY GLDebugMessageCallback(GLenum source, GLenum type, GLuint id,
                                     GLenum severity, GLsizei length,
                                     const GLchar *msg, const void *data)
{
    char* _source;
    switch (source) {
        case GL_DEBUG_SOURCE_API:
        _source = "API";
        break;

        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
        _source = "WINDOW SYSTEM";
        break;

        case GL_DEBUG_SOURCE_SHADER_COMPILER:
        _source = "SHADER COMPILER";
        break;

        case GL_DEBUG_SOURCE_THIRD_PARTY:
        _source = "THIRD PARTY";
        break;

        case GL_DEBUG_SOURCE_APPLICATION:
        _source = "APPLICATION";
        break;

        case GL_DEBUG_SOURCE_OTHER:
        _source = "UNKNOWN";
        break;

        default:
        _source = "UNHANDLED";
        break;
    }

    char* _type;
    switch (type) {
        case GL_DEBUG_TYPE_ERROR:
        _type = "ERROR";
        break;

        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
        _type = "DEPRECATED BEHAVIOR";
        break;

        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
        _type = "UNDEFINED BEHAVIOR";
        break;

        case GL_DEBUG_TYPE_PORTABILITY:
        _type = "PORTABILITY";
        break;

        case GL_DEBUG_TYPE_PERFORMANCE:
        _type = "PERFORMANCE";
        break;

        case GL_DEBUG_TYPE_OTHER:
        _type = "OTHER";
        break;

        case GL_DEBUG_TYPE_MARKER:
        _type = "MARKER";
        break;

        case GL_DEBUG_TYPE_PUSH_GROUP:
        _type = "GL_DEBUG_TYPE_PUSH_GROUP";
        break;

        case GL_DEBUG_TYPE_POP_GROUP:
        _type = "GL_DEBUG_TYPE_POP_GROUP";
        break;

    	default:
        _type = "UNHANDLED";
        break;
    }

    char* _severity;
    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:
        _severity = "HIGH";
        break;

        case GL_DEBUG_SEVERITY_MEDIUM:
        _severity = "MEDIUM";
        break;

        case GL_DEBUG_SEVERITY_LOW:
        _severity = "LOW";
        break;

        case GL_DEBUG_SEVERITY_NOTIFICATION:
        _severity = "NOTIFICATION";
        break;

        default:
        _severity = "UNHANDLED";
        break;
    }

    //if(severity != GL_DEBUG_SEVERITY_NOTIFICATION)
    fprintf(stderr,"%d: %s of %s severity, raised from %s: %s\n", id, _type, _severity, _source, msg);

    if (type == GL_DEBUG_TYPE_ERROR || type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR || severity == GL_DEBUG_SEVERITY_HIGH)
        ShowError(msg);
}
#endif

////////////////////////////////////////////////////////////////////

int getNumberOfDisplays()
{
#ifdef ENABLE_SDL
   return SDL_GetNumVideoDisplays();
#else
   return GetSystemMetrics(SM_CMONITORS);
#endif
}

void EnumerateDisplayModes(const int display, std::vector<VideoMode>& modes)
{
   modes.clear();

#ifdef ENABLE_SDL
   const int amount = SDL_GetNumDisplayModes(display);
   for (int mode = 0; mode < amount; ++mode) {
      SDL_DisplayMode myMode;
      SDL_GetDisplayMode(display, mode, &myMode);
      VideoMode vmode = {};
      vmode.width = myMode.w;
      vmode.height = myMode.h;
      switch (myMode.format) {
      case SDL_PIXELFORMAT_RGB24:
      case SDL_PIXELFORMAT_BGR24:
      case SDL_PIXELFORMAT_RGB888:
      case SDL_PIXELFORMAT_RGBX8888:
      case SDL_PIXELFORMAT_BGR888:
      case SDL_PIXELFORMAT_BGRX8888:
      case SDL_PIXELFORMAT_ARGB8888:
      case SDL_PIXELFORMAT_RGBA8888:
      case SDL_PIXELFORMAT_ABGR8888:
      case SDL_PIXELFORMAT_BGRA8888:
         vmode.depth = 32;
         break;
      case SDL_PIXELFORMAT_RGB565:
      case SDL_PIXELFORMAT_BGR565:
      case SDL_PIXELFORMAT_ABGR1555:
      case SDL_PIXELFORMAT_BGRA5551:
      case SDL_PIXELFORMAT_ARGB1555:
      case SDL_PIXELFORMAT_RGBA5551:
         vmode.depth = 16;
         break;
      case SDL_PIXELFORMAT_ARGB2101010:
         vmode.depth = 30;
         break;
      default:
         vmode.depth = 0;
      }
      vmode.refreshrate = myMode.refresh_rate;
      modes.push_back(vmode);
   }
#else
   std::vector<DisplayConfig> displays;
   getDisplayList(displays);
   if (display >= (int)displays.size())
      return;
   const int adapter = displays[display].adapter;

   IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
   if (d3d == nullptr)
   {
      ShowError("Could not create D3D9 object.");
      return;
   }

   //for (int j = 0; j < 2; ++j)
   const int j = 0; // limit to 32bit only nowadays
   {
      const D3DFORMAT fmt = (D3DFORMAT)((j == 0) ? colorFormat::RGB8 : colorFormat::RGB5);
      const unsigned numModes = d3d->GetAdapterModeCount(adapter, fmt);

      for (unsigned i = 0; i < numModes; ++i)
      {
         D3DDISPLAYMODE d3dmode;
         d3d->EnumAdapterModes(adapter, fmt, i, &d3dmode);

         if (d3dmode.Width >= 640)
         {
            VideoMode mode;
            mode.width = d3dmode.Width;
            mode.height = d3dmode.Height;
            mode.depth = (fmt == (D3DFORMAT)colorFormat::RGB5) ? 16 : 32;
            mode.refreshrate = d3dmode.RefreshRate;
            modes.push_back(mode);
         }
      }
   }

   SAFE_RELEASE(d3d);
#endif
}

//int getDisplayList(std::vector<DisplayConfig>& displays)
//{
//   int maxAdapter = SDL_GetNumVideoDrivers();
//   int display = 0;
//   for (display = 0; display < getNumberOfDisplays(); ++display)
//   {
//      SDL_Rect displayBounds;
//      if (SDL_GetDisplayBounds(display, &displayBounds) == 0) {
//         DisplayConfig displayConf;
//         displayConf.display = display;
//         displayConf.adapter = 0;
//         displayConf.isPrimary = (displayBounds.x == 0) && (displayBounds.y == 0);
//         displayConf.top = displayBounds.x;
//         displayConf.left = displayBounds.x;
//         displayConf.width = displayBounds.w;
//         displayConf.height = displayBounds.h;
//
//         strncpy_s(displayConf.DeviceName, SDL_GetDisplayName(displayConf.display), 32);
//         strncpy_s(displayConf.GPU_Name, SDL_GetVideoDriver(displayConf.adapter), MAX_DEVICE_IDENTIFIER_STRING-1);
//
//         displays.push_back(displayConf);
//      }
//   }
//   return display;
//}

BOOL CALLBACK MonitorEnumList(__in  HMONITOR hMonitor, __in  HDC hdcMonitor, __in  LPRECT lprcMonitor, __in  LPARAM dwData)
{
   std::map<std::string,DisplayConfig>* data = reinterpret_cast<std::map<std::string,DisplayConfig>*>(dwData);
   MONITORINFOEX info;
   info.cbSize = sizeof(MONITORINFOEX);
   GetMonitorInfo(hMonitor, &info);
   DisplayConfig config = {};
   config.top = info.rcMonitor.top;
   config.left = info.rcMonitor.left;
   config.width = info.rcMonitor.right - info.rcMonitor.left;
   config.height = info.rcMonitor.bottom - info.rcMonitor.top;
   config.isPrimary = (config.top == 0) && (config.left == 0);
   config.display = (int)data->size(); // This number does neither map to the number form display settings nor something else.
#ifdef ENABLE_SDL
   config.adapter = config.display;
#else
   config.adapter = -1;
#endif
   memcpy(config.DeviceName, info.szDevice, CCHDEVICENAME); // Internal display name e.g. "\\\\.\\DISPLAY1"
   data->insert(std::pair<std::string, DisplayConfig>(config.DeviceName, config));
   return TRUE;
}

int getDisplayList(std::vector<DisplayConfig>& displays)
{
   displays.clear();
   std::map<std::string, DisplayConfig> displayMap;
   // Get the resolution of all enabled displays.
   EnumDisplayMonitors(nullptr, nullptr, MonitorEnumList, reinterpret_cast<LPARAM>(&displayMap));

#ifndef ENABLE_SDL
   IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
   if (pD3D == nullptr)
   {
      ShowError("Could not create D3D9 object.");
      throw 0;
   }
   // Map the displays to the DX9 adapter. Otherwise this leads to an performance impact on systems with multiple GPUs
   const int adapterCount = pD3D->GetAdapterCount();
   for (int i = 0;i < adapterCount;++i) {
      D3DADAPTER_IDENTIFIER9 adapter;
      pD3D->GetAdapterIdentifier(i, 0, &adapter);
      std::map<std::string, DisplayConfig>::iterator display = displayMap.find(adapter.DeviceName);
      if (display != displayMap.end()) {
         display->second.adapter = i;
         strncpy_s(display->second.GPU_Name, adapter.Description, sizeof(display->second.GPU_Name)-1);
      }
   }
   SAFE_RELEASE(pD3D);
#endif

   // Apply the same numbering as windows
   int i = 0;
   for (std::map<std::string, DisplayConfig>::iterator display = displayMap.begin(); display != displayMap.end(); ++display)
   {
      if (display->second.adapter >= 0) {
         display->second.display = i;
#ifdef ENABLE_SDL
         const char* name = SDL_GetDisplayName(display->second.adapter);
         if(name != nullptr)
            strncpy_s(display->second.GPU_Name, name, sizeof(display->second.GPU_Name) - 1);
         else
            display->second.GPU_Name[0] = '\0';
#endif
         displays.push_back(display->second);
      }
      i++;
   }
   return i;
}

bool getDisplaySetupByID(const int display, int &x, int &y, int &width, int &height)
{
   std::vector<DisplayConfig> displays;
   getDisplayList(displays);
   for (std::vector<DisplayConfig>::iterator displayConf = displays.begin(); displayConf != displays.end(); ++displayConf) {
      if ((display == -1 && displayConf->isPrimary) || display == displayConf->display) {
         x = displayConf->left;
         y = displayConf->top;
         width = displayConf->width;
         height = displayConf->height;
         return true;
      }
   }
   x = 0;
   y = 0;
   width = GetSystemMetrics(SM_CXSCREEN);
   height = GetSystemMetrics(SM_CYSCREEN);
   return false;
}

int getPrimaryDisplay()
{
   std::vector<DisplayConfig> displays;
   getDisplayList(displays);
   for (std::vector<DisplayConfig>::iterator displayConf = displays.begin(); displayConf != displays.end(); ++displayConf)
      if (displayConf->isPrimary)
         return displayConf->adapter;
   return 0;
}

////////////////////////////////////////////////////////////////////

VertexBuffer* RenderDevice::m_quadVertexBuffer = nullptr;
unsigned int RenderDevice::m_stats_drawn_triangles = 0;

#ifndef ENABLE_SDL
#define CHECKNVAPI(s) { NvAPI_Status hr = (s); if (hr != NVAPI_OK) { NvAPI_ShortString ss; NvAPI_GetErrorMessage(hr,ss); g_pvp->MessageBox(ss, "NVAPI", MB_OK | MB_ICONEXCLAMATION); } }
static bool NVAPIinit = false; //!! meh

bool RenderDevice::m_INTZ_support = false;
bool RenderDevice::m_useNvidiaApi = false;

#ifdef USE_D3D9EX
 typedef HRESULT(WINAPI *pD3DC9Ex)(UINT SDKVersion, IDirect3D9Ex**);
 static pD3DC9Ex mDirect3DCreate9Ex = nullptr;
#endif

#define DWM_EC_DISABLECOMPOSITION         0
#define DWM_EC_ENABLECOMPOSITION          1
typedef HRESULT(STDAPICALLTYPE *pDICE)(BOOL* pfEnabled);
static pDICE mDwmIsCompositionEnabled = nullptr;
typedef HRESULT(STDAPICALLTYPE *pDF)();
static pDF mDwmFlush = nullptr;
typedef HRESULT(STDAPICALLTYPE *pDEC)(UINT uCompositionAction);
static pDEC mDwmEnableComposition = nullptr;
#endif

#ifdef _DEBUG
#ifdef ENABLE_SDL
static void CheckForGLLeak()
{
   //TODO
}
#else
static void CheckForD3DLeak(IDirect3DDevice9* d3d)
{
   IDirect3DSwapChain9 *swapChain;
   CHECKD3D(d3d->GetSwapChain(0, &swapChain));

   D3DPRESENT_PARAMETERS pp;
   CHECKD3D(swapChain->GetPresentParameters(&pp));
   SAFE_RELEASE(swapChain);

   // idea: device can't be reset if there are still allocated resources
   HRESULT hr = d3d->Reset(&pp);
   if (FAILED(hr))
   {
      g_pvp->MessageBox("WARNING! Direct3D resource leak detected!", "Visual Pinball", MB_ICONWARNING);
   }
}
#endif
#endif

bool RenderDevice::isVRinstalled()
{
#ifdef ENABLE_VR
   return vr::VR_IsRuntimeInstalled();
#else
   return false;
#endif
}

#ifdef ENABLE_VR
vr::IVRSystem* RenderDevice::m_pHMD = nullptr;
#endif

bool RenderDevice::isVRturnedOn()
{
#ifdef ENABLE_VR
   if (vr::VR_IsHmdPresent()) {
      vr::EVRInitError VRError = vr::VRInitError_None;
      if (!m_pHMD)
         m_pHMD = vr::VR_Init(&VRError, vr::VRApplication_Background);
      if (VRError == vr::VRInitError_None && vr::VRCompositor()) {
         for (int device = 0; device < vr::k_unMaxTrackedDeviceCount; device++) {
            if ((m_pHMD->GetTrackedDeviceClass(device) == vr::TrackedDeviceClass_HMD)) {
               vr::VR_Shutdown();
               m_pHMD = nullptr;
               return true;
            }
         }
      } else
         m_pHMD = nullptr;
   }
#endif
   return false;
}

void RenderDevice::turnVROff()
{
#ifdef ENABLE_VR
   if (m_pHMD)
   {
      vr::VR_Shutdown();
      m_pHMD = nullptr;
   }
#endif
}

void RenderDevice::InitVR() {
#ifdef ENABLE_VR
   vr::EVRInitError VRError = vr::VRInitError_None;
   if (!m_pHMD) {
      m_pHMD = vr::VR_Init(&VRError, vr::VRApplication_Scene);
      if (VRError != vr::VRInitError_None) {
         m_pHMD = nullptr;
         char buf[1024];
         sprintf_s(buf, sizeof(buf), "Unable to init VR runtime: %s", vr::VR_GetVRInitErrorAsEnglishDescription(VRError));
         ShowError(buf);
         std::runtime_error vrInitFailed(buf);
         throw(vrInitFailed);
      }
      if (!vr::VRCompositor())
         /*if (VRError != vr::VRInitError_None)*/ {
            m_pHMD = nullptr;
            char buf[1024];
            sprintf_s(buf, sizeof(buf), "Unable to init VR compositor");// :% s", vr::VR_GetVRInitErrorAsEnglishDescription(VRError));
            ShowError(buf);
            std::runtime_error vrInitFailed(buf);
            throw(vrInitFailed);
         }
   }
   m_pHMD->GetRecommendedRenderTargetSize(&m_Buf_width, &m_Buf_height);

   //Calculate left EyeProjection Matrix relative to HMD position
   vr::HmdMatrix34_t mat34 = m_pHMD->GetEyeToHeadTransform(vr::Eye_Left);
   Matrix3D matEye2Head;
   for (int i = 0;i < 3;i++)
      for (int j = 0;j < 4;j++)
         matEye2Head.m[j][i] = mat34.m[i][j];
   for (int j = 0;j < 4;j++)
      matEye2Head.m[j][3] = (j == 3) ? 1.0f : 0.0f;

   matEye2Head.Invert();

   const float nearPlane = LoadValueFloatWithDefault("PlayerVR", "nearPlane", 5.0f) / 100.0f;
   const float farPlane = 5000.0f;//LoadValueFloatWithDefault("PlayerVR", "farPlane", 5000.0f) / 100.0f;

   vr::HmdMatrix44_t mat44 = m_pHMD->GetProjectionMatrix(vr::Eye_Left, nearPlane, farPlane);//5cm to 50m should be a reasonable range
   mat44.m[2][2] = -1.0f;
   mat44.m[2][3] = -nearPlane;
   Matrix3D matProjection;
   for (int i = 0;i < 4;i++)
      for (int j = 0;j < 4;j++)
            matProjection.m[j][i] = mat44.m[i][j];

   m_matProj[0] = matEye2Head * matProjection;

   //Calculate right EyeProjection Matrix relative to HMD position
   mat34 = m_pHMD->GetEyeToHeadTransform(vr::Eye_Right);
   for (int i = 0;i < 3;i++)
      for (int j = 0;j < 4;j++)
         matEye2Head.m[j][i] = mat34.m[i][j];
   for (int j = 0;j < 4;j++)
      matEye2Head.m[j][3] = (j == 3) ? 1.0f : 0.0f;

   matEye2Head.Invert();

   mat44 = m_pHMD->GetProjectionMatrix(vr::Eye_Right, nearPlane, farPlane);//5cm to 500m should be a reasonable range
   mat44.m[2][2] = -1.0f;
   mat44.m[2][3] = -nearPlane;
   for (int i = 0;i < 4;i++)
      for (int j = 0;j < 4;j++)
            matProjection.m[j][i] = mat44.m[i][j];

   m_matProj[1] = matEye2Head * matProjection;

   if (vr::k_unMaxTrackedDeviceCount > 0) {
      m_rTrackedDevicePose = new vr::TrackedDevicePose_t[vr::k_unMaxTrackedDeviceCount];
   }
   else {
      std::runtime_error noDevicesFound("No Tracking devices found");
      throw(noDevicesFound);
   }

   m_slope = LoadValueFloatWithDefault("Player", "VRSlope", 6.5f);
   m_orientation = LoadValueFloatWithDefault("Player", "VROrientation", 0.0f);
   m_tablex = LoadValueFloatWithDefault("Player", "VRTableX", 0.0f);
   m_tabley = LoadValueFloatWithDefault("Player", "VRTableY", 0.0f);
   m_tablez = LoadValueFloatWithDefault("Player", "VRTableZ", 80.0f);
   m_roomOrientation = LoadValueFloatWithDefault("Player", "VRRoomOrientation", 0.0f);
   m_roomx = LoadValueFloatWithDefault("Player", "VRRoomX", 0.0f);
   m_roomy = LoadValueFloatWithDefault("Player", "VRRoomY", 0.0f);

   updateTableMatrix();

#else

   std::runtime_error unknownStereoMode("This version of Visual Pinball was compiled without VR support");
   throw(unknownStereoMode);
#endif
}

#ifdef ENABLE_SDL
RenderDevice::RenderDevice(const int width, const int height, const bool fullscreen, const int colordepth, int VSync, const float AAfactor, const int stereo3D, const unsigned int FXAA, const bool ss_refl, const bool useNvidiaApi, const bool disable_dwm, const int BWrendering, const RenderDevice* primaryDevice)
   : m_texMan(*this), m_width(width), m_height(height), m_fullscreen(fullscreen),
   m_colorDepth(colordepth), m_vsync(VSync), m_AAfactor(AAfactor), m_stereo3D(stereo3D), m_FXAA(FXAA),
   m_ssRefl(ss_refl), m_disableDwm(disable_dwm), m_BWrendering(BWrendering), m_GLversion(0)
{
   //m_useNvidiaApi = useNvidiaApi;

#ifdef ENABLE_VR
   m_pHMD = nullptr;
   m_rTrackedDevicePose = nullptr;
#endif
   for (int i = 0; i < RENDERSTATE_COUNT; ++i) renderStateCache[i] = 0;
}

void RenderDevice::CreateDevice(int &refreshrate, UINT adapterIndex)
{
   m_stats_drawn_triangles = 0;

   const int displays = getNumberOfDisplays();
   if ((int)adapterIndex >= displays)
      m_adapter = 0;
   else
      m_adapter = adapterIndex;

   const bool video10bit = (m_colorDepth == SDL_PIXELFORMAT_ARGB2101010);

   //SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

/*   SDL_GL_SetAttribute(SDL_GL_RED_SIZE, video10bit ? 10 : 8);
   SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, video10bit ? 10 : 8);
   SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, video10bit ? 10 : 8);
   SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, video10bit ? 2 : 8);
   SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);*/

   SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
   // ATM Supersampling is used. MSAA can be enabled here with useAA ? 0 : 4
   SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);

   //SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

   int disp_x, disp_y, disp_w, disp_h;
   getDisplaySetupByID(m_adapter, disp_x, disp_y, disp_w, disp_h);

   const bool disableVRPreview = (m_stereo3D == STEREO_VR) && LoadValueBoolWithDefault("PlayerVR", "VRPreviewDisabled", false);

   if (!disableVRPreview)
      m_sdl_playfieldHwnd = SDL_CreateWindow(
         "Visual Pinball Player SDL", disp_x + (disp_w - m_width) / 2, disp_y + (disp_h - m_height) / 2, m_width, m_height,
         SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | (m_fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
   else
      m_sdl_playfieldHwnd = SDL_CreateWindow(
         "Visual Pinball Player SDL", disp_x + (disp_w - 640) / 2, disp_y + (disp_h - 480) / 2, 640, 480,
         SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);

   SDL_SysWMinfo wmInfo;
   SDL_VERSION(&wmInfo.version);
   SDL_GetWindowWMInfo(m_sdl_playfieldHwnd, &wmInfo);
   m_windowHwnd = wmInfo.info.win.window;

   m_sdl_context = SDL_GL_CreateContext(m_sdl_playfieldHwnd);

   SDL_GL_MakeCurrent(m_sdl_playfieldHwnd, m_sdl_context);

   if (!gladLoadGLLoader(SDL_GL_GetProcAddress)) {
      ShowError("Glad failed");
      exit(-1);
   }

   int gl_majorVersion = 0;
   int gl_minorVersion = 0;
   glGetIntegerv(GL_MAJOR_VERSION, &gl_majorVersion);
   glGetIntegerv(GL_MINOR_VERSION, &gl_minorVersion);

   if (gl_majorVersion < 3 || (gl_majorVersion == 3 && gl_minorVersion < 2)) {
      char errorMsg[256];
      sprintf_s(errorMsg, 256, "Your graphics card only supports OpenGL %d.%d, but VPVR requires OpenGL 3.2 or newer.", gl_majorVersion, gl_minorVersion);
      ShowError(errorMsg);
      exit(-1);
   }

   m_GLversion = gl_majorVersion*100 + gl_minorVersion;

   // Enable debugging layer of OpenGL
#ifdef _DEBUG
   glEnable(GL_DEBUG_OUTPUT); // on its own is the 'fast' version
   //glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS); // callback is in sync with errors, so a breakpoint can be placed on the callback in order to get a stacktrace for the GL error
   glDebugMessageCallback(GLDebugMessageCallback, nullptr);
#endif
#if 0
   glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_FALSE); // disable all
   glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_ERROR, GL_DONT_CARE, 0, nullptr, GL_TRUE); // enable only errors
#endif

   GLint frameBuffer[4];
   glGetIntegerv(GL_VIEWPORT, frameBuffer);
   const int fbWidth = frameBuffer[2];
   const int fbHeight = frameBuffer[3];

   switch (m_stereo3D) {
   case STEREO_OFF:
      m_Buf_width = fbWidth;
      m_Buf_height = fbHeight;
      m_Buf_widthBlur = m_Buf_width / 3;
      m_Buf_heightBlur = m_Buf_height / 3;
      m_Buf_width = (int)(m_Buf_width * m_AAfactor);
      m_Buf_height = (int)(m_Buf_height * m_AAfactor);
      break;
   case STEREO_TB:
   case STEREO_INT:
      m_Buf_width = fbWidth;
      m_Buf_height = fbHeight * 2;
      m_Buf_widthBlur = m_Buf_width / 3;
      m_Buf_heightBlur = m_Buf_height / 3;
      m_Buf_width = (int)(m_Buf_width * m_AAfactor);
      m_Buf_height = (int)(m_Buf_height * m_AAfactor);
      break;
   case STEREO_SBS:
      m_Buf_width = fbWidth * 2;
      m_Buf_height = fbHeight;
      m_Buf_widthBlur = m_Buf_width / 3;
      m_Buf_heightBlur = m_Buf_height / 3;
      m_Buf_width = (int)(m_Buf_width * m_AAfactor);
      m_Buf_height = (int)(m_Buf_height * m_AAfactor);
      break;
#ifdef ENABLE_VR
   case STEREO_VR:
      if (LoadValueBoolWithDefault("PlayerVR", "scaleToFixedWidth", false)) {
         float width;
         g_pplayer->m_ptable->get_Width(&width);
         m_scale = LoadValueFloatWithDefault("PlayerVR", "scaleAbsolute", 55.0f) * 0.01f / width;
      }
      else
         m_scale = 0.000540425f * LoadValueFloatWithDefault("PlayerVR", "scaleRelative", 1.0f);
      if (m_scale <= 0.f)
         m_scale = 0.000540425f;// Scale factor for VPUnits to Meters
      InitVR();
      m_Buf_width = m_Buf_width * 2;
      m_Buf_widthBlur = m_Buf_width / 3;
      m_Buf_heightBlur = m_Buf_height / 3;
      m_Buf_width = (int)(m_Buf_width * m_AAfactor);
      m_Buf_height = (int)(m_Buf_height * m_AAfactor);
      break;
#endif
   default:
      char buf[1024];
      sprintf_s(buf, sizeof(buf), "Unknown stereo Mode id: %d", m_stereo3D);
      std::runtime_error unknownStereoMode(buf);
      throw(unknownStereoMode);
   }

   if (m_stereo3D == STEREO_VR || m_vsync > refreshrate)
      m_vsync = 0;
   SDL_GL_SetSwapInterval(m_vsync);

   m_autogen_mipmap = true;

   // Retrieve a reference to the back buffer.
   m_pBackBuffer = new RenderTarget;
   m_pBackBuffer->width = fbWidth;
   m_pBackBuffer->height = fbHeight;

   glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint*)(&m_pBackBuffer->framebuffer));

   constexpr colorFormat renderBufferFormat = RGBA16F;

   // alloc float buffer for rendering (optionally 2x2 res for manual super sampling)
   m_pOffscreenBackBufferTexture = CreateTexture(m_Buf_width, m_Buf_height, 0, RENDERTARGET_MSAA_DEPTH, renderBufferFormat, nullptr, m_stereo3D);

   // If we are doing MSAA we need a texture with the same dimensions as the Back Buffer to resolve the end result to, can also use it for Post-AA
   if (g_pplayer->m_MSAASamples > 1 || m_FXAA > 0)
      m_pOffscreenNonMSAABlitTexture = CreateTexture(m_Buf_width, m_Buf_height, 0, RENDERTARGET_DEPTH, renderBufferFormat, nullptr, m_stereo3D);
   else
      m_pOffscreenNonMSAABlitTexture = nullptr;

   if ((g_pplayer != nullptr) && (g_pplayer->m_ptable->m_reflectElementsOnPlayfield || (g_pplayer->m_reflectionForBalls && (g_pplayer->m_ptable->m_useReflectionForBalls == -1)) || (g_pplayer->m_ptable->m_useReflectionForBalls == 1)))
      m_pMirrorTmpBufferTexture = CreateTexture(m_Buf_width, m_Buf_height, 0, RENDERTARGET_DEPTH, renderBufferFormat, nullptr, m_stereo3D);

   // alloc bloom tex at 1/3 x 1/3 res (allows for simple HQ downscale of clipped input while saving memory)
   m_pBloomBufferTexture = CreateTexture(m_Buf_widthBlur, m_Buf_heightBlur, 0, RENDERTARGET, renderBufferFormat, nullptr, m_stereo3D);

   // temporary buffer for gaussian blur
   m_pBloomTmpBufferTexture = CreateTexture(m_Buf_widthBlur, m_Buf_heightBlur, 0, RENDERTARGET, renderBufferFormat, nullptr, m_stereo3D);

   // alloc temporary buffer for postprocessing, we don't use this anymore
   //if ((m_FXAA > 0) || (m_stereo3D > 0))
     // m_pOffscreenBackBufferStereoTexture = CreateTexture(m_Buf_width, m_Buf_height, 0, RENDERTARGET_MSAA_DEPTH, renderBufferFormat, nullptr, 0);
   //else
      m_pOffscreenBackBufferStereoTexture = nullptr;

   if (m_stereo3D == STEREO_VR) {
      //AMD Debugging
      colorFormat renderBufferFormatVR;
      const int textureModeVR = LoadValueIntWithDefault("Player", "textureModeVR", 1);
      switch (textureModeVR) {
      case 0:
         renderBufferFormatVR = RGB8;
         break;
      case 2:
         renderBufferFormatVR = RGB16F;
         break;
      case 3:
         renderBufferFormatVR = RGBA16F;
         break;
      case 1:
      default:
         renderBufferFormatVR = RGBA8;
         break;
      }
      m_pOffscreenVRLeft = CreateTexture(m_Buf_width / 2, m_Buf_height, 0, RENDERTARGET, renderBufferFormatVR, nullptr, 0);
      m_pOffscreenVRRight = CreateTexture(m_Buf_width / 2, m_Buf_height, 0, RENDERTARGET, renderBufferFormatVR, nullptr, 0);
   }

   // Non-MSAA Buffers for post-processing
   m_pOffscreenBackBufferPPTexture1 = CreateTexture(m_Buf_width, m_Buf_height, 0, RENDERTARGET_DEPTH, renderBufferFormat, nullptr, 0);
   m_pOffscreenBackBufferPPTexture2 = CreateTexture(m_Buf_width, m_Buf_height, 0, RENDERTARGET_DEPTH, renderBufferFormat, nullptr, 0);

   // Use postprocessing buffer instead of separate reflectionbuffer
   /*if (m_ssRefl)
      m_pReflectionBufferTexture = CreateTexture(m_Buf_width, m_Buf_height, 0, RENDERTARGET_MSAA_DEPTH, renderBufferFormat, nullptr, 0);
   else*/
      m_pReflectionBufferTexture = nullptr;

   if (video10bit && (m_FXAA == Quality_SMAA || m_FXAA == Standard_DLAA))
      ShowError("SMAA or DLAA post-processing AA should not be combined with 10bit-output rendering (will result in visible artifacts)!");

   currentDeclaration = nullptr;
   //m_curShader = nullptr;

   // fill state caches with dummy values
   memset(textureStateCache, 0xCC, sizeof(DWORD) * 8 * TEXTURE_STATE_CACHE_SIZE);
   memset(textureSamplerCache, 0xCC, sizeof(DWORD) * 8 * TEXTURE_SAMPLER_CACHE_SIZE);

   // initialize performance counters
   m_curDrawCalls = m_frameDrawCalls = 0;
   m_curStateChanges = m_frameStateChanges = 0;
   m_curTextureChanges = m_frameTextureChanges = 0;
   m_curParameterChanges = m_frameParameterChanges = 0;
   m_curTextureUpdates = m_frameTextureUpdates = 0;

   m_maxaniso = 0;
   glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &m_maxaniso);

   if (m_quadVertexBuffer == nullptr) {
      VertexBuffer::CreateVertexBuffer(4, USAGE_STATIC, MY_D3DFVF_TEX, &m_quadVertexBuffer, PRIMARY_DEVICE); //!!
      Vertex3D_TexelOnly* bufvb;
      m_quadVertexBuffer->lock(0, 0, (void**)&bufvb, USAGE_STATIC);
      static constexpr float verts[4 * 5] = //GL Texture coordinates
      {
         1.0f, 1.0f, 0.0f, 1.0f, 0.0f,
         0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
         1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
         0.0f, 0.0f, 0.0f, 0.0f, 1.0f
      };
      memcpy(bufvb, verts, 4 * sizeof(Vertex3D_TexelOnly));
      m_quadVertexBuffer->unlock();
   }

   SetRenderState(RenderDevice::ZFUNC, RenderDevice::Z_LESSEQUAL);
}

bool RenderDevice::LoadShaders()
{
   bool shaderCompilationOkay = true;

   char glShaderPath[MAX_PATH];
   /*DWORD length =*/ GetModuleFileName(nullptr, glShaderPath, MAX_PATH);

   if (m_stereo3D == STEREO_OFF) {
      Shader::Defines = "#define eyes 1\n#define enable_VR 0";
   } else  if (m_stereo3D == STEREO_VR) {
      Shader::Defines = "#define eyes 2\n#define enable_VR 1";
   } else {
      Shader::Defines = "#define eyes 2\n#define enable_VR 0";
   }

   Shader::shaderPath = string(glShaderPath);
   Shader::shaderPath = Shader::shaderPath.substr(0, Shader::shaderPath.find_last_of("\\/"));
   Shader::shaderPath.append("\\glshader\\");
   basicShader = new Shader(this);
   shaderCompilationOkay = basicShader->Load("BasicShader.glfx", 0) && shaderCompilationOkay;

   ballShader = new Shader(this);
   shaderCompilationOkay = ballShader->Load("ballShader.glfx", 0) && shaderCompilationOkay;

   DMDShader = new Shader(this);
   if (m_stereo3D == STEREO_VR)
      shaderCompilationOkay = DMDShader->Load("DMDShaderVR.glfx", 0) && shaderCompilationOkay;
   else
      shaderCompilationOkay = DMDShader->Load("DMDShader.glfx", 0) && shaderCompilationOkay;
   DMDShader->SetVector(SHADER_quadOffsetScale, 0.0f, 0.0f, 1.0f, 1.0f);
   DMDShader->SetVector(SHADER_quadOffsetScaleTex, 0.0f, 0.0f, 1.0f, 1.0f);

   FBShader = new Shader(this);
   shaderCompilationOkay = FBShader->Load("FBShader.glfx", 0) && shaderCompilationOkay;
   shaderCompilationOkay = FBShader->Load("SMAA.glfx", 0) && shaderCompilationOkay;
   FBShader->SetVector(SHADER_quadOffsetScale, 0.0f, 0.0f, 1.0f, 1.0f);
   FBShader->SetVector(SHADER_quadOffsetScaleTex, 0.0f, 0.0f, 1.0f, 1.0f);

   if (m_stereo3D) {
      StereoShader = new Shader(this);
      shaderCompilationOkay = StereoShader->Load("StereoShader.glfx", 0) && shaderCompilationOkay;
   }
   else
      StereoShader = nullptr;

   flasherShader = new Shader(this);
   shaderCompilationOkay = flasherShader->Load("flasherShader.glfx", 0) && shaderCompilationOkay;

   lightShader = new Shader(this);
   shaderCompilationOkay = lightShader->Load("lightShader.glfx", 0) && shaderCompilationOkay;

#ifdef SEPARATE_CLASSICLIGHTSHADER
   classicLightShader = new Shader(this);
   shaderCompilationOkay = classicLightShader->Load("classicLightShader.glfx", 0) && shaderCompilationOkay;
#endif

   if (shaderCompilationOkay && m_FXAA == Quality_SMAA)
      UploadAndSetSMAATextures();
   else
   {
      m_SMAAareaTexture = 0;
      m_SMAAsearchTexture = 0;
   }

   // Initialize uniform to default value
   if (shaderCompilationOkay)
       basicShader->SetFlasherColorAlpha(vec4(1.0f, 1.0f, 1.0f, 1.0f));

   return shaderCompilationOkay;
}

RenderDevice::~RenderDevice()
{
   if (m_quadVertexBuffer)
      m_quadVertexBuffer->release();
   m_quadVertexBuffer = nullptr;

   FreeShader();

#ifdef ENABLE_VR
   if (m_pHMD)
   {
      turnVROff();
      SaveValueFloat("Player", "VRSlope", m_slope);
      SaveValueFloat("Player", "VROrientation", m_orientation);
      SaveValueFloat("Player", "VRTableX", m_tablex);
      SaveValueFloat("Player", "VRTableY", m_tabley);
      SaveValueFloat("Player", "VRTableZ", m_tablez);
      SaveValueFloat("Player", "VRRoomOrientation", m_roomOrientation);
      SaveValueFloat("Player", "VRRoomX", m_roomx);
      SaveValueFloat("Player", "VRRoomY", m_roomy);
   }
#endif

   SDL_GL_DeleteContext(m_sdl_context);
   SDL_DestroyWindow(m_sdl_playfieldHwnd);
}

#else

RenderDevice::RenderDevice(const int width, const int height, const bool fullscreen, const int colordepth, int VSync, const float AAfactor, const int stereo3D, const unsigned int FXAA, const bool ss_refl, const bool useNvidiaApi, const bool disable_dwm, const int BWrendering, const RenderDevice* primaryDevice)
   : m_texMan(*this), m_windowHwnd(g_pplayer->GetHwnd()), m_width(width), m_height(height), m_fullscreen(fullscreen),
      m_colorDepth(colordepth), m_vsync(VSync), m_AAfactor(AAfactor), m_stereo3D(stereo3D), m_FXAA(FXAA),
      m_ssRefl(ss_refl), m_disableDwm(disable_dwm), m_BWrendering(BWrendering)
{
   m_useNvidiaApi = useNvidiaApi;

   switch (stereo3D) {
   case STEREO_OFF:
      m_Buf_width = m_width;
      m_Buf_height = m_height;
      m_Buf_widthBlur = m_width / 3;
      m_Buf_heightBlur = m_height / 3;
      m_Buf_widthSS = m_width * m_AAfactor;
      m_Buf_heightSS = m_height * m_AAfactor;
      break;
   case STEREO_TB:
   case STEREO_INT:
      m_Buf_width = m_width;
      m_Buf_height = m_height * 2;
      m_Buf_widthBlur = m_width / 3;
      m_Buf_heightBlur = m_height * 2 / 3;
      m_Buf_widthSS = m_width * m_AAfactor;
      m_Buf_heightSS = m_height * m_AAfactor;
      break;
   case STEREO_SBS:
      m_Buf_width = m_width * 2;
      m_Buf_height = m_height;
      m_Buf_widthBlur = m_width * 2 / 3;
      m_Buf_heightBlur = m_height / 3;
      m_Buf_widthSS = m_width * m_AAfactor;
      m_Buf_heightSS = m_height * m_AAfactor;
      break;
   default:
      char buf[1024];
      sprintf_s(buf, sizeof(buf), "Unknown stereo Mode id: %d", stereo3D);
      std::runtime_error unknownStereoMode(buf);
      throw(unknownStereoMode);
   }

   m_stats_drawn_triangles = 0;

   m_adapter = D3DADAPTER_DEFAULT;     // for now, always use the default adapter

   mDwmIsCompositionEnabled = (pDICE)GetProcAddress(GetModuleHandle(TEXT("dwmapi.dll")), "DwmIsCompositionEnabled"); //!! remove as soon as win xp support dropped and use static link
   mDwmEnableComposition = (pDEC)GetProcAddress(GetModuleHandle(TEXT("dwmapi.dll")), "DwmEnableComposition"); //!! remove as soon as win xp support dropped and use static link
   mDwmFlush = (pDF)GetProcAddress(GetModuleHandle(TEXT("dwmapi.dll")), "DwmFlush"); //!! remove as soon as win xp support dropped and use static link

   if (mDwmIsCompositionEnabled && mDwmEnableComposition)
   {
      BOOL dwm = 0;
      mDwmIsCompositionEnabled(&dwm);
      m_dwm_enabled = m_dwm_was_enabled = !!dwm;

      if (m_dwm_was_enabled && m_disableDwm && IsWindowsVistaOr7()) // windows 8 and above will not allow do disable it, but will still return S_OK
      {
         mDwmEnableComposition(DWM_EC_DISABLECOMPOSITION);
         m_dwm_enabled = false;
      }
   }
   else
   {
      m_dwm_was_enabled = false;
      m_dwm_enabled = false;
   }

   currentDeclaration = nullptr;
   //m_curShader = nullptr;

   // fill state caches with dummy values
   memset(textureStateCache, 0xCC, sizeof(DWORD) * TEXTURE_SAMPLERS * TEXTURE_STATE_CACHE_SIZE);
   memset(textureSamplerCache, 0xCC, sizeof(DWORD) * TEXTURE_SAMPLERS * TEXTURE_SAMPLER_CACHE_SIZE);

   // initialize performance counters
   m_curDrawCalls = m_frameDrawCalls = 0;
   m_curStateChanges = m_frameStateChanges = 0;
   m_curTextureChanges = m_frameTextureChanges = 0;
   m_curParameterChanges = m_frameParameterChanges = 0;
   m_curTechniqueChanges = m_frameTechniqueChanges = 0;
   m_curTextureUpdates = m_frameTextureUpdates = 0;

   m_curLockCalls = m_frameLockCalls = 0; //!! meh
}

void RenderDevice::CreateDevice(int &refreshrate, UINT adapterIndex)
{
#ifdef USE_D3D9EX
   m_pD3DEx = nullptr;
   m_pD3DDeviceEx = nullptr;

   mDirect3DCreate9Ex = (pD3DC9Ex)GetProcAddress(GetModuleHandle(TEXT("d3d9.dll")), "Direct3DCreate9Ex"); //!! remove as soon as win xp support dropped and use static link
   if (mDirect3DCreate9Ex)
   {
      const HRESULT hr = mDirect3DCreate9Ex(D3D_SDK_VERSION, &m_pD3DEx);
      if (FAILED(hr) || (m_pD3DEx == nullptr))
      {
         ShowError("Could not create D3D9Ex object.");
         throw 0;
      }
      m_pD3DEx->QueryInterface(__uuidof(IDirect3D9), reinterpret_cast<void **>(&m_pD3D));
   }
   else
#endif
   {
      m_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
      if (m_pD3D == nullptr)
      {
         ShowError("Could not create D3D9 object.");
         throw 0;
      }
   }

   m_adapter = m_pD3D->GetAdapterCount() > (int)adapterIndex ? adapterIndex : 0;

   D3DDEVTYPE devtype = D3DDEVTYPE_HAL;

   // Look for 'NVIDIA PerfHUD' adapter
   // If it is present, override default settings
   // This only takes effect if run under NVPerfHud, otherwise does nothing
   for (UINT adapter = 0; adapter < m_pD3D->GetAdapterCount(); adapter++)
   {
      D3DADAPTER_IDENTIFIER9 Identifier;
      m_pD3D->GetAdapterIdentifier(adapter, 0, &Identifier);
      if (strstr(Identifier.Description, "PerfHUD") != 0)
      {
         m_adapter = adapter;
         devtype = D3DDEVTYPE_REF;
         break;
      }
   }

   D3DCAPS9 caps;
   m_pD3D->GetDeviceCaps(m_adapter, devtype, &caps);

   // check which parameters can be used for anisotropic filter
   m_mag_aniso = (caps.TextureFilterCaps & D3DPTFILTERCAPS_MAGFANISOTROPIC) != 0;
   m_maxaniso = caps.MaxAnisotropy;

   if (((caps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) != 0) || ((caps.TextureCaps & D3DPTEXTURECAPS_POW2) != 0))
      ShowError("D3D device does only support power of 2 textures");

   //if (caps.NumSimultaneousRTs < 2)
   //   ShowError("D3D device doesn't support multiple render targets!");

   bool video10bit = LoadValueBoolWithDefault("Player", "Render10Bit", false);

   if (!m_fullscreen && video10bit)
   {
      ShowError("10Bit-Monitor support requires 'Force exclusive Fullscreen Mode' to be also enabled!");
      video10bit = false;
   }

   // get the current display format
   D3DFORMAT format;
   if (!m_fullscreen)
   {
      D3DDISPLAYMODE mode;
      CHECKD3D(m_pD3D->GetAdapterDisplayMode(m_adapter, &mode));
      format = mode.Format;
      refreshrate = mode.RefreshRate;
   }
   else
   {
      format = (D3DFORMAT)(video10bit ? colorFormat::RGBA10 : ((m_colorDepth == 16) ? colorFormat::RGB5 : colorFormat::RGB8));
   }

   // limit vsync rate to actual refresh rate, otherwise special handling in renderloop
   if (m_vsync > refreshrate)
      m_vsync = 0;

   D3DPRESENT_PARAMETERS params;
   params.BackBufferWidth = m_width;
   params.BackBufferHeight = m_height;
   params.BackBufferFormat = format;
   params.BackBufferCount = 1;
   params.MultiSampleType = /*useAA ? D3DMULTISAMPLE_4_SAMPLES :*/ D3DMULTISAMPLE_NONE; // D3DMULTISAMPLE_NONMASKABLE? //!! useAA now uses super sampling/offscreen render
   params.MultiSampleQuality = 0; // if D3DMULTISAMPLE_NONMASKABLE then set to > 0
   params.SwapEffect = D3DSWAPEFFECT_DISCARD;  // FLIP ?
   params.hDeviceWindow = m_windowHwnd;
   params.Windowed = !m_fullscreen;
   params.EnableAutoDepthStencil = FALSE;
   params.AutoDepthStencilFormat = D3DFMT_UNKNOWN;      // ignored
   params.Flags = /*fullscreen ? D3DPRESENTFLAG_LOCKABLE_BACKBUFFER :*/ /*(stereo3D ?*/ 0 /*: D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL)*/; // D3DPRESENTFLAG_LOCKABLE_BACKBUFFER only needed for SetDialogBoxMode() below, but makes rendering slower on some systems :/
   params.FullScreen_RefreshRateInHz = m_fullscreen ? refreshrate : 0;
#ifdef USE_D3D9EX
   params.PresentationInterval = (m_pD3DEx && (m_vsync != 1)) ? D3DPRESENT_INTERVAL_IMMEDIATE : (!!m_vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE); //!! or have a special mode to force normal vsync?
#else
   params.PresentationInterval = !!m_vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
#endif

   // check if our HDR texture format supports/does sRGB conversion on texture reads, which must NOT be the case as we always set SRGBTexture=true independent of the format!
   HRESULT hr = m_pD3D->CheckDeviceFormat(m_adapter, devtype, params.BackBufferFormat, D3DUSAGE_QUERY_SRGBREAD, D3DRTYPE_TEXTURE, (D3DFORMAT)colorFormat::RGBA32F);
   if (SUCCEEDED(hr))
      ShowError("D3D device does support D3DFMT_A32B32G32R32F SRGBTexture reads (which leads to wrong tex colors)");
   // now the same for our LDR/8bit texture format the other way round
   hr = m_pD3D->CheckDeviceFormat(m_adapter, devtype, params.BackBufferFormat, D3DUSAGE_QUERY_SRGBREAD, D3DRTYPE_TEXTURE, (D3DFORMAT)colorFormat::RGBA8);
   if (!SUCCEEDED(hr))
      ShowError("D3D device does not support D3DFMT_A8R8G8B8 SRGBTexture reads (which leads to wrong tex colors)");

   // check if auto generation of mipmaps can be used, otherwise will be done via d3dx
   m_autogen_mipmap = (caps.Caps2 & D3DCAPS2_CANAUTOGENMIPMAP) != 0;
   if (m_autogen_mipmap)
      m_autogen_mipmap = (m_pD3D->CheckDeviceFormat(m_adapter, devtype, params.BackBufferFormat, textureUsage::AUTOMIPMAP, D3DRTYPE_TEXTURE, (D3DFORMAT)colorFormat::RGBA8) == D3D_OK);

   //m_autogen_mipmap = false; //!! could be done to support correct sRGB/gamma correct generation of mipmaps which is not possible with auto gen mipmap in DX9! at the moment disabled, as the sRGB software path is super slow for similar mipmap filter quality

#ifndef DISABLE_FORCE_NVIDIA_OPTIMUS
   if (!NVAPIinit)
   {
      if (NvAPI_Initialize() == NVAPI_OK)
         NVAPIinit = true;
   }
#endif

   // Determine if INTZ is supported
#ifdef ENABLE_SDL
   m_INTZ_support = false;
#else
   m_INTZ_support = (m_pD3D->CheckDeviceFormat( m_adapter, devtype, params.BackBufferFormat,
                     D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, ((D3DFORMAT)(MAKEFOURCC('I','N','T','Z'))))) == D3D_OK;
#endif

   // check if requested MSAA is possible
   DWORD MultiSampleQualityLevels;
   if (!SUCCEEDED(m_pD3D->CheckDeviceMultiSampleType(m_adapter,
      devtype, params.BackBufferFormat,
      params.Windowed, params.MultiSampleType, &MultiSampleQualityLevels)))
   {
      ShowError("D3D device does not support this MultiSampleType");
      params.MultiSampleType = D3DMULTISAMPLE_NONE;
      params.MultiSampleQuality = 0;
   }
   else
      params.MultiSampleQuality = min(params.MultiSampleQuality, MultiSampleQualityLevels);

   const bool softwareVP = LoadValueBoolWithDefault("Player", "SoftwareVertexProcessing", false);
   const DWORD flags = softwareVP ? D3DCREATE_SOFTWARE_VERTEXPROCESSING : D3DCREATE_HARDWARE_VERTEXPROCESSING;

   // Create the D3D device. This optionally goes to the proper fullscreen mode.
   // It also creates the default swap chain (front and back buffer).
#ifdef USE_D3D9EX
   if (m_pD3DEx)
   {
      D3DDISPLAYMODEEX mode;
      mode.Size = sizeof(D3DDISPLAYMODEEX);
      if (m_fullscreen)
      {
         mode.Format = params.BackBufferFormat;
         mode.Width = params.BackBufferWidth;
         mode.Height = params.BackBufferHeight;
         mode.RefreshRate = params.FullScreen_RefreshRateInHz;
         mode.ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
      }

      hr = m_pD3DEx->CreateDeviceEx(
         m_adapter,
         devtype,
         m_windowHwnd,
         flags /*| D3DCREATE_PUREDEVICE*/,
         &params,
         m_fullscreen ? &mode : nullptr,
         &m_pD3DDeviceEx);
      if (FAILED(hr))
      {
         if (m_fullscreen)
         {
            const int result = GetSystemMetrics(SM_REMOTESESSION);
            const bool isRemoteSession = (result != 0);
            if (isRemoteSession)
               ShowError("Try disabling exclusive Fullscreen Mode for Remote Desktop Connections");
         }
         ReportFatalError(hr, __FILE__, __LINE__);
      }

      m_pD3DDeviceEx->QueryInterface(__uuidof(IDirect3DDevice9), reinterpret_cast<void**>(&m_pD3DDevice));

      // Get the display mode so that we can report back the actual refresh rate.
      CHECKD3D(m_pD3DDeviceEx->GetDisplayModeEx(0, &mode, nullptr));

      refreshrate = mode.RefreshRate;
   }
   else
#endif
   {
      hr = m_pD3D->CreateDevice(
         m_adapter,
         devtype,
         m_windowHwnd,
         flags /*| D3DCREATE_PUREDEVICE*/,
         &params,
         &m_pD3DDevice);

      if (FAILED(hr))
         ReportError("Fatal Error: unable to create D3D device!", hr, __FILE__, __LINE__);

      // Get the display mode so that we can report back the actual refresh rate.
      D3DDISPLAYMODE mode;
      hr = m_pD3DDevice->GetDisplayMode(m_adapter, &mode);
      if (FAILED(hr))
         ReportError("Fatal Error: unable to get supported video mode list!", hr, __FILE__, __LINE__);

      refreshrate = mode.RefreshRate;
   }

   /*if (m_fullscreen)
       hr = m_pD3DDevice->SetDialogBoxMode(TRUE);*/ // needs D3DPRESENTFLAG_LOCKABLE_BACKBUFFER, but makes rendering slower on some systems :/

   // Retrieve a reference to the back buffer.
   hr = m_pD3DDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &m_pBackBuffer);
   if (FAILED(hr))
      ReportError("Fatal Error: unable to create back buffer!", hr, __FILE__, __LINE__);

   const D3DFORMAT render_format = (D3DFORMAT)((m_BWrendering == 1) ? colorFormat::RG16F : ((m_BWrendering == 2) ? colorFormat::RED16F : colorFormat::RGBA16F));

   // alloc float buffer for rendering (optionally 2x2 res for manual super sampling)
   hr = m_pD3DDevice->CreateTexture(m_Buf_widthSS, m_Buf_heightSS, 1, D3DUSAGE_RENDERTARGET, render_format, (D3DPOOL)memoryPool::DEFAULT, &m_pOffscreenBackBufferTexture, nullptr); //!! colorFormat::RGBA32F?
   if (FAILED(hr))
      ReportError("Fatal Error: unable to create render buffer!", hr, __FILE__, __LINE__);

   // alloc buffer for screen space fake reflection rendering (optionally 2x2 res for manual super sampling)
   if (m_ssRefl)
   {
      hr = m_pD3DDevice->CreateTexture(m_Buf_widthSS, m_Buf_heightSS, 1, D3DUSAGE_RENDERTARGET, render_format, (D3DPOOL)memoryPool::DEFAULT, &m_pReflectionBufferTexture, nullptr); //!! D3DFMT_A32B32G32R32F?
      if (FAILED(hr))
         ReportError("Fatal Error: unable to create reflection buffer!", hr, __FILE__, __LINE__);
   }
   else
      m_pReflectionBufferTexture = nullptr;

   if (g_pplayer != nullptr)
   {
      const bool drawBallReflection = ((g_pplayer->m_reflectionForBalls && (g_pplayer->m_ptable->m_useReflectionForBalls == -1)) || (g_pplayer->m_ptable->m_useReflectionForBalls == 1));
      if ((g_pplayer->m_ptable->m_reflectElementsOnPlayfield /*&& g_pplayer->m_pf_refl*/) || drawBallReflection)
      {
         hr = m_pD3DDevice->CreateTexture(m_Buf_widthSS, m_Buf_heightSS, 1, D3DUSAGE_RENDERTARGET, render_format, (D3DPOOL)memoryPool::DEFAULT, &m_pMirrorTmpBufferTexture, nullptr); //!! colorFormat::RGBA32?
         if (FAILED(hr))
            ReportError("Fatal Error: unable to create reflection map!", hr, __FILE__, __LINE__);
      }
   }
   // alloc bloom tex at 1/3 x 1/3 res (allows for simple HQ downscale of clipped input while saving memory)
   hr = m_pD3DDevice->CreateTexture(m_Buf_widthBlur, m_Buf_heightBlur, 1, D3DUSAGE_RENDERTARGET, render_format, (D3DPOOL)memoryPool::DEFAULT, &m_pBloomBufferTexture, nullptr); //!! 8bit enough?
   if (FAILED(hr))
      ReportError("Fatal Error: unable to create bloom buffer!", hr, __FILE__, __LINE__);

   // temporary buffer for gaussian blur
   hr = m_pD3DDevice->CreateTexture(m_Buf_widthBlur, m_Buf_heightBlur, 1, D3DUSAGE_RENDERTARGET, render_format, (D3DPOOL)memoryPool::DEFAULT, &m_pBloomTmpBufferTexture, nullptr); //!! 8bit are enough! //!! but used also for bulb light transmission hack now!
   if (FAILED(hr))
      ReportError("Fatal Error: unable to create blur buffer!", hr, __FILE__, __LINE__);

   // alloc temporary buffer for stereo3D/post-processing AA/sharpen
   if ((m_stereo3D > 0) || (m_FXAA > 0) || m_sharpen)
   {
      hr = m_pD3DDevice->CreateTexture(m_Buf_width, m_Buf_height, 1, D3DUSAGE_RENDERTARGET, (D3DFORMAT)(video10bit ? colorFormat::RGBA10 : colorFormat::RGBA), (D3DPOOL)memoryPool::DEFAULT, &m_pOffscreenBackBufferStereoTexture, nullptr);
      if (FAILED(hr))
         ReportError("Fatal Error: unable to create stereo3D/post-processing AA/sharpen buffer!", hr, __FILE__, __LINE__);
   }
   else
      m_pOffscreenBackBufferStereoTexture = nullptr;

   // alloc one more temporary buffer for SMAA
   if (m_FXAA == Quality_SMAA)
   {
      hr = m_pD3DDevice->CreateTexture(m_Buf_width, m_Buf_height, 1, D3DUSAGE_RENDERTARGET, (D3DFORMAT)(video10bit ? colorFormat::RGBA10 : colorFormat::RGBA), (D3DPOOL)memoryPool::DEFAULT, &m_pOffscreenBackBufferPPTexture1, nullptr);
      if (FAILED(hr))
         ReportError("Fatal Error: unable to create SMAA buffer!", hr, __FILE__, __LINE__);
   }
   else
      m_pOffscreenBackBufferPPTexture1 = nullptr;

   if (video10bit && (m_FXAA == Quality_SMAA || m_FXAA == Standard_DLAA))
      ShowError("SMAA or DLAA post-processing AA should not be combined with 10Bit-output rendering (will result in visible artifacts)!");

   //

   // create default vertex declarations for shaders
   CreateVertexDeclaration(VertexTexelElement, &m_pVertexTexelDeclaration);
   CreateVertexDeclaration(VertexNormalTexelElement, &m_pVertexNormalTexelDeclaration);
   //CreateVertexDeclaration( VertexNormalTexelTexelElement, &m_pVertexNormalTexelTexelDeclaration );
   CreateVertexDeclaration(VertexTrafoTexelElement, &m_pVertexTrafoTexelDeclaration);

   if (m_quadVertexBuffer == nullptr) {
      VertexBuffer::CreateVertexBuffer(4, 0, MY_D3DFVF_TEX, &m_quadVertexBuffer);
      Vertex3D_TexelOnly* bufvb;
      m_quadVertexBuffer->lock(0, 0, (void**)&bufvb, VertexBuffer::WRITEONLY);
      static const float verts[4 * 5] =
      {
         1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
         0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
         1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
         0.0f, 0.0f, 0.0f, 0.0f, 0.0f
      };
      memcpy(bufvb, verts, 4 * sizeof(Vertex3D_TexelOnly));
      m_quadVertexBuffer->unlock();
   }
}

bool RenderDevice::LoadShaders()
{
   bool shaderCompilationOkay = true;

   basicShader = new Shader(this);
   shaderCompilationOkay = basicShader->Load(g_basicShaderCode, sizeof(g_basicShaderCode)) && shaderCompilationOkay;

   ballShader = new Shader(this);
   ballShader->Load(g_ballShaderCode, sizeof(g_ballShaderCode));

   DMDShader = new Shader(this);
   shaderCompilationOkay = DMDShader->Load(g_dmdShaderCode, sizeof(g_dmdShaderCode)) && shaderCompilationOkay;

   FBShader = new Shader(this);
   shaderCompilationOkay = FBShader->Load(g_FBShaderCode, sizeof(g_FBShaderCode)) && shaderCompilationOkay;

   if (m_stereo3D != STEREO_OFF) {
      StereoShader = new Shader(this);
      shaderCompilationOkay = StereoShader->Load(g_StereoShaderCode, sizeof(g_StereoShaderCode)) && shaderCompilationOkay;
   }
   else
      StereoShader = nullptr;

   flasherShader = new Shader(this);
   shaderCompilationOkay = flasherShader->Load(g_flasherShaderCode, sizeof(g_flasherShaderCode)) && shaderCompilationOkay;

   lightShader = new Shader(this);
   shaderCompilationOkay = lightShader->Load(g_lightShaderCode, sizeof(g_lightShaderCode)) && shaderCompilationOkay;

#ifdef SEPARATE_CLASSICLIGHTSHADER
   classicLightShader = new Shader(this);
   shaderCompilationOkay = classicLightShader->Load(g_classicLightShaderCode, sizeof(g_classicLightShaderCode)) && shaderCompilationOkay;
#endif

   if (!shaderCompilationOkay)
   {
      ReportError("Fatal Error: shader compilation failed!", -1, __FILE__, __LINE__);
      return false;
   }

   if (shaderCompilationOkay && m_FXAA == Quality_SMAA)
      UploadAndSetSMAATextures();
   else
   {
      m_SMAAareaTexture = nullptr;
      m_SMAAsearchTexture = nullptr;
   }

   return true;
}

RenderDevice::~RenderDevice()
{
   if (m_quadVertexBuffer)
      m_quadVertexBuffer->release();
   m_quadVertexBuffer = nullptr;

   //m_quadDynVertexBuffer->release();

#ifndef DISABLE_FORCE_NVIDIA_OPTIMUS
   if (srcr_cache != nullptr)
      CHECKNVAPI(NvAPI_D3D9_UnregisterResource(srcr_cache)); //!! meh
   srcr_cache = nullptr;
   if (srct_cache != nullptr)
      CHECKNVAPI(NvAPI_D3D9_UnregisterResource(srct_cache)); //!! meh
   srct_cache = nullptr;
   if (dest_cache != nullptr)
      CHECKNVAPI(NvAPI_D3D9_UnregisterResource(dest_cache)); //!! meh
   dest_cache = nullptr;
   if (NVAPIinit) //!! meh
      CHECKNVAPI(NvAPI_Unload());
   NVAPIinit = false;
#endif

   //
   m_pD3DDevice->SetStreamSource(0, nullptr, 0, 0);
   m_pD3DDevice->SetIndices(nullptr);
   m_pD3DDevice->SetVertexShader(nullptr);
   m_pD3DDevice->SetPixelShader(nullptr);
   m_pD3DDevice->SetFVF(D3DFVF_XYZ);
   //m_pD3DDevice->SetVertexDeclaration(nullptr); // invalid call
   //m_pD3DDevice->SetRenderTarget(0, nullptr); // invalid call
   m_pD3DDevice->SetDepthStencilSurface(nullptr);

   FreeShader();

   SAFE_RELEASE(m_pVertexTexelDeclaration);
   SAFE_RELEASE(m_pVertexNormalTexelDeclaration);
   //SAFE_RELEASE(m_pVertexNormalTexelTexelDeclaration);
   SAFE_RELEASE(m_pVertexTrafoTexelDeclaration);

   m_texMan.UnloadAll();
   SAFE_RELEASE(m_pOffscreenBackBufferTexture);
   SAFE_RELEASE(m_pOffscreenBackBufferStereoTexture);
   SAFE_RELEASE(m_pOffscreenBackBufferPPTexture1);
   SAFE_RELEASE(m_pReflectionBufferTexture);

   if (g_pplayer)
   {
      const bool drawBallReflection = ((g_pplayer->m_reflectionForBalls && (g_pplayer->m_ptable->m_useReflectionForBalls == -1)) || (g_pplayer->m_ptable->m_useReflectionForBalls == 1));
      if ((g_pplayer->m_ptable->m_reflectElementsOnPlayfield /*&& g_pplayer->m_pf_refl*/) || drawBallReflection)
         SAFE_RELEASE(m_pMirrorTmpBufferTexture);
   }
   SAFE_RELEASE(m_pBloomBufferTexture);
   SAFE_RELEASE(m_pBloomTmpBufferTexture);
   SAFE_RELEASE(m_pBackBuffer);

   SAFE_RELEASE(m_SMAAareaTexture);
   SAFE_RELEASE(m_SMAAsearchTexture);

#ifdef _DEBUG
   CheckForD3DLeak(m_pD3DDevice);
#endif

#ifdef USE_D3D9EX
   //!! if (m_pD3DDeviceEx == m_pD3DDevice) m_pD3DDevice = nullptr; //!! needed for Caligula if m_adapter > 0 ?? weird!! BUT MESSES UP FULLSCREEN EXIT (=hangs)
   SAFE_RELEASE_NO_RCC(m_pD3DDeviceEx);
#endif
#ifdef DEBUG_REFCOUNT_TRIGGER
   SAFE_RELEASE(m_pD3DDevice);
#else
   FORCE_RELEASE(m_pD3DDevice); //!! why is this necessary for some setups? is the refcount still off for some settings?
#endif
#ifdef USE_D3D9EX
   SAFE_RELEASE_NO_RCC(m_pD3DEx);
#endif
#ifdef DEBUG_REFCOUNT_TRIGGER
   SAFE_RELEASE(m_pD3D);
#else
   FORCE_RELEASE(m_pD3D); //!! why is this necessary for some setups? is the refcount still off for some settings?
#endif

   /*
    * D3D sets the FPU to single precision/round to nearest int mode when it's initialized,
    * but doesn't bother to reset the FPU when it's destroyed. We reset it manually here.
    */
   _fpreset();

   if (m_dwm_was_enabled)
      mDwmEnableComposition(DWM_EC_ENABLECOMPOSITION);
}
#endif

bool RenderDevice::DepthBufferReadBackAvailable() const
{
#ifdef ENABLE_SDL
   return true;
#else
   if (m_INTZ_support && !m_useNvidiaApi)
      return true;
   // fall back to NVIDIAs NVAPI, only handle DepthBuffer ReadBack if API was initialized
   return NVAPIinit;
#endif
}

void RenderDevice::FreeShader()
{
   if (basicShader)
   {
      basicShader->SetTextureNull(SHADER_Texture0);
      basicShader->SetTextureNull(SHADER_Texture1);
      basicShader->SetTextureNull(SHADER_Texture2);
      basicShader->SetTextureNull(SHADER_Texture3);
      basicShader->SetTextureNull(SHADER_Texture4);
      delete basicShader;
      basicShader = 0;
   }
   if (ballShader)
   {
      ballShader->SetTextureNull(SHADER_Texture0);
      ballShader->SetTextureNull(SHADER_Texture1);
      ballShader->SetTextureNull(SHADER_Texture2);
      ballShader->SetTextureNull(SHADER_Texture3);
      delete ballShader;
      ballShader = 0;
   }
   if (DMDShader)
   {
      DMDShader->SetTextureNull(SHADER_Texture0);
      delete DMDShader;
      DMDShader = 0;
   }
   if (FBShader)
   {
      FBShader->SetTextureNull(SHADER_Texture0);
      FBShader->SetTextureNull(SHADER_Texture1);
      FBShader->SetTextureNull(SHADER_Texture3);
      FBShader->SetTextureNull(SHADER_Texture4);

      FBShader->SetTextureNull(SHADER_areaTex2D);
      FBShader->SetTextureNull(SHADER_searchTex2D);

      delete FBShader;
      FBShader = 0;
   }
   if (StereoShader)
   {
      StereoShader->SetTextureNull(SHADER_Texture0);
      StereoShader->SetTextureNull(SHADER_Texture1);

      delete StereoShader;
      StereoShader = 0;
   }
   if (flasherShader)
   {
      flasherShader->SetTextureNull(SHADER_Texture0);
      flasherShader->SetTextureNull(SHADER_Texture1);
      delete flasherShader;
      flasherShader = 0;
   }
   if (lightShader)
   {
      delete lightShader;
      lightShader = 0;
   }
#ifdef SEPARATE_CLASSICLIGHTSHADER
   if (classicLightShader)
   {
      classicLightShader->SetTextureNull(SHADER_Texture0);
      classicLightShader->SetTextureNull(SHADER_Texture1);
      classicLightShader->SetTextureNull(SHADER_Texture2);
      delete classicLightShader;
      classicLightShader = 0;
   }
#endif
}

void RenderDevice::BeginScene()
{
#ifndef ENABLE_SDL
   CHECKD3D(m_pD3DDevice->BeginScene());
#endif
}

void RenderDevice::EndScene()
{
#ifndef ENABLE_SDL
   CHECKD3D(m_pD3DDevice->EndScene());
#endif
}

/*static void FlushGPUCommandBuffer(IDirect3DDevice9* pd3dDevice)
{
   IDirect3DQuery9* pEventQuery;
   pd3dDevice->CreateQuery(D3DQUERYTYPE_EVENT, &pEventQuery);

   if (pEventQuery)
   {
      pEventQuery->Issue(D3DISSUE_END);
      while (S_FALSE == pEventQuery->GetData(nullptr, 0, D3DGETDATA_FLUSH))
         ;
      SAFE_RELEASE(pEventQuery);
   }
}*/

bool RenderDevice::SetMaximumPreRenderedFrames(const DWORD frames)
{
#ifdef USE_D3D9EX
   if (m_pD3DEx && frames > 0 && frames <= 20) // frames can range from 1 to 20, 0 resets to default DX
   {
      CHECKD3D(m_pD3DDeviceEx->SetMaximumFrameLatency(frames));
      return true;
   }
   else
#endif
      return false;
}

void RenderDevice::Flip(const bool vsync)
{
#ifdef ENABLE_SDL
   SDL_GL_SwapWindow(m_sdl_playfieldHwnd);
#ifdef ENABLE_VR
   //glFlush();
   //glFinish();
#endif

#else

   bool dwm = false;
   if (vsync) // xp does neither have d3dex nor dwm, so vsync will always be specified during device set
      dwm = m_dwm_enabled;

#ifdef USE_D3D9EX
   if (m_pD3DEx && vsync && !dwm)
   {
      m_pD3DDeviceEx->WaitForVBlank(0); //!! does not seem to work on win8?? -> may depend on desktop compositing and the like
      /*D3DRASTER_STATUS r;
      CHECKD3D(m_pD3DDevice->GetRasterStatus(0, &r)); // usually not supported, also only for pure devices?!

      while (!r.InVBlank)
      {
      uSleep(10);
      m_pD3DDevice->GetRasterStatus(0, &r);
      }*/
   }
#endif

   CHECKD3D(m_pD3DDevice->Present(nullptr, nullptr, nullptr, nullptr)); //!! could use D3DPRESENT_DONOTWAIT and do some physics work meanwhile??

   if (mDwmFlush && vsync && dwm)
      mDwmFlush(); //!! also above present?? (internet sources are not clear about order)
#endif
   // reset performance counters
   m_frameDrawCalls = m_curDrawCalls;
   m_frameStateChanges = m_curStateChanges;
   m_frameTextureChanges = m_curTextureChanges;
   m_frameParameterChanges = m_curParameterChanges;
   m_frameTechniqueChanges = m_curTechniqueChanges;
   m_curDrawCalls = m_curStateChanges = m_curTextureChanges = m_curParameterChanges = m_curTechniqueChanges = 0;
   m_frameTextureUpdates = m_curTextureUpdates;
   m_curTextureUpdates = 0;

   //m_frameLockCalls = m_curLockCalls;
   //m_curLockCalls = 0;
}

RenderTarget* RenderDevice::DuplicateRenderTarget(RenderTarget* src)
{
   RenderTarget *dup;
#ifdef ENABLE_SDL
   //TODO - Function seems to be unused
   return nullptr;
#else
   D3DSURFACE_DESC desc;
   src->GetDesc(&desc);
   CHECKD3D(m_pD3DDevice->CreateRenderTarget(desc.Width, desc.Height, desc.Format,
      desc.MultiSampleType, desc.MultiSampleQuality, FALSE /* lockable */, &dup, nullptr));
#endif
   return dup;
}

void RenderDevice::CopySurface(RenderTarget* dest, RenderTarget* src)
{
#ifdef ENABLE_SDL
   //TODO - Function seems to be unused for SDL
   return;
#else
   CHECKD3D(m_pD3DDevice->StretchRect(src, nullptr, dest, nullptr, D3DTEXF_NONE));
#endif
}

D3DTexture* RenderDevice::DuplicateTexture(RenderTarget* src)
{
#ifdef ENABLE_SDL
   //TODO - Function seems to be unused
   return nullptr;
#else
   D3DSURFACE_DESC desc;
   src->GetDesc(&desc);
   D3DTexture* dup;
   CHECKD3D(m_pD3DDevice->CreateTexture(desc.Width, desc.Height, 1,
      D3DUSAGE_RENDERTARGET, desc.Format, (D3DPOOL)memoryPool::DEFAULT, &dup, nullptr)); // D3DUSAGE_AUTOGENMIPMAP?
   return dup;
#endif
}

D3DTexture* RenderDevice::DuplicateTextureSingleChannel(RenderTarget* src)
{
#ifdef ENABLE_SDL
   //TODO - Function seems to be unused
   return nullptr;
#else
   D3DSURFACE_DESC desc;
   src->GetDesc(&desc);
   desc.Format = D3DFMT_L8;
   D3DTexture* dup;
   CHECKD3D(m_pD3DDevice->CreateTexture(desc.Width, desc.Height, 1,
      D3DUSAGE_RENDERTARGET, desc.Format, (D3DPOOL)memoryPool::DEFAULT, &dup, nullptr)); // D3DUSAGE_AUTOGENMIPMAP?
   return dup;
#endif
}

D3DTexture* RenderDevice::DuplicateDepthTexture(RenderTarget* src)
{
#ifdef ENABLE_SDL
   //TODO - Function seems to be unused
   return nullptr;
#else
   D3DSURFACE_DESC desc;
   src->GetDesc(&desc);
   D3DTexture* dup;
   CHECKD3D(m_pD3DDevice->CreateTexture(desc.Width, desc.Height, 1,
      D3DUSAGE_DEPTHSTENCIL, (D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z'), (D3DPOOL)memoryPool::DEFAULT, &dup, nullptr)); // D3DUSAGE_AUTOGENMIPMAP?
   return dup;
#endif
}

#ifdef ENABLE_SDL

void RenderDevice::CopyDepth(RenderTarget* dest, RenderTarget* src) {
   //Not required for GL.
}

D3DTexture* RenderDevice::UploadTexture(BaseTexture* surf, int *pTexWidth, int *pTexHeight, const bool clamptoedge)
{
   colorFormat format;
   if (surf->m_format == BaseTexture::SRGBA)
       format = colorFormat::SRGBA;
   else if (surf->m_format == BaseTexture::RGBA)
       format = colorFormat::RGBA;
   else if (surf->m_format == BaseTexture::SRGB)
       format = colorFormat::SRGB;
   else if (surf->m_format == BaseTexture::RGB)
       format = colorFormat::RGB;
   else if (surf->m_format == BaseTexture::RGB_FP16)
       format = colorFormat::RGB16F;
   else if (surf->m_format == BaseTexture::RGB_FP32)
       format = colorFormat::RGB32F;
   D3DTexture *tex = CreateTexture(surf->width(), surf->height(), 0, STATIC, format, surf->data(), 0, clamptoedge);
   if (pTexWidth) *pTexWidth = surf->width();
   if (pTexHeight) *pTexHeight = surf->height();
   return tex;
}

void RenderDevice::UploadAndSetSMAATextures()
{
   m_SMAAsearchTexture = CreateTexture(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 0, STATIC, GREY, (void*)&searchTexBytes[0], 0);
   m_SMAAareaTexture = CreateTexture(AREATEX_WIDTH, AREATEX_HEIGHT, 0, STATIC, GREY_ALPHA, (void*)&areaTexBytes[0], 0);

   FBShader->SetTexture(SHADER_areaTex2D, m_SMAAareaTexture);
   FBShader->SetTexture(SHADER_searchTex2D, m_SMAAsearchTexture);
}

#else

void RenderDevice::CopySurface(D3DTexture* dest, RenderTarget* src)
{
   IDirect3DSurface9 *textureSurface;
   CHECKD3D(dest->GetSurfaceLevel(0, &textureSurface));
   CHECKD3D(m_pD3DDevice->StretchRect(src, nullptr, textureSurface, nullptr, D3DTEXF_NONE));
   SAFE_RELEASE_NO_RCC(textureSurface);
}

void RenderDevice::CopySurface(RenderTarget* dest, D3DTexture* src)
{
   IDirect3DSurface9 *textureSurface;
   CHECKD3D(src->GetSurfaceLevel(0, &textureSurface));
   CHECKD3D(m_pD3DDevice->StretchRect(textureSurface, nullptr, dest, nullptr, D3DTEXF_NONE));
   SAFE_RELEASE_NO_RCC(textureSurface);
}

void RenderDevice::CopySurface(void* dest, void* src)
{
   if (!m_useNvidiaApi && m_INTZ_support)
      CopySurface((D3DTexture*)dest, (D3DTexture*)src);
   else
      CopySurface((RenderTarget*)dest, (RenderTarget*)src);
}

void RenderDevice::CopySurface(D3DTexture* dest, D3DTexture* src)
{
   IDirect3DSurface9 *destTextureSurface;
   CHECKD3D(dest->GetSurfaceLevel(0, &destTextureSurface));
   IDirect3DSurface9 *srcTextureSurface;
   CHECKD3D(src->GetSurfaceLevel(0, &srcTextureSurface));
   const HRESULT hr = m_pD3DDevice->StretchRect(srcTextureSurface, nullptr, destTextureSurface, nullptr, D3DTEXF_NONE);
   if (FAILED(hr))
   {
      ShowError("Unable to access texture surface!\r\nTry to set \"Alternative Depth Buffer processing\" in the video options!\r\nOr disable Ambient Occlusion and/or 3D stereo!");
   }
   SAFE_RELEASE_NO_RCC(destTextureSurface);
   SAFE_RELEASE_NO_RCC(srcTextureSurface);
}

void RenderDevice::CopyDepth(D3DTexture* dest, RenderTarget* src)
{
#ifndef DISABLE_FORCE_NVIDIA_OPTIMUS
   if (NVAPIinit)
   {
      if (src != srcr_cache)
      {
         if (srcr_cache != nullptr)
            CHECKNVAPI(NvAPI_D3D9_UnregisterResource(srcr_cache)); //!! meh
         CHECKNVAPI(NvAPI_D3D9_RegisterResource(src)); //!! meh
         srcr_cache = src;
      }
      if (dest != dest_cache)
      {
         if (dest_cache != nullptr)
            CHECKNVAPI(NvAPI_D3D9_UnregisterResource(dest_cache)); //!! meh
         CHECKNVAPI(NvAPI_D3D9_RegisterResource(dest)); //!! meh
         dest_cache = dest;
      }

      //CHECKNVAPI(NvAPI_D3D9_AliasSurfaceAsTexture(m_pD3DDevice,src,dest,0));
      CHECKNVAPI(NvAPI_D3D9_StretchRectEx(m_pD3DDevice, src, nullptr, dest, nullptr, D3DTEXF_NONE));
   }
#endif
#if 0 // leftover resolve z code, maybe useful later-on
   else //if (m_RESZ_support)
   {
#define RESZ_CODE 0x7FA05000
      IDirect3DSurface9 *pDSTSurface;
      m_pD3DDevice->GetDepthStencilSurface(&pDSTSurface);
      IDirect3DSurface9 *pINTZDSTSurface;
      dest->GetSurfaceLevel(0, &pINTZDSTSurface);
      // Bind depth buffer
      m_pD3DDevice->SetDepthStencilSurface(pINTZDSTSurface);

      m_pD3DDevice->BeginScene();

      m_pD3DDevice->SetVertexShader(nullptr);
      m_pD3DDevice->SetPixelShader(nullptr);
      m_pD3DDevice->SetFVF(D3DFVF_XYZ);

      // Bind depth stencil texture to texture sampler 0
      m_pD3DDevice->SetTexture(0, dest);

      // Perform a dummy draw call to ensure texture sampler 0 is set before the resolve is triggered
      // Vertex declaration and shaders may need to me adjusted to ensure no debug
      // error message is produced
      m_pD3DDevice->SetRenderState(RenderDevice:ZENABLE, RenderDevice::RS_FALSE);
      m_pD3DDevice->SetRenderState(RenderDevice:ZWRITEENABLE, RenderDevice::RS_FALSE);
      m_pD3DDevice->SetRenderState(RenderDevice::COLORWRITEENABLE, 0);
      vec3 vDummyPoint(0.0f, 0.0f, 0.0f);
      m_pD3DDevice->DrawPrimitiveUP(RenderDevice::POINTLIST, 1, vDummyPoint, sizeof(vec3));
      m_pD3DDevice->SetRenderState(RenderDevice:ZWRITEENABLE, RenderDevice::RS_TRUE);
      m_pD3DDevice->SetRenderState(RenderDevice:ZENABLE, RenderDevice::RS_TRUE);
      m_pD3DDevice->SetRenderState(RenderDevice::COLORWRITEENABLE, 0x0F);

      // Trigger the depth buffer resolve; after this call texture sampler 0
      // will contain the contents of the resolve operation
      m_pD3DDevice->SetRenderState(D3DRS_POINTSIZE, RESZ_CODE);

      // This hack to fix resz hack, has been found by Maksym Bezus!!!
      // Without this line resz will be resolved only for first frame
      m_pD3DDevice->SetRenderState(D3DRS_POINTSIZE, 0); // TROLOLO!!!

      m_pD3DDevice->EndScene();

      m_pD3DDevice->SetDepthStencilSurface(pDSTSurface);
      SAFE_RELEASE_NO_RCC(pINTZDSTSurface);
      SAFE_RELEASE(pDSTSurface);
   }
#endif
}

void RenderDevice::CopyDepth(D3DTexture* dest, D3DTexture* src)
{
   if (!m_useNvidiaApi)
      CopySurface(dest, src); // if INTZ used as texture format this (usually) works, although not really specified somewhere
#ifndef DISABLE_FORCE_NVIDIA_OPTIMUS
   else if (NVAPIinit)
   {
      if (src != srct_cache)
      {
         if (srct_cache != nullptr)
            CHECKNVAPI(NvAPI_D3D9_UnregisterResource(srct_cache)); //!! meh
         CHECKNVAPI(NvAPI_D3D9_RegisterResource(src)); //!! meh
         srct_cache = src;
      }
      if (dest != dest_cache)
      {
         if (dest_cache != nullptr)
            CHECKNVAPI(NvAPI_D3D9_UnregisterResource(dest_cache)); //!! meh
         CHECKNVAPI(NvAPI_D3D9_RegisterResource(dest)); //!! meh
         dest_cache = dest;
      }

      //CHECKNVAPI(NvAPI_D3D9_AliasSurfaceAsTexture(m_pD3DDevice,src,dest,0));
      CHECKNVAPI(NvAPI_D3D9_StretchRectEx(m_pD3DDevice, src, nullptr, dest, nullptr, D3DTEXF_NONE));
   }
#endif
#if 0 // leftover manual pixel shader texture copy
   BeginScene(); //!!

   IDirect3DSurface9 *oldRT;
   CHECKD3D(m_pD3DDevice->GetRenderTarget(0, &oldRT));

   SetRenderTarget(dest);

   FBShader->SetTexture(SHADER_Texture0, src);
   FBShader->SetFloat(SHADER_mirrorFactor, 1.f); //!! use separate pass-through shader instead??
   FBShader->SetTechnique(SHADER_TECHNIQUE_fb_mirror);

   SetRenderState(RenderDevice::ALPHABLENDENABLE, FALSE); // paranoia set //!!
   SetRenderStateCulling(RenderDevice::CULL_NONE);
   SetRenderState(RenderDevice::ZWRITEENABLE, RenderDevice::RS_FALSE);
   SetRenderState(RenderDevice::ZENABLE, FALSE);

   FBShader->Begin(0);
   DrawFullscreenQuad();
   FBShader->End();

   SetRenderTarget(oldRT);
   SAFE_RELEASE_NO_RCC(oldRT);

   EndScene(); //!!
#endif
}

void RenderDevice::CopyDepth(D3DTexture* dest, void* src)
{
   if (!m_useNvidiaApi && m_INTZ_support)
      CopyDepth(dest, (D3DTexture*)src);
   else
      CopyDepth(dest, (RenderTarget*)src);
}

D3DTexture* RenderDevice::CreateSystemTexture(BaseTexture* const surf) {
   return CreateSystemTexture(surf->width(),
                              surf->height(), 
                              (D3DFORMAT)((m_compress_textures && ((surf->width() & 3) == 0) && ((surf->height() & 3) == 0) && (surf->width() > 256) && (surf->height() > 256) && (surf->m_format != BaseTexture::RGB_FP)) ? 
                                         colorFormat::DXT5 : 
                                         ((surf->m_format == BaseTexture::RGB_FP) ? 
                                                colorFormat::RGBA32F : 
                                                colorFormat::RGBA)),
                              surf->data(),
                              surf->pitch());
}

D3DTexture* RenderDevice::CreateSystemTexture(const int texwidth, const int texheight, const D3DFORMAT texformat, const void* data, const int pitch)
{
   IDirect3DTexture9 *sysTex;
   HRESULT hr;
   hr = m_pD3DDevice->CreateTexture(texwidth, texheight, (texformat != colorFormat::DXT5 && m_autogen_mipmap) ? 1 : 0, 0, (D3DFORMAT)texformat, (D3DPOOL)memoryPool::SYSTEM, &sysTex, nullptr);
   if (FAILED(hr))
   {
      ReportError("Fatal Error: unable to create texture!", hr, __FILE__, __LINE__);
   }

   // copy data into system memory texture
   if (texformat == colorFormat::RGBA32F)
   {
      D3DLOCKED_RECT locked;
      CHECKD3D(sysTex->LockRect(0, &locked, nullptr, 0));

      // old RGBA copy code, just for reference:
      //BYTE *pdest = (BYTE*)locked.pBits;
      //for (int y = 0; y < texheight; ++y)
      //   memcpy(pdest + y*locked.Pitch, surf->data() + y*surf->pitch(), 4 * texwidth);

      float * const __restrict pdest = (float*)locked.pBits;
      const float * const __restrict psrc = (float*)(surf->data());
      for (int i = 0; i < texwidth*texheight; ++i)
      {
         pdest[i * 4    ] = psrc[i * 3    ];
         pdest[i * 4 + 1] = psrc[i * 3 + 1];
         pdest[i * 4 + 2] = psrc[i * 3 + 2];
         pdest[i * 4 + 3] = 1.f;
      }

      CHECKD3D(sysTex->UnlockRect(0));
   }
   else
   {
      IDirect3DSurface9* sysSurf;
      CHECKD3D(sysTex->GetSurfaceLevel(0, &sysSurf));
      RECT sysRect;
      sysRect.top = 0;
      sysRect.left = 0;
      sysRect.right = texwidth;
      sysRect.bottom = texheight;
      CHECKD3D(D3DXLoadSurfaceFromMemory(sysSurf, nullptr, nullptr, surf->data(), (D3DFORMAT)colorFormat::RGBA, surf->pitch(), nullptr, &sysRect, D3DX_FILTER_NONE, 0));
      SAFE_RELEASE_NO_RCC(sysSurf);
   }

   if (!(texformat != colorFormat::DXT5 && m_autogen_mipmap))
      CHECKD3D(D3DXFilterTexture(sysTex, nullptr, D3DX_DEFAULT, D3DX_DEFAULT)); //!! D3DX_FILTER_SRGB
      // normal maps or float textures are already in linear space!
      //CHECKD3D(D3DXFilterTexture(sysTex, nullptr, D3DX_DEFAULT, (texformat == D3DFMT_A32B32G32R32F || linearRGB) ? D3DX_DEFAULT : (D3DX_FILTER_TRIANGLE | ((isPowerOf2(texwidth) && isPowerOf2(texheight)) ? 0 : D3DX_FILTER_DITHER) | D3DX_FILTER_SRGB))); // DX9 doc says default equals box filter (and dither for non power of 2 tex size), but actually it seems to be triangle!

   return sysTex;
}

D3DTexture* RenderDevice::UploadTexture(BaseTexture* const surf, int* const pTexWidth, int* const pTexHeight, const bool clamptoedge)
{
   const unsigned int texwidth = surf->width();
   const unsigned int texheight = surf->height();

   if (pTexWidth) *pTexWidth = texwidth;
   if (pTexHeight) *pTexHeight = texheight;

   const BaseTexture::Format basetexformat = surf->m_format;

   D3DTexture *sysTex = CreateSystemTexture(surf);

   const colorFormat texformat = (m_compress_textures && ((texwidth & 3) == 0) && ((texheight & 3) == 0) && (texwidth > 256) && (texheight > 256) && (basetexformat != BaseTexture::RGB_FP)) ? colorFormat::DXT5 : ((basetexformat == BaseTexture::RGB_FP) ? colorFormat::RGBA32F : colorFormat::RGBA);

   D3DTexture *tex;
   HRESULT hr = m_pD3DDevice->CreateTexture(texwidth, texheight, (texformat != colorFormat::DXT5 && m_autogen_mipmap) ? 0 : sysTex->GetLevelCount(), (texformat != colorFormat::DXT5 && m_autogen_mipmap) ? D3DUSAGE_AUTOGENMIPMAP : 0, (D3DFORMAT)texformat, (D3DPOOL)memoryPool::DEFAULT, &tex, nullptr, clamptoedge);
   if (FAILED(hr))
      ReportError("Fatal Error: out of VRAM!", hr, __FILE__, __LINE__);

   m_curTextureUpdates++;
   hr = m_pD3DDevice->UpdateTexture(sysTex, tex);
   if (FAILED(hr))
      ReportError("Fatal Error: uploading texture failed!", hr, __FILE__, __LINE__);

   SAFE_RELEASE(sysTex);

   if (texformat != colorFormat::DXT5 && m_autogen_mipmap)
      tex->GenerateMipSubLevels(); // tell driver that now is a good time to generate mipmaps

   return tex;
}

void RenderDevice::UploadAndSetSMAATextures()
{
   {
   IDirect3DTexture9 *sysTex;
   HRESULT hr = m_pD3DDevice->CreateTexture(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 0, 0, (D3DFORMAT)colorFormat::GREY8, (D3DPOOL)memoryPool::SYSTEM, &sysTex, nullptr);
   if (FAILED(hr))
      ReportError("Fatal Error: unable to create texture!", hr, __FILE__, __LINE__);
   hr = m_pD3DDevice->CreateTexture(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 0, 0, (D3DFORMAT)colorFormat::GREY8, (D3DPOOL)memoryPool::DEFAULT, &m_SMAAsearchTexture, nullptr);
   if (FAILED(hr))
      ReportError("Fatal Error: out of VRAM!", hr, __FILE__, __LINE__);

   //!! use D3DXLoadSurfaceFromMemory
   D3DLOCKED_RECT locked;
   CHECKD3D(sysTex->LockRect(0, &locked, nullptr, 0));
   void * const pdest = locked.pBits;
   const void * const psrc = searchTexBytes;
   memcpy(pdest,psrc,SEARCHTEX_SIZE);
   CHECKD3D(sysTex->UnlockRect(0));

   CHECKD3D(m_pD3DDevice->UpdateTexture(sysTex, m_SMAAsearchTexture));
   SAFE_RELEASE(sysTex);
   }
   //
   {
   IDirect3DTexture9 *sysTex;
   HRESULT hr = m_pD3DDevice->CreateTexture(AREATEX_WIDTH, AREATEX_HEIGHT, 0, 0, (D3DFORMAT)colorFormat::GREYA8, (D3DPOOL)memoryPool::SYSTEM, &sysTex, nullptr);
   if (FAILED(hr))
      ReportError("Fatal Error: unable to create texture!", hr, __FILE__, __LINE__);
   hr = m_pD3DDevice->CreateTexture(AREATEX_WIDTH, AREATEX_HEIGHT, 0, 0, (D3DFORMAT)colorFormat::GREYA8, (D3DPOOL)memoryPool::DEFAULT, &m_SMAAareaTexture, nullptr);
   if (FAILED(hr))
      ReportError("Fatal Error: out of VRAM!", hr, __FILE__, __LINE__);

   //!! use D3DXLoadSurfaceFromMemory
   D3DLOCKED_RECT locked;
   CHECKD3D(sysTex->LockRect(0, &locked, nullptr, 0));
   void * const pdest = locked.pBits;
   const void * const psrc = areaTexBytes;
   memcpy(pdest,psrc,AREATEX_SIZE);
   CHECKD3D(sysTex->UnlockRect(0));

   CHECKD3D(m_pD3DDevice->UpdateTexture(sysTex, m_SMAAareaTexture));
   SAFE_RELEASE(sysTex);
   }

   //

   FBShader->SetTexture(SHADER_areaTex2D, m_SMAAareaTexture, true);
   FBShader->SetTexture(SHADER_searchTex2D, m_SMAAsearchTexture, true);
}
#endif

void RenderDevice::UpdateTexture(D3DTexture* const tex, BaseTexture* const surf)
{
#ifdef ENABLE_SDL
   if (surf->m_format == BaseTexture::RGBA)
      tex->format = colorFormat::RGBA;
   else if (surf->m_format == BaseTexture::SRGBA)
      tex->format = colorFormat::SRGBA;
   else if (surf->m_format == BaseTexture::RGB)
      tex->format = colorFormat::RGB;
   else if (surf->m_format == BaseTexture::SRGB)
      tex->format = colorFormat::SRGB;
   else if (surf->m_format == BaseTexture::RGB_FP16)
      tex->format = colorFormat::RGB16F;
   else if (surf->m_format == BaseTexture::RGB_FP32)
      tex->format = colorFormat::RGB32F;
   colorFormat Format = tex->format;
   const GLuint col_type = ((Format == RGBA32F) || (Format == RGB32F)) ? GL_FLOAT : ((Format == RGBA16F) || (Format == RGB16F)) ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
   const GLuint col_format = ((Format == GREY) || (Format == RED16F)) ? GL_RED : ((Format == GREY_ALPHA) || (Format == RG16F)) ? GL_RG : ((Format == RGB) || (Format == RGB8) || (Format == SRGB) || (Format == SRGB8) || (Format == RGB5) || (Format == RGB10) || (Format == RGB16F) || (Format == RGB32F)) ? GL_RGB : GL_RGBA;
   glBindTexture(GL_TEXTURE_2D, tex->texture);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, surf->width(), surf->height(), col_format, col_type, surf->data());
   glGenerateMipmap(GL_TEXTURE_2D); // Generate mip-maps
   glBindTexture(GL_TEXTURE_2D, 0);
#else
   IDirect3DTexture9* sysTex = CreateSystemTexture(surf);
   m_curTextureUpdates++;
   CHECKD3D(m_pD3DDevice->UpdateTexture(sysTex, tex));
   SAFE_RELEASE(sysTex);
#endif
}

void RenderDevice::SetSamplerState(const DWORD Sampler, const DWORD minFilter, const DWORD magFilter, const SamplerStateValues mipFilter)
{
#ifdef ENABLE_SDL
/*   glSamplerParameteri(Sampler, GL_TEXTURE_MIN_FILTER, minFilter ? (mipFilter ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR) : GL_NEAREST);
   glSamplerParameteri(Sampler, GL_TEXTURE_MAG_FILTER, magFilter ? (mipFilter ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR) : GL_NEAREST);
   m_curStateChanges += 2;*/
#else
   if (textureSamplerCache[Sampler][D3DSAMP_MINFILTER] != minFilter)
   {
      CHECKD3D(m_pD3DDevice->SetSamplerState(Sampler, D3DSAMP_MINFILTER, minFilter));
      textureSamplerCache[Sampler][D3DSAMP_MINFILTER] = minFilter;
      m_curStateChanges++;
   }
   if (textureSamplerCache[Sampler][D3DSAMP_MAGFILTER] != magFilter)
   {
      CHECKD3D(m_pD3DDevice->SetSamplerState(Sampler, D3DSAMP_MAGFILTER, magFilter));
      textureSamplerCache[Sampler][D3DSAMP_MAGFILTER] = magFilter;
      m_curStateChanges++;
   }
   if (textureSamplerCache[Sampler][D3DSAMP_MIPFILTER] != mipFilter)
   {
      CHECKD3D(m_pD3DDevice->SetSamplerState(Sampler, D3DSAMP_MIPFILTER, mipFilter));
      textureSamplerCache[Sampler][D3DSAMP_MIPFILTER] = mipFilter;
      m_curStateChanges++;
   }
#endif
}

void RenderDevice::SetSamplerAnisotropy(const DWORD Sampler, DWORD Value)
{
#ifndef ENABLE_SDL
   if (textureSamplerCache[Sampler][D3DSAMP_MAXANISOTROPY] != Value)
   {
      CHECKD3D(m_pD3DDevice->SetSamplerState(Sampler, D3DSAMP_MAXANISOTROPY, Value));
      textureSamplerCache[Sampler][D3DSAMP_MAXANISOTROPY] = Value;

      m_curStateChanges++;
   }
#endif
}

void RenderDevice::RenderDevice::SetTextureAddressMode(const DWORD Sampler, const SamplerStateValues mode)
{
#ifndef ENABLE_SDL
   if (textureSamplerCache[Sampler][D3DSAMP_ADDRESSU] != mode)
   {
      CHECKD3D(m_pD3DDevice->SetSamplerState(Sampler, D3DSAMP_ADDRESSU, mode));
      textureSamplerCache[Sampler][D3DSAMP_ADDRESSU] = mode;
      m_curStateChanges++;
   }
   if (textureSamplerCache[Sampler][D3DSAMP_ADDRESSV] != mode)
   {
      CHECKD3D(m_pD3DDevice->SetSamplerState(Sampler, D3DSAMP_ADDRESSV, mode));
      textureSamplerCache[Sampler][D3DSAMP_ADDRESSV] = mode;
      m_curStateChanges++;
   }
#endif
}

void RenderDevice::SetTextureFilter(const DWORD texUnit, DWORD mode)
{
   // user can override the standard/faster-on-low-end trilinear by aniso filtering
   if ((mode == TEXTURE_MODE_TRILINEAR) && m_force_aniso)
      mode = TEXTURE_MODE_ANISOTROPIC;

   switch (mode)
   {
   default:
   case TEXTURE_MODE_POINT:
      // Don't filter textures, no mipmapping.
      SetSamplerState(texUnit, POINT, POINT, NONE);
      break;

   case TEXTURE_MODE_BILINEAR:
      // Interpolate in 2x2 texels, no mipmapping.
      SetSamplerState(texUnit, LINEAR, LINEAR, NONE);
      break;

   case TEXTURE_MODE_TRILINEAR:
      // Filter textures on 2 mip levels (interpolate in 2x2 texels). And filter between the 2 mip levels.
      SetSamplerState(texUnit, LINEAR, LINEAR, LINEAR);
      break;

   case TEXTURE_MODE_ANISOTROPIC:
      // Full HQ anisotropic Filter. Should lead to driver doing whatever it thinks is best.
      SetSamplerState(texUnit, LINEAR, LINEAR, LINEAR);
      //if (m_maxaniso>0) // done on the texture, not the sampler
   	  //   SetSamplerAnisotropy(texUnit, min(m_maxaniso, (DWORD)16));
      break;
   }
}

#ifndef ENABLE_SDL
void RenderDevice::SetTextureStageState(const DWORD p1, const D3DTEXTURESTAGESTATETYPE p2, const DWORD p3)
{
   if ((unsigned int)p2 < TEXTURE_STATE_CACHE_SIZE && p1 < TEXTURE_SAMPLERS)
   {
      if (textureStateCache[p1][p2] == p3)
      {
         // texture stage state hasn't changed since last call of this function -> do nothing here
         return;
      }
      textureStateCache[p1][p2] = p3;
   }
   CHECKD3D(m_pD3DDevice->SetTextureStageState(p1, p2, p3));

   m_curStateChanges++;
}

void RenderDevice::SetRenderTarget(RenderTarget* surf, bool ignoreStereo)
{
   if (surf)
   {
      CHECKD3D(m_pD3DDevice->SetRenderTarget(0, surf));
   }
   else
   {
      CHECKD3D(m_pD3DDevice->SetRenderTarget(0, m_pBackBuffer));
   }
}
#endif

void RenderDevice::SetRenderTarget(D3DTexture* texture, bool ignoreStereo)
{
#ifdef ENABLE_SDL
   static GLfloat viewPorts[] = { 0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f };
   static int currentFrameBuffer = -1;
   //static int currentStereoMode = -1;
   if (currentFrameBuffer != (texture ? texture->framebuffer : 0)) {
      currentFrameBuffer = texture ? texture->framebuffer : 0;
      glBindFramebuffer(GL_FRAMEBUFFER, currentFrameBuffer);
      //currentStereoMode = -1;
   }
   if (texture && (texture->texture) > 0) Shader::setTextureDirty(texture->texture);
   if (texture) {
      //currentStereoMode = ignoreStereo ? 0 : texture->stereo;
      if (ignoreStereo)
      {
         glViewport(0, 0, texture->width, texture->height);
      }
      else
         switch (texture->stereo) {
         case STEREO_OFF:
            glViewport(0, 0, texture->width, texture->height);
            lightShader->SetBool(SHADER_ignoreStereo, true); // For non-stereo lightbulb texture, can't use pre-processor for this
            break;
         case STEREO_TB:
         case STEREO_INT:
            glViewport(0, 0, texture->width, texture->height / 2); // Set default viewport width/height values of all viewports before we define the array or we get undefined behaviour in shader (flickering viewports).
            viewPorts[2] = viewPorts[6] = (float)texture->width;
            viewPorts[3] = viewPorts[7] = (float)texture->height / 2.0f;
            viewPorts[4] = 0.0f;
            viewPorts[5] = (float)texture->height / 2.0f;
            glViewportArrayv(0, 2, viewPorts);
            lightShader->SetBool(SHADER_ignoreStereo, false);
            break;
         case STEREO_SBS:
         case STEREO_VR:
            glViewport(0, 0, texture->width / 2, texture->height); // Set default viewport width/height values of all viewports before we define the array or we get undefined behaviour in shader (flickering viewports).
            viewPorts[2] = viewPorts[6] = (float)texture->width / 2.0f;
            viewPorts[3] = viewPorts[7] = (float)texture->height;
            viewPorts[4] = (float)texture->width / 2.0f;
            viewPorts[5] = 0.0f;
            glViewportArrayv(0, 2, viewPorts);
            lightShader->SetBool(SHADER_ignoreStereo, false);
            break;
         }
   }
   else {
      //currentStereoMode = 0;
      glViewport(0, 0, m_pBackBuffer->width, m_pBackBuffer->height);
   }
#else
   RenderTarget *surf = nullptr;
   texture->GetSurfaceLevel(0, &surf);
   CHECKD3D(m_pD3DDevice->SetRenderTarget(0, surf));
   SAFE_RELEASE_NO_RCC(surf);
#endif
}

#ifndef ENABLE_SDL
void RenderDevice::SetZBuffer(D3DTexture* surf)
{
   IDirect3DSurface9 *textureSurface;
   CHECKD3D(surf->GetSurfaceLevel(0, &textureSurface));
   CHECKD3D(m_pD3DDevice->SetDepthStencilSurface(textureSurface));
   SAFE_RELEASE_NO_RCC(textureSurface);
}
#endif

void RenderDevice::SetZBuffer(RenderTarget* surf)
{
#ifndef ENABLE_SDL
   CHECKD3D(m_pD3DDevice->SetDepthStencilSurface(surf));
#endif
}

void RenderDevice::UnSetZBuffer()
{
#ifndef ENABLE_SDL
   CHECKD3D(m_pD3DDevice->SetDepthStencilSurface(nullptr));
#endif
}

inline bool RenderDevice::SetRenderStateCache(const RenderStates p1, DWORD p2)
{
#ifdef DEBUG
   if (p1 >= RENDERSTATE_COUNT)
      return false;//Throw error or similar?
#endif
   if (renderStateCache[p1] != p2) {
      renderStateCache[p1] = p2;
      return false;
   }
   return true;
}

void RenderDevice::SetRenderState(const RenderStates p1, DWORD p2)
{
   if (SetRenderStateCache(p1, p2)) return;
#ifdef ENABLE_SDL
   switch (p1) {
      //glEnable and glDisable functions
   case ALPHABLENDENABLE:
      if (p2) glEnable(GL_BLEND); else glDisable(GL_BLEND);
      break;
   case ZENABLE:
      if (p2) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
      break;
   case BLENDOP:
      glBlendEquation(p2);
      break;
   case SRCBLEND:
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      break;
   case DESTBLEND:
      glBlendFunc(renderStateCache[SRCBLEND], renderStateCache[DESTBLEND]);
      break;
   case ZFUNC:
      glDepthFunc(p2);
      break;
   case ZWRITEENABLE:
      glDepthMask(p2 ? GL_TRUE : GL_FALSE);
      break;
   case COLORWRITEENABLE:
      glColorMask((p2 & 1) ? GL_TRUE : GL_FALSE, (p2 & 2) ? GL_TRUE : GL_FALSE, (p2 & 4) ? GL_TRUE : GL_FALSE, (p2 & 8) ? GL_TRUE : GL_FALSE);
      break;
      //Replaced by specific function
   case DEPTHBIAS:
   case CULLMODE:
   case CLIPPLANEENABLE:
   case ALPHAFUNC:
   case ALPHATESTENABLE:
      //No effect or not implemented in OpenGL 
   case LIGHTING:
   case CLIPPING:
   case ALPHAREF:
   case SRGBWRITEENABLE:
   default:
      break;
   }
#else
   CHECKD3D(m_pD3DDevice->SetRenderState((D3DRENDERSTATETYPE)p1, p2));
#endif
   m_curStateChanges++;
}

void RenderDevice::SetRenderStateCulling(RenderStateValue cull)
{
   if (g_pplayer && (g_pplayer->m_ptable->m_tblMirrorEnabled ^ g_pplayer->m_ptable->m_reflectionEnabled))
   {
      if (cull == CULL_CCW)
         cull = CULL_CW;
      else if (cull == CULL_CW)
         cull = CULL_CCW;
   }
   if (renderStateCache[RenderStates::CULLMODE] == CULL_NONE && (cull != CULL_NONE))
      glEnable(GL_CULL_FACE);
   if (SetRenderStateCache(CULLMODE, cull)) return;

#ifdef ENABLE_SDL
   if (cull == CULL_NONE) {
      glDisable(GL_CULL_FACE);
   }
   else {
      glFrontFace(cull);
      glCullFace(GL_FRONT);
   }
#else
   CHECKD3D(m_pD3DDevice->SetRenderState((D3DRENDERSTATETYPE)CULLMODE, cull));
#endif
   m_curStateChanges++;
}

void RenderDevice::SetRenderStateDepthBias(float bias)
{
   if (SetRenderStateCache(DEPTHBIAS, *((DWORD*)&bias))) return;

#ifdef ENABLE_SDL
   if (bias == 0.0f)
      glDisable(GL_POLYGON_OFFSET_FILL);
   else {
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(0.0f, bias);
   }
#else
   bias *= BASEDEPTHBIAS;
   CHECKD3D(m_pD3DDevice->SetRenderState((D3DRENDERSTATETYPE)DEPTHBIAS, *((DWORD*)&bias)));
#endif
   m_curStateChanges++;
}

void RenderDevice::SetRenderStateClipPlane0(const bool enabled)
{
   if (SetRenderStateCache(CLIPPLANEENABLE, enabled ? PLANE0 : 0)) return;

#ifdef ENABLE_SDL
   // Basicshader already prepared with proper clipplane so just need to enable/disable it
   if (enabled)
      glEnable(GL_CLIP_DISTANCE0);
   else
      glDisable(GL_CLIP_DISTANCE0);
#else
   CHECKD3D(m_pD3DDevice->SetRenderState((D3DRENDERSTATETYPE)CLIPPLANEENABLE, enabled ? PLANE0 : 0));
#endif 
   m_curStateChanges++;
}

void RenderDevice::SetRenderStateAlphaTestFunction(const DWORD testValue, const RenderStateValue testFunction, const bool enabled)
{
#ifdef ENABLE_SDL
   //TODO Needs to be done in shader
#else 
   if (!SetRenderStateCache(ALPHAREF, testValue))
      CHECKD3D(m_pD3DDevice->SetRenderState((D3DRENDERSTATETYPE)ALPHAREF, testValue));
   if (!SetRenderStateCache(ALPHATESTENABLE, enabled ? RS_TRUE : RS_FALSE))
      CHECKD3D(m_pD3DDevice->SetRenderState((D3DRENDERSTATETYPE)ALPHATESTENABLE, enabled ? RS_TRUE : RS_FALSE));
   if (!SetRenderStateCache(ALPHAFUNC, testFunction))
      CHECKD3D(m_pD3DDevice->SetRenderState((D3DRENDERSTATETYPE)ALPHAFUNC, testFunction));
#endif
}

void RenderDevice::CreateVertexDeclaration(const VertexElement * const element, VertexDeclaration ** declaration)
{
#ifndef ENABLE_SDL
   CHECKD3D(m_pD3DDevice->CreateVertexDeclaration(element, declaration));
#endif
}

void RenderDevice::SetVertexDeclaration(VertexDeclaration * declaration)
{
#ifndef ENABLE_SDL
   if (declaration != currentDeclaration)
   {
      CHECKD3D(m_pD3DDevice->SetVertexDeclaration(declaration));
      currentDeclaration = declaration;

      m_curStateChanges++;
   }
#endif
}

#ifndef ENABLE_SDL
void* RenderDevice::AttachZBufferTo(D3DTexture* surfTexture)
{
   RenderTarget* surf;
   surfTexture->GetSurfaceLevel(0, &surf);
   return AttachZBufferTo(surf);
}
#endif

void* RenderDevice::AttachZBufferTo(RenderTarget* surf)
{
#ifndef ENABLE_SDL
   D3DSURFACE_DESC desc;
   surf->GetDesc(&desc);

   if (!m_useNvidiaApi && m_INTZ_support)
   {
      D3DTexture* dup;
      CHECKD3D(m_pD3DDevice->CreateTexture(desc.Width, desc.Height, 1,
               D3DUSAGE_DEPTHSTENCIL, (D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z'), (D3DPOOL)memoryPool::DEFAULT, &dup, nullptr)); // D3DUSAGE_AUTOGENMIPMAP?

      return dup;
   }
   else
   {
      IDirect3DSurface9 *pZBuf;
      const HRESULT hr = m_pD3DDevice->CreateDepthStencilSurface(desc.Width, desc.Height, D3DFMT_D16 /*D3DFMT_D24X8*/, //!!
                                                                 desc.MultiSampleType, desc.MultiSampleQuality, FALSE, &pZBuf, nullptr);
      if (FAILED(hr))
         ReportError("Fatal Error: unable to create depth buffer!", hr, __FILE__, __LINE__);

      return pZBuf;
   }
   SAFE_RELEASE_NO_RCC(surf); //!! ?
#else
   return nullptr;
#endif
}

//Only used for DX9
void RenderDevice::DrawPrimitive(const PrimitiveTypes type, const DWORD fvf, const void* vertices, const DWORD vertexCount)
{
#ifndef ENABLE_SDL
   const unsigned int np = ComputePrimitiveCount(type, vertexCount);
   m_stats_drawn_triangles += np;

   VertexDeclaration * declaration = fvfToDecl(fvf);
   SetVertexDeclaration(declaration);

   HRESULT hr = m_pD3DDevice->DrawPrimitiveUP((D3DPRIMITIVETYPE)type, np, vertices, fvfToSize(fvf));

   if (FAILED(hr))
      ReportError("Fatal Error: DrawPrimitiveUP failed!", hr, __FILE__, __LINE__);

   VertexBuffer::bindNull();    // DrawPrimitiveUP sets the VB to nullptr

   m_curDrawCalls++;
#endif
}

//Use this function if you want to render a stereo object
void RenderDevice::DrawTexturedQuad()
{
   DrawPrimitiveVB(RenderDevice::TRIANGLESTRIP, MY_D3DFVF_TEX, m_quadVertexBuffer, 0, 4, true);
}

//Used for processing a Texture to the next Framebuffer with a shader.
void RenderDevice::DrawTexturedQuadPostProcess()
{
   DrawPrimitiveVB(RenderDevice::TRIANGLESTRIP, MY_D3DFVF_TEX, m_quadVertexBuffer, 0, 4, false);
}

void RenderDevice::DrawPrimitiveVB(const PrimitiveTypes type, const DWORD fvf, VertexBuffer* vb, const DWORD startVertex, const DWORD vertexCount, const bool stereo)
{
   const unsigned int np = ComputePrimitiveCount(type, vertexCount);
   m_stats_drawn_triangles += np;

   vb->bind();
#ifdef ENABLE_SDL
   //glDrawArraysInstanced(type, vb->getOffset() + startVertex, vertexCount, m_stereo3D != STEREO_OFF ? 2 : 1); // Do instancing in geometry shader instead
   glDrawArrays(type, vb->getOffset() + startVertex, vertexCount);
#else
   VertexDeclaration * declaration = fvfToDecl(fvf);
   SetVertexDeclaration(declaration);

   const HRESULT hr = m_pD3DDevice->DrawPrimitive((D3DPRIMITIVETYPE)type, startVertex, np);
   if (FAILED(hr))
      ReportError("Fatal Error: DrawPrimitive failed!", hr, __FILE__, __LINE__);
#endif
   m_curDrawCalls++;
}

void RenderDevice::DrawIndexedPrimitiveVB(const PrimitiveTypes type, const DWORD fvf, VertexBuffer* vb, const DWORD startVertex, const DWORD vertexCount, IndexBuffer* ib, const DWORD startIndex, const DWORD indexCount)
{
   if (vb == nullptr || ib == nullptr) //!! happens for primitives that are grouped on player init render call?!?
      return;

   const unsigned int np = ComputePrimitiveCount(type, indexCount);
   m_stats_drawn_triangles += np;
   vb->bind();
   ib->bind();
#ifdef ENABLE_SDL
   const int offset = ib->getOffset() + (ib->getIndexFormat() == IndexBuffer::FMT_INDEX16 ? 2 : 4) * startIndex;
   //glDrawElementsInstancedBaseVertex(type, indexCount, ib->getIndexFormat() == IndexBuffer::FMT_INDEX16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (void*)offset, m_stereo3D != STEREO_OFF ? 2 : 1, vb->getOffset() + startVertex); // Do instancing in geometry shader instead
   glDrawElementsBaseVertex(type, indexCount, ib->getIndexFormat() == IndexBuffer::FMT_INDEX16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (void*)offset, vb->getOffset() + startVertex);
#else
   VertexDeclaration * declaration = fvfToDecl(fvf);
   SetVertexDeclaration(declaration);

   // render
   CHECKD3D(m_pD3DDevice->DrawIndexedPrimitive((D3DPRIMITIVETYPE)type, startVertex, 0, vertexCount, startIndex, np));
#endif
   m_curDrawCalls++;
}

#ifdef ENABLE_VR
void RenderDevice::UpdateVRPosition()
{
   if (!m_pHMD) return;

   vr::VRCompositor()->WaitGetPoses(m_rTrackedDevicePose, vr::k_unMaxTrackedDeviceCount, nullptr, 0);

   for (int device = 0; device < vr::k_unMaxTrackedDeviceCount; device++) {
      if ((m_rTrackedDevicePose[device].bPoseIsValid) && (m_pHMD->GetTrackedDeviceClass(device) == vr::TrackedDeviceClass_HMD)) {
         hmdPosition = m_rTrackedDevicePose[device];
         for (int i = 0;i < 3;i++)
            for (int j = 0;j < 4;j++)
               m_matView.m[j][i] = hmdPosition.mDeviceToAbsoluteTracking.m[i][j];
         for (int j = 0;j < 4;j++)
            m_matView.m[j][3] = (j == 3) ? 1.0f : 0.0f;
         break;
      }
   }
   m_matView.Invert();
   m_matView = m_tableWorld * m_matView;
}

void RenderDevice::tableUp()
{
   m_tablez += 1.0f;
   if (m_tablez > 250.0f) m_tablez = 250.0f;
   updateTableMatrix();
}

void RenderDevice::tableDown()
{
   m_tablez -= 1.0f;
   if (m_tablez < 0.0f) m_tablez = 0.0f;
   updateTableMatrix();
}

//Do not change the position of the room
void RenderDevice::recenterTable()
{
   //hmdPosition;
   m_orientation = -RADTOANG(atan2(hmdPosition.mDeviceToAbsoluteTracking.m[0][2], hmdPosition.mDeviceToAbsoluteTracking.m[0][0]));
   if (m_orientation < 0.0f) m_orientation += 360.0f;
   const float w = 100.f*0.5f*m_scale*(g_pplayer->m_ptable->m_right  - g_pplayer->m_ptable->m_left);
   const float h = 100.f*     m_scale*(g_pplayer->m_ptable->m_bottom - g_pplayer->m_ptable->m_top) + 20.0f;
   const float c = cos(ANGTORAD(m_orientation));
   const float s = sin(ANGTORAD(m_orientation));
   m_tablex =  100.0f*hmdPosition.mDeviceToAbsoluteTracking.m[0][3] - c * w + s * h;
   m_tabley = -100.0f*hmdPosition.mDeviceToAbsoluteTracking.m[2][3] + s * w + c * h;
   updateTableMatrix();
}

//Change the position of the room, but keep the table at the same position in the room
void RenderDevice::recenterRoom()
{
   recenterTable();
   //TODO: new code when room is working
}

void RenderDevice::updateTableMatrix()
{
   Matrix3D tmp;
   m_tableWorld.SetIdentity();
   //Tilt playfield.
   m_tableWorld.RotateXMatrix(ANGTORAD(-m_slope));
   tmp.SetIdentity();
   //Convert from VPX scale and coords to VR

   tmp.m[0][0] = -m_scale;  tmp.m[0][1] = 0.0f;  tmp.m[0][2] = 0.0f;
   tmp.m[1][0] = 0.0f;  tmp.m[1][1] = 0.0f;  tmp.m[1][2] = -m_scale;
   tmp.m[2][0] = 0.0f;  tmp.m[2][1] = m_scale;  tmp.m[2][2] = 0.0f;
   m_tableWorld = m_tableWorld * tmp;
   tmp.SetIdentity();
   tmp.RotateYMatrix(ANGTORAD(180.f - m_orientation - m_roomOrientation));//Rotate table around VR height axis
   m_tableWorld = m_tableWorld * tmp;
   tmp.SetIdentity();
   tmp.SetTranslation((m_roomx+m_tablex) / 100.0f, m_tablez / 100.0f, -(m_roomy+m_tabley) / 100.0f);//Locate front left corner of the table in the room -x is to the right, -y is up and -z is back - all units in meters
   m_tableWorld = m_tableWorld * tmp;

   m_roomWorld.SetIdentity();

   tmp.SetIdentity();
   tmp.RotateYMatrix(ANGTORAD(180.f - m_roomOrientation));//Rotate room around VR height axis
   tmp.SetIdentity();
   tmp.SetTranslation((m_roomx) / 100.0f, 0.0f, -(m_roomy) / 100.0f); //!! unused?!
}

void RenderDevice::SetTransformVR()
{
   Shader::SetTransform(TRANSFORMSTATE_PROJECTION, m_matProj, m_stereo3D != STEREO_OFF ? 2:1);
   Shader::SetTransform(TRANSFORMSTATE_VIEW, &m_matView, 1);
}
#endif

void RenderDevice::Clear(const DWORD flags, const D3DCOLOR color, const D3DVALUE z, const DWORD stencil)
{
#ifdef ENABLE_SDL
   static float clear_r=0.f, clear_g = 0.f, clear_b = 0.f, clear_a = 0.f, clear_z=1.f;//Default OpenGL Values
   static GLint clear_s=0;

   if (clear_s != stencil) { clear_s = stencil;  glClearStencil(stencil); }
   if (clear_z != z) { clear_z = z;  glClearDepthf(z); }
   const float r = (float)( color & 0xff) / 255.0f;
   const float g = (float)((color & 0xff00) >> 8) / 255.0f;
   const float b = (float)((color & 0xff0000) >> 16) / 255.0f;
   const float a = (float)((color & 0xff000000) >> 24) / 255.0f;
   if ((r != clear_r) || (g != clear_g) || (b != clear_b) || (a != clear_a)) { clear_z = z;  glClearColor(r,g,b,a); }
   glClear(flags);
#else
   CHECKD3D(m_pD3DDevice->Clear(0, nullptr, flags, color, z, stencil));
#endif
}

#ifdef ENABLE_SDL
static ViewPort viewPort;
#endif

void RenderDevice::SetViewport(const ViewPort* p1)
{
#ifdef ENABLE_SDL
   memcpy(&viewPort, p1, sizeof(ViewPort));
#else
   CHECKD3D(m_pD3DDevice->SetViewport((D3DVIEWPORT9*)p1));
#endif
}

void RenderDevice::GetViewport(ViewPort* p1)
{
#ifdef ENABLE_SDL
   memcpy(p1, &viewPort, sizeof(ViewPort));
#else
   CHECKD3D(m_pD3DDevice->GetViewport((D3DVIEWPORT9*)p1));
#endif
}

D3DTexture* RenderDevice::CreateTexture(UINT Width, UINT Height, UINT Levels, textureUsage Usage, colorFormat Format, void* data, int stereo, bool clamptoedge) {
#ifdef ENABLE_SDL
   D3DTexture* tex = new D3DTexture();
   tex->usage = Usage;
   tex->width = Width;
   tex->height = Height;
   tex->format = Format;
   //tex->slot = -1;
   const GLuint col_type = ((Format == RGBA32F) || (Format == RGB32F)) ? GL_FLOAT : ((Format == RGBA16F) || (Format == RGB16F)) ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
   const GLuint col_format = ((Format == GREY) || (Format == RED16F)) ? GL_RED : ((Format == GREY_ALPHA) || (Format == RG16F)) ? GL_RG : ((Format == RGB) || (Format == RGB8) 
      || (Format == SRGB) || (Format == SRGB8) || (Format == RGB5) || (Format == RGB10) || (Format == RGB16F) || (Format == RGB32F)) ? GL_RGB : GL_RGBA;
   const bool col_is_linear = (Format == GREY) || (Format == RED16F) || (Format == GREY_ALPHA) || (Format == RG16F) || (Format == RGB5) || (Format == RGB) || (Format == RGB8) || 
      (Format == RGB10) || (Format == RGB16F) || (Format == RGB32F) || (Format == RGBA16F) || (Format == RGBA32F) || (Format == RGBA) || (Format == RGBA8) || (Format == RGBA10) || 
      (Format == DXT5) || (Format == BC6U) || (Format == BC6S) || (Format == BC7);

   // Create MSAA/Non-MSAA Renderbuffers
   if ((tex->usage == RENDERTARGET) || (tex->usage == RENDERTARGET_DEPTH) || (tex->usage == RENDERTARGET_MSAA) || (tex->usage == RENDERTARGET_MSAA_DEPTH)) {
      tex->stereo = stereo;
      glGenFramebuffers(1, &tex->framebuffer);
      glBindFramebuffer(GL_FRAMEBUFFER, tex->framebuffer);
      glGenTextures(1, &tex->texture);

      if (g_pplayer->m_MSAASamples > 1 && (tex->usage == RENDERTARGET_MSAA || tex->usage == RENDERTARGET_MSAA_DEPTH))
      {
         glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, tex->texture);
         glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, g_pplayer->m_MSAASamples, Format, Width, Height, GL_TRUE);
         glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, tex->texture, 0);

         if (tex->usage == RENDERTARGET_MSAA_DEPTH)
         {
            glGenRenderbuffers(1, &tex->zBuffer);
            glBindRenderbuffer(GL_RENDERBUFFER, tex->zBuffer);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, g_pplayer->m_MSAASamples, GL_DEPTH_COMPONENT, Width, Height);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, tex->zBuffer);
         }
         else
         {
            tex->zTexture = 0;
            tex->zBuffer = 0;
         }
      }
      else // RENDERTARGET & RENDERTARGET_DEPTH
      {
         glBindTexture(GL_TEXTURE_2D, tex->texture);
         glTexImage2D(GL_TEXTURE_2D, 0, Format, Width, Height, 0, GL_RGBA, col_type, nullptr);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex->texture, 0);

         if (tex->usage == RENDERTARGET_DEPTH || tex->usage == RENDERTARGET_MSAA_DEPTH) {
            glGenRenderbuffers(1, &tex->zBuffer);
            glBindRenderbuffer(GL_RENDERBUFFER, tex->zBuffer);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, Width, Height);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, tex->zBuffer);
         }
         else
         {
            tex->zTexture = 0;
            tex->zBuffer = 0;
         }
      }

      constexpr GLenum DrawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
      glDrawBuffers(1, DrawBuffers);

      const int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
      if (status != GL_FRAMEBUFFER_COMPLETE) {
         char msg[256];
         const char* errorCode;
         switch (status) {
         case GL_FRAMEBUFFER_UNDEFINED:
            errorCode = "GL_FRAMEBUFFER_UNDEFINED";
            break;
         case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            errorCode = "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
            break;
         case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            errorCode = "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
            break;
         case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
            errorCode = "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER";
            break;
         case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
            errorCode = "GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER";
            break;
         case GL_FRAMEBUFFER_UNSUPPORTED:
            errorCode = "GL_FRAMEBUFFER_UNSUPPORTED";
            break;
         case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
            errorCode = "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
            break;
         case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
            errorCode = "GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS";
            break;
         default:
            errorCode = "unknown";
            break;
         }
         sprintf_s(msg, 256, "glCheckFramebufferStatus returned 0x%0002X %s", glCheckFramebufferStatus(tex->framebuffer), errorCode);
         ShowError(msg);
         exit(-1);
      }
      return tex;
   }

   // normal textures:

   tex->framebuffer = 0;
   tex->zTexture = 0;
   tex->stereo = 0;

   glGenTextures(1, &tex->texture);
   glBindTexture(GL_TEXTURE_2D, tex->texture);

   if (clamptoedge)
   {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   }
   else
   {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
   }

   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); // Use mipmap filtering GL_LINEAR_MIPMAP_LINEAR
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); // MAG Filter does not support mipmaps

   if (Format == GREY) {//Hack so that GL_RED behaves as GL_GREY
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ALPHA);
      Format = RGB8;
   }
   else if (Format == GREY_ALPHA) {//Hack so that GL_RG behaves as GL_GREY_ALPHA
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_GREEN);
      Format = RGB8;
   }
   else {//Default
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_GREEN);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_BLUE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ALPHA);

      // Anisotropic filtering
      if (m_maxaniso > 0)
         glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, min(max(1.f,m_maxaniso),16.f));
   }

   colorFormat comp_format = Format;
   if (m_compress_textures && ((Width & 3) == 0) && ((Height & 3) == 0) && (Width > 256) && (Height > 256))
      if (col_type == GL_FLOAT || col_type == GL_HALF_FLOAT)
         if (GLAD_GL_ARB_texture_compression_bptc)
            comp_format = colorFormat::BC6S; // We should use unsigned BC6 but this needs to know before hand if the texture is only positive
      else if (GLAD_GL_ARB_texture_compression_bptc)
         comp_format = col_is_linear ? colorFormat::BC7 : colorFormat::SBC7;
      else
         comp_format = col_is_linear ? colorFormat::DXT5 : colorFormat::SDXT5;
   
   const int num_mips = (int)std::log2(float(std::max(Width, Height))) + 1;
   if (m_GLversion >= 403)
      glTexStorage2D(GL_TEXTURE_2D, num_mips, comp_format, Width, Height);
   else { // should never be triggered nowadays
      GLsizei w = Width;
      GLsizei h = Height;
      for (int i = 0; i < num_mips; i++) {
         glTexImage2D(GL_TEXTURE_2D, i, comp_format, w, h, 0, col_format, col_type, nullptr);
         w = max(1, (w / 2));
         h = max(1, (h / 2));
      }
   }

   if (data)
   {
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Width, Height, col_format, col_type, data);
      glGenerateMipmap(GL_TEXTURE_2D); // Generate mip-maps, when using TexStorage will generate same amount as specified in TexStorage, otherwise good idea to limit by GL_TEXTURE_MAX_LEVEL
   }
#else //D3DTexture* RenderDevice::CreateTexture(UINT Width, UINT Height, UINT Levels, textureUsage Usage, colorFormat Format, void* data) {
   D3DPOOL Pool;
   D3DTexture* tex;
   HRESULT hr;

   switch (Usage) {
   case RENDERTARGET:
      //case RENDERTARGET_DEPTH:
   case DEPTH:
      Pool = (D3DPOOL)memoryPool::DEFAULT;
      break;
   case AUTOMIPMAP:
   case STATIC:
   case DYNAMIC:
   default:
      Pool = D3DPOOL_SYSTEMMEM;
      break;
   }

   hr = m_pD3DDevice->CreateTexture(Width, Height, Levels, Usage, (D3DFORMAT)Format, (D3DPOOL)Pool, &tex, nullptr);
   if (FAILED(hr))
   {
      ShowError("Could not create D3D9 texture.");
      throw 0;
   }
   if (data) {
      IDirect3DTexture9* sysTex = CreateSystemTexture(Width, Height, (D3DFORMAT)Format, data, Width, false);
      m_curTextureUpdates++;
      CHECKD3D(m_pD3DDevice->UpdateTexture(sysTex, tex));
      SAFE_RELEASE(sysTex);
   }
#endif
   return tex;
}

#ifdef ENABLE_SDL
HRESULT RenderDevice::Create3DFont(INT Height, UINT Width, UINT Weight, UINT MipLevels, BOOL Italic, DWORD CharSet, DWORD OutputPrecision, DWORD Quality, DWORD PitchAndFamily, LPCTSTR pFacename, TTF_Font *ppFont)
{
   //ppFont = TTF_OpenFont(pFacename, 24);
   //TODO see https://stackoverflow.com/questions/30016083/sdl2-opengl-sdl2-ttf-displaying-text
   return 0;
}
#else
HRESULT RenderDevice::Create3DFont(INT Height, UINT Width, UINT Weight, UINT MipLevels, BOOL Italic, DWORD CharSet, DWORD OutputPrecision, DWORD Quality, DWORD PitchAndFamily, LPCTSTR pFacename, FontHandle *ppFont)
{
   return D3DXCreateFont(m_pD3DDevice, Height, Width, Weight, MipLevels, Italic, CharSet, OutputPrecision, Quality, PitchAndFamily, pFacename, ppFont);
}
#endif
