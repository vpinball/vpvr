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
Shader::ShaderTechnique* Shader::compileGLShader(const string& fileNameRoot, string& shaderCodeName, const string& vertex, const string& geometry, const string& fragment)
{
   bool success = true;
   ShaderTechnique* shader = nullptr;
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
         char msg[16384];
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
   if (vertexShader)
      glDeleteShader(vertexShader);
   if (geometryShader)
      glDeleteShader(geometryShader);
   if (fragmentShader)
      glDeleteShader(fragmentShader);
   delete [] fragmentSource;
   delete [] geometrySource;
   delete [] vertexSource;

   if (success) {
      shader = new ShaderTechnique { -1, shaderCodeName };
      shader->program = shaderprogram;
      for (int i = 0; i < SHADER_ATTRIBUTE_COUNT; ++i)
         shader->attributeLocation[i] = { 0, -1, 0 };
      for (int i = 0; i < SHADER_UNIFORM_COUNT; ++i)
         shader->uniformLocation[i] = { 0, -1, 0, 0 };

      int count = 0;
      glGetProgramiv(shaderprogram, GL_ACTIVE_UNIFORMS, &count);
      char uniformName[256];
      for (int i = 0;i < count;++i) {
         GLenum type;
         int size;
         int length;
         glGetActiveUniform(shader->program, (GLuint)i, 256, &length, &size, &type, uniformName);
         int location = glGetUniformLocation(shader->program, uniformName);
         if (location >= 0 && size > 0) {
            uniformLoc newLoc = { type, location, size, -1 };
            // hack for packedLights, but works for all arrays
            for (int i2 = 0; i2 < length; i2++)
            {
               if (uniformName[i2] == '[') {
                  uniformName[i2] = 0;
                  break;
               }
            }
            auto uniformIndex = getUniformByName(uniformName);
            if (uniformIndex < SHADER_UNIFORM_COUNT)
            {
               shader->uniformLocation[uniformIndex] = newLoc;
               if (shaderUniformNames[uniformIndex].default_tex_unit != -1)
               { 
                  // Unlike DirectX, OpenGL won't return 0 if the texture is not bound to a black texture
                  // This will cause error for static pre-render which, done before bulb light transmission is evaluated and bound
                  SetTextureNull(uniformIndex); 
               }
            }
         }
      }

      glGetProgramiv(shaderprogram, GL_ACTIVE_UNIFORM_BLOCKS, &count);
      for (int i = 0;i < count;++i) {
         int size;
         int length;
         glGetActiveUniformBlockName(shader->program, (GLuint)i, 256, &length, uniformName);
         glGetActiveUniformBlockiv(shader->program, (GLuint)i, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
         int location = glGetUniformBlockIndex(shader->program, uniformName);
         if (location >= 0 && size>0) {
            uniformLoc newLoc = { ~0u, location, size, -1 };
            glGenBuffers(1, &newLoc.blockBuffer);
            //hack for packedLights, but works for all arrays - I don't need it for uniform blocks now and I'm not sure if it makes any sense, but maybe someone else in the future?
            for (int i2 = 0;i2 < length;i2++) {
               if (uniformName[i2] == '[') {
                  uniformName[i2] = 0;
                  break;
               }
            }
            auto uniformIndex = getUniformByName(uniformName);
            if (uniformIndex < SHADER_UNIFORM_COUNT) shader->uniformLocation[uniformIndex] = newLoc;
         }
      }

      glGetProgramiv(shaderprogram, GL_ACTIVE_ATTRIBUTES, &count);
      for (int i = 0; i < SHADER_ATTRIBUTE_COUNT; ++i)
         shader->attributeLocation[i] = { 0, -1, 0};
      for (int i = 0;i < count;++i) {
         GLenum type;
         int size;
         int length;
         char attributeName[256];
         glGetActiveAttrib(shader->program, (GLuint)i, 256, &length, &size, &type, attributeName);
         int location = glGetAttribLocation(shader->program, attributeName);
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
            auto index = getAttributeByName(attributeName);
            if (index < SHADER_ATTRIBUTE_COUNT) shader->attributeLocation[index] = newLoc;
         }
      }
   }
   return shader;
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
   LOG(3, (const char*)shaderCodeName, "Start parsing file");
   robin_hood::unordered_map<string, string> values;
   const bool parsing = parseFile(shaderCodeName, shaderCodeName, 0, values, "GLOBAL");
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
            int shaderTechniqueIndex = -1;
            for (int i = 0; i < SHADER_TECHNIQUE_COUNT; ++i)
            {
               if (element[0] == shaderTechniqueNames[i])
               {
                  shaderTechniqueIndex = i;
                  break;
               }
            }
            if (shaderTechniqueIndex == -1)
            {
               LOG(3, (const char*)shaderCodeName, string("Unexpected technique skipped: ").append(element[0]));
            }
            else
            {
               string vertexShaderCode = vertex;
               vertexShaderCode.append("\n//").append(_technique).append("\n//").append(element[2]).append("\n");
               vertexShaderCode.append(analyzeFunction(shaderCodeName, _technique, element[2], values)).append("\0");
               string geometryShaderCode;
               if (elem == 5 && element[3].length() > 0)
               {
                  geometryShaderCode = geometry;
                  geometryShaderCode.append("\n//").append(_technique).append("\n//").append(element[3]).append("\n");
                  geometryShaderCode.append(analyzeFunction(shaderCodeName, _technique, element[3], values)).append("\0");
               }
               string fragmentShaderCode = fragment;
               fragmentShaderCode.append("\n//").append(_technique).append("\n//").append(element[elem - 1]).append("\n");
               fragmentShaderCode.append(analyzeFunction(shaderCodeName, _technique, element[elem - 1], values)).append("\0");
               ShaderTechnique* build = compileGLShader(shaderCodeName, element[0] /*.append("_").append(element[1])*/, vertexShaderCode, geometryShaderCode, fragmentShaderCode);
               if (build != nullptr)
               {
                  m_techniques[shaderTechniqueIndex] = build;
                  tecCount++;
               }
               else
               {
                  char msg[128];
                  sprintf_s(msg, sizeof(msg), "Fatal Error: Shader compilation failed for %s!", shaderCodeName);
                  ReportError(msg, -1, __FILE__, __LINE__);
                  if (logFile)
                     logFile->close();
                  return false;
               }
            }
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
   for (int i = 0; i < SHADER_UNIFORM_COUNT; ++i)
   {
      int type = 0;
      for (int j = 0; j < SHADER_TECHNIQUE_COUNT; ++j)
      {
         if (m_techniques[j] != nullptr)
         {
            type = m_techniques[j]->uniformLocation[i].type;
            break;
         }
      }
      if (type == ~0u)
      {
         for (int j = 0; j <= SHADER_TECHNIQUE_COUNT; ++j)
         {
            if (type == ~0u && m_uniformCache[j][i].fp.data)
               free(m_uniformCache[j][i].fp.data);
         }
      }
   }
   for (int i = 0; i < SHADER_TECHNIQUE_COUNT; ++i)
   {
      if (m_techniques[i] != nullptr)
      {
         glDeleteProgram(m_techniques[i]->program);
         delete m_techniques[i];
         m_techniques[i] = nullptr;
      }
   }
}

void Shader::setAttributeFormat(DWORD fvf)
{
   if (m_technique == SHADER_TECHNIQUE_INVALID)
      return;
   for (int i = 0; i < SHADER_ATTRIBUTE_COUNT; ++i)
   {
      const int location = m_techniques[m_technique]->attributeLocation[i].location;
      if (location >= 0) {
         size_t offset;
         glEnableVertexAttribArray(m_techniques[m_technique]->attributeLocation[i].location);
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
         glVertexAttribPointer(m_techniques[m_technique]->attributeLocation[i].location, m_techniques[m_technique]->attributeLocation[i].size, GL_FLOAT, GL_FALSE,
            (fvf == MY_D3DFVF_TEX) ? 20 : 32, (void*)offset);
      }
   }
}

void Shader::SetTechnique(ShaderTechniques _technique)
{
   assert(current_shader != this);
   assert(_technique < SHADER_TECHNIQUE_COUNT);
   if (m_techniques[m_technique] == nullptr)
   {
      m_technique = SHADER_TECHNIQUE_INVALID;
      char msg[256];
      sprintf_s(msg, sizeof(msg), "Fatal Error: Could not find shader technique %s", shaderTechniqueNames[_technique].c_str());
      ShowError(msg);
      exit(-1);
   }
   m_renderDevice->m_curTechniqueChanges++;
   m_technique = _technique;
}

void Shader::SetTextureNull(const ShaderUniforms texelName)
{
   SetTexture(texelName, m_noTexture);
}

void Shader::SetTechniqueMetal(ShaderTechniques _technique, const bool isMetal)
{
   SetTechnique(_technique);
   SetBool(SHADER_is_metal, isMetal);
}

void Shader::SetTexture(const ShaderUniforms texelName, Texture* texel, const TextureFilter filter, const bool clampU, const bool clampV, const bool force_linear_rgb)
{
   if (!texel || !texel->m_pdsBuffer)
      SetTextureNull(texelName);
   else
      SetTexture(texelName, m_renderDevice->m_texMan.LoadTexture(texel->m_pdsBuffer, filter, clampU, clampV, force_linear_rgb));
}

void Shader::SetTexture(const ShaderUniforms texelName, Sampler* texel)
{
   if (!texel)
      return;
   m_uniformCache[SHADER_TECHNIQUE_COUNT][texelName].sampler = texel;
   ApplyUniform(texelName);
}

void Shader::SetUniformBlock(const ShaderUniforms hParameter, const float* pMatrix, const size_t size)
{
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   floatP elem;
   auto element = &m_uniformCache[SHADER_TECHNIQUE_COUNT][hParameter].fp;
   if (element->len < size) {
      free(element->data);
      elem.data = (float*)malloc(size * sizeof(float));
      elem.len = size;
   }
   else
      elem = *element;
   memcpy(elem.data, pMatrix, size * sizeof(float));
   m_uniformCache[SHADER_TECHNIQUE_COUNT][hParameter].fp = elem;
   ApplyUniform(hParameter);
}

void Shader::SetMatrix(const ShaderUniforms hParameter, const Matrix3D* pMatrix)
{
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   memcpy(m_uniformCache[SHADER_TECHNIQUE_COUNT][hParameter].fvval, pMatrix->m16, 16 * sizeof(float));
   ApplyUniform(hParameter);
}

void Shader::SetVector(const ShaderUniforms hParameter, const vec4* pVector)
{
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   memcpy(m_uniformCache[SHADER_TECHNIQUE_COUNT][hParameter].fvval, pVector, 4 * sizeof(float));
   ApplyUniform(hParameter);
}

void Shader::SetVector(const ShaderUniforms hParameter, const float x, const float y, const float z, const float w)
{
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   m_uniformCache[SHADER_TECHNIQUE_COUNT][hParameter].fvval[0] = x;
   m_uniformCache[SHADER_TECHNIQUE_COUNT][hParameter].fvval[1] = y;
   m_uniformCache[SHADER_TECHNIQUE_COUNT][hParameter].fvval[2] = z;
   m_uniformCache[SHADER_TECHNIQUE_COUNT][hParameter].fvval[3] = w;
   ApplyUniform(hParameter);
}

void Shader::SetFloat(const ShaderUniforms hParameter, const float f)
{
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   m_uniformCache[SHADER_TECHNIQUE_COUNT][hParameter].fval = f;
   ApplyUniform(hParameter);
}

void Shader::SetInt(const ShaderUniforms hParameter, const int i)
{
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   m_uniformCache[SHADER_TECHNIQUE_COUNT][hParameter].ival = i;
   ApplyUniform(hParameter);
}

void Shader::SetBool(const ShaderUniforms hParameter, const bool b)
{
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   const int i = b ? 1 : 0;
   m_uniformCache[SHADER_TECHNIQUE_COUNT][hParameter].ival = i;
   ApplyUniform(hParameter);
}

void Shader::SetFloatArray(const ShaderUniforms hParameter, const float* pData, const unsigned int count)
{
   floatP elem;
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   auto element = &m_uniformCache[SHADER_TECHNIQUE_COUNT][hParameter].fp;
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
   memcpy(elem.data, pData, count * sizeof(float));
   m_uniformCache[SHADER_TECHNIQUE_COUNT][hParameter].fp = elem;
   ApplyUniform(hParameter);
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

void Shader::ApplyUniform(const ShaderUniforms uniformName)
{
   if (current_shader != this)
      return;
   // FIXME cache sampler states and only apply the ones that actually need it
   const uniformLoc currentUniform = m_techniques[m_technique]->uniformLocation[uniformName];
   if (currentUniform.location < 0 || currentUniform.type == 0 || currentUniform.size == 0)
      return;
   bool isCacheInvalid = !m_isCacheValid[m_technique];
   UniformCache* src = &(m_uniformCache[SHADER_TECHNIQUE_COUNT][uniformName]);
   UniformCache* dst = &(m_uniformCache[m_technique][uniformName]);
   switch (currentUniform.type)
   {
   case ~0u: // Uniform blocks (currentUniform.size is in byte, src->fp.len is in number of floats)
      if (isCacheInvalid || memcmp(src->fp.data, dst->fp.data, currentUniform.size) != 0)
      {
         if (dst->fp.data == nullptr)
         {
            dst->fp.len = src->fp.len;
            dst->fp.data = (float*)malloc(currentUniform.size);
         }
         memcpy(dst->fp.data, src->fp.data, currentUniform.size);
         glBindBuffer(GL_UNIFORM_BUFFER, currentUniform.blockBuffer);
         glBufferData(GL_UNIFORM_BUFFER, currentUniform.size, src->fp.data, GL_STREAM_DRAW);
      }
      glUniformBlockBinding(m_techniques[m_technique]->program, currentUniform.location, 0);
      glBindBufferRange(GL_UNIFORM_BUFFER, 0, currentUniform.blockBuffer, 0, currentUniform.size);
      break;
   case GL_FLOAT:
      if (isCacheInvalid || dst->fval != src->fval)
      {
         dst->fval = src->fval;
         glUniform1f(currentUniform.location, src->fval);
      }
      break;
   case GL_BOOL:
   case GL_INT:
      if (isCacheInvalid || dst->ival != src->ival)
      {
         dst->ival = src->ival;
         glUniform1i(currentUniform.location, src->ival);
      }
      break;
   case GL_FLOAT_VEC2:
      if (isCacheInvalid || memcmp(src, dst, 2 * sizeof(float)) != 0)
      {
         memcpy(dst, src, 2 * sizeof(float));
         glUniform2fv(currentUniform.location, 1, src->fvval);
      }
      break;
   case GL_FLOAT_VEC3:
      if (isCacheInvalid || memcmp(src, dst, 3 * sizeof(float)) != 0)
      {
         memcpy(dst, src, 3 * sizeof(float));
         glUniform3fv(currentUniform.location, 1, src->fvval);
      }
      break;
   case GL_FLOAT_VEC4:
      if (isCacheInvalid || memcmp(src, dst, 4 * sizeof(float)) != 0)
      {
         memcpy(dst, src, 4 * sizeof(float));
         glUniform4fv(currentUniform.location, 1, src->fvval);
      }
      break;
   case GL_FLOAT_MAT4:
      if (isCacheInvalid || memcmp(src, dst, 4 * 4 * sizeof(float)) != 0)
      {
         memcpy(dst, src, 4 * 4 * sizeof(float));
         glUniformMatrix4fv(currentUniform.location, 1, GL_FALSE, src->fvval);
      }
      break;
   case GL_SAMPLER_2D:
   case GL_SAMPLER_2D_MULTISAMPLE:
   {
      Sampler* texel = m_uniformCache[SHADER_TECHNIQUE_COUNT][uniformName].sampler;
      if (texel == nullptr)
         texel = m_noTexture;
      int tex_unit = shaderUniformNames[uniformName].default_tex_unit;
      if (tex_unit == -1)
      {
         // Texture units are preaffected to avoid collision (copied from DX9 implementation) but somes are not.
         //TODO might cause problems if we overwrite an already bound texture => could be fixed with the texture cache, too
         tex_unit = m_nextTextureSlot;
         m_nextTextureSlot = m_nextTextureSlot + 1;
      }
      glActiveTexture(GL_TEXTURE0 + tex_unit);
      glBindTexture(texel->IsMSAA() ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, texel->GetCoreTexture());
      glUniform1i(currentUniform.location, tex_unit);
      // FIXME initial implementation, entirely unoptimized (we could cache samplers, reuse the ones already bound, avoid setting sampler states,...)
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, texel->GetClampU());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, texel->GetClampV());
      switch (texel->GetFilter())
      {
      case SF_NONE: // No mipmapping
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
         break;
      case SF_POINT: // Point sampled (aka nearest mipmap) texture filtering.
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
         break;
      case SF_BILINEAR: // Bilinar texture filtering.
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         break;
      case SF_TRILINEAR: // Trilinar texture filtering.
         // FIXME cleanup sampler filtering
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         break;
      case SF_ANISOTROPIC: // Anisotropic texture filtering.
         // FIXME cleanup sampler filtering
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         break;
      }
      break;
   }
   default:
   {
      char msg[256];
      sprintf_s(msg, sizeof(msg), "Unknown uniform type 0x%0002X for %s in %s", currentUniform.type, shaderUniformNames[uniformName].name.c_str(), m_techniques[m_technique]->name.c_str());
      ShowError(msg);
      break;
   }
   }
}
