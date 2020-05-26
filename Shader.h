#pragma once

#ifdef ENABLE_SDL
#include <map>
#include <string>

typedef char* D3DXHANDLE;
typedef void ID3DXEffect;
#endif
class Shader
{
public:
   Shader(RenderDevice *renderDevice);
   ~Shader();

#ifdef ENABLE_SDL
   bool Load(const char* shaderCodeName, UINT codeSize);
#else
   bool Load(const BYTE* shaderCodeName, UINT codeSize);
#endif
   void Unload();

   void Begin(const unsigned int pass);
   void End();

   void SetTexture(const D3DXHANDLE texelName, Texture *texel, const bool linearRGB);
   void SetTexture(const D3DXHANDLE texelName, D3DTexture *texel, const bool linearRGB);
   void SetTextureDepth(const D3DXHANDLE texelName, D3DTexture *texel);
   void SetTextureNull(const D3DXHANDLE texelName);
   void SetMaterial(const Material * const mat);

   void SetDisableLighting(const float value); // only set top
   void SetDisableLighting(const vec4& value); // set top and below
   void SetAlphaTestValue(const float value);
   void SetFlasherColorAlpha(const vec4& color);
   void SetFlasherData(const vec4& color, const float mode);
   void SetLightColorIntensity(const vec4& color);
   void SetLightColor2FalloffPower(const vec4& color);
   void SetLightData(const vec4& color);
   void SetLightImageBackglassMode(const bool imageMode, const bool backglassMode);

   void SetTechnique(const D3DXHANDLE technique);

   void SetMatrix(const D3DXHANDLE hParameter, const Matrix3D* pMatrix);
   void SetUniformBlock(const D3DXHANDLE hParameter, const float* pMatrix, const int size);
   void SetVector(const D3DXHANDLE hParameter, const vec4* pVector);
   void SetVector(const D3DXHANDLE hParameter, const float x, const float y, const float z, const float w);
   void SetFloat(const D3DXHANDLE hParameter, const float f);
   void SetInt(const D3DXHANDLE hParameter, const int i);
   void SetBool(const D3DXHANDLE hParameter, const bool b);
   void SetFloatArray(const D3DXHANDLE hParameter, const float* pData, const unsigned int count);

   static void SetTransform(const TransformStateType p1, const Matrix3D * p2, const int count);
   static void GetTransform(const TransformStateType p1, Matrix3D* p2, const int count);
   static Shader* getCurrentShader();
private:
   static Shader* m_currentShader;
   static RenderDevice *m_renderDevice;

   // caches:

   Material currentMaterial;

   vec4 currentDisableLighting; // x and y: top and below, z and w unused

   static const DWORD TEXTURESET_STATE_CACHE_SIZE = 5; // current convention: SetTexture gets "TextureX", where X 0..4
   BaseTexture *currentTexture[TEXTURESET_STATE_CACHE_SIZE];
   float   currentAlphaTestValue;
   char    currentTechnique[64];

   vec4 currentFlasherColor; // flasher only-data
   vec4 currentFlasherData;
   float currentFlasherMode;

   vec4 currentLightColor; // all light only-data
   vec4 currentLightColor2;
   vec4 currentLightData;
   unsigned int currentLightImageMode;
   unsigned int currentLightBackglassMode;
   static int shaderCount;
   int shaderID;
   int currentShader;

#ifdef ENABLE_SDL
   bool compileGLShader(const char* fileNameRoot, string shaderCodeName, string vertex, string geometry, string fragment);

   struct attributeLoc {
      GLenum type;
      int location;
      int size;
   };
   struct uniformLoc {
      GLenum type;
      int location;
      int size;
      GLuint blockBuffer;
   };
   struct glShader {
      int program;
      std::map<string, attributeLoc> *attributeLocation;
      std::map<string, uniformLoc> *uniformLocation;
   };
   struct floatP {
      size_t len;
      float* data;
   };
   std::map<string, glShader> shaderList;
   std::map<string, float> uniformFloat;
   std::map<string, floatP> uniformFloatP;
   std::map<string, int> uniformInt;
   std::map<string, int> uniformTex;
   char technique[256];
   static Matrix3D mWorld, mView, mProj[2];
   static int lastShaderProgram;
   static D3DTexture* noTexture;
   static D3DTexture* noTextureMSAA;
   static float* zeroData;
   glShader* m_currentTechnique;
   static int nextTextureSlot;
   static int* textureSlotList;
   static std::map<int, int> slotTextureList;
   static int maxSlots;
public:
   void setAttributeFormat(DWORD fvf);
   static std::string shaderPath;
   static std::string Defines;
   static void setTextureDirty(int TextureID);
#else
   ID3DXEffect * m_shader;
#endif
};
