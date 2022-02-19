texture Texture0; // Left Eye
texture Texture1; // Right Eye

float4 width_height_rotated_flipLR;

sampler2D texSampler0 : TEXUNIT0 = sampler_state
{
   Texture = (Texture0);
   MIPFILTER = NONE;
   MAGFILTER = LINEAR;
   MINFILTER = LINEAR;
   ADDRESSU = Clamp;
   ADDRESSV = Clamp;
};

sampler2D texSampler1 : TEXUNIT1 = sampler_state
{
   Texture = (Texture1);
   MIPFILTER = NONE; //!! ??
   MAGFILTER = LINEAR;
   MINFILTER = LINEAR;
   ADDRESSU = Clamp;
   ADDRESSV = Clamp;
};

struct VS_OUTPUT_2D
{
   float4 pos  : POSITION;
   float2 tex0 : TEXCOORD0;
   float2 tex1 : TEXCOORD1;
};

VS_OUTPUT_2D vs_main_tb(float4 vPosition  : POSITION0,
   float2 tc : TEXCOORD0)
{
   VS_OUTPUT_2D Out;

   Out.pos = float4(vPosition.x*2.0 - 1.0, 1.0 - vPosition.y*2.0, 0.0, 1.0);
   Out.tex0 = float2(tc.x,tc.y * 2.0);
   Out.tex1 = float2(tc.x, tc.y * 2.0 - 1.0);

   return Out;
}

float4 ps_main_tb(in VS_OUTPUT_2D IN) : COLOR
{
   if (IN.tex0.y<1.0)
      return tex2D(texSampler0, IN.tex0);
   else 
      return tex2D(texSampler1, IN.tex1);
}

VS_OUTPUT_2D vs_main_sbs(float4 vPosition  : POSITION0,
   float2 tc : TEXCOORD0)
{
   VS_OUTPUT_2D Out;

   Out.pos = float4(vPosition.x*2.0 - 1.0, 1.0 - vPosition.y*2.0, 0.0, 1.0);
   Out.tex0 = float2(tc.x * 2.0, tc.y);
   Out.tex1 = float2(tc.x * 2.0 - 1.0, tc.y);

   return Out;
}

float4 ps_main_sbs(in VS_OUTPUT_2D IN) : COLOR
{
   if (IN.tex0.x < 1.0) {
      return tex2D(texSampler0, IN.tex0);
   } else {
      return tex2D(texSampler1, IN.tex1);
   }
}

VS_OUTPUT_2D vs_main_int(float4 vPosition  : POSITION0,
   float2 tc : TEXCOORD0)
{
   VS_OUTPUT_2D Out;

   float height = width_height_rotated_flipLR.y;

   Out.pos = float4(vPosition.x*2.0 - 1.0, 1.0 - vPosition.y*2.0, 0.0, 1.0);
   Out.tex0 = float2(tc.x, tc.y/* + 0.5/height*/);
   Out.tex1 = float2(tc.x, tc.y/* - 0.5/height*/);

   return Out;
}

float4 ps_main_int(in VS_OUTPUT_2D IN, float4 screenSpace : VPOS) : COLOR
{
   float height = width_height_rotated_flipLR.y;
   if (frac(screenSpace.y*0.5) < 0.25) {
      float4 color = tex2D(texSampler0, IN.tex0);
      return color;
   }
   else {
      float4 color = tex2D(texSampler1, IN.tex1);
      return color;
   }
}

technique stereo_TB
{
   pass P0
   {
      VertexShader = compile vs_3_0 vs_main_tb();
      PixelShader = compile ps_3_0 ps_main_tb();
   }
}

technique stereo_SBS
{
   pass P0
   {
      VertexShader = compile vs_3_0 vs_main_sbs();
      PixelShader = compile ps_3_0 ps_main_sbs();
   }
}

technique stereo_Int
{
   pass P0
   {
      VertexShader = compile vs_3_0 vs_main_int();
      PixelShader = compile ps_3_0 ps_main_int();
   }
}