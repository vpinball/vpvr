//
// Lights
//

#define NUM_LIGHTS 2

#define iLightPointNum NUM_LIGHTS
#define iLightPointBallsNum (NUM_LIGHTS+NUM_BALL_LIGHTS)

#if iLightPointBallsNum == iLightPointNum // basic shader
uniform vec4 lightPos[iLightPointNum];
uniform vec4 lightEmission[iLightPointNum];
#else
uniform vec4 lightPos[iLightPointBallsNum];
uniform vec4 lightEmission[iLightPointBallsNum];
#endif
uniform int lightSources;

uniform vec4 cAmbient_LightRange;// = vec4(0.0,0.0,0.0, 0.0); //!! remove completely, just rely on envmap/IBL?

uniform vec2 fenvEmissionScale_TexWidth;

uniform vec2 fDisableLighting_top_below;// = vec2(0.,0.);

//
// Material Params
//

uniform vec4 cBase_Alpha;// = vec4(0.5,0.5,0.5, 1.0); //!! 0.04-0.95 in RGB

uniform vec4 Roughness_WrapL_Edge_Thickness;// = vec4(4.0, 0.5, 1.0, 0.05); // wrap in [0..1] for rim/wrap lighting

//
// Material Helper Functions
//

float GeometricOpacity(float NdotV, float alpha, float blending, float t)
{
    //old version without thickness
    //return mix(alpha, 1.0, blending*pow(1.0-abs(NdotV),5)); // fresnel for falloff towards silhouette

    //new version (COD/IW, t = thickness), t = 0.05 roughly corresponds to above version
    float x = abs(NdotV); // flip normal in case of wrong orientation (backside lighting)
    float g = blending - blending * ( x / (x * (1.0 - t) + t) ); // Smith-Schlick G
    return mix(alpha, 1.0, g); // fake opacity lerp to ‘shadowed’
}

vec3 FresnelSchlick(vec3 spec, float LdotH, float edge)
{
    return spec + (vec3(edge,edge,edge) - spec) * pow(1.0 - LdotH, 5); // UE4: vec3(edge,edge,edge) = clamp(50.0*spec.g,0.0, 1.0)
}

float3 mul_w1(float3 v, float4x4 m)
{
    return v.x*m[0].xyz + (v.y*m[1].xyz + (v.z*m[2].xyz + m[3].xyz));
}

vec3 DoPointLight(vec3 pos, vec3 N, vec3 V, vec3 diffuse, vec3 glossy, float edge, float glossyPower, int i, bool is_metal) 
{ 
   // early out here or maybe we can add more material elements without lighting later?
   if(fDisableLighting_top_below.x == 1.0)
      return diffuse;

   vec3 lightDir = mul_w1(lightPos[i].xyz, inverse(matView)) - pos; //!! do in vertex shader?! or completely before?!
   vec3 L = normalize(lightDir);
   float NdotL = dot(N, L);
   vec3 Out = vec3(0.0,0.0,0.0);
   
   // compute diffuse color (lambert with optional rim/wrap component)
   if(!is_metal && (NdotL + Roughness_WrapL_Edge_Thickness.y > 0.0))
      Out = diffuse * ((NdotL + Roughness_WrapL_Edge_Thickness.y) / sqr(1.0+Roughness_WrapL_Edge_Thickness.y));
    
   // add glossy component (modified ashikhmin/blinn bastard), not fully energy conserving, but good enough
   if(NdotL > 0.0)
   {
      vec3 H = normalize(L + V); // half vector
      float NdotH = dot(N, H);
      float LdotH = dot(L, H);
      float VdotH = dot(V, H);
      if((NdotH > 0.0) && (LdotH > 0.0) && (VdotH > 0.0))
         Out += FresnelSchlick(glossy, LdotH, edge) * (((glossyPower + 1.0) / (8.0*VdotH)) * pow(NdotH, glossyPower));
   }
 
   //float fAtten = clamp( 1.0 - dot(lightDir/cAmbient_LightRange.w, lightDir/cAmbient_LightRange.w) ,0.0, 1.0);
   //float fAtten = 1.0/dot(lightDir,lightDir); // original/correct falloff
   
   float sqrl_lightDir = dot(lightDir,lightDir); // tweaked falloff to have ranged lightsources
   float fAtten = 1.0 - sqrl_lightDir*sqrl_lightDir/(cAmbient_LightRange.w*cAmbient_LightRange.w*cAmbient_LightRange.w*cAmbient_LightRange.w); //!! pre-mult/invert cAmbient_LightRange.w?
   fAtten = fAtten*fAtten/(sqrl_lightDir + 1.0);

   vec3 ambient = glossy;
   if(!is_metal)
       ambient += diffuse;

   vec3 result;
   result = Out * lightEmission[i].xyz * fAtten + ambient * cAmbient_LightRange.xyz;

   if(fDisableLighting_top_below.x != 0.0)
       return mix(result,diffuse,fDisableLighting_top_below.x);
   else
       return result;
}

// does /PI-corrected lookup/final color already
vec3 DoEnvmapDiffuse(vec3 N, vec3 diffuse)
{
   vec2 uv = vec2( // remap to 2D envmap coords
		0.5 + atan2_approx_div2PI(N.y, N.x),
	    acos_approx_divPI(N.z));

   vec3 env;
   if (!hdrEnvTextures)
        env = InvGamma(textureLod(Texture2, uv, 0).rgb);
   else
        env = textureLod(Texture2, uv, 0).bgr;
        
   return diffuse * env*fenvEmissionScale_TexWidth.x;
}

//!! PI?
// very very crude approximation by abusing miplevels
vec3 DoEnvmapGlossy(vec2 Ruv, vec3 glossy, float glossyPower)
{
   float mip = min(log2(fenvEmissionScale_TexWidth.y * sqrt(3.0)) - 0.5*log2(glossyPower + 1.0), log2(fenvEmissionScale_TexWidth.y)-1.); //!! do diffuse lookup instead of this limit/min, if too low?? and blend?
   vec3 env;
   if (!hdrEnvTextures)
        env = InvGamma(textureLod(Texture1, Ruv, mip).rgb);
   else
        env = textureLod(Texture1, Ruv, mip).bgr;

   return glossy * env*fenvEmissionScale_TexWidth.x;
}

//!! PI?
vec3 DoEnvmap2ndLayer(vec3 color1stLayer, float NdotV, vec2 Ruv, vec3 specular)
{
   vec3 w = FresnelSchlick(specular, NdotV, Roughness_WrapL_Edge_Thickness.z); //!! ?
   vec3 env;
   if (!hdrEnvTextures)
        env = InvGamma(tex2Dlod(Texture1, float4(Ruv, 0., 0.)).rgb);
   else
        env = tex2Dlod(Texture1, float4(Ruv, 0., 0.)).bgr;

   return mix(color1stLayer, env*fenvEmissionScale_TexWidth.x, w); // weight (optional) lower diffuse/glossy layer with clearcoat/specular
}
vec3 lightLoop(vec3 pos, vec3 N, vec3 V, vec3 diffuse, vec3 glossy, vec3 specular, float edge, bool fix_normal_orientation, bool is_metal) // input vectors (N,V) are normalized for BRDF evals
{
   // normalize BRDF layer inputs //!! use diffuse = (1-glossy)*diffuse instead?
   float diffuseMax = max(diffuse.x,max(diffuse.y,diffuse.z));
   float glossyMax = max(glossy.x,max(glossy.y,glossy.z));
   float specularMax = max(specular.x,max(specular.y,specular.z)); //!! not needed as 2nd layer only so far
   float sum = diffuseMax + glossyMax /*+ specularMax*/;
   if(sum > 1.0)
   {
      float invsum = 1.0/sum;
      diffuse  *= invsum;
      glossy   *= invsum;
      //specular *= invsum;
   }

   float NdotV = dot(N,V);
   if(fix_normal_orientation && (NdotV < 0.0)) // flip normal in case of wrong orientation? (backside lighting), currently disabled if normal mapping active, for that case we should actually clamp the normal with respect to V instead (see f.e. 'view-dependant shading normal adaptation')
   {
      N = -N;
	  NdotV = -NdotV;
   }

   vec3 color = vec3(0.0, 0.0, 0.0);

   // 1st Layer
    if((!is_metal && (diffuseMax > 0.0)) || (glossyMax > 0.0))
    {
        for(int i = 0; i < lightSources; i++)//Rendering issues when doing this in an loop. Weird.
            color += DoPointLight(pos, N, V, diffuse, glossy, edge, Roughness_WrapL_Edge_Thickness.x, i, is_metal); // no clearcoat needed as only pointlights so far
    }

   if(!is_metal && (diffuseMax > 0.0))
      color += DoEnvmapDiffuse(normalize((vec4(N,0.0) * matView).xyz), diffuse); // trafo back to world for lookup into world space envmap // actually: mul(vec4(N,0.0), matViewInverseInverseTranspose), but optimized to save one matrix

   if((glossyMax > 0.0) || (specularMax > 0.0))
   {  
	   vec3 R = (2.0*NdotV)*N - V; // reflect(-V,n);
	   R = normalize((vec4(R,0.0) * matView).xyz); // trafo back to world for lookup into world space envmap // actually: mul(vec4(R,0.0), matViewInverseInverseTranspose), but optimized to save one matrix

	   vec2 Ruv = vec2( // remap to 2D envmap coords
			0.5 + atan2_approx_div2PI(R.y, R.x),
			acos_approx_divPI(R.z));

	   if(glossyMax > 0.0)
		  color += DoEnvmapGlossy(Ruv, glossy, Roughness_WrapL_Edge_Thickness.x);

	   // 2nd Layer
	   if(fix_normal_orientation && specularMax > 0.0)
		  color = DoEnvmap2ndLayer(color, NdotV, Ruv, specular);
   }

   return /*Gamma(ToneMap(*/color/*))*/;
}
