#include "stdafx.h"
#include "Shader.h"
#include "typeDefs3D.h"
#include "RenderDevice.h"

#include <windows.h>

#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <regex>

#ifdef _DEBUG
//Writes all compile/parse errors/warnings to a file. (1=only errors, 2=warnings, 3=info)
#define DEBUG_LEVEL_LOG 1
//Writes all shaders that are compiled to separate files (e.g. ShaderName_Technique_Pass.vs and .fs) (2=always 1=only if compile failed)
#define WRITE_SHADER_FILES 1
#else 
#define DEBUG_LEVEL_LOG 1
#define WRITE_SHADER_FILES 1
#endif

static std::ofstream* logFile = NULL;
std::string Shader::shaderPath = "";
Matrix3D Shader::mWorld, Shader::mView, Shader::mProj;
int Shader::lastShaderProgram = -1;
D3DTexture* Shader::noTexture = NULL;
static float zeroValues[16] = { 0.0f,0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
float* Shader::zeroData = zeroValues;
int Shader::nextTextureSlot = 0;
int* Shader::textureSlotList = NULL;
std::map<int, int> Shader::slotTextureList;
int Shader::maxSlots = 0;

Shader::~Shader()
{
   shaderCount--;
   this->Unload();
   if (shaderCount == 0) {
      free(textureSlotList);
      maxSlots = 0;
      nextTextureSlot = 0;
      delete noTexture;
      noTexture = NULL;
   }
   textureSlotList = NULL;
   slotTextureList.clear();
   if (m_currentShader == this)
      m_currentShader = NULL;
}


void LOG(int level, const char* fileNameRoot, string message) {
   if (level <= DEBUG_LEVEL_LOG) {
      if (!logFile) {
         string name = Shader::shaderPath;
         name.append("log\\").append(fileNameRoot).append(".log");
         logFile = new std::ofstream();
         logFile->open(name);
         if (!logFile->is_open()) {
            char msg[512];
            TCHAR full_path[MAX_PATH];
            GetFullPathName(_T(name.c_str()), MAX_PATH, full_path, NULL);
            sprintf_s(msg, 512, "could not create logfile %s", full_path);
            ShowError(msg);
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
         (*logFile) << level << ":";
         break;
      }
      (*logFile) << message << "\n";
   }
}

//parse a file. Is called recursively for includes
bool parseFile(const char* fileNameRoot, const char* fileName, int level, std::map<string, string> &values, string parentMode) {
   if (level > 16) {//Can be increased, but looks very much like an infinite recursion.
      LOG(1, fileNameRoot, string("Reached more than 16 include while trying to include ").append(fileName).append("levels. Aborting..."));
      return false;
   }
   if (level > 8) {
      LOG(2, fileNameRoot, string("Reached include level ").append(std::to_string(level)).append(" while trying to include ").append(fileName).append("levels. Check for recursion and try to avoid includes with includes."));
   }
   string line;
   string currentMode = parentMode;
   std::map<string, string>::iterator currentElemIt = values.find(parentMode);
   string currentElement = (currentElemIt != values.end()) ? currentElemIt->second : "";
   std::ifstream glfxFile;
   glfxFile.open(string(Shader::shaderPath).append(fileName), std::ifstream::in);
   size_t linenumber = 0;
   if (glfxFile.is_open())
   {
      while (getline(glfxFile, line))
      {
         linenumber++;
         if (line.compare(0, 4, "////") == 0) {
            string newMode = line.substr(4, line.length() - 4);
            if (newMode.compare(currentMode) != 0) {
               values[currentMode] = currentElement;
               currentElemIt = values.find(newMode);
               currentElement = (currentElemIt != values.end()) ? currentElemIt->second : "";
               currentMode = newMode;
            }
         }
         else if (line.compare(0, 9, "#include ") == 0) {
            size_t start = line.find('"', 8);
            size_t end = line.find('"', start + 1);
            values[currentMode] = currentElement;
            if ((start == string::npos) || (end == string::npos) || (end <= start) || !parseFile(fileNameRoot, line.substr(start + 1, end - start - 1).c_str(), level + 1, values, currentMode)) {
               LOG(1, fileNameRoot, string(fileName).append("(").append(std::to_string(linenumber)).append("):").append(line).append(" failed."));
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
      LOG(1, fileNameRoot, string(fileName).append(" not found."));
      return false;
   }
   return true;
}

//compile and link shader. Also write the created shader files
bool Shader::compileGLShader(const char* fileNameRoot, string shaderCodeName, string vertex, string fragment) {
   bool success = true;
   int result;
   GLuint vertexShader = 0;
   GLuint fragmentShader = 0;
   GLuint shaderprogram = 0;
   GLchar* fragmentSource = NULL;

   //Vertex Shader
   GLchar* vertexSource = (GLchar*)malloc(vertex.length() + 1);
   memcpy((void*)vertexSource, vertex.c_str(), vertex.length());
   vertexSource[vertex.length()] = 0;

   vertexShader = glCreateShader(GL_VERTEX_SHADER);
   CHECKD3D();
   CHECKD3D(glShaderSource(vertexShader, 1, &vertexSource, 0));
   CHECKD3D(glCompileShader(vertexShader));


   CHECKD3D(glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &result));
   if (result == FALSE)
   {
      GLint maxLength;
      CHECKD3D(glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &maxLength));
      char* errorText = (char *)malloc(maxLength);

      CHECKD3D(glGetShaderInfoLog(vertexShader, maxLength, &maxLength, errorText));
      LOG(1, fileNameRoot, string(shaderCodeName).append(": Vertex Shader compilation failed with: ").append(errorText));
      free(errorText);
      success = false;
   }
   //Fragment Shader
   if (success) {
      fragmentSource = (GLchar*)malloc(fragment.length() + 1);
      memcpy((void*)fragmentSource, fragment.c_str(), fragment.length());
      fragmentSource[fragment.length()] = 0;

      fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
      CHECKD3D();
      CHECKD3D(glShaderSource(fragmentShader, 1, &fragmentSource, 0));
      CHECKD3D(glCompileShader(fragmentShader));


      CHECKD3D(glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &result));
      if (result == FALSE)
      {
         GLint maxLength;
         CHECKD3D(glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &maxLength));
         char* errorText = (char *)malloc(maxLength);

         CHECKD3D(glGetShaderInfoLog(fragmentShader, maxLength, &maxLength, errorText));
         LOG(1, fileNameRoot, string(shaderCodeName).append(": Fragment Shader compilation failed with: ").append(errorText));
         free(errorText);
         success = false;
      }
   }
   if (success) {
      shaderprogram = glCreateProgram();

      CHECKD3D(glAttachShader(shaderprogram, vertexShader));
      CHECKD3D(glAttachShader(shaderprogram, fragmentShader));

      CHECKD3D(glLinkProgram(shaderprogram));

      CHECKD3D(glGetProgramiv(shaderprogram, GL_LINK_STATUS, (int *)&result));
      if (result == FALSE)
      {
         GLint maxLength;
         CHECKD3D(glGetProgramiv(shaderprogram, GL_INFO_LOG_LENGTH, &maxLength));

         /* The maxLength includes the NULL character */
         char* errorText = (char *)malloc(maxLength);

         /* Notice that glGetProgramInfoLog, not glGetShaderInfoLog. */
         CHECKD3D(glGetProgramInfoLog(shaderprogram, maxLength, &maxLength, errorText));
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
      shaderCode.open(string(shaderPath).append("log\\").append(shaderCodeName).append(".frag"));
      shaderCode << fragment;
      shaderCode.close();
   }
   CHECKD3D(glDeleteShader(vertexShader));
   CHECKD3D(glDeleteShader(fragmentShader));
   free(fragmentSource);
   free(vertexSource);
   if (success) {
      int count;
      glShader shader;
      shader.program = shaderprogram;
      CHECKD3D(glGetProgramiv(shaderprogram, GL_ACTIVE_UNIFORMS, &count));
      char uniformName[256];
      shader.uniformLocation = new std::map<string, uniformLoc>;
      for (int i = 0;i < count;++i) {
         GLenum type;
         int size;
         int length;
         CHECKD3D(glGetActiveUniform(shader.program, (GLuint)i, 256, &length, &size, &type, uniformName));
         int location = glGetUniformLocation(shader.program, uniformName);
         CHECKD3D();
         if (location >= 0) {
            uniformLoc newLoc;
            newLoc.location = location;
            newLoc.type = type;
            //hack for packedLights, but works for all arrays
            newLoc.size = size;
            for (int i = 0;i < length;i++) {
               if (uniformName[i] == '[') {
                  uniformName[i] = 0;
                  break;
               }
            }
            shader.uniformLocation->operator[](uniformName) = newLoc;
         }
      }
      CHECKD3D(glGetProgramiv(shaderprogram, GL_ACTIVE_ATTRIBUTES, &count));
      char attributeName[256];
      shader.attributeLocation = new std::map<string, attributeLoc>;
      for (int i = 0;i < count;++i) {
         GLenum type;
         int size;
         int length;
         CHECKD3D(glGetActiveAttrib(shader.program, (GLuint)i, 256, &length, &size, &type, attributeName));
         int location = glGetAttribLocation(shader.program, attributeName);
         CHECKD3D();
         if (location >= 0) {
            attributeLoc newLoc;
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
            shader.attributeLocation->operator[](attributeName) = newLoc;
         }
      }
      shaderList.insert(std::pair<string, glShader>(shaderCodeName, shader));
   }
   return success;
}

//Check if technique is valid and replace %PARAMi% with the values in the function header
string analyzeFunction(const char* shaderCodeName, string technique, string functionName, std::map<string, string> &values) {
   int start, end;
   start = functionName.find("(");
   end = functionName.find(")");
   if ((start == string::npos) || (end == string::npos) || (start > end)) {
      LOG(2, (const char*)shaderCodeName, string("Invalid technique: ").append(technique));
      return "";
   }
   std::map<string, string>::iterator it = values.find(functionName.substr(0, start));
   string functionCode = (it != values.end()) ? it->second : "";
   if (end > start + 1) {
      std::stringstream params(functionName.substr(start + 1, end - start - 1));
      std::string param;
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
   if (!textureSlotList) {
      glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxSlots);
      textureSlotList = (int*)malloc(maxSlots * sizeof(int));
      for (int i = 0;i < maxSlots;++i)
         textureSlotList[i] = -2;
   }
   m_currentTechnique = NULL;
   LOG(3, (const char*)shaderCodeName, "Start parsing file");
   std::map<string, string> values;
   bool success = parseFile((const char*)shaderCodeName, (const char*)shaderCodeName, 0, values, "GLOBAL");
   if (!success) {
      LOG(1, (const char*)shaderCodeName, "Parsing failed\n");
   }
   else {
      LOG(3, (const char*)shaderCodeName, "Parsing successful. Start compiling shaders");
   }
   std::map<string, string>::iterator it = values.find("GLOBAL");
   string global = (it != values.end()) ? it->second : "";
   it = values.find("VERTEX");
   string vertex = global;
   vertex.append((it != values.end()) ? it->second : "");
   it = values.find("FRAGMENT");
   string fragment = global;
   fragment.append((it != values.end()) ? it->second : "");
   it = values.find("TECHNIQUES");
   std::stringstream techniques((it != values.end()) ? it->second : "");
   std::string technique;
   if (techniques)
   {
      int tecCount = 0;
      while (std::getline(techniques, technique, '\n')) {//Parse Technique e.g. basic_with_texture_normal_isMetal:P0:vs_main():ps_main_texture(1,1)
         if ((technique.length() > 0) && (technique.compare(0, 2, "//") != 0))//Skip empty lines and comments
         {
            std::stringstream elements(technique);
            int elem = 0;
            std::string element[4];
            //Split :
            while ((elem < 4) && std::getline(elements, element[elem], ':')) {
               elem++;
            }
            if (elem < 4) {
               continue;
            }
            string vertexShaderCode = vertex;
            vertexShaderCode.append("\n//").append(technique).append("\n//").append(element[2]).append("\n");
            vertexShaderCode.append(analyzeFunction(shaderCodeName, technique, element[2], values)).append("\0");
            string fragmentShaderCode = fragment;
            fragmentShaderCode.append("\n//").append(technique).append("\n//").append(element[3]).append("\n");
            fragmentShaderCode.append(analyzeFunction(shaderCodeName, technique, element[3], values)).append("\0");
            int build = compileGLShader(shaderCodeName, element[0].append("_").append(element[1]), vertexShaderCode, fragmentShaderCode);
            if (build) tecCount++;
            success = success && build;
         }
      }
      LOG(3, (const char*)shaderCodeName, string("Compiled successfully ").append(std::to_string(tecCount)).append(" shaders."));
   }
   else {
      LOG(1, (const char*)shaderCodeName, "No techniques found.\n");
      success = false;
   }
   //GLOBAL+VERTEX
   //GLOBAL+FRAGMENT
   //values durchiterieren und compileGLShader ausführen
   if (logFile) {
      logFile->close();
   }
   logFile = NULL;
   //Set default values from Material.fxh for uniforms.
   SetVector("cBase_Alpha", 0.5f, 0.5f, 0.5f, 1.0f);
   SetVector("Roughness_WrapL_Edge_Thickness", 4.0f, 0.5f, 1.0f, 0.05f);
   return success;
}

void Shader::Unload()
{
   //Free all uniform cache pointers
   for (auto it = uniformFloatP.begin(); it != uniformFloatP.end(); it++)
   {
      if (it->second.data)
         free(it->second.data);
   }
   uniformFloatP.clear();
   //Delete all glPrograms and their uniformLocation cache
   for (auto it = shaderList.begin(); it != shaderList.end(); it++)
   {
      CHECKD3D(glDeleteProgram(it->second.program));
      it->second.uniformLocation->clear();
   }
   shaderList.clear();
}

void Shader::setAttributeFormat(DWORD fvf)
{
   for (auto it = m_currentTechnique->attributeLocation->begin(); it != m_currentTechnique->attributeLocation->end(); it++)
   {
      int offset;
      int size;
      attributeLoc currentAttribute = it->second;
      CHECKD3D(glEnableVertexAttribArray(currentAttribute.location));
      switch (fvf) {
      case MY_D3DFVF_TEX:
         if (it->first.compare("vPosition") == 0) offset = 0;
         else if (it->first.compare("tc") == 0) offset = 12;
         else if (it->first.compare("tex0") == 0) offset = 12;
         else {
            ReportError("unknown Attribute", 666, __FILE__, __LINE__);
            exit(-1);
         }
         size = sizeof(float)*(3 + 2);
         break;
      case MY_D3DFVF_NOTEX2_VERTEX:
      case MY_D3DTRANSFORMED_NOTEX2_VERTEX:
         if (it->first.compare("vPosition") == 0) offset = 0;
         else if (it->first.compare("vNormal") == 0) offset = 12;
         else if (it->first.compare("tc") == 0) offset = 24;
         else if (it->first.compare("tex0") == 0) offset = 24;
         else {
            ReportError("unknown Attribute", 666, __FILE__, __LINE__);
            exit(-1);
         }
         size = sizeof(float)*(3 + 3 + 2);
         break;
      default:
         //broken?
         ReportError("unknown Attribute configuration", 666,__FILE__, __LINE__);
         exit(-1);
      }
      CHECKD3D(glVertexAttribPointer(currentAttribute.location, currentAttribute.size, GL_FLOAT, GL_FALSE, (fvf == MY_D3DFVF_TEX) ? 20 : 32, (void*)offset));
   }
}

void Shader::Begin(const unsigned int pass)
{
   nextTextureSlot = 0;
   m_currentShader = this;
   CHECKD3D();
   char msg[256];
   string techName = string(technique).append("_P").append(std::to_string(pass));
   auto tec = shaderList.find(techName);
   if (tec == shaderList.end()) {
      sprintf_s(msg, 256, "Could not find shader technique %s", technique);
      ShowError(msg);
      exit(-1);
   }
   m_currentTechnique = &(tec->second);
   if (lastShaderProgram != m_currentTechnique->program)
   {
      CHECKD3D(glUseProgram(m_currentTechnique->program));
      lastShaderProgram = m_currentTechnique->program;
   }
   //Set all uniforms
   for (auto it = m_currentTechnique->uniformLocation->begin(); it != m_currentTechnique->uniformLocation->end(); it++)
   {
      uniformLoc currentUniform = it->second;
      switch (currentUniform.type) {
      case GL_FLOAT:
      {
         auto valueF = uniformFloat.find(it->first);
            CHECKD3D(glUniform1f(currentUniform.location, (valueF != uniformFloat.end()) ? valueF->second : 0.0f));
      }
      break;
      case GL_BOOL:
      case GL_INT:
      {
         auto valueI = uniformInt.find(it->first);
            CHECKD3D(glUniform1i(currentUniform.location, (valueI != uniformInt.end()) ? valueI->second : 0));
      }
      break;
      case GL_FLOAT_VEC2:
      {
         auto valueFP = uniformFloatP.find(it->first);
         if ((valueFP != uniformFloatP.end()) && valueFP->second.data)
            {CHECKD3D(glUniform2f(currentUniform.location, valueFP->second.data[0], valueFP->second.data[1]));}
         else 
            {CHECKD3D(glUniform2f(currentUniform.location, 0.0f, 0.0f));}
      }
      break;
      case GL_FLOAT_VEC3:
      {
         auto valueFP = uniformFloatP.find(it->first);
         if ((valueFP != uniformFloatP.end()) && valueFP->second.data)
            {CHECKD3D(glUniform3f(currentUniform.location, valueFP->second.data[0], valueFP->second.data[1], valueFP->second.data[2]));}
         else 
            {CHECKD3D(glUniform3f(currentUniform.location, 0.0f, 0.0f, 0.0f));}
      }
      break;
      case GL_FLOAT_VEC4:
      {
         auto valueFP = uniformFloatP.find(it->first);
         if ((valueFP != uniformFloatP.end()) && valueFP->second.data)
            {CHECKD3D(glUniform4fv(currentUniform.location, valueFP->second.len/4, valueFP->second.data));}
         else 
            {CHECKD3D(glUniform4f(currentUniform.location, 0.0f, 0.0f, 0.0f, 0.0f));}
      }
      break;
      case GL_FLOAT_MAT2:
      {
         auto valueFP = uniformFloatP.find(it->first);
         CHECKD3D(glUniformMatrix2fv(currentUniform.location, 1, GL_FALSE, ((valueFP != uniformFloatP.end()) && valueFP->second.data) ? valueFP->second.data : zeroData));
      }
      break;
      case GL_FLOAT_MAT3:
      {
         auto valueFP = uniformFloatP.find(it->first);
         CHECKD3D(glUniformMatrix3fv(currentUniform.location, 1, GL_FALSE, ((valueFP != uniformFloatP.end()) && valueFP->second.data) ? valueFP->second.data : zeroData));
      }
      break;
      case GL_FLOAT_MAT4:
      {
         auto valueFP = uniformFloatP.find(it->first);
         CHECKD3D(glUniformMatrix4fv(currentUniform.location, 1, GL_FALSE, ((valueFP != uniformFloatP.end()) && valueFP->second.data) ? valueFP->second.data : zeroData));
      }
      break;
      case GL_FLOAT_MAT4x3:
      {
         auto valueFP = uniformFloatP.find(it->first);
         CHECKD3D(glUniformMatrix4x3fv(currentUniform.location, 1, GL_FALSE, ((valueFP != uniformFloatP.end()) && valueFP->second.data) ? valueFP->second.data : zeroData));
      }
      break;
      case GL_FLOAT_MAT4x2:
      {
         auto valueFP = uniformFloatP.find(it->first);
         CHECKD3D(glUniformMatrix4x2fv(currentUniform.location, 1, GL_FALSE, ((valueFP != uniformFloatP.end()) && valueFP->second.data) ? valueFP->second.data : zeroData));
      }
      break;
      case GL_FLOAT_MAT3x4:
      {
         auto valueFP = uniformFloatP.find(it->first);
         CHECKD3D(glUniformMatrix3x4fv(currentUniform.location, 1, GL_FALSE, ((valueFP != uniformFloatP.end()) && valueFP->second.data) ? valueFP->second.data : zeroData));
      }
      break;
      case GL_FLOAT_MAT3x2:
      {
         auto valueFP = uniformFloatP.find(it->first);
         CHECKD3D(glUniformMatrix3x2fv(currentUniform.location, 1, GL_FALSE, ((valueFP != uniformFloatP.end()) && valueFP->second.data) ? valueFP->second.data : zeroData));
      }
      break;
      case GL_FLOAT_MAT2x3:
      {
         auto valueFP = uniformFloatP.find(it->first);
         CHECKD3D(glUniformMatrix2x3fv(currentUniform.location, 1, GL_FALSE, ((valueFP != uniformFloatP.end()) && valueFP->second.data) ? valueFP->second.data : zeroData));
      }
      break;
      case GL_SAMPLER_2D:
      {
         auto valueT = uniformTex.find(it->first);
/*         int Shader::nextTextureSlot = 0;
         static int* textureSlotList = NULL;
         static std::map<int, int> slotTextureList;*/
         int TextureID;
         if (valueT != uniformTex.end()) {
            TextureID = valueT->second;
         }
         else {
            if (!noTexture) { 
               unsigned int data[4] = { 0xff0000ff, 0xffffff00, 0xffff0000, 0xff00ff00 };
               noTexture = m_renderDevice->CreateTexture(2, 2, 0, STATIC, RGBA, &data);
            }
            TextureID = noTexture->texture;
         }
//Texture Cache
/*         auto slot = slotTextureList.find(TextureID);
         if ((slot == slotTextureList.end()) || (textureSlotList[slot->second] != TextureID)) {
            CHECKD3D(glActiveTexture(GL_TEXTURE0 + nextTextureSlot));
            CHECKD3D(glBindTexture(GL_TEXTURE_2D, TextureID));//TODO implement a cache for textures
            CHECKD3D(glUniform1i(currentUniform.location, nextTextureSlot));
            slotTextureList[TextureID] = nextTextureSlot;
            textureSlotList[nextTextureSlot] = TextureID;
            nextTextureSlot = (++nextTextureSlot) % maxSlots;
         }
         else {
            CHECKD3D(glActiveTexture(GL_TEXTURE0 + slot->second));
            CHECKD3D(glBindTexture(GL_TEXTURE_2D, TextureID));//TODO implement a cache for textures
            CHECKD3D(glUniform1i(currentUniform.location, slot->second));
         }
         */
         CHECKD3D(glActiveTexture(GL_TEXTURE0 + nextTextureSlot));
         CHECKD3D(glBindTexture(GL_TEXTURE_2D, TextureID));//TODO implement a cache for textures
         CHECKD3D(glUniform1i(currentUniform.location, nextTextureSlot));
         nextTextureSlot = (++nextTextureSlot) % maxSlots;
      }
      break;
      default:
         sprintf_s(msg, 256, "Unknown uniform type 0x%0002X for %s in %s", currentUniform.type, it->first.c_str(), techName.c_str());
         ShowError(msg);
      }
   }
}

void Shader::setTextureDirty(int TextureID) {//Invalidate cache
   auto slot = slotTextureList.find(TextureID);
   if ((slot != slotTextureList.end()) && (textureSlotList[slot->second] == TextureID)) {
      textureSlotList[slot->second] = -1;
   }
}


void Shader::End()
{
   //Nothing to do for GL
   CHECKD3D();
}

void Shader::SetTextureDepth(const D3DXHANDLE texelName, D3DTexture *texel) {
   uniformTex[texelName] = texel->zTexture;
}

void Shader::SetTexture(const D3DXHANDLE texelName, Texture *texel, const bool linearRGB)
{
   if (!texel || !texel->m_pdsBuffer) {
      SetTextureNull(texelName);
   }
   else {
      SetTexture(texelName, m_renderDevice->m_texMan.LoadTexture(texel->m_pdsBuffer, linearRGB), linearRGB);
      SetBool("SRGBTexture", linearRGB);
   }
}

void Shader::SetTexture(const D3DXHANDLE texelName, D3DTexture *texel, const bool linearRGB)
{
   if (texel)
      uniformTex[texelName] = texel->texture;
   else
      uniformTex[texelName] = 0;
}

void Shader::SetTextureNull(const D3DXHANDLE texelName)
{
   //Using an unset texture leads to undefined behavior, so keeping the texture is absolutely fine.
}

void Shader::SetTechnique(const D3DXHANDLE technique)
{
   strcpy_s(this->technique, technique);
}

void Shader::SetMatrix(const D3DXHANDLE hParameter, const Matrix3D* pMatrix)
{
   auto element = uniformFloatP.find(hParameter);
   floatP elem;
   if ((element == uniformFloatP.end()) || (element->second.data == NULL)) {
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
   memcpy(elem.data, pMatrix, 16 * sizeof(float));
   uniformFloatP[hParameter] = elem;
}

void Shader::SetVector(const D3DXHANDLE hParameter, const vec4* pVector)
{
   auto element = uniformFloatP.find(hParameter);
   floatP elem;
   if ((element == uniformFloatP.end()) || (element->second.data == NULL)) {
      elem.data = (float*)malloc(4 * sizeof(float));
      elem.len = 4;
   }
   else if (element->second.len < 4) {
      free(element->second.data);
      elem.data = (float*)malloc(4 * sizeof(float));
      elem.len = 4;
   }
   else
      elem = element->second;
   memcpy(elem.data, pVector, 4 * sizeof(float));
   uniformFloatP[hParameter] = elem;
}

void Shader::SetVector(const D3DXHANDLE hParameter, const float x, const float y, const float z, const float w)
{
   auto element = uniformFloatP.find(hParameter);
   floatP elem;
   if ((element == uniformFloatP.end()) || (element->second.data == NULL)) {
      elem.data = (float*)malloc(4 * sizeof(float));
      elem.len = 4;
   }
   else if (element->second.len < 4) {
      free(element->second.data);
      elem.data = (float*)malloc(4 * sizeof(float));
      elem.len = 4;
   }
   else
      elem = element->second;
   elem.data[0] = x;
   elem.data[1] = y;
   elem.data[2] = z;
   elem.data[3] = w;
   uniformFloatP[hParameter] = elem;
}

void Shader::SetFloat(const D3DXHANDLE hParameter, const float f)
{
   uniformFloat[hParameter] = f;
}

void Shader::SetInt(const D3DXHANDLE hParameter, const int i)
{
   uniformInt[hParameter] = i;
}

void Shader::SetBool(const D3DXHANDLE hParameter, const bool b)
{
   uniformInt[hParameter] = b ? 1 : 0;
}

void Shader::SetFloatArray(const D3DXHANDLE hParameter, const float* pData, const unsigned int count)
{
   auto element = uniformFloatP.find(hParameter);
   floatP elem;
   if ((element == uniformFloatP.end()) || (element->second.data == NULL)) {
      elem.data = (float*)malloc(count * sizeof(float));
      elem.len = count;
   }
   else if (element->second.len < count) {
      free(element->second.data);
      elem.data = (float*)malloc(count * sizeof(float));
      elem.len = count;
   }
   else
      elem = element->second;
   memcpy(elem.data, pData, count * sizeof(float));
   uniformFloatP[hParameter] = elem;
}

void Shader::GetTransform(const TransformStateType p1, Matrix3D* p2)
{
   switch (p1) {
   case TRANSFORMSTATE_WORLD:
      memcpy(p2, &Shader::mWorld, sizeof(Matrix3D));
      break;
   case TRANSFORMSTATE_VIEW:
      memcpy(p2, &Shader::mView, sizeof(Matrix3D));
      break;
   case TRANSFORMSTATE_PROJECTION:
      memcpy(p2, &Shader::mProj, sizeof(Matrix3D));
      break;
   }
}

void Shader::SetTransform(const TransformStateType p1, const Matrix3D * p2)
{
   switch (p1) {
   case TRANSFORMSTATE_WORLD:
      memcpy(&Shader::mWorld, p2, sizeof(Matrix3D));
      break;
   case TRANSFORMSTATE_VIEW:
      memcpy(&Shader::mView, p2, sizeof(Matrix3D));
      break;
   case TRANSFORMSTATE_PROJECTION:
      memcpy(&Shader::mProj, p2, sizeof(Matrix3D));
      break;
   }
}