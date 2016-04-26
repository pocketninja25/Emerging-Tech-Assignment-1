//--------------------------------------------------------------------------------------
//	File: PostProcess.fx
//
//	Full screen post processing
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Global Variables
//--------------------------------------------------------------------------------------

// Texture maps
Texture2D SceneTexture;   // Texture containing the scene to copy to the full screen quad
Texture2D PostProcessMap; // Second map for special purpose textures used during post-processing

// Samplers to use with the above texture maps. Specifies texture filtering and addressing mode to use when accessing texture pixels
// Use point sampling for the scene texture (i.e. no bilinear/trilinear blending) since don't want to blur it in the copy process
SamplerState PointSample
{
    Filter = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};

// Use the usual filtering for the special purpose post-processing textures (e.g. the noise map)
SamplerState TrilinearWrap
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Wrap;
    AddressV = Wrap;
};

// Other variables used for individual post-processes
float3 TintColour;
float2 NoiseScale;
float2 NoiseOffset;
float  DistortLevel;
float  BurnLevel;
float  Wiggle;


//--------------------------------------------------------------------------------------
// Structures
//--------------------------------------------------------------------------------------

// This vertex shader input uses a special input type, the vertex ID. This value is automatically generated and does not come from a vertex buffer.
// The value starts at 0 and increases by one with each vertex processed. As this is the only input for post processing - **no vertex/index buffers are required**
struct VS_POSTPROCESS_INPUT
{
    uint vertexId : SV_VertexID;
};

// Simple vertex shader output / pixel shader input for the main post processing step
struct PS_POSTPROCESS_INPUT
{
    float4 ProjPos : SV_POSITION;
	float2 UV      : TEXCOORD0;
};


//--------------------------------------------------------------------------------------
// Vertex Shaders
//--------------------------------------------------------------------------------------

// This rather unusual shader generates its own vertices - the input data is merely the vertex ID - an automatically generated increasing index.
// No vertex or index buffer required, so convenient on the C++ side. Probably not so efficient, but fine for a full-screen quad.
PS_POSTPROCESS_INPUT FullScreenQuad(VS_POSTPROCESS_INPUT vIn)
{
    PS_POSTPROCESS_INPUT vOut;
	
	float4 QuadPos[4] = { float4(-1.0, 1.0, 0.0, 1.0),
	                      float4(-1.0,-1.0, 0.0, 1.0),
						  float4( 1.0, 1.0, 0.0, 1.0),
						  float4( 1.0,-1.0, 0.0, 1.0) };
	float2 QuadUV[4] =  { float2(0.0, 0.0),
	                      float2(0.0, 1.0),
						  float2(1.0, 0.0),
						  float2(1.0, 1.0) };

	vOut.ProjPos = QuadPos[vIn.vertexId];
	vOut.UV = QuadUV[vIn.vertexId];
	
    return vOut;
}


//--------------------------------------------------------------------------------------
// Post-processing Pixel Shaders
//--------------------------------------------------------------------------------------

// Post-processing shader that simply outputs the scene texture, i.e. no post-processing. A waste of processing, but illustrative
float4 PPCopyShader( PS_POSTPROCESS_INPUT ppIn ) : SV_Target
{
	float3 ppColour = SceneTexture.Sample( PointSample, ppIn.UV );
	return float4( ppColour, 0.1f);
}


// Post-processing shader that tints the scene texture to a given colour
float4 PPTintShader( PS_POSTPROCESS_INPUT ppIn ) : SV_Target
{
	// Sample the texture colour (look at shader above) and multiply it with the tint colour (variables near top)
	float3 ppColour = SceneTexture.Sample( PointSample, ppIn.UV) * TintColour;
	return float4( ppColour, 0.1f);
}


// Post-processing shader that tints the scene texture to a given colour
float4 PPGreyNoiseShader( PS_POSTPROCESS_INPUT ppIn ) : SV_Target
{
	const float NoiseStrength = 0.8f; // How noticable the noise is

	// Get texture colour, and average r, g & b to get a single grey value
    float3 texColour = SceneTexture.Sample( PointSample, ppIn.UV );
    float grey = (texColour.r + texColour.g + texColour.b) / 3;
    
    // Get noise UV by scaling and offseting texture UV. Scaling adjusts how fine the noise is.
    // The offset is randomised to give a constantly changing noise effect (like tv static)
    float2 noiseUV = ppIn.UV * NoiseScale + NoiseOffset;
    grey += NoiseStrength * (PostProcessMap.Sample( TrilinearWrap, noiseUV ).r - 0.5f); // Noise can increase or decrease grey value
    
    // Output final colour
	return float4( grey, grey, grey, 0.1f);
}



// Post-processing shader that "burns" the image away
float4 PPBurnShader( PS_POSTPROCESS_INPUT ppIn ) : SV_Target
{
	const float4 White = 1.0f;
	
	// Pixels are burnt with these colours at the edges
	const float4 BurnColour = float4(0.8f, 0.4f, 0.0f, 0.1f);
	const float4 GlowColour = float4(1.0f, 0.8f, 0.0f, 0.1f);
	const float GlowAmount = 0.15f; // Thickness of glowing area
	const float Crinkle = 0.1f; // Amount of texture crinkle at the edges 

	// Get burn texture colour
    float4 burnTexture = PostProcessMap.Sample( TrilinearWrap, ppIn.UV );
    
    // The range of burning colours are from BurnLevel  to BurnLevelMax
	float BurnLevelMax = BurnLevel + GlowAmount; 

    // Output black when current burn texture value below burning range
    if (burnTexture.r <= BurnLevel)
    {
		return float4( 1.0f, 1.0f, 1.0f, 1.0f );
	}
    
    // Output scene texture untouched when current burnTexture texture value above burning range
	else if (burnTexture.r >= BurnLevelMax)
    {
		float3 ppColour = SceneTexture.Sample(PointSample, ppIn.UV);
		return float4( ppColour, 0.1f);
	}
	
	else // Draw burning edges
	{
		float3 ppColour;

		// Get level of glow (0 = none, 1 = max)
		float GlowLevel = 1.0f - (burnTexture.r - BurnLevel) / GlowAmount;

		// Extract direction to crinkle (2D vector) from the g & b components of the burn texture sampled above (converting from 0->1 range to -0.5->0.5 range)
		float2 CrinkleVector = (burnTexture.g, burnTexture.b) - float2(0.5f, 0.5f);
		
		// Get main texture colour using crinkle offset
	    float4 texColour =  SceneTexture.Sample( PointSample, ppIn.UV- GlowLevel * Crinkle * CrinkleVector );

		// Split glow into two regions - the very edge and the inner section
		GlowLevel *= 2.0f;
		if (GlowLevel < 1.0f)
		{		
			// Blend from main texture colour on inside to burn tint in middle of burning area
			ppColour = lerp( texColour, BurnColour * texColour, GlowLevel );
		}
		else
		{
			// Blend from burn tint in middle of burning area to bright glow at the burning edges
			ppColour = lerp( BurnColour * texColour, GlowColour, GlowLevel - 1.0f );
		}
		return float4( ppColour, 0.1f);
	}
}


// Post-processing shader that tints the scene texture to a given colour
float4 PPDistortShader( PS_POSTPROCESS_INPUT ppIn ) : SV_Target
{
	const float LightStrength = 0.025f;
	
	// Get distort texture colour
    float4 distortTexture = PostProcessMap.Sample( TrilinearWrap, ppIn.UV );

	// Get direction (2D vector) to distort UVs from the g & b components of the distort texture (converting from 0->1 range to -0.5->0.5 range)
	float2 DistortVector = (distortTexture.g, distortTexture.b) - float2(0.5f, 0.5f);
			
	// Simple fake diffuse lighting formula based on 2D vector, light coming from top-left
	float light = dot( normalize(DistortVector), float2(0.707f, 0.707f) ) * LightStrength;
	
	// Get final colour by adding fake light colour plus scene texture sampled with distort texture offset
	float3 ppColour = light + SceneTexture.Sample( PointSample, ppIn.UV + DistortLevel * DistortVector );

    return float4( ppColour, 0.1f );
}


// Post-processing shader that tints the scene texture to a given colour
float4 PPSpiralShader( PS_POSTPROCESS_INPUT ppIn ) : SV_Target
{
	// Get vector from UV centre to pixel UV
	const float2 CentreUV = float2(0.5f, 0.5f);
	float2 CentreOffsetUV = ppIn.UV - CentreUV;
	float CentreDistance = length( CentreOffsetUV ); // Distance of pixel from UV (i.e. screen) centre
	
	// Get sin and cos of spiral amount, increasing with distance from centre
	float s, c;
	sincos( CentreDistance * Wiggle * Wiggle, s, c );
	
	// Create a (2D) rotation matrix and apply to the vector - i.e. rotate the
	// vector around the centre by the spiral amount
	matrix<float,2,2> Rot2D = { c, s,
	                           -s, c };
	float2 RotOffsetUV = mul( CentreOffsetUV, Rot2D );

	// Sample texture at new position (centre UV + rotated UV offset)
    float3 ppColour = SceneTexture.Sample( PointSample, CentreUV + RotOffsetUV );

    return float4( ppColour, 0.1f );
}


//--------------------------------------------------------------------------------------
// States
//--------------------------------------------------------------------------------------

RasterizerState CullNone  // Cull none of the polygons, i.e. show both sides
{
	CullMode = None;
};

DepthStencilState DisableDepth   // Disable depth buffer entirely
{
	DepthFunc      = ALWAYS;
	DepthWriteMask = ZERO;
};

BlendState NoBlending // Switch off blending - pixels will be opaque
{
    BlendEnable[0] = FALSE;
};
BlendState AlphaBlending
{
    BlendEnable[0] = TRUE;
    SrcBlend = SRC_ALPHA;
    DestBlend = INV_SRC_ALPHA;
    BlendOp = ADD;
};


//--------------------------------------------------------------------------------------
// Post Processing Techniques
//--------------------------------------------------------------------------------------

// Simple copy technique - no post-processing (pointless but illustrative)
technique10 PPCopy
{
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, FullScreenQuad() ) );
        SetGeometryShader( NULL );                                   
        SetPixelShader( CompileShader( ps_4_0, PPCopyShader() ) );

		SetBlendState( AlphaBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
		SetRasterizerState( CullNone ); 
		SetDepthStencilState( DisableDepth, 0 );
     }
}


// Tint the scene to a colour
technique10 PPTint
{
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, FullScreenQuad() ) );
        SetGeometryShader( NULL );                                   
        SetPixelShader( CompileShader( ps_4_0, PPTintShader() ) );

		SetBlendState(AlphaBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
		SetRasterizerState( CullNone ); 
		SetDepthStencilState( DisableDepth, 0 );
     }
}

// Turn the scene greyscale and add some animated noise
technique10 PPGreyNoise
{
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, FullScreenQuad() ) );
        SetGeometryShader( NULL );                                   
        SetPixelShader( CompileShader( ps_4_0, PPGreyNoiseShader() ) );

		SetBlendState(AlphaBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
		SetRasterizerState( CullNone ); 
		SetDepthStencilState( DisableDepth, 0 );
     }
}

// Burn the scene away
technique10 PPBurn
{
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, FullScreenQuad() ) );
        SetGeometryShader( NULL );                                   
        SetPixelShader( CompileShader( ps_4_0, PPBurnShader() ) );

		SetBlendState(AlphaBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
		SetRasterizerState( CullNone ); 
		SetDepthStencilState( DisableDepth, 0 );
     }
}

// Distort the scene as though viewed through cut glass
technique10 PPDistort
{
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, FullScreenQuad() ) );
        SetGeometryShader( NULL );                                   
        SetPixelShader( CompileShader( ps_4_0, PPDistortShader() ) );

		SetBlendState(AlphaBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
		SetRasterizerState( CullNone ); 
		SetDepthStencilState( DisableDepth, 0 );
     }
}

// Spin the image in a vortex
technique10 PPSpiral
{
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, FullScreenQuad() ) );
        SetGeometryShader( NULL );                                   
        SetPixelShader( CompileShader( ps_4_0, PPSpiralShader() ) );

		SetBlendState(AlphaBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
		SetRasterizerState( CullNone ); 
		SetDepthStencilState( DisableDepth, 0 );
     }
}
