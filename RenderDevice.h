#pragma once

#include <map>
#include "typeDefs3D.h"

#include "Material.h"
#include "Texture.h"
#include "stdafx.h"
#include "IndexBuffer.h"
#include "VertexBuffer.h"
#include "TextureManager.h"

#ifdef ENABLE_VR
#include <openvr.h>
#endif

void ReportFatalError(const HRESULT hr, const char *file, const int line);
void ReportError(const char *errorText, const HRESULT hr, const char *file, const int line);

#ifdef _DEBUG
#ifdef ENABLE_SDL
void checkGLErrors(const char *file, const int line);
//#define CHECKD3D(s) {s;}
#define CHECKD3D(s) { s;checkGLErrors(__FILE__, __LINE__);}
#else //ENABLE_SDL
#define CHECKD3D(s) { const HRESULT hrTmp = (s); if (FAILED(hrTmp)) ReportFatalError(hrTmp, __FILE__, __LINE__); }
#endif
#else //_DEBUG
#define CHECKD3D(s) {s;}
#endif

bool IsWindows10_1803orAbove();

struct VideoMode
{
   int width;
   int height;
   int depth;
   int refreshrate;
   int display;
};

int getNumberOfDisplays();
void EnumerateDisplayModes(const int adapter, std::vector<VideoMode>& modes);


enum TransformStateType {
#ifdef ENABLE_SDL
   TRANSFORMSTATE_WORLD = 0,
   TRANSFORMSTATE_VIEW = 1,
   TRANSFORMSTATE_PROJECTION = 2
#else
   TRANSFORMSTATE_WORLD = D3DTS_WORLD,
   TRANSFORMSTATE_VIEW = D3DTS_VIEW,
   TRANSFORMSTATE_PROJECTION = D3DTS_PROJECTION
#endif
};

#ifdef ENABLE_SDL
enum UsageFlags {
   USAGE_STATIC = GL_STATIC_DRAW,
   USAGE_DYNAMIC = GL_DYNAMIC_DRAW
};
#else
enum UsageFlags {
   USAGE_STATIC = D3DUSAGE_WRITEONLY,
   USAGE_DYNAMIC = D3DUSAGE_DYNAMIC      // to be used for vertex/index buffers which are locked every frame/very often
};
#endif

class TextureManager;
class Shader;

class RenderDevice
{
public:
#ifdef ENABLE_SDL
   enum RenderStates
   {
      ALPHABLENDENABLE = GL_BLEND,
      ALPHATESTENABLE,
      ALPHAREF,
      ALPHAFUNC,
      BLENDOP,
      CLIPPING,
      CLIPPLANEENABLE,
      CULLMODE,
      DESTBLEND,
      LIGHTING,
      SRCBLEND,
      ZENABLE = GL_DEPTH_TEST,
      ZFUNC,
      ZWRITEENABLE,
      DEPTHBIAS = GL_POLYGON_OFFSET_FILL,
      COLORWRITEENABLE
   };

   enum RenderStateValue
   {
      //Booleans
      RS_FALSE = 0,
      RS_TRUE = 1,
      //Culling
      CULL_NONE = 0,
      CULL_CW = GL_CW,
      CULL_CCW = GL_CCW,
      //Depth functions
      Z_ALWAYS = GL_ALWAYS,
      Z_LESS = GL_LESS,
      Z_LESSEQUAL = GL_LEQUAL,
      Z_GREATER = GL_GREATER,
      Z_GREATEREQUAL = GL_GEQUAL,
      //Blending ops
      BLENDOP_MAX = GL_MAX,
      BLENDOP_ADD = GL_FUNC_ADD,
      BLENDOP_SUB = GL_FUNC_SUBTRACT,
      BLENDOP_REVSUBTRACT = GL_FUNC_REVERSE_SUBTRACT,
      //Blending values
      ZERO = GL_ZERO,
      ONE = GL_ONE,
      SRC_ALPHA = GL_SRC_ALPHA,
      DST_ALPHA = GL_DST_ALPHA,
      SRC_COLOR = GL_SRC_COLOR,
      DST_COLOR = GL_DST_COLOR,
      INVSRC_ALPHA = GL_ONE_MINUS_SRC_ALPHA,
      INVSRC_COLOR = GL_ONE_MINUS_SRC_COLOR,
      //Clipping planes
      PLANE0 = 1,

      UNDEFINED
   };

   enum SamplerStateValues {
      NONE = 0,
      POINT = 0,
      LINEAR = 1,
      TEX_WRAP = GL_REPEAT,
      TEX_CLAMP = GL_CLAMP_TO_EDGE,
      TEX_MIRROR = GL_MIRRORED_REPEAT
   };

   enum PrimitveTypes {
      TRIANGLEFAN = GL_TRIANGLE_FAN,
      TRIANGLESTRIP = GL_TRIANGLE_STRIP,
      TRIANGLELIST = GL_TRIANGLES,
      POINTLIST = GL_POINTS,
      LINELIST = GL_LINES,
      LINESTRIP = GL_LINE_STRIP
};

   SDL_Window *m_sdl_hwnd;
   SDL_GLContext  m_sdl_context;

   RenderDevice(HWND *hwnd, const int display, const int width, const int height, const bool fullscreen, const int colordepth, int &refreshrate, int VSync, const bool useAA, const int stereo3D, const unsigned int FXAA, const bool ss_refl, const bool useNvidiaApi, const bool disable_dwm, const int BWrendering);

#else
   enum RenderStates
   {
      ALPHABLENDENABLE = D3DRS_ALPHABLENDENABLE,
      ALPHATESTENABLE = D3DRS_ALPHATESTENABLE,
      ALPHAREF = D3DRS_ALPHAREF,
      ALPHAFUNC = D3DRS_ALPHAFUNC,
      BLENDOP = D3DRS_BLENDOP,
      CLIPPING = D3DRS_CLIPPING,
      CLIPPLANEENABLE = D3DRS_CLIPPLANEENABLE,
      CULLMODE = D3DRS_CULLMODE,
      DESTBLEND = D3DRS_DESTBLEND,
      LIGHTING = D3DRS_LIGHTING,
      SRCBLEND = D3DRS_SRCBLEND,
      ZENABLE = D3DRS_ZENABLE,
      ZFUNC = D3DRS_ZFUNC,
      ZWRITEENABLE = D3DRS_ZWRITEENABLE,
      DEPTHBIAS = D3DRS_DEPTHBIAS,
      COLORWRITEENABLE = D3DRS_COLORWRITEENABLE
   };

   enum RenderStateValue
   {
      //Booleans
      RS_FALSE = FALSE,
      RS_TRUE = TRUE,
      //Culling
      CULL_NONE = D3DCULL_NONE,
      CULL_CW = D3DCULL_CW,
      CULL_CCW = D3DCULL_CCW,
      //Depth functions
      Z_ALWAYS = D3DCMP_ALWAYS,
      Z_LESS = D3DCMP_LESS,
      Z_LESSEQUAL = D3DCMP_LESSEQUAL,
      Z_GREATER = D3DCMP_GREATER,
      Z_GREATEREQUAL = D3DCMP_GREATEREQUAL,
      //Blending ops
      BLENDOP_MAX = D3DBLENDOP_MAX,
      BLENDOP_ADD = D3DBLENDOP_ADD,
      BLENDOP_REVSUBTRACT = D3DBLENDOP_REVSUBTRACT,
      //Blending values
      ZERO = D3DBLEND_ZERO,
      ONE = D3DBLEND_ONE,
      SRC_ALPHA = D3DBLEND_SRCALPHA,
      DST_ALPHA = D3DBLEND_DESTALPHA,
      INVSRC_ALPHA = D3DBLEND_INVSRCALPHA,
      INVSRC_COLOR = D3DBLEND_INVSRCCOLOR,
      //Clipping planes
      PLANE0 = D3DCLIPPLANE0,

      UNDEFINED
   };

   enum SamplerStateValues {
      NONE = D3DTEXF_NONE,
      POINT = D3DTEXF_POINT,
      LINEAR = D3DTEXF_LINEAR,
      TEX_WRAP = D3DTADDRESS_WRAP,
      TEX_CLAMP = D3DTADDRESS_CLAMP,
      TEX_MIRROR = D3DTADDRESS_MIRROR
   };

   enum PrimitveTypes {
      TRIANGLEFAN = D3DPT_TRIANGLEFAN,
      TRIANGLESTRIP = D3DPT_TRIANGLESTRIP,
      TRIANGLELIST = D3DPT_TRIANGLELIST,
      POINTLIST = D3DPT_POINTLIST,
      LINELIST = D3DPT_LINELIST,
      LINESTRIP = D3DPT_LINESTRIP
   };
   RenderDevice(const HWND hwnd, const int display, const int width, const int height, const bool fullscreen, const int colordepth, int &refreshrate, int VSync, const bool useAA, const int stereo3D, const unsigned int FXAA, const bool ss_refl, const bool useNvidiaApi, const bool disable_dwm, const int BWrendering);
#endif
   void InitVR();

   ~RenderDevice();

   void BeginScene();
   void EndScene();

   void Clear(const DWORD flags, const D3DCOLOR color, const D3DVALUE z, const DWORD stencil);
   void Flip(const bool vsync);

   bool SetMaximumPreRenderedFrames(const DWORD frames);
   
   D3DTexture* GetBackBufferTexture() const { return m_pOffscreenBackBufferTexture; }
   D3DTexture* GetBackBufferTmpTexture() const { return m_pOffscreenBackBufferStereoTexture; }
   D3DTexture* GetOffscreenVR(int eye) const { return eye == 0 ? m_pOffscreenVRLeft : m_pOffscreenVRRight;}
   D3DTexture* GetBackBufferSMAATexture() const { return m_pOffscreenBackBufferSMAATexture; }
   D3DTexture* GetMirrorTmpBufferTexture() const { return m_pMirrorTmpBufferTexture; }
   D3DTexture* GetReflectionBufferTexture() const { return m_pReflectionBufferTexture; }
   RenderTarget* GetOutputBackBuffer() const { return m_pBackBuffer; }

   D3DTexture* GetBloomBufferTexture() const { return m_pBloomBufferTexture; }
   D3DTexture* GetBloomTmpBufferTexture() const { return m_pBloomTmpBufferTexture; }

   RenderTarget* DuplicateRenderTarget(RenderTarget* src);
   D3DTexture* DuplicateTexture(RenderTarget* src);
   D3DTexture* DuplicateTextureSingleChannel(RenderTarget* src);
   D3DTexture* DuplicateDepthTexture(RenderTarget* src);

#ifndef ENABLE_SDL
   void SetRenderTarget(RenderTarget* surf);
   void SetZBuffer(D3DTexture* surf);
   void* AttachZBufferTo(D3DTexture* surfTexture);
#endif
   void SetRenderTarget(D3DTexture* texture, bool ignoreStereo = false);
   void SetZBuffer(RenderTarget* surf);
   void UnSetZBuffer();

   void* AttachZBufferTo(RenderTarget* surf);
   void CopySurface(RenderTarget* dest, RenderTarget* src);
#ifdef ENABLE_SDL
   void CopyDepth(RenderTarget* dest, RenderTarget* src);
#else
   void CopySurface(D3DTexture* dest, RenderTarget* src);
   void CopySurface(RenderTarget* dest, D3DTexture* src);
   void CopySurface(D3DTexture* dest, D3DTexture* src);
   void CopySurface(void* dest, void* src);

   void CopyDepth(D3DTexture* dest, RenderTarget* src);
   void CopyDepth(D3DTexture* dest, D3DTexture* src);
   void CopyDepth(D3DTexture* dest, void* src);
#endif

   bool DepthBufferReadBackAvailable();

#ifndef ENABLE_SDL
   D3DTexture* CreateSystemTexture(BaseTexture* surf, const bool linearRGB);
   D3DTexture* CreateSystemTexture(const int texwidth, const int texheight, const D3DFORMAT texformat, const void* data, const int pitch, const bool linearRGB);
#endif
   D3DTexture* UploadTexture(BaseTexture* surf, int *pTexWidth = NULL, int *pTexHeight = NULL, const bool linearRGB = true);
   void UpdateTexture(D3DTexture* tex, BaseTexture* surf, const bool linearRGB);

   void SetRenderState(const RenderStates p1, DWORD p2);
   bool SetRenderStateCache(const RenderStates p1, DWORD p2);
   void SetRenderStateCulling(RenderStateValue cull);
   void SetRenderStateDepthBias(float bias);
   void SetRenderStateClipPlane0(bool enabled);
   void SetRenderStateAlphaTestFunction(DWORD testValue, RenderStateValue testFunction, bool enabled);

   void SetTextureFilter(const DWORD texUnit, DWORD mode);
   void SetTextureAddressMode(const DWORD texUnit, const SamplerStateValues mode);
#ifndef ENABLE_SDL
   void SetTextureStageState(const DWORD stage, const D3DTEXTURESTAGESTATETYPE type, const DWORD value);
#endif
   void SetSamplerState(const DWORD Sampler, const DWORD minFilter, const DWORD magFilter, const SamplerStateValues mipFilter);
   void SetSamplerAnisotropy(const DWORD Sampler, DWORD Value);

   D3DTexture* CreateTexture(UINT Width, UINT Height, UINT Levels, textureUsage Usage, colorFormat Format, void* data, int stereo);
//   HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, textureUsage Usage, colorFormat Format, memoryPool Pool, D3DTexture** ppTexture, HANDLE* pSharedHandle);

#ifdef ENABLE_SDL
   HRESULT Create3DFont(INT Height, UINT Width, UINT Weight, UINT MipLevels, BOOL Italic, DWORD CharSet, DWORD OutputPrecision, DWORD Quality, DWORD PitchAndFamily, LPCTSTR pFacename, TTF_Font *ppFont);
#else
   HRESULT Create3DFont(INT Height, UINT Width, UINT Weight, UINT MipLevels, BOOL Italic, DWORD CharSet, DWORD OutputPrecision, DWORD Quality, DWORD PitchAndFamily, LPCTSTR pFacename, FontHandle *ppFont);
#endif

   void DrawTexturedQuad();
   void DrawTexturedQuadPostProcess();
   
   void DrawPrimitiveVB(const PrimitveTypes type, const DWORD fvf, VertexBuffer* vb, const DWORD startVertex, const DWORD vertexCount, bool stereo);
   void DrawIndexedPrimitiveVB(const PrimitveTypes type, const DWORD fvf, VertexBuffer* vb, const DWORD startVertex, const DWORD vertexCount, IndexBuffer* ib, const DWORD startIndex, const DWORD indexCount);

   void SetViewport(const ViewPort*);
   void GetViewport(ViewPort*);

   void SetTransformVR();

   void ForceAnisotropicFiltering(const bool enable) { m_force_aniso = enable; }
   void CompressTextures(const bool enable) { m_compress_textures = enable; }

   //VR stuff
   unsigned int getBufwidth() { return m_Buf_width; }
   unsigned int getBufheight() { return m_Buf_width; }
   void UpdateVRPosition();

   // performance counters
   unsigned int Perf_GetNumDrawCalls() const      { return m_frameDrawCalls; }
   unsigned int Perf_GetNumStateChanges() const   { return m_frameStateChanges; }
   unsigned int Perf_GetNumTextureChanges() const { return m_frameTextureChanges; }
   unsigned int Perf_GetNumParameterChanges() const { return m_frameParameterChanges; }
   unsigned int Perf_GetNumTextureUploads() const { return m_frameTextureUpdates; }

   void FreeShader();

   void CreateVertexDeclaration(const VertexElement * const element, VertexDeclaration ** declaration);
   void SetVertexDeclaration(VertexDeclaration * declaration);

#ifndef ENABLE_SDL
   inline IDirect3DDevice9* GetCoreDevice() const
   {
      return m_pD3DDevice;
   }
#endif

private:
   void DrawPrimitive(const PrimitveTypes type, const DWORD fvf, const void* vertices, const DWORD vertexCount);

   void UploadAndSetSMAATextures();
   D3DTexture* m_SMAAsearchTexture;
   D3DTexture* m_SMAAareaTexture;
   int m_stereo3D;

#ifdef ENABLE_SDL
#else
#ifdef USE_D3D9EX
   IDirect3D9Ex* m_pD3DEx;

   IDirect3DDevice9Ex* m_pD3DDeviceEx;
#endif
   IDirect3D9* m_pD3D;
   IDirect3DDevice9* m_pD3DDevice;
#endif

   RenderTarget* m_pBackBuffer;

   //If stereo is enabled the right eye is the right/bottom part with 4px in between
   D3DTexture* m_pOffscreenBackBufferTexture;
   D3DTexture* m_pOffscreenBackBufferStereoTexture; // stereo/FXAA only
   D3DTexture* m_pOffscreenBackBufferSMAATexture;// SMAA only
   D3DTexture* m_pOffscreenVRLeft;
   D3DTexture* m_pOffscreenVRRight;

   D3DTexture* m_pBloomBufferTexture;
   D3DTexture* m_pBloomTmpBufferTexture;
   D3DTexture* m_pMirrorTmpBufferTexture;
   D3DTexture* m_pReflectionBufferTexture;

   UINT m_adapter;      // index of the display adapter to use

   static const DWORD TEXTURE_STATE_CACHE_SIZE = 256;
   static const DWORD TEXTURE_SAMPLER_CACHE_SIZE = 14;

   std::map<RenderStates, DWORD> renderStateCache;          // for caching
   DWORD textureStateCache[8][TEXTURE_STATE_CACHE_SIZE];     // dto.
   DWORD textureSamplerCache[8][TEXTURE_SAMPLER_CACHE_SIZE]; // dto.

   VertexBuffer* m_curVertexBuffer;       // for caching
   IndexBuffer* m_curIndexBuffer;         // dto.
   VertexDeclaration *currentDeclaration; // dto.

   VertexBuffer *m_quadVertexBuffer;      // internal vb for rendering quads
   //VertexBuffer *m_quadDynVertexBuffer;   // internal vb for rendering dynamic quads

   DWORD m_maxaniso;
   bool m_mag_aniso;

   bool m_autogen_mipmap;
   //bool m_RESZ_support;
   bool m_force_aniso;
   bool m_compress_textures;

   bool m_dwm_was_enabled;
   bool m_dwm_enabled;

   unsigned int m_Buf_width;
   unsigned int m_Buf_height;

   unsigned int m_Buf_widthBlur;
   unsigned int m_Buf_heightBlur;

   unsigned int m_Buf_widthSS;
   unsigned int m_Buf_heightSS;


   //VR/Stereo Stuff

#ifdef ENABLE_VR
   vr::IVRSystem *m_pHMD;
   Matrix3D m_matProj[2];
   Matrix3D m_matView;
   Matrix3D m_tableWorld;
   vr::TrackedDevicePose_t *m_rTrackedDevicePose;

#endif

public:
   static bool m_useNvidiaApi;
   static bool m_INTZ_support;

   // performance counters
   unsigned m_curDrawCalls, m_frameDrawCalls;
   unsigned m_curStateChanges, m_frameStateChanges;
   unsigned m_curTextureChanges, m_frameTextureChanges;
   unsigned m_curParameterChanges, m_frameParameterChanges;
   unsigned m_curTextureUpdates, m_frameTextureUpdates;

   Shader *ballShader;
   Shader *basicShader;
   Shader *DMDShader;
   Shader *FBShader;
   Shader *flasherShader;
   Shader *lightShader;
   Shader *StereoShader;
#ifdef SEPARATE_CLASSICLIGHTSHADER
   Shader *classicLightShader;
#else
#define classicLightShader basicShader
#endif

   //Shader* m_curShader; // for caching

   TextureManager m_texMan;

   unsigned int m_stats_drawn_triangles;

   static VertexDeclaration* m_pVertexTexelDeclaration;
   static VertexDeclaration* m_pVertexNormalTexelDeclaration;
   //static VertexDeclaration* m_pVertexNormalTexelTexelDeclaration;
   static VertexDeclaration* m_pVertexTrafoTexelDeclaration;
};

