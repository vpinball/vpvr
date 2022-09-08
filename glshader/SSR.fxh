////ps_main_fb_ss_refl

// uses tex_fb_filtered, tex_fb_unfiltered, tex_ao_dither & tex_depth & w_h_height.xy

in vec2 tex0;

vec4 SSR_bumpHeight_fresnelRefl_scale_FS;

vec3 approx_bump_normal(vec2 coords, vec2 offs, float scale, float sharpness)
{
    vec3 lumw = float3(0.212655,0.715158,0.072187);

    float lpx = dot(textureLod(tex_fb_filtered, vec2(coords.x+offs.x,coords.y), 0).xyz, lumw);
    float lmx = dot(textureLod(tex_fb_filtered, vec2(coords.x-offs.x,coords.y), 0).xyz, lumw);
    float lpy = dot(textureLod(tex_fb_filtered, vec2(coords.x,coords.y+offs.y), 0).xyz, lumw);
    float lmy = dot(textureLod(tex_fb_filtered, vec2(coords.x,coords.y-offs.y), 0).xyz, lumw);

    float dpx = textureLod(tex_depth, vec2(coords.x+offs.x,coords.y), 0).x;
    float dmx = textureLod(tex_depth, vec2(coords.x-offs.x,coords.y), 0).x;
    float dpy = textureLod(tex_depth, vec2(coords.x,coords.y+offs.y), 0).x;
    float dmy = textureLod(tex_depth, vec2(coords.x,coords.y-offs.y), 0).x;

    vec2 xymult = max(1.0 - vec2(abs(dmx - dpx), abs(dmy - dpy)) * sharpness, 0.0);

    return normalize(vec3(vec2(lmx - lpx,lmy - lpy)*xymult/offs, scale));
}

float normal_fade_factor(vec3 n)
{
    return min(sqr(1.0-n.z)*0.5 + max(SSR_bumpHeight_fresnelRefl_scale_FS.w == 0.0 ? n.y : n.x,0.0) + abs(SSR_bumpHeight_fresnelRefl_scale_FS.w == 0.0 ? n.x : n.y)*0.5,1.0); // dot(n,float3(0,0,1))  dot(n,float3(0,1,0))  dot(n,float3(1,0,0)) -> penalty for z-axis/up (geometry like playfield), bonus for y-axis (like backwall) and x-axis (like sidewalls)
}

void main()
{
	vec2 u = tex0 + w_h_height.xy*0.5;

	vec3 color0 = textureLod(tex_fb_unfiltered, u, 0).xyz; // original pixel

	float depth0 = color0.x;
	if((depth0 == 1.0) || (depth0 == 0.0)) {//!!! early out if depth too large (=BG) or too small (=DMD,etc -> retweak render options (depth write on), otherwise also screwup with stereo)
		color = vec4(color0, 1.0);
		return;
	}

	vec3 normal = normalize(get_nonunit_normal(depth0,u));
	vec3 normal_b = approx_bump_normal(u, 0.01 * w_h_height.xy / depth0, depth0 / (0.05*depth0 + 0.0001), 1000.0); //!! magic
	     normal_b = normalize(vec3(normal.xy*normal_b.z + normal_b.xy*normal.z, normal.z*normal_b.z));
	     normal_b = normalize(mix(normal,normal_b, SSR_bumpHeight_fresnelRefl_scale_FS.x * normal_fade_factor(normal))); // have less impact of fake bump normals on playfield, etc

	vec3 V = normalize(vec3(0.5-u, -0.5)); // WTF?! cam is in 0,0,0 but why z=-0.5?

	float fresnel = (SSR_bumpHeight_fresnelRefl_scale_FS.y + (1.0-SSR_bumpHeight_fresnelRefl_scale_FS.y) * pow(1.0-saturate(dot(V,normal_b)),5)) // fresnel for falloff towards silhouette
	               * SSR_bumpHeight_fresnelRefl_scale_FS.z // user scale
	               * sqr(normal_fade_factor(normal_b/*normal*/)); // avoid reflections on playfield, etc

#if 0 // test code
	color = vec4(0.,sqr(normal_fade_factor(normal_b/*normal*/)),0., 1.0);
	return;
#endif

	if(fresnel < 0.01) {//!! early out if contribution too low
		color = vec4(color0, 1.0);
		return;
	}

	int samples = 32;
	float ReflBlurWidth = 2.2; //!! magic, small enough to not collect too much, and large enough to have cool reflection effects

	float ushift = /*hash(IN.tex0) + w_h_height.zw*/ // jitter samples via hash of position on screen and then jitter samples by time //!! see below for non-shifted variant
	               /*frac(*/textureLod(tex_ao_dither, tex0/(64.0*w_h_height.xy), 0).z /*+ w_h_height.z)*/; // use dither texture instead nowadays // 64 is the hardcoded dither texture size for AOdither.bmp

	vec2 offsMul = normal_b.xy * (/*w_h_height.xy*/ vec2(1.0/1920.0,1.0/1080.0) * ReflBlurWidth * (32./float(samples))); //!! makes it more resolution independent?? test with 4xSSAA

	// loop in screen space, simply collect all pixels in the normal direction (not even a depth check done!)
	vec3 refl = vec3(0.,0.,0.);
	float color0w = 0.;
	for(int i=1; i</*=*/samples; i++) //!! due to jitter
	{
		vec2 offs = (float(i)+ushift)*offsMul + u; //!! jitter per pixel (uses blue noise tex)
		vec3 color = textureLod(tex_fb_filtered, offs, 0).xyz;
		
		/*if(i==1) // necessary special case as compiler warns/'optimizes' sqrt below into rqsrt?!
		{
			refl += color;
		}
		else
		{*/
         float w = sqrt(float(i-1)/float(samples)); //!! fake falloff for samples more far away
         refl += color*(1.0-w); //!! dampen large color values in addition?
         color0w += w;
		//}
	}

	refl = color0*color0w + refl;
	refl = refl * 1.0/float(samples-1); //!! -1 due to jitter

	color = vec4(mix(color0, refl, min(fresnel,1.0)), 1.0);
}
