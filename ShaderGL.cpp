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
int Shader::m_currentShaderProgram = -1;
Sampler* Shader::noTexture = nullptr;
Sampler* Shader::noTextureMSAA = nullptr;
static const float zeroValues[16] = { 0.0f,0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
const float* Shader::zeroData = zeroValues;
int Shader::nextTextureSlot = 0;
int* Shader::textureSlotList = nullptr;
//std::map<int, int> Shader::slotTextureList;
int Shader::maxSlots = 0;

static const string shaderAttributeNames[SHADER_ATTRIBUTE_COUNT]{
   "vPosition", "vNormal", "tc", "tex0"
};

ShaderUniforms Shader::getUniformByName(const string& name) {
   for (int i = 0;i < SHADER_UNIFORM_COUNT; ++i)
      if (name == shaderUniformNames[i].name)
         return (ShaderUniforms) i;

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
      int count = 0;
      shader = new ShaderTechnique { -1, shaderCodeName };
      shader->program = shaderprogram;
      for (int i = 0; i < SHADER_ATTRIBUTE_COUNT; ++i) {
         shader->attributeLocation[i] = { 0, -1, 0 };
      }
      for (int i = 0; i < SHADER_UNIFORM_COUNT; ++i) {
         shader->uniformLocation[i] = { 0, -1, 0, 0 };
      }

      glGetProgramiv(shaderprogram, GL_ACTIVE_UNIFORMS, &count);
      char uniformName[256];
      for (int i = 0;i < count;++i) {
         GLenum type;
         int size;
         int length;
         glGetActiveUniform(shader->program, (GLuint)i, 256, &length, &size, &type, uniformName);
         int location = glGetUniformLocation(shader->program, uniformName);
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
            auto uniformIndex = getUniformByName(uniformName);
            if (uniformIndex < SHADER_UNIFORM_COUNT) shader->uniformLocation[uniformIndex] = newLoc;
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
   if (!textureSlotList) {
      glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxSlots);
      textureSlotList = new int[maxSlots];
      for (int i = 0;i < maxSlots;++i)
         textureSlotList[i] = -2;
   }
   m_currentShaderProgram = -1;
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
      if(uniformFloatP[i].data)
         free(uniformFloatP[i].data);
   for (int i = 0; i < SHADER_TECHNIQUE_COUNT; ++i)
      if (m_techniques[i] != nullptr)
      {
         glDeleteProgram(m_techniques[i]->program);
         m_techniques[i] = nullptr;
      }
}

void Shader::setAttributeFormat(DWORD fvf)
{
   if (m_technique == nullptr) {
      return;
   }
   for (int i = 0; i < SHADER_ATTRIBUTE_COUNT; ++i)
   {
      const int location = m_technique->attributeLocation[i].location;
      if (location >= 0) {
         size_t offset;
         glEnableVertexAttribArray(m_technique->attributeLocation[i].location);
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
         glVertexAttribPointer(m_technique->attributeLocation[i].location, m_technique->attributeLocation[i].size, GL_FLOAT, GL_FALSE, (fvf == MY_D3DFVF_TEX) ? 20 : 32, (void*)offset);
      }
   }
}

void Shader::Begin(const unsigned int pass)
{
   m_currentShader = this;
   char msg[256];
   if (m_technique == nullptr)
   {
      sprintf_s(msg, sizeof(msg), "Could not find shader technique");
      ShowError(msg);
      exit(-1);
   }
   if (m_currentShaderProgram == m_technique->program)
      return;

   nextTextureSlot = 0;
   glUseProgram(m_technique->program);
   m_currentShaderProgram = m_technique->program;

   //Set all uniforms
   for (int uniformName = 0; uniformName < SHADER_UNIFORM_COUNT; ++uniformName)
   {
      const uniformLoc currentUniform = m_technique->uniformLocation[uniformName];
      if (currentUniform.location < 0 || currentUniform.type == 0 || currentUniform.size == 0) continue;
      switch (currentUniform.type) {
      case ~0u: {//Uniform blocks
         const auto valueFP = uniformFloatP[uniformName];
         glBindBuffer(GL_UNIFORM_BUFFER, currentUniform.blockBuffer);
         glBufferData(GL_UNIFORM_BUFFER, currentUniform.size, valueFP.data, GL_STREAM_DRAW);
         glUniformBlockBinding(m_currentShaderProgram, currentUniform.location, 0);
         glBindBufferRange(GL_UNIFORM_BUFFER, 0, currentUniform.blockBuffer, 0, currentUniform.size);
      }
      break;
      case GL_FLOAT:
      {
         const float valueF = uniformFloat[uniformName];
         glUniform1f(currentUniform.location, valueF);
      }
      break;
      case GL_BOOL:
      case GL_INT:
      {
         const int valueI = uniformInt[uniformName];
         glUniform1i(currentUniform.location, valueI);
      }
      break;
      case GL_FLOAT_VEC2:
      {
         auto valueFP = uniformFloatP[uniformName].data;
         if (valueFP)
            glUniform2f(currentUniform.location, valueFP[0], valueFP[1]);
         else
            glUniform2f(currentUniform.location, 0.0f, 0.0f);
      }
      break;
      case GL_FLOAT_VEC3:
      {
         auto valueFP = uniformFloatP[uniformName].data;
         if (valueFP)
            glUniform3f(currentUniform.location, valueFP[0], valueFP[1], valueFP[2]);
         else
            glUniform3f(currentUniform.location, 0.0f, 0.0f, 0.0f);
      }
      break;
      case GL_FLOAT_VEC4:
      {
         auto valueFP = uniformFloatP[uniformName].data;
         if (valueFP)
         {
             if (uniformFloatP[uniformName].len > 4)
                 glUniform4fv(currentUniform.location, uniformFloatP[uniformName].len / 4, uniformFloatP[uniformName].data);
             else
                 glUniform4f(currentUniform.location, valueFP[0], valueFP[1], valueFP[2], valueFP[3]);
         }
         else 
            glUniform4f(currentUniform.location, 0.0f, 0.0f, 0.0f, 0.0f);
      }
      break;
      case GL_FLOAT_MAT2:
      {
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
         glUniformMatrix2fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_FLOAT_MAT3:
      {
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
         glUniformMatrix3fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_FLOAT_MAT4:
      {
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
         glUniformMatrix4fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_FLOAT_MAT4x3:
      {
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
         glUniformMatrix4x3fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_FLOAT_MAT4x2:
      {
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
         glUniformMatrix4x2fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_FLOAT_MAT3x4:
      {
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
         glUniformMatrix3x4fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_FLOAT_MAT2x4:
      {
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
         glUniformMatrix2x4fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_FLOAT_MAT3x2:
      {
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
         glUniformMatrix3x2fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_FLOAT_MAT2x3:
      {
         const auto valueFP = uniformFloatP[uniformName].data ? uniformFloatP[uniformName].data : zeroData;
         glUniformMatrix2x3fv(currentUniform.location, 1, GL_FALSE, valueFP);
      }
      break;
      case GL_SAMPLER_2D_MULTISAMPLE:
      {
         int TextureID;
         if (uniformTex[uniformName]>0) {
            TextureID = uniformTex[uniformName];
         }
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
         int TextureID;
         if (uniformTex[uniformName]>0)
            TextureID = uniformTex[uniformName];
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
         sprintf_s(msg, sizeof(msg), "Unknown uniform type 0x%0002X for %s in %s", currentUniform.type, 
            shaderUniformNames[uniformName].name.c_str(), m_technique->name.c_str());
         ShowError(msg);
         break;
      }
   }
}

void Shader::End()
{
   //Nothing to do for GL
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
   if (!texel || (uniformTex[texelName] == texel->GetCoreTexture()))
      return;

   uniformTex[texelName] = texel->GetCoreTexture();

   if (m_technique != nullptr && m_currentShaderProgram == m_technique->program)
   {
      const auto location = m_technique->uniformLocation[texelName];
      if (location.location == -1)
         return;
      glActiveTexture(GL_TEXTURE0 + nextTextureSlot);
      if (texel->IsMSAA())
         glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, texel->GetCoreTexture());
      else
         glBindTexture(GL_TEXTURE_2D, texel->GetCoreTexture());
      glUniform1i(location.location, nextTextureSlot);
      nextTextureSlot = (nextTextureSlot + 1) % maxSlots; //TODO might cause problems if we overwrite an already bound texture => could be fixed with the texture cache, too
   }
}

void Shader::SetTextureNull(const ShaderUniforms texelName)
{
   //Using an unset texture leads to undefined behavior, so keeping the texture is absolutely fine.
    SetTexture(texelName, m_nullTexture, TextureFilter::TEXTURE_MODE_NONE, false, false, false);
}

void Shader::SetTechnique(ShaderTechniques _technique)
{
   assert(_technique < SHADER_TECHNIQUE_COUNT);
   m_renderDevice->m_curTechniqueChanges++;
   m_technique = m_techniques[_technique];
   if (m_technique == nullptr)
   {
      m_technique = nullptr;
      char msg[256];
      sprintf_s(msg, sizeof(msg), "Fatal Error: Could not find shader technique %s", shaderTechniqueNames[_technique].c_str());
      ShowError(msg);
      exit(-1);
   }
}

void Shader::SetTechniqueMetal(ShaderTechniques _technique, const bool isMetal)
{
   SetTechnique(_technique);
   SetBool(SHADER_is_metal, isMetal);
}

void Shader::SetUniformBlock(const ShaderUniforms hParameter, const float* pMatrix, const size_t size)
{
   floatP elem;
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   auto element = &uniformFloatP[hParameter];
   if (element->len < size) {
      free(element->data);
      elem.data = (float*)malloc(size * sizeof(float));
      elem.len = size;
   }
   else
      elem = *element;
   memcpy(elem.data, pMatrix, size * sizeof(float));
   uniformFloatP[hParameter] = elem;
   if (m_technique != nullptr && m_currentShaderProgram == m_technique->program)
   {
      const auto location = m_technique->uniformLocation[hParameter];
      if (location.location == -1) return;
      glBindBuffer(GL_UNIFORM_BUFFER, location.blockBuffer);
      glBufferData(GL_UNIFORM_BUFFER, sizeof(GLfloat) * size, elem.data, GL_STREAM_DRAW);
      glUniformBlockBinding(m_technique->program, location.location, 0);
      glBindBufferRange(GL_UNIFORM_BUFFER, 0, location.blockBuffer, 0, sizeof(GLfloat) * size);
   }
}

void Shader::SetMatrix(const ShaderUniforms hParameter, const Matrix3D* pMatrix)
{
   floatP elem;
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   auto element = &uniformFloatP[hParameter];
   if (element->len < 16) {
      free(element->data);
      elem.data = (float*)malloc(16 * sizeof(float));
      elem.len = 16;
   }
   else
      elem = *element;
   memcpy(elem.data, pMatrix->m16, 16 * sizeof(float));
   uniformFloatP[hParameter] = elem;
   if (m_technique != nullptr && m_currentShaderProgram == m_technique->program)
   {
      const auto location = m_technique->uniformLocation[hParameter];
      if (location.location == -1) return;
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

void Shader::SetVector(const ShaderUniforms hParameter, const vec4* pVector)
{
   floatP elem;
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
   memcpy(elem.data, pVector, 4 * sizeof(float));
   uniformFloatP[hParameter] = elem;
   if (m_technique != nullptr && m_currentShaderProgram == m_technique->program)
   {
      const auto location = m_technique->uniformLocation[hParameter];
      if (location.location == -1) return;
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

void Shader::SetVector(const ShaderUniforms hParameter, const float x, const float y, const float z, const float w)
{
   floatP elem;
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
   elem.data[0] = x;
   elem.data[1] = y;
   elem.data[2] = z;
   elem.data[3] = w;
   uniformFloatP[hParameter] = elem;
   if (m_technique != nullptr && m_currentShaderProgram == m_technique->program)
   {
      const auto location = m_technique->uniformLocation[hParameter];
      if (location.location == -1) return;
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

void Shader::SetFloat(const ShaderUniforms hParameter, const float f)
{
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   if (uniformFloat[hParameter] == f) return;
   uniformFloat[hParameter] = f;
   if (m_technique != nullptr && m_currentShaderProgram == m_technique->program)
   {
      const auto location = m_technique->uniformLocation[hParameter];
      if (location.location == -1) return;
      glUniform1f(location.location, f);
   }
}

void Shader::SetInt(const ShaderUniforms hParameter, const int i)
{
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   if (uniformInt[hParameter] == i) return;
   uniformInt[hParameter] = i;
   if (m_technique != nullptr && m_currentShaderProgram == m_technique->program)
   {
      const auto location = m_technique->uniformLocation[hParameter];
      if (location.location == -1) return;
      glUniform1i(location.location, i);
   }
}

void Shader::SetBool(const ShaderUniforms hParameter, const bool b)
{
   if (hParameter >= SHADER_UNIFORM_COUNT) return;
   const int i = b ? 1 : 0;
   if (uniformInt[hParameter] == i) return;
   uniformInt[hParameter] = i;
   if (m_technique != nullptr && m_currentShaderProgram == m_technique->program)
   {
      const auto location = m_technique->uniformLocation[hParameter];
      if (location.location == -1) return;
      glUniform1i(location.location, i);
   }
}

void Shader::SetFloatArray(const ShaderUniforms hParameter, const float* pData, const unsigned int count)
{
   floatP elem;
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
   memcpy(elem.data, pData, count * sizeof(float));
   uniformFloatP[hParameter] = elem;
   if (m_technique != nullptr && m_currentShaderProgram == m_technique->program)
   {
      const auto location = m_technique->uniformLocation[hParameter];
      if (location.location == -1) return;
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
