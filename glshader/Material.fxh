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

vec3 FresnelSchlick(vec3 spec, float LdotH, float edge)
{
    return spec + (vec3(edge,edge,edge) - spec) * pow(1.0 - LdotH, 5); // UE4: vec3(edge,edge,edge) = clamp(50.0*spec.g,0.0, 1.0)
}

vec3 DoPointLight(vec3 pos, vec3 N, vec3 V, vec3 diffuse, vec3 glossy, float edge, int i, bool is_metal) 
{ 
   // early out here or maybe we can add more material elements without lighting later?
   if(fDisableLighting_top_below.x == 1.0)
      return diffuse;

   vec3 lightDir = (matView * vec4(lightPos[i].xyz, 1.0)).xyz - pos; //!! do in vertex shader?! or completely before?!
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
         Out += FresnelSchlick(glossy, LdotH, edge) * clamp((( Roughness_WrapL_Edge_Thickness.x + 1.0) / (8.0*VdotH)) * pow(NdotH,  Roughness_WrapL_Edge_Thickness.x), 0.0, 1.0);
   }
 
   //float fAtten = clamp( 1.0 - dot(lightDir/cAmbient_LightRange.w, lightDir/cAmbient_LightRange.w) ,0.0, 1.0);
   //float fAtten = 1.0/dot(lightDir,lightDir); // original/correct falloff
   
   float sqrl_lightDir = dot(lightDir,lightDir); // tweaked falloff to have ranged lightsources
   float fAtten = clamp(1.0 - sqrl_lightDir*sqrl_lightDir/(cAmbient_LightRange.w*cAmbient_LightRange.w*cAmbient_LightRange.w*cAmbient_LightRange.w),0.0, 1.0); //!! pre-mult/invert cAmbient_LightRange.w?
   fAtten = fAtten*fAtten/(sqrl_lightDir + 1.0);

   vec3 ambient = glossy;
   if(!is_metal)
       ambient += diffuse;

   vec3 result;
#if !enable_VR
      result = Out * lightEmission[i].xyz * fAtten + ambient * cAmbient_LightRange.xyz;
#else
      result = Out * lightEmission[i].xyz * (fAtten*0.00001) + ambient * cAmbient_LightRange.xyz;
#endif

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
   
   // Abuse mipmaps to reduce shimmering in VR
   int mipLevels = textureQueryLevels(Texture2);
   if (!hdrEnvTextures)
        env = InvGamma(textureLod(Texture2, uv, mipLevels/1.6).rgb);
   else
        env = textureLod(Texture2, uv, mipLevels/1.6).bgr;
        
   return diffuse * env*fenvEmissionScale_TexWidth.x;
}
