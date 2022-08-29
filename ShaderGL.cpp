#include "stdafx.h"
#include "Shader.h"
#include "typedefs3D.h"
#include "RenderDevice.h"

#include <Windows.h>

#include <iostream>
#include <fstream>
#include <string>
#include <inc/robin_hood.h>
#include <regex>

#if DEBUG_LEVEL_LOG == 0
#define LOG(a,b,c)
#endif

static std::ofstream* logFile = nullptr;
string Shader::shaderPath;
string Shader::Defines;
Matrix3D Shader::mWorld, Shader::mView, Shader::mProj[2];
int Shader::lastShaderProgram = -1;
Sampler* Shader::noTexture = nullptr;
Sampler* Shader::noTextureMSAA = nullptr;
static const float zeroValues[16] = { 0.0f,0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
const float* Shader::zeroData = zeroValues;
int Shader::nextTextureSlot = 0;
int* Shader::textureSlotList = nullptr;
//std::map<int, int> Shader::slotTextureList;
int Shader::maxSlots = 0;

#ifdef TWEAK_GL_SHADER
//Todo: Optimize to improve shader loading time.
static const string shaderUniformNames[SHADER_UNIFORM_COUNT]{
   "blend_modulate_vs_add", "alphaTestValue", "eye", "fKickerScale",
   //Vectors and Float Arrays
   "Roughness_WrapL_Edge_Thickness", "cBase_Alpha", "lightCenter_maxRange", "lightColor2_falloff_power", "lightColor_intensity", "matrixBlock", "fenvEmissionScale_TexWidth",
   "invTableRes_playfield_height_reflection", "lightEmission", "lightPos", "orientation", "cAmbient_LightRange", "cClearcoat_EdgeAlpha", "cGlossy_ImageLerp",
   "fDisableLighting_top_below", "backBoxSize", "quadOffsetScale", "quadOffsetScaleTex", "vColor_Intensity", "w_h_height", "alphaTestValueAB_filterMode_addBlend",
   "amount_blend_modulate_vs_add_flasherMode", "staticColor_Alpha", "width_height_rotated_flipLR", "vRes_Alpha_time", "mirrorFactor", "SSR_bumpHeight_fresnelRefl_scale_FS", "AO_scale_timeblur",
   //Integer and Bool
   "ignoreStereo", "disableLighting", "lightSources", "doNormalMapping", "is_metal", "color_grade", "do_bloom", "lightingOff", "objectSpaceNormalMap", "do_dither", //!! disableLighting not wired/used yet in shaders
   //Textures
   "Texture0", "Texture1", "Texture2", "Texture3", "Texture4", "edgesTex2D", "blendTex2D", "areaTex2D", "searchTex2D"
};

static const string shaderAttributeNames[SHADER_ATTRIBUTE_COUNT]{
   "vPosition", "vNormal", "tc", "tex0"
};

static const string shaderTechniqueNames[SHADER_TECHNIQUE_COUNT]{
   "RenderBall", "RenderBall_DecalMode", "RenderBall_CabMode", "RenderBall_CabMode_DecalMode", "RenderBallTrail",
   "basic_without_texture", "basic_with_texture", "basic_depth_only_without_texture", "basic_depth_only_with_texture", "bg_decal_without_texture",
   "bg_decal_with_texture", "kickerBoolean", "light_with_texture", "light_without_texture",
   "basic_DMD", "basic_DMD_ext", "basic_DMD_world", "basic_DMD_world_ext", "basic_noDMD", "basic_noDMD_world", "basic_noDMD_notex",
   "AO", "NFAA", "DLAA_edge", "DLAA", "FXAA1", "FXAA2", "FXAA3", "fb_tonemap", "fb_bloom",
   "fb_AO", "fb_tonemap_AO", "fb_tonemap_AO_static", "fb_tonemap_no_filterRGB", "fb_tonemap_no_filterRG", "fb_tonemap_no_filterR",
   "fb_tonemap_AO_no_filter", "fb_tonemap_AO_no_filter_static", "fb_bloom_horiz9x9", "fb_bloom_vert9x9", "fb_bloom_horiz19x19", "fb_bloom_vert19x19",
   "fb_bloom_horiz19x19h", "fb_bloom_vert19x19h", "SSReflection", "fb_mirror", "basic_noLight", "bulb_light",
   "SMAA_ColorEdgeDetection", "SMAA_BlendWeightCalculation", "SMAA_NeighborhoodBlending",
   "stereo_TB", "stereo_SBS", "stereo_Int", "stereo_AMD_DEBUG"
};

shaderUniforms Shader::getUniformByName(const string& name) {
   for (int i = 0;i < SHADER_UNIFORM_COUNT; ++i)
      if (name == shaderUniformNames[i])
         return shaderUniforms(i);

   LOG(1, m_shaderCodeName, string("getUniformByName Could not find uniform ").append(name).append(" in shaderUniformNames."));
   return SHADER_UNIFORM_INVALID;
}

shaderAttributes Shader::getAttributeByName(const string& name) {
   for (int i = 0;i < SHADER_ATTRIBUTE_COUNT; ++i)
      if (name == shaderAttributeNames[i])
         return shaderAttributes(i);

   LOG(1, m_shaderCodeName, string("getAttributeByName Could not find attribute ").append(name).append(" in shaderAttributeNames."));
   return SHADER_ATTRIBUTE_INVALID;
}

shaderTechniques Shader::getTechniqueByName(const string& name) {
   for (int i = 0;i < SHADER_TECHNIQUE_COUNT; ++i)
      if (name == shaderTechniqueNames[i])
         return shaderTechniques(i);

   LOG(1, m_shaderCodeName, string("getTechniqueByName: Could not find technique ").append(name).append(" in shaderTechniqueNames."));
   return SHADER_TECHNIQUE_INVALID;
}

#endif

Shader::~Shader()
{
   shaderCount--;
   this->Unload();
   if (shaderCount == 0) {
      delete [] textureSlotList;
      textureSlotList = nullptr;
      maxSlots = 0;
      nextTextureSlot = 0;
      delete noTexture;
      noTexture = nullptr;
   }
   //slotTextureList.clear();
   if (m_currentShader == this)
      m_currentShader = nullptr;
   delete m_nullTexture;
}

#if DEBUG_LEVEL_LOG > 0
void Shader::LOG(const int level, const string& fileNameRoot, const string& message) {
   if (level <= DEBUG_LEVEL_LOG) {
      if (!logFile) {
         string name = Shader::shaderPath;
         name.append("log\\").append(fileNameRoot).append(".log");
         logFile = new std::ofstream();
bla:
         logFile->open(name);
         if (!logFile->is_open()) {
            const wstring wzMkPath = g_pvp->m_wzMyPath + L"glshader";
            if (_wmkdir(wzMkPath.c_str()) != 0 || _wmkdir((wzMkPath + L"\\log").c_str()) != 0)
            {
                char msg[512];
                TCHAR full_path[MAX_PATH];
                GetFullPathName(_T(name.c_str()), MAX_PATH, full_path, nullptr);
                sprintf_s(msg, sizeof(msg), "Could not create logfile %s", full_path);
                ShowError(msg);
            }
            else
                goto bla;
         }
      }
      switch (level) {
      case 1:
         (*logFile) << "E:";
         break;
      case 2:
         (*logFile) << "W:";
         break;
      case 3:
         (*logFile) << "I:";
         break;
      default:
         (*logFile) << level << ':';
         break;
      }
      (*logFile) << message << '\n';
   }
}
#endif

//parse a file. Is called recursively for includes
bool Shader::parseFile(const string& fileNameRoot, const string& fileName, int level, robin_hood::unordered_map<string, string> &values, const string& parentMode) {
   if (level > 16) {//Can be increased, but looks very much like an infinite recursion.
      LOG(1, fileNameRoot, string("Reached more than 16 includes while trying to include ").append(fileName).append(" Aborting..."));
      return false;
   }
   if (level > 8) {
      LOG(2, fileNameRoot, string("Reached include level ").append(std::to_string(level)).append(" while trying to include ").append(fileName).append(" Check for recursion and try to avoid includes with includes."));
   }
   string currentMode = parentMode;
   robin_hood::unordered_map<string, string>::iterator currentElemIt = values.find(parentMode);
   string currentElement = (currentElemIt != values.end()) ? currentElemIt->second : string();
   std::ifstream glfxFile;
   glfxFile.open(string(Shader::shaderPath).append(fileName), std::ifstream::in);
   if (glfxFile.is_open())
   {
      string line;
      size_t linenumber = 0;
      while (getline(glfxFile, line))
      {
         linenumber++;
         if (line.compare(0, 4, "////") == 0) {
            string newMode = line.substr(4, line.length() - 4);
            if (newMode == "DEFINES") {
               currentElement.append(Shader::Defines).append("\n");
            } else if (newMode != currentMode) {
               values[currentMode] = currentElement;
               currentElemIt = values.find(newMode);
               currentElement = (currentElemIt != values.end()) ? currentElemIt->second : string();
               currentMode = newMode;
            }
         }
         else if (line.compare(0, 9, "#include ") == 0) {
            const size_t start = line.find('"', 8);
            const size_t end = line.find('"', start + 1);
            values[currentMode] = currentElement;
            if ((start == string::npos) || (end == string::npos) || (end <= start) || !parseFile(fileNameRoot, line.substr(start + 1, end - start - 1), level + 1, values, currentMode)) {
               LOG(1, fileNameRoot, fileName + "(" + std::to_string(linenumber) + "):" + line + " failed.");
            }
            currentElement = values[currentMode];
         }
         else {
            currentElement.append(line).append("\n");
         }
      }
      values[currentMode] = currentElement;
      glfxFile.close();
   }
   else {
      LOG(1, fileNameRoot, fileName + " not found.");
      return false;
   }
   return true;
}

//compile and link shader. Also write the created shader files
bool Shader::compileGLShader(const string& fileNameRoot, const string& shaderCodeName, const string& vertex, const string& geometry, const string& fragment) {
   bool success = true;
   GLuint geometryShader = 0;
   GLchar* geometrySource = nullptr;
   GLuint fragmentShader = 0;
   GLchar* fragmentSource = nullptr;

   //Vertex Shader
   GLchar* vertexSource = new GLchar[vertex.length() + 1];
   memcpy((void*)vertexSource, vertex.c_str(), vertex.length());
   vertexSource[vertex.length()] = 0;

   GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
   glShaderSource(vertexShader, 1, &vertexSource, nullptr);
   glCompileShader(vertexShader);

   int result;
   glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &result);
   if (result == FALSE)
   {
      GLint maxLength;
      glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &maxLength);
      char* errorText = (char *)malloc(maxLength);

      glGetShaderInfoLog(vertexShader, maxLength, &maxLength, errorText);
      LOG(1, fileNameRoot, string(shaderCodeName).append(": Vertex Shader compilation failed with: ").append(errorText));
      char msg[2048];
      sprintf_s(msg, sizeof(msg), "Fatal Error: Vertex Shader compilation of %s:%s failed!\n\n%s", fileNameRoot.c_str(), shaderCodeName.c_str(),errorText);
      ReportError(msg, -1, __FILE__, __LINE__);
      free(errorText);
      success = false;
   }
   //Geometry Shader
   if (success && geometry.length()>0) {
      geometrySource = new GLchar[geometry.length() + 1];
      memcpy((void*)geometrySource, geometry.c_str(), geometry.length());
      geometrySource[geometry.length()] = 0;

      geometryShader = glCreateShader(GL_GEOMETRY_SHADER);
      glShaderSource(geometryShader, 1, &geometrySource, nullptr);
      glCompileShader(geometryShader);

      glGetShaderiv(geometryShader, GL_COMPILE_STATUS, &result);
      if (result == FALSE)
      {
         GLint maxLength;
         glGetShaderiv(geometryShader, GL_INFO_LOG_LENGTH, &maxLength);
         char* errorText = (char *)malloc(maxLength);

         glGetShaderInfoLog(geometryShader, maxLength, &maxLength, errorText);
         LOG(1, fileNameRoot, string(shaderCodeName).append(": Geometry Shader compilation failed with: ").append(errorText));
         char msg[2048];
         sprintf_s(msg, sizeof(msg), "Fatal Error: Geometry Shader compilation of %s:%s failed!\n\n%s", fileNameRoot.c_str(), shaderCodeName.c_str(), errorText);
         ReportError(msg, -1, __FILE__, __LINE__);
         free(errorText);
         success = false;
      }
   }
   //Fragment Shader
   if (success) {
      fragmentSource = new GLchar[fragment.length() + 1];
      memcpy((void*)fragmentSource, fragment.c_str(), fragment.length());
      fragmentSource[fragment.length()] = 0;

      fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
      glShaderSource(fragmentShader, 1, &fragmentSource, nullptr);
      glCompileShader(fragmentShader);

      glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &result);
      if (result == FALSE)
      {
         GLint maxLength;
         glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &maxLength);
         char* errorText = (char *)malloc(maxLength);

         glGetShaderInfoLog(fragmentShader, maxLength, &maxLength, errorText);
         LOG(1, fileNameRoot, shaderCodeName + ": Fragment Shader compilation failed with: " + errorText);
         char msg[2048];
         sprintf_s(msg, sizeof(msg), "Fatal Error: Fragment Shader compilation of %s:%s failed!\n\n%s", fileNameRoot.c_str(), shaderCodeName.c_str(), errorText);
         ReportError(msg, -1, __FILE__, __LINE__);
         free(errorText);
         success = false;
      }
   }

   GLuint shaderprogram = 0;
   if (success) {
      shaderprogram = glCreateProgram();

      glAttachShader(shaderprogram, vertexShader);
      if (geometryShader>0) glAttachShader(shaderprogram, geometryShader);
      glAttachShader(shaderprogram, fragmentShader);

      glLinkProgram(shaderprogram);

      glGetProgramiv(shaderprogram, GL_LINK_STATUS, (int *)&result);
      if (result == FALSE)
      {
         GLint maxLength;
         glGetProgramiv(shaderprogram, GL_INFO_LOG_LENGTH, &maxLength);

         /* The maxLength includes the NULL character */
         char* errorText = (char *)malloc(maxLength);

         /* Notice that glGetProgramInfoLog, not glGetShaderInfoLog. */
         glGetProgramInfoLog(shaderprogram, maxLength, &maxLength, errorText);
         LOG(1, fileNameRoot, string(shaderCodeName).append(": Linking Shader failed with: ").append(errorText));
         free(errorText);
         success = false;
      }
   }
   if ((WRITE_SHADER_FILES == 2) || ((WRITE_SHADER_FILES == 1) && !success)) {
      std::ofstream shaderCode;
      shaderCode.open(string(shaderPath).append("log\\").append(shaderCodeName).append(".vert"));
      shaderCode << vertex;
      shaderCode.close();
      shaderCode.open(string(shaderPath).append("log\\").append(shaderCodeName).append(".geom"));
      shaderCode << geometry;
      shaderCode.close();
      shaderCode.open(string(shaderPath).append("log\\").append(shaderCodeName).append(".frag"));
      shaderCode << fragment;
      shaderCode.close();
   }
   glDeleteShader(vertexShader);
   glDeleteShader(geometryShader);
   glDeleteShader(fragmentShader);
   delete [] fragmentSource;
   delete [] geometrySource;
   delete [] vertexSource;

   if (success) {
      int count = 0;
      glShader shader;
#ifdef TWEAK_GL_SHADER
      shader.program = -1;
      for (int i = 0; i < SHADER_ATTRIBUTE_COUNT; ++i) {
         shader.attributeLocation[i] = { 0, -1, 0 };
      }
      for (int i = 0; i < SHADER_UNIFORM_COUNT; ++i) {
         shader.uniformLocation[i] = { 0, -1, 0, 0 };
      }
#endif
      shader.program = shaderprogram;

      glGetProgramiv(shaderprogram, GL_ACTIVE_UNIFORMS, &count);
      char uniformName[256];
#ifndef TWEAK_GL_SHADER
      shader.uniformLocation = new std::map<string, uniformLoc>; //!! unordered_map?
#endif
      for (int i = 0;i < count;++i) {
         GLenum type;
         int size;
         int length;
         glGetActiveUniform(shader.program, (GLuint)i, 256, &length, &size, &type, uniformName);
         int location = glGetUniformLocation(shader.program, uniformName);
         if (location >= 0 && size>0) {
            uniformLoc newLoc = {};
            newLoc.location = location;
            newLoc.type = type;
            //hack for packedLights, but works for all arrays
            newLoc.size = size;
            for (int i2 = 0;i2 < length;i2++) {
               if (uniformName[i2] == '[') {
                  uniformName[i2] = 0;
                  break;
               }
            }
#ifdef TWEAK_GL_SHADER
            auto uniformIndex = getUniformByName(uniformName);
            if (uniformIndex < SHADER_UNIFORM_COUNT) shader.uniformLocation[uniformIndex] = newLoc;
#else
            shader.uniformLocation->operator[](uniformName) = newLoc;
#endif
         }
      }

      glGetProgramiv(shaderprogram, GL_ACTIVE_UNIFORM_BLOCKS, &count);
      for (int i = 0;i < count;++i) {
         int size;
         int length;
         glGetActiveUniformBlockName(shader.program, (GLuint)i, 256, &length, uniformName);
         glGetActiveUniformBlockiv(shader.program, (GLuint)i, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
         int location = glGetUniformBlockIndex(shader.program, uniformName);
         if (location >= 0 && size>0) {
            uniformLoc newLoc = {};
            newLoc.location = location;
            newLoc.type = ~0u;
            glGenBuffers(1, &newLoc.blockBuffer);
            //hack for packedLights, but works for all arrays - I don't need it for uniform blocks now and I'm not sure if it makes any sense, but maybe someone else in the future?
            newLoc.size = size;
            for (int i2 = 0;i2 < length;i2++) {
               if (uniformName[i2] == '[') {
                  uniformName[i2] = 0;
                  break;
               }
            }
#ifdef TWEAK_GL_SHADER
            auto uniformIndex = getUniformByName(uniformName);
            if (uniformIndex < SHADER_UNIFORM_COUNT) shader.uniformLocation[uniformIndex] = newLoc;
#else
            shader.uniformLocation->operator[](uniformName) = newLoc;
#endif
         }
      }

      glGetProgramiv(shaderprogram, GL_ACTIVE_ATTRIBUTES, &count);
#ifdef TWEAK_GL_SHADER
      for (int i = 0; i < SHADER_ATTRIBUTE_COUNT; ++i) shader.attributeLocation[i] = { 0, -1, 0};
#else
      shader.attributeLocation = new std::map<string, attributeLoc>; //!! unordered_map?
#endif
      for (int i = 0;i < count;++i) {
         GLenum type;
         int size;
         int length;
         char attributeName[256];
         glGetActiveAttrib(shader.program, (GLuint)i, 256, &length, &size, &type, attributeName);
         int location = glGetAttribLocation(shader.program, attributeName);
         if (location >= 0) {
            attributeLoc newLoc = {};
            newLoc.location = location;
            newLoc.type = type;
            switch (type) {
            case GL_FLOAT_VEC2:
               newLoc.size = 2 * size;
               break;
            case GL_FLOAT_VEC3:
               newLoc.size = 3 * size;
               break;
            case GL_FLOAT_VEC4:
               newLoc.size = 4 * size;
               break;
            default:
               newLoc.size = size;
               break;
            }
#ifdef TWEAK_GL_SHADER
            auto index = getAttributeByName(attributeName);
            if (index < SHADER_ATTRIBUTE_COUNT) shader.attributeLocation[index] = newLoc;
#else
            shader.attributeLocation->operator[](attributeName) = newLoc;
#endif
         }
      }
#ifdef TWEAK_GL_SHADER
      auto techniqueIndex = getTechniqueByName(shaderCodeName);
      if (techniqueIndex < SHADER_TECHNIQUE_COUNT) shaderList[techniqueIndex] = shader;
#else
      shaderList.insert(std::pair<string, glShader>(shaderCodeName, shader));
#endif
   }
   return success;
}

//Check if technique is valid and replace %PARAMi% with the values in the function header
string Shader::analyzeFunction(const char* shaderCodeName, const string& _technique, const string& functionName, const robin_hood::unordered_map<string, string> &values) {
   const size_t start = functionName.find('(');
   const size_t end = functionName.find(')');
   if ((start == string::npos) || (end == string::npos) || (start > end)) {
      LOG(2, (const char*)shaderCodeName, string("Invalid technique: ").append(_technique));
      return string();
   }
   const robin_hood::unordered_map<string, string>::const_iterator it = values.find(functionName.substr(0, start));
   string functionCode = (it != values.end()) ? it->second : string();
   if (end > start + 1) {
      std::stringstream params(functionName.substr(start + 1, end - start - 1));
      string param;
      int paramID = 0;
      while (std::getline(params, param, ',')) {
         functionCode = std::regex_replace(functionCode, std::regex(string("%PARAM").append(std::to_string(paramID)).append("%")), param);
         paramID++;
      }
   }
   return functionCode;
}

bool Shader::Load(const char* shaderCodeName, UINT codeSize)
{
   m_shaderCodeName = shaderCodeName;
   if (!textureSlotList) {
      glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxSlots);
      textureSlotList = new int[maxSlots];
      for (int i = 0;i < maxSlots;++i)
         textureSlotList[i] = -2;
   }
   m_currentTechnique = nullptr;
   LOG(3, (const char*)shaderCodeName, "Start parsing file");
   robin_hood::unordered_map<string, string> values;
   const bool parsing = parseFile(m_shaderCodeName, m_shaderCodeName, 0, values, "GLOBAL");
   if (!parsing) {
      LOG(1, (const char*)shaderCodeName, "Parsing failed");
      char msg[128];
      sprintf_s(msg, sizeof(msg), "Fatal Error: Shader parsing of %s failed!", shaderCodeName);
      ReportError(msg, -1, __FILE__, __LINE__);
      if (logFile)
         logFile->close();
      return false;
   }
   else {
      LOG(3, (const char*)shaderCodeName, "Parsing successful. Start compiling shaders");
   }
   robin_hood::unordered_map<string, string>::iterator it = values.find("GLOBAL");
   string global = (it != values.end()) ? it->second : string();

   it = values.find("VERTEX");
   string vertex = global;
   vertex.append((it != values.end()) ? it->second : string());

   it = values.find("GEOMETRY");
   string geometry = global;
   geometry.append((it != values.end()) ? it->second : string());

   it = values.find("FRAGMENT");
   string fragment = global;
   fragment.append((it != values.end()) ? it->second : string());

   it = values.find("TECHNIQUES");
   std::stringstream techniques((it != values.end()) ? it->second : string());
   if (techniques)
   {
      string _technique;
      int tecCount = 0;
      while (std::getline(techniques, _technique, '\n')) {//Parse Technique e.g. basic_with_texture:P0:vs_main():gs_optional_main():ps_main_texture()
         if ((_technique.length() > 0) && (_technique.compare(0, 2, "//") != 0))//Skip empty lines and comments
         {
            std::stringstream elements(_technique);
            int elem = 0;
            string element[5];
            //Split :
            while ((elem < 5) && std::getline(elements, element[elem], ':')) {
               elem++;
            }
            if (elem < 4) {
               continue;
            }
            string vertexShaderCode = vertex;
            vertexShaderCode.append("\n//").append(_technique).append("\n//").append(element[2]).append("\n");
            vertexShaderCode.append(analyzeFunction(shaderCodeName, _technique, element[2], values)).append("\0");
            string geometryShaderCode;
            if (elem == 5 && element[3].length() > 0) {
               geometryShaderCode = geometry;
               geometryShaderCode.append("\n//").append(_technique).append("\n//").append(element[3]).append("\n");
               geometryShaderCode.append(analyzeFunction(shaderCodeName, _technique, element[3], values)).append("\0");
            }
            string fragmentShaderCode = fragment;
            fragmentShaderCode.append("\n//").append(_technique).append("\n//").append(element[elem-1]).append("\n");
            fragmentShaderCode.append(analyzeFunction(shaderCodeName, _technique, element[elem-1], values)).append("\0");
            const bool build = compileGLShader(shaderCodeName, element[0]/*.append("_").append(element[1])*/, vertexShaderCode, geometryShaderCode, fragmentShaderCode);
            if(!build)
            {
               char msg[128];
               sprintf_s(msg, sizeof(msg), "Fatal Error: Shader compilation failed for %s!", shaderCodeName);
               ReportError(msg, -1, __FILE__, __LINE__);
               if (logFile)
                  logFile->close();
               return false;
            }
            tecCount++;
         }
      }
      LOG(3, (const char*)shaderCodeName, string("Compiled successfully ").append(std::to_string(tecCount)).append(" shaders."));
   }
   else {
      LOG(1, (const char*)shaderCodeName, "No techniques found.");
      char msg[128];
      sprintf_s(msg, sizeof(msg), "Fatal Error: No shader techniques found in %s!", shaderCodeName);
      ReportError(msg, -1, __FILE__, __LINE__);
      if (logFile)
         logFile->close();
      return false;
   }

   if (logFile)
      logFile->close();
   logFile = nullptr;

   //Set default values from Material.fxh for uniforms.
   SetVector(SHADER_cBase_Alpha, 0.5f, 0.5f, 0.5f, 1.0f);
   SetVector(SHADER_Roughness_WrapL_Edge_Thickness, 4.0f, 0.5f, 1.0f, 0.05f);
   return true;
}

void Shader::Unload()
{
#ifdef TWEAK_GL_SHADER
   for (int i = 0; i < SHADER_UNIFORM_COUNT; ++i)
      if(uniformFloatP[i].data)
         free(uniformFloatP[i].data);
#else
   //Free all uniform cache pointers
   if (uniformFloatP.size() > 0)
      for (auto it = uniformFloatP.begin(); it != uniformFloatP.end(); it++)
         if (it->second.data)
            free(it->second.data);
   uniformFloatP.clear();
   //Delete all glPrograms and their uniformLocation cache
   if (shaderList.size() > 0)
      for (auto it = shaderList.begin(); it != shaderList.end(); it++)
      {
         glDeleteProgram(it->second.program);
         it->second.uniformLocation->clear();
      }
   shaderList.clear();
#endif
}

void Shader::setAttributeFormat(DWORD fvf)
{
   if (!m_currentTechnique) {
      return;
   }
#ifdef TWEAK_GL_SHADER
   for (int i = 0; i < SHADER_ATTRIBUTE_COUNT; ++i)
   {
      const int location = m_currentTechnique->attributeLocation[i].location;
      if (location >= 0) {
         size_t offset;
         glEnableVertexAttribArray(m_currentTechnique->attributeLocation[i].location);
         switch (i) {
         case SHADER_ATTRIBUTE_POS:
            offset = 0;
            break;
         case SHADER_ATTRIBUTE_NORM:
            offset = 12;
            break;
         case SHADER_ATTRIBUTE_TC:
         case SHADER_ATTRIBUTE_TEX:
            offset = (fvf == MY_D3DFVF_TEX) ? 12 : 24;
            break;
         default:
            ReportError("Unknown Attribute", 666, __FILE__, __LINE__);
            offset = 0;
            break;
         }
         glVertexAttribPointer(m_currentTechnique->attributeLocation[i].location, m_currentTechnique->attributeLocation[i].size, GL_FLOAT, GL_FALSE, (fvf == MY_D3DFVF_TEX) ? 20 : 32, (void*)offset);
      }
   }
#else
   for (auto it = m_currentTechnique->attributeLocation->begin(); it != m_currentTechnique->attributeLocation->end(); it++)
   {
      attributeLoc currentAttribute = it->second;
      glEnableVertexAttribArray(currentAttribute.location);
      int offset;
      int size;
      switch (fvf) {
      case MY_D3DFVF_TEX:
         if (it->first == SHADER_ATTRIBUTE_POS) offset = 0;
         else if (it->first == SHADER_ATTRIBUTE_TC) offset = 12;
         else if (it->first == SHADER_ATTRIBUTE_TEX) offset = 12;
         else {
            ReportError("Unknown Attribute", 666, __FILE__, __LINE__);
            exit(-1);
         }
         size = sizeof(float)*(3 + 2);
         break;
      case MY_D3DFVF_NOTEX2_VERTEX:
      case MY_D3DTRANSFORMED_NOTEX2_VERTEX:
         if (it->first == SHADER_ATTRIBUTE_POS) offset = 0;
         else if (it->first == SHADER_ATTRIBUTE_NORM) offset = 12;
         else if (it->first == SHADER_ATTRIBUTE_TC) offset = 24;
         else if (it->first == SHADER_ATTRIBUTE_TEX) offset = 24;
         else {
            ReportError("Unknown Attribute", 666, __FILE__, __LINE__);
            exit(-1);
         }
         size = sizeof(float)*(3 + 3 + 2);
         break;
      default:
         //broken?
         ReportError("Unknown Attribute configuration", 666,__FILE__, __LINE__);
         exit(-1);
      }
      glVertexAttribPointer(currentAttribute.location, currentAttribute.size, GL_FLOAT, GL_FALSE, (fvf == MY_D3DFVF_TEX) ? 20 : 32, (void*)offset;
   }
#endif
}

void Shader::Begin(const unsigned int pass)
{
   m_currentShader = this;
   char msg[256];
#ifdef TWEAK_GL_SHADER
   if (technique >= SHADER_TECHNIQUE_COUNT) {
      sprintf_s(msg, sizeof(msg), "Could not find shader technique ID %i", technique);
      ShowError(msg);
      exit(-1);
   }
   m_currentTechnique = &shaderList[technique];
   if (m_currentTechnique->program == -1) {
      sprintf_s(msg, sizeof(msg), "Could not find shader technique %s", shaderTechniqueNames[technique].c_str());
      ShowError(msg);
      exit(-1);
   }
#else
   string techName = string(technique);

   auto tec = shaderList.find(techName);
   if (tec == shaderList.end()) {
      sprintf_s(msg, sizeof(msg), "Could not find shader technique %s", technique);
      ShowError(msg);
      exit(-1);
   }
   m_currentTechnique = &(tec->second);
#endif

   if (lastShaderProgram != m_currentTechnique->program)
   {
      nextTextureSlot = 0;
      glUseProgram(m_currentTechnique->program);
      lastShaderProgram = m_currentTechnique->program;
   }
   else
      return;

   //Set all uniforms
#ifdef TWEAK_GL_SHADER
   for (int uniformName = 0; uniformName < SHADER_UNIFORM_COUNT; ++uniformName)
   {
      const uniformLoc currentUniform = m_currentTechnique->uniformLocation[uniformName];
      if (currentUniform.location < 0 || currentUniform.type == 0 || currentUniform.size == 0) continue;
#else
   for (auto it = m_currentTechnique->uniformLocation->begin(); it != m_currentTechnique->uniformLocation->end(); it++)
   {
      uniformLoc currentUniform = it->second;
      string uniformName = it->first;
#endif
      switch (currentUniform.type) {
      case ~0u: {//Uniform blocks
#ifdef TWEAK_GL_SHADER
         const auto valueFP = uniformFloatP[uniformName];
#else
         const auto valueFP = uniformFloatP.find(uniformName)->second;
#endif
         glBindBuffer(GL_UNIFORM_BUFFER, currentUniform.blockBuffer);
         glBufferData(GL_UNIFORM_BUFFER, currentUniform.size, valueFP.data, GL_STREAM_DRAW);
         glUniformBlockBinding(lastShaderProgram, currentUniform.location, 0);
         glBindBufferRange(GL_UNIFORM_BUFFER, 0, currentUniform.blockBuffer, 0, currentUniform.size);
      }
      break;
      case GL_FLOAT:
      {
#ifdef TWEAK_GL_SHADER
         const float valueF = uniformFloat[uniformName];
#else
         auto entry = uniformFloat.find(uniformName);
         auto valueF = (entry != uniformFloat.end()) ? entry->second : 0.0f;
#endif
         glUniform1f(currentUniform.location, valueF);
      }
      break;
      case GL_BOOL:
      case GL_INT:
      {
#ifdef TWEAK_GL_SHADER
         const int valueI = uniformInt[uniformName];
#else
         const auto entry = uniformInt.find(uniformName);
         const auto valueI = (entry != uniformInt.end()) ? entry->second : 0;
#endif
         glUniform1i(currentUniform.location, valueI);
      }
      break;
      case GL_FLOAT_VEC2:
      {
#ifdef TWEAK_GL_SHADER
         auto valueFP = uniformFloatP[uniformName].data;
         if (valueFP)
            glUniform2f(currentUniform.location, valueFP[0], valueFP[1]);
#else
         auto valueFP = uniformFloatP.find(uniformName);
         if ((valueFP != uniformFloatP.end()) && valueFP->second.data)
            glUniform2f(currentUniform.location, valueFP->second.data[0], valueFP->second.data[1]);
#endif
         else
            glUniform2f(currentUniform.location, 0.0f, 0.0f);
      }
      break;
      case GL_FLOAT_VEC3:
      {
#ifdef TWEAK_GL_SHADER
         auto valueFP = uniformFloatP[uniformName].data;
         if (valueFP)
            glUniform3f(currentUniform.location, valueFP[0], valueFP[1], valueFP[2]);
#else
         auto valueFP = uniformFloatP.find(uniformName);
         if ((valueFP != uniformFloatP.end()) && valueFP->second.data)
            glUniform3f(currentUniform.location, valueFP->second.data[0], valueFP->second.data[1], valueFP->second.data[2]);
#endif
         else
            glUniform3f(currentUniform.location, 0.0f, 0.0f, 0.0f);
      }
      break;
      case GL_FLOAT_VEC4:
      {
#ifdef TWEAK_GL_SHADER
         auto valueFP = uniformFloatP[uniformName].data;
         if (valueFP)
         {
             if (uniformFloatP[uniformName].len > 4)
                 glUniform4fv(currentUniform.location, uniformFloatP[uniformName].len / 4, uniformFloatP[uniformName].data);
             else
                 glUniform4f(currentUniform.location, valueFP[0], valueFP[1], valueFP[2], valueFP[3]);
         }
#else
         auto valueFP = uniformFloatP.find(uniformName);
         if ((valueFP != uniformFloatP.end()) && valueFP->second.data)
            glUniform4fv(currentUniform.location, valueFP->second.len/4, valueFP->second.data);
#endif
         else 
            glUniform4f(currentUniform.location, 0.0f, 0.0f, 0.0f, 0.0f);
      }
      break;
      case GL_FLOAT_MAT2:
      {
#ifdef TWEAK_GL_SHADER
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
#else
         const auto entry = uniformFloatP.find(uniformName);
         const auto valueFP = ((entry != uniformFloatP.end()) && entry->second.data) ? entry->second.data : zeroData;
#endif
         glUniformMatrix2fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_FLOAT_MAT3:
      {
#ifdef TWEAK_GL_SHADER
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
#else
         const auto entry = uniformFloatP.find(uniformName);
         const auto valueFP = ((entry != uniformFloatP.end()) && entry->second.data) ? entry->second.data : zeroData;
#endif
         glUniformMatrix3fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_FLOAT_MAT4:
      {
#ifdef TWEAK_GL_SHADER
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
#else
         const auto entry = uniformFloatP.find(uniformName);
         const auto valueFP = ((entry != uniformFloatP.end()) && entry->second.data) ? entry->second.data : zeroData;
#endif
         glUniformMatrix4fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_FLOAT_MAT4x3:
      {
#ifdef TWEAK_GL_SHADER
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
#else
         const auto entry = uniformFloatP.find(uniformName);
         const auto valueFP = ((entry != uniformFloatP.end()) && entry->second.data) ? entry->second.data : zeroData;
#endif
         glUniformMatrix4x3fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_FLOAT_MAT4x2:
      {
#ifdef TWEAK_GL_SHADER
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
#else
         const auto entry = uniformFloatP.find(uniformName);
         const auto valueFP = ((entry != uniformFloatP.end()) && entry->second.data) ? entry->second.data : zeroData;
#endif
         glUniformMatrix4x2fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_FLOAT_MAT3x4:
      {
#ifdef TWEAK_GL_SHADER
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
#else
         const auto entry = uniformFloatP.find(uniformName);
         const auto valueFP = ((entry != uniformFloatP.end()) && entry->second.data) ? entry->second.data : zeroData;
#endif
         glUniformMatrix3x4fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_FLOAT_MAT2x4:
      {
#ifdef TWEAK_GL_SHADER
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
#else
         const auto entry = uniformFloatP.find(uniformName);
         const auto valueFP = ((entry != uniformFloatP.end()) && entry->second.data) ? entry->second.data : zeroData;
#endif
         glUniformMatrix2x4fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_FLOAT_MAT3x2:
      {
#ifdef TWEAK_GL_SHADER
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
#else
         const auto entry = uniformFloatP.find(uniformName);
         const auto valueFP = ((entry != uniformFloatP.end()) && entry->second.data) ? entry->second.data : zeroData;
#endif
         glUniformMatrix3x2fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_FLOAT_MAT2x3:
      {
#ifdef TWEAK_GL_SHADER
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
#else
         const auto entry = uniformFloatP.find(uniformName);
         const auto valueFP = ((entry != uniformFloatP.end()) && entry->second.data) ? entry->second.data : zeroData;
#endif
         glUniformMatrix2x3fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_SAMPLER_2D_MULTISAMPLE:
      {
#ifdef TWEAK_GL_SHADER
         int TextureID;
         if (uniformTex[uniformName]>0) {
            TextureID = uniformTex[uniformName];
         }
#else
         const auto valueT = uniformTex.find(uniformName);
         int TextureID;
         if (valueT != uniformTex.end()) {
            TextureID = valueT->second;
         }
#endif
         else {
            if (!noTextureMSAA) {
               constexpr unsigned int data[4] = { 0xff0000ff, 0xffffff00, 0xffff0000, 0xff00ff00 };
               GLuint glTexture;
               glGenTextures(1, &glTexture);
               glBindTexture(GL_TEXTURE_2D, glTexture);
               glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, g_pplayer->m_MSAASamples, GL_RGBA, 2, 2, GL_TRUE);
               noTexture = new Sampler(m_renderDevice, glTexture, true, false, false);
            }
            TextureID = noTextureMSAA->GetCoreTexture();
         }
         glActiveTexture(GL_TEXTURE0 + nextTextureSlot);
         glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, TextureID);
         glUniform1i(currentUniform.location, nextTextureSlot);

         nextTextureSlot = (nextTextureSlot+1) % maxSlots;
      }
      break;
      case GL_SAMPLER_2D:
      {
#ifdef TWEAK_GL_SHADER
         int TextureID;
         if (uniformTex[uniformName]>0)
            TextureID = uniformTex[uniformName];
#else
         auto valueT = uniformTex.find(uniformName);
/*         int Shader::nextTextureSlot = 0;
         static int* textureSlotList = nullptr;
         static std::map<int, int> slotTextureList;*/
         int TextureID;
         if (valueT != uniformTex.end())
            TextureID = valueT->second;
#endif
         else {
            if (!noTexture) {
               constexpr unsigned int data[4] = { 0xff0000ff, 0xffffff00, 0xffff0000, 0xff00ff00 };
               GLuint glTexture;
               glGenTextures(1, &glTexture);
               glBindTexture(GL_TEXTURE_2D, glTexture);
               glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
               noTexture = new Sampler(m_renderDevice, glTexture, true, false, false);
            }
            TextureID = noTexture->GetCoreTexture();
         }
//Texture Cache
/*         auto slot = slotTextureList.find(TextureID);
         if ((slot == slotTextureList.end()) || (textureSlotList[slot->second] != TextureID)) {
            glActiveTexture(GL_TEXTURE0 + nextTextureSlot);
            glBindTexture(GL_TEXTURE_2D, TextureID);//TODO implement a cache for textures
            glUniform1i(currentUniform.location, nextTextureSlot);
            slotTextureList[TextureID] = nextTextureSlot;
            textureSlotList[nextTextureSlot] = TextureID;
            nextTextureSlot = (++nextTextureSlot) % maxSlots;
         }
         else {
            glActiveTexture(GL_TEXTURE0 + slot->second);
            glBindTexture(GL_TEXTURE_2D, TextureID);//TODO implement a cache for textures
            glUniform1i(currentUniform.location, slot->second);
         }
         */
         glActiveTexture(GL_TEXTURE0 + nextTextureSlot);
         glBindTexture(GL_TEXTURE_2D, TextureID);//TODO implement a cache for textures
         glUniform1i(currentUniform.location, nextTextureSlot);
         nextTextureSlot = (nextTextureSlot+1) % maxSlots;
      }
      break;
      default:
#ifdef TWEAK_GL_SHADER
         sprintf_s(msg, sizeof(msg), "Unknown uniform type 0x%0002X for %s in %s", currentUniform.type, 
            shaderUniformNames[uniformName].c_str(), shaderTechniqueNames[technique].c_str());
#else
         sprintf_s(msg, sizeof(msg), "Unknown uniform type 0x%0002X for %s in %s", currentUniform.type, uniformName.c_str(), techName.c_str());
#endif
         ShowError(msg);
         break;
      }
   }
}

void Shader::setTextureDirty(int TextureID) {//Invalidate cache
/*   auto slot = slotTextureList.find(TextureID);
   if ((slot != slotTextureList.end()) && (textureSlotList[slot->second] == TextureID)) {
      textureSlotList[slot->second] = -1;
   }*/
}


void Shader::End()
{
   //Nothing to do for GL
}

void Shader::SetTexture(const SHADER_UNIFORM_HANDLE texelName, Texture *texel, const TextureFilter filter, const bool clampU, const bool clampV, const bool force_linear_rgb)
{
   if (!texel || !texel->m_pdsBuffer)
      SetTextureNull(texelName);
   else
      SetTexture(texelName, m_renderDevice->m_texMan.LoadTexture(texel->m_pdsBuffer, filter, clampU, clampV, force_linear_rgb));
}

void Shader::SetTexture(const SHADER_UNIFORM_HANDLE texelName, Sampler* texel)
{
   if (!texel || (uniformTex[texelName] == texel->GetCoreTexture()))
      return;

   uniformTex[texelName] = texel->GetCoreTexture();

   if (m_currentTechnique && lastShaderProgram == m_currentTechnique->program)
   {
#ifdef TWEAK_GL_SHADER
      const auto location = m_currentTechnique->uniformLocation[texelName];
      if (location.location == -1)
         return;
#else
      auto loc = m_currentTechnique->uniformLocation->find(texelName);
      if (loc == m_currentTechnique->uniformLocation->end())
         return;
      auto location = loc->second;
#endif
      glActiveTexture(GL_TEXTURE0 + nextTextureSlot);
      if (texel->IsMSAA())
         glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, texel->GetCoreTexture());
      else
         glBindTexture(GL_TEXTURE_2D, texel->GetCoreTexture());
      glUniform1i(location.location, nextTextureSlot);
      nextTextureSlot = (nextTextureSlot + 1) % maxSlots; //TODO might cause problems if we overwrite an already bound texture => could be fixed with the texture cache, too
   }
}

void Shader::SetTextureNull(const SHADER_UNIFORM_HANDLE texelName)
{
   //Using an unset texture leads to undefined behavior, so keeping the texture is absolutely fine.
    SetTexture(texelName, m_nullTexture, TextureFilter::TEXTURE_MODE_NONE, false, false, false);
}

void Shader::SetTechnique(const SHADER_TECHNIQUE_HANDLE _technique)
{
#ifdef TWEAK_GL_SHADER
   technique = _technique;
#else
   strcpy_s(technique, _technique);
#endif
   m_renderDevice->m_curTechniqueChanges++;
}

void Shader::SetTechniqueMetal(const SHADER_TECHNIQUE_HANDLE _technique, const bool isMetal)
{
   SetTechnique(_technique);
   SetBool(SHADER_is_metal, isMetal);
}

void Shader::SetUniformBlock(const SHADER_UNIFORM_HANDLE hParameter, const float* pMatrix, const size_t size)
{
   floatP elem;
#ifdef TWEAK_GL_SHADER
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   auto element = &uniformFloatP[hParameter];
   if (element->len < size) {
      free(element->data);
      elem.data = (float*)malloc(size * sizeof(float));
      elem.len = size;
   }
   else
      elem = *element;
#else
   auto element = uniformFloatP.find(hParameter);
   if ((element == uniformFloatP.end()) || (element->second.data == nullptr)) {
      elem.data = (float*)malloc(size * sizeof(float));
      elem.len = size;
   } else
   if (element->second.len < size) {
      free(element->second.data);
      elem.data = (float*)malloc(size * sizeof(float));
      elem.len = size;
   } else
      elem = element->second;
#endif
   memcpy(elem.data, pMatrix, size * sizeof(float));
   uniformFloatP[hParameter] = elem;
   if (m_currentTechnique && lastShaderProgram == m_currentTechnique->program) {
#ifdef TWEAK_GL_SHADER
      const auto location = m_currentTechnique->uniformLocation[hParameter];
      if (location.location == -1) return;
#else
      auto loc = m_currentTechnique->uniformLocation->find(hParameter);
      if (loc == m_currentTechnique->uniformLocation->end()) return;
      auto location = loc->second;
#endif
      glBindBuffer(GL_UNIFORM_BUFFER, location.blockBuffer);
      glBufferData(GL_UNIFORM_BUFFER, sizeof(GLfloat) * size, elem.data, GL_STREAM_DRAW);
      glUniformBlockBinding(lastShaderProgram, location.location, 0);
      glBindBufferRange(GL_UNIFORM_BUFFER, 0, location.blockBuffer, 0, sizeof(GLfloat) * size);
   }
}

void Shader::SetMatrix(const SHADER_UNIFORM_HANDLE hParameter, const Matrix3D* pMatrix)
{
   floatP elem;
#ifdef TWEAK_GL_SHADER
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   auto element = &uniformFloatP[hParameter];
   if (element->len < 16) {
      free(element->data);
      elem.data = (float*)malloc(16 * sizeof(float));
      elem.len = 16;
   }
   else
      elem = *element;
#else
   auto element = uniformFloatP.find(hParameter);
   if ((element == uniformFloatP.end()) || (element->second.data == nullptr)) {
      elem.data = (float*)malloc(16 * sizeof(float));
      elem.len = 16;
   }
   else if (element->second.len < 16) {
      free(element->second.data);
      elem.data = (float*)malloc(16 * sizeof(float));
      elem.len = 16;
   }
   else
      elem = element->second;
#endif
   memcpy(elem.data, pMatrix->m16, 16 * sizeof(float));
   uniformFloatP[hParameter] = elem;
   if (m_currentTechnique && lastShaderProgram == m_currentTechnique->program) {
#ifdef TWEAK_GL_SHADER
      const auto location = m_currentTechnique->uniformLocation[hParameter];
      if (location.location == -1) return;
#else
      auto loc = m_currentTechnique->uniformLocation->find(hParameter);
      if (loc == m_currentTechnique->uniformLocation->end()) return;
      auto location = loc->second;
#endif
      switch (location.type) {
      case GL_FLOAT_MAT2:
         glUniformMatrix2fv(location.location, 1, GL_FALSE, elem.data);
         break;
      case GL_FLOAT_MAT3:
         glUniformMatrix3fv(location.location, 1, GL_FALSE, elem.data);
         break;
      case GL_FLOAT_MAT4:
         glUniformMatrix4fv(location.location, 1, GL_FALSE, elem.data);
         break;
      case GL_FLOAT_MAT4x3:
         glUniformMatrix4x3fv(location.location, 1, GL_FALSE, elem.data);
         break;
      case GL_FLOAT_MAT4x2:
         glUniformMatrix4x2fv(location.location, 1, GL_FALSE, elem.data);
         break;
      case GL_FLOAT_MAT3x4:
         glUniformMatrix3x4fv(location.location, 1, GL_FALSE, elem.data);
         break;
      case GL_FLOAT_MAT2x4:
         glUniformMatrix2x4fv(location.location, 1, GL_FALSE, elem.data);
         break;
      case GL_FLOAT_MAT3x2:
         glUniformMatrix3x2fv(location.location, 1, GL_FALSE, elem.data);
         break;
      case GL_FLOAT_MAT2x3:
         glUniformMatrix2x3fv(location.location, 1, GL_FALSE, elem.data);
         break;
      }
   }
}

void Shader::SetVector(const SHADER_UNIFORM_HANDLE hParameter, const vec4* pVector)
{
   floatP elem;
#ifdef TWEAK_GL_SHADER
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   auto element = &uniformFloatP[hParameter];
   if (element->len < 4) {
      free(element->data);
      elem.data = (float*)malloc(4 * sizeof(float));
      elem.len = 4;
   }
   else {
      if (element->data[0] == pVector->x && element->data[1] == pVector->y && element->data[2] == pVector->z && element->data[3] == pVector->w) return;
      elem = *element;
   }
#else
   auto element = uniformFloatP.find(hParameter);
   if ((element == uniformFloatP.end()) || (element->second.data == nullptr)) {
      elem.data = (float*)malloc(4 * sizeof(float));
      elem.len = 4;
   }
   else if (element->second.len < 4) {
      free(element->second.data);
      elem.data = (float*)malloc(4 * sizeof(float));
      elem.len = 4;
   }
   else {
      elem = element->second;
      if (elem.data[0] == pVector->x && elem.data[1] == pVector->y && elem.data[2] == pVector->z && elem.data[3] == pVector->w) return;
   }
#endif
   memcpy(elem.data, pVector, 4 * sizeof(float));
   uniformFloatP[hParameter] = elem;
   if (m_currentTechnique && lastShaderProgram == m_currentTechnique->program) {
#ifdef TWEAK_GL_SHADER
      const auto location = m_currentTechnique->uniformLocation[hParameter];
      if (location.location == -1) return;
#else
      auto loc = m_currentTechnique->uniformLocation->find(hParameter);
      if (loc == m_currentTechnique->uniformLocation->end()) return;
      auto location = loc->second;
#endif
      switch (location.type) {
      case GL_FLOAT_VEC2:
         glUniform2fv(location.location, 1, elem.data);
         break;
      case GL_FLOAT_VEC3:
         glUniform3fv(location.location, 1, elem.data);
         break;
      case GL_FLOAT_VEC4:
         glUniform4fv(location.location, 1, elem.data);
         break;
      }
   }
}

void Shader::SetVector(const SHADER_UNIFORM_HANDLE hParameter, const float x, const float y, const float z, const float w)
{
   floatP elem;
#ifdef TWEAK_GL_SHADER
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   auto element = &uniformFloatP[hParameter];
   if (element->len < 4) {
      free(element->data);
      elem.data = (float*)malloc(4 * sizeof(float));
      elem.len = 4;
   }
   else {
      if (element->data[0] == x && element->data[1] == y && element->data[2] == z && element->data[3] == w) return;
      elem = *element;
   }
#else
   auto element = uniformFloatP.find(hParameter);
   if ((element == uniformFloatP.end()) || (element->second.data == nullptr)) {
      elem.data = (float*)malloc(4 * sizeof(float));
      elem.len = 4;
   }
   else if (element->second.len < 4) {
      free(element->second.data);
      elem.data = (float*)malloc(4 * sizeof(float));
      elem.len = 4;
   }
   else {
      elem = element->second;
      if (elem.data[0] == x && elem.data[1] == y && elem.data[2] == z && elem.data[3] == w) return;
   }
#endif
   elem.data[0] = x;
   elem.data[1] = y;
   elem.data[2] = z;
   elem.data[3] = w;
   uniformFloatP[hParameter] = elem;
   if (m_currentTechnique && lastShaderProgram == m_currentTechnique->program) {
#ifdef TWEAK_GL_SHADER
      const auto location = m_currentTechnique->uniformLocation[hParameter];
      if (location.location == -1) return;
#else
      auto loc = m_currentTechnique->uniformLocation->find(hParameter);
      if (loc == m_currentTechnique->uniformLocation->end()) return;
      auto location = loc->second;
#endif
      switch (location.type) {
      case GL_FLOAT_VEC2:
         glUniform2fv(location.location, 1, elem.data);
         break;
      case GL_FLOAT_VEC3:
         glUniform3fv(location.location, 1, elem.data);
         break;
      case GL_FLOAT_VEC4:
         glUniform4fv(location.location, 1, elem.data);
         break;
      }
   }
}

void Shader::SetFloat(const SHADER_UNIFORM_HANDLE hParameter, const float f)
{
#ifdef TWEAK_GL_SHADER
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
#endif
   if (uniformFloat[hParameter] == f) return;
   uniformFloat[hParameter] = f;
   if (m_currentTechnique && lastShaderProgram == m_currentTechnique->program) {
#ifdef TWEAK_GL_SHADER
      const auto location = m_currentTechnique->uniformLocation[hParameter];
      if (location.location == -1) return;
#else
      auto loc = m_currentTechnique->uniformLocation->find(hParameter);
      if (loc == m_currentTechnique->uniformLocation->end()) return;
      auto location = loc->second;
#endif
      glUniform1f(location.location, f);
   }
}

void Shader::SetInt(const SHADER_UNIFORM_HANDLE hParameter, const int i)
{
#ifdef TWEAK_GL_SHADER
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
#endif
   if (uniformInt[hParameter] == i) return;
   uniformInt[hParameter] = i;
   if (m_currentTechnique && lastShaderProgram == m_currentTechnique->program) {
#ifdef TWEAK_GL_SHADER
      const auto location = m_currentTechnique->uniformLocation[hParameter];
      if (location.location == -1) return;
#else
      auto loc = m_currentTechnique->uniformLocation->find(hParameter);
      if (loc == m_currentTechnique->uniformLocation->end()) return;
      auto location = loc->second;
#endif
      glUniform1i(location.location, i);
   }
}

void Shader::SetBool(const SHADER_UNIFORM_HANDLE hParameter, const bool b)
{
#ifdef TWEAK_GL_SHADER
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
#endif
   const int i = b ? 1 : 0;
   if (uniformInt[hParameter] == i) return;
   uniformInt[hParameter] = i;
   if (m_currentTechnique && lastShaderProgram == m_currentTechnique->program) {
#ifdef TWEAK_GL_SHADER
      const auto location = m_currentTechnique->uniformLocation[hParameter];
      if (location.location == -1) return;
#else
      auto loc = m_currentTechnique->uniformLocation->find(hParameter);
      if (loc == m_currentTechnique->uniformLocation->end()) return;
      auto location = loc->second;
#endif
      glUniform1i(location.location, i);
   }
}

void Shader::SetFloatArray(const SHADER_UNIFORM_HANDLE hParameter, const float* pData, const unsigned int count)
{
   floatP elem;
#ifdef TWEAK_GL_SHADER
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   auto element = &uniformFloatP[hParameter];
   if (element->len < count) {
      free(element->data);
      elem.data = (float*)malloc(count * sizeof(float));
      elem.len = count;
   }
   else {
      bool identical = true;
      for (size_t i = 0;i < count;++i) {
         if (element->data[i] != pData[i]) {
            identical = false;
            break;
         }
      }
      if (identical) return;
      elem = *element;
   }
#else
   auto element = uniformFloatP.find(hParameter);
   if ((element == uniformFloatP.end()) || (element->second.data == nullptr)) {
      elem.data = (float*)malloc(count * sizeof(float));
      elem.len = count;
   }
   else if (element->second.len < count) {
      free(element->second.data);
      elem.data = (float*)malloc(count * sizeof(float));
      elem.len = count;
   }
   else {
      elem = element->second;
      bool identical = true;
      for (size_t i = 0;i < count;++i) {
         if (elem.data[i] != pData[i]) {
            identical = false;
            break;
         }
      }
      if (identical) return;
   }
#endif
   memcpy(elem.data, pData, count * sizeof(float));
   uniformFloatP[hParameter] = elem;
   if (m_currentTechnique && lastShaderProgram == m_currentTechnique->program) {
#ifdef TWEAK_GL_SHADER
      const auto location = m_currentTechnique->uniformLocation[hParameter];
      if (location.location == -1) return;
#else
      auto loc = m_currentTechnique->uniformLocation->find(hParameter);
      if (loc == m_currentTechnique->uniformLocation->end()) return;
      auto location = loc->second;
#endif
      glUniform4fv(location.location, count, elem.data);
   }
}

void Shader::GetTransform(const TransformStateType p1, Matrix3D* p2, const int count)
{
   switch (p1) {
   case TRANSFORMSTATE_WORLD:
      *p2 = Shader::mWorld;
      break;
   case TRANSFORMSTATE_VIEW:
      *p2 = Shader::mView;
      break;
   case TRANSFORMSTATE_PROJECTION:
      for(int i = 0; i < count; ++i)
         p2[i] = Shader::mProj[i];
      break;
   }
}

void Shader::SetTransform(const TransformStateType p1, const Matrix3D * p2, const int count)
{
   switch (p1) {
   case TRANSFORMSTATE_WORLD:
      Shader::mWorld = *p2;
      break;
   case TRANSFORMSTATE_VIEW:
      Shader::mView = *p2;
      break;
   case TRANSFORMSTATE_PROJECTION:
      for(int i = 0; i < count; ++i)
         Shader::mProj[i] = p2[i];
      break;
   }
}
