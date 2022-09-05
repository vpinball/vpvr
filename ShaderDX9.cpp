#include "stdafx.h"
#include "Shader.h"
#include "typedefs3D.h"
#include "RenderDevice.h"

// loads an HLSL effect file
// if fromFile is true the shaderName should point to the full filename (with path) to the .fx file
// if fromFile is false the shaderName should be the resource name not the IDC_XX_YY value. Search vpinball_eng.rc for ".fx" to see an example
bool Shader::Load(const BYTE* shaderCodeName, UINT codeSize)
{
   LPD3DXBUFFER pBufferErrors;
   DWORD dwShaderFlags = 0; //D3DXSHADER_SKIPVALIDATION // these do not have a measurable effect so far (also if used in the offline fxc step): D3DXSHADER_PARTIALPRECISION, D3DXSHADER_PREFER_FLOW_CONTROL/D3DXSHADER_AVOID_FLOW_CONTROL
   HRESULT hr;
   /*
   if (fromFile)
   {
   dwShaderFlags = D3DXSHADER_DEBUG|D3DXSHADER_SKIPOPTIMIZATION;
   hr = D3DXCreateEffectFromFile(	m_renderDevice->GetCoreDevice(),		// pDevice
   shaderName,			// pSrcFile
   NULL,				// pDefines
   NULL,				// pInclude
   dwShaderFlags,		// Flags
   NULL,				// pPool
   &m_shader,			// ppEffect
   &pBufferErrors);		// ppCompilationErrors
   }
   else
   {
   hr = D3DXCreateEffectFromResource(	m_renderDevice->GetCoreDevice(),		// pDevice
   NULL,
   shaderName,			// resource name
   NULL,				// pDefines
   NULL,				// pInclude
   dwShaderFlags,		// Flags
   NULL,				// pPool
   &m_shader,			// ppEffect
   &pBufferErrors);		// ppCompilationErrors
   }
   */
   hr = D3DXCreateEffect(m_renderDevice->GetCoreDevice(), shaderCodeName, codeSize, NULL, NULL, dwShaderFlags, NULL, &m_shader, &pBufferErrors);
   if (FAILED(hr))
   {
      if (pBufferErrors)
      {
         LPVOID pCompileErrors = pBufferErrors->GetBufferPointer();
         g_pvp->MessageBox((const char*)pCompileErrors, "Compile Error", MB_OK | MB_ICONEXCLAMATION);
      }
      else
         g_pvp->MessageBox("Unknown Error", "Compile Error", MB_OK | MB_ICONEXCLAMATION);

      return false;
   }
   return true;
}

void Shader::Unload()
{
   if (m_shader)
   {
      SAFE_RELEASE(m_shader);
   }
}

Shader::~Shader()
{
   shaderCount--;
   this->Unload();
}

void Shader::Begin(const unsigned int pass)
{
   m_currentShader = this;
   unsigned int cPasses;
   CHECKD3D(m_shader->Begin(&cPasses, 0));
   CHECKD3D(m_shader->BeginPass(pass));
}

void Shader::End()
{
   CHECKD3D(m_shader->EndPass());
   CHECKD3D(m_shader->End());
}

void Shader::SetTexture(const ShaderUniforms texelName, Texture *texel, const bool clampU, const bool clampV, const bool force_linear_rgb)
{
   const unsigned int idx = texelName[strlen(texelName) - 1] - '0'; // current convention: SetTexture gets "TextureX", where X 0..4
   const bool cache = idx < TEXTURESET_STATE_CACHE_SIZE;

   if (!texel || !texel->m_pdsBuffer) {
      if (cache) {
         currentTexture[idx] = nullptr; // invalidate cache
      }
      CHECKD3D(m_shader->SetTexture(texelName, nullptr));

      m_renderDevice->m_curTextureChanges++;

      return;
   }

   if (!cache || (texel->m_pdsBuffer != currentTexture[idx]))
   {
      if (cache)
         currentTexture[idx] = texel->m_pdsBuffer;
      CHECKD3D(m_shader->SetTexture(texelName, m_renderDevice->m_texMan.LoadTexture(texel->m_pdsBuffer, clampU, clampV, force_linear_rgb)));

      m_renderDevice->m_curTextureChanges++;
   }
}

void Shader::SetTexture(const ShaderUniforms texelName, D3DTexture *texel)
{
   const unsigned int idx = texelName[strlen(texelName) - 1] - '0'; // current convention: SetTexture gets "TextureX", where X 0..4
   if (idx < TEXTURESET_STATE_CACHE_SIZE)
      currentTexture[idx] = nullptr; // direct set of device tex invalidates the cache

   CHECKD3D(m_shader->SetTexture(texelName, texel));

   m_renderDevice->m_curTextureChanges++;
}

void Shader::SetTextureNull(const ShaderUniforms texelName)
{
   const unsigned int idx = texelName[strlen(texelName) - 1] - '0'; // current convention: SetTexture gets "TextureX", where X 0..4
   const bool cache = idx < TEXTURESET_STATE_CACHE_SIZE;

   if (cache)
      currentTexture[idx] = nullptr; // direct set of device tex invalidates the cache

   CHECKD3D(m_shader->SetTexture(texelName, nullptr));

   m_renderDevice->m_curTextureChanges++;
}

void Shader::SetTechnique(const ShaderTechniques _technique)
{
   if (strcmp(currentTechnique, _technique) /*|| (m_renderDevice->m_curShader != this)*/)
   {
      strncpy_s(currentTechnique, _technique, sizeof(currentTechnique) - 1);
      //m_renderDevice->m_curShader = this;
      CHECKD3D(m_shader->SetTechnique(_technique));
      m_renderDevice->m_curTechniqueChanges++;
   }
}

void Shader::SetMatrix(const ShaderUniforms hParameter, const Matrix3D* pMatrix)
{
   /*CHECKD3D(*/m_shader->SetMatrix(hParameter, (const D3DXMATRIX*)pMatrix)/*)*/; // leads to invalid calls when setting some of the matrices (as hlsl compiler optimizes some down to less than 4x4)
   m_renderDevice->m_curParameterChanges++;
}

void Shader::SetVector(const ShaderUniforms hParameter, const vec4* pVector)
{
   CHECKD3D(m_shader->SetVector(hParameter, pVector));
   m_renderDevice->m_curParameterChanges++;
}

void Shader::SetVector(const ShaderUniforms hParameter, const float x, const float y, const float z, const float w)
{
   vec4 pVector = vec4(x, y, z, w);
   CHECKD3D(m_shader->SetVector(hParameter, &pVector));
}

void Shader::SetFloat(const ShaderUniforms hParameter, const float f)
{
   CHECKD3D(m_shader->SetFloat(hParameter, f));
   m_renderDevice->m_curParameterChanges++;
}

void Shader::SetInt(const ShaderUniforms hParameter, const int i)
{
   CHECKD3D(m_shader->SetInt(hParameter, i));
   m_renderDevice->m_curParameterChanges++;
}

void Shader::SetBool(const ShaderUniforms hParameter, const bool b)
{
   CHECKD3D(m_shader->SetBool(hParameter, b));
   m_renderDevice->m_curParameterChanges++;
}

void Shader::SetFloatArray(const ShaderUniforms hParameter, const float* pData, const unsigned int count)
{
   CHECKD3D(m_shader->SetValue(hParameter, pData, count*sizeof(float)));
   m_renderDevice->m_curParameterChanges++;
}

void Shader::GetTransform(const TransformStateType p1, Matrix3D* p2, const int count)
{
   CHECKD3D(m_renderDevice->GetCoreDevice()->GetTransform((D3DTRANSFORMSTATETYPE)p1, p2));
}

void Shader::SetTransform(const TransformStateType p1, const Matrix3D * p2, const int count)
{
   CHECKD3D(m_renderDevice->GetCoreDevice()->SetTransform((D3DTRANSFORMSTATETYPE)p1, p2));
}
