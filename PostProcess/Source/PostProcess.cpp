/*******************************************
	PostProcess.cpp

	Main scene and game functions
********************************************/

#include <Windows.h>
#include <sstream>
#include <string>
using namespace std;

#include <d3d10.h>
#include <d3dx10.h>

#include "Defines.h"
#include "CVector3.h"
#include "Camera.h"
#include "Light.h"
#include "EntityManager.h"
#include "Messenger.h"
#include "CParseLevel.h"
#include "PostProcess.h"

namespace gen
{

//*****************************************************************************
// Post-process data
//*****************************************************************************

// Enumeration of different post-processes
enum PostProcesses
{
	Copy, Tint, GreyNoise, Burn, Distort, Spiral,  
	NumPostProcesses
};

// Currently used post process
PostProcesses CurrentPostProcess = Tint;


// Separate effect file for post-processes, not necessary to use a separate file, but convenient given the architecture of this lab
ID3D10Effect* PPEffect;

// Technique name for each post-process
const string PPTechniqueNames[NumPostProcesses] = {	"PPCopy", "PPTint", "PPGreyNoise", "PPBurn", "PPDistort", "PPSpiral", };

// Technique pointers for each post-process
ID3D10EffectTechnique* PPTechniques[NumPostProcesses];


// Will render the scene to a texture in a first pass, then copy that texture to the back buffer in a second post-processing pass
// So need a texture and two "views": a render target view (to render into the texture - 1st pass) and a shader resource view (use the rendered texture as a normal texture - 2nd pass)
ID3D10Texture2D*          SceneTexture = NULL;
ID3D10RenderTargetView*   SceneRenderTarget = NULL;
ID3D10ShaderResourceView* SceneShaderResource = NULL;

// Additional textures used by post-processes
ID3D10ShaderResourceView* NoiseMap = NULL;
ID3D10ShaderResourceView* BurnMap = NULL;
ID3D10ShaderResourceView* DistortMap = NULL;

// Variables to link C++ post-process textures to HLSL shader variables
ID3D10EffectShaderResourceVariable* SceneTextureVar = NULL;
ID3D10EffectShaderResourceVariable* PostProcessMapVar = NULL; // Single shader variable used for the three maps above (noise, burn, distort). Only one is needed at a time

// Other variables for individual post-processes
ID3D10EffectVectorVariable* TintColourVar = NULL;
ID3D10EffectVectorVariable* NoiseScaleVar = NULL;
ID3D10EffectVectorVariable* NoiseOffsetVar = NULL;
ID3D10EffectScalarVariable* DistortLevelVar = NULL;
ID3D10EffectScalarVariable* BurnLevelVar = NULL;
ID3D10EffectScalarVariable* WiggleVar = NULL;


//*****************************************************************************


//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------

// Control speed
const float CameraRotSpeed = 2.0f;
float CameraMoveSpeed = 80.0f;

// Amount of time to pass before calculating new average update time
const float UpdateTimePeriod = 0.25f;



//-----------------------------------------------------------------------------
// Global system variables
//-----------------------------------------------------------------------------

// Folders used for meshes/textures and effect files
extern const string MediaFolder;
extern const string ShaderFolder;

// Get reference to global DirectX variables from another source file
extern ID3D10Device*           g_pd3dDevice;
extern IDXGISwapChain*         SwapChain;
extern ID3D10DepthStencilView* DepthStencilView;
extern ID3D10RenderTargetView* BackBufferRenderTarget;
extern ID3DX10Font*            OSDFont;

// Actual viewport dimensions (fullscreen or windowed)
extern TUInt32 BackBufferWidth;
extern TUInt32 BackBufferHeight;

// Current mouse position
extern TUInt32 MouseX;
extern TUInt32 MouseY;

// Messenger class for sending messages to and between entities
extern CMessenger Messenger;


//-----------------------------------------------------------------------------
// Global game/scene variables
//-----------------------------------------------------------------------------

// Entity manager and level parser
CEntityManager EntityManager;
CParseLevel LevelParser( &EntityManager );

// Other scene elements
const int NumLights = 2;
CLight*  Lights[NumLights];
CCamera* MainCamera;

// Sum of recent update times and number of times in the sum - used to calculate
// average over a given time period
float SumUpdateTimes = 0.0f;
int NumUpdateTimes = 0;
float AverageUpdateTime = -1.0f; // Invalid value at first


//-----------------------------------------------------------------------------
// Game Constants
//-----------------------------------------------------------------------------

// Lighting
const SColourRGBA AmbientColour( 0.3f, 0.3f, 0.4f, 1.0f );
CVector3 LightCentre( 0.0f, 30.0f, 50.0f );
const float LightOrbit = 170.0f;
const float LightOrbitSpeed = 0.2f;


//-----------------------------------------------------------------------------
// Scene management
//-----------------------------------------------------------------------------

// Creates the scene geometry
bool SceneSetup()
{
	// Prepare render methods
	InitialiseMethods();
	
	// Read templates and entities from XML file
	if (!LevelParser.ParseFile( "Entities.xml" )) return false;
	
	// Set camera position and clip planes suitable for space game
	MainCamera = new CCamera( CVector3( 0.0f, 50, -150 ), CVector3(ToRadians(15.0f), 0, 0) );
	MainCamera->SetNearFarClip( 2.0f, 300000.0f ); 

	// Sunlight
	Lights[0] = new CLight( CVector3( -10000.0f, 6000.0f, 0000.0f), SColourRGBA(1.0f, 0.8f, 0.6f) * 12000, 20000.0f ); // Colour is multiplied by light brightness

	// Light orbiting area
	Lights[1] = new CLight( LightCentre, SColourRGBA(0.0f, 0.2f, 1.0f) * 50, 100.0f );

	return true;
}


// Release everything in the scene
void SceneShutdown()
{
	// Release render methods
	ReleaseMethods();

	// Release lights
	for (int light = NumLights - 1; light >= 0; --light)
	{
		delete Lights[light];
	}

	// Release camera
	delete MainCamera;

	// Destroy all entities
	EntityManager.DestroyAllEntities();
	EntityManager.DestroyAllTemplates();
}


//*****************************************************************************
// Post Processing Setup
//*****************************************************************************

// Prepare resources required for the post-processing pass
bool PostProcessSetup()
{
	// Create the "scene texture" - the texture into which the scene will be rendered in the first pass
	D3D10_TEXTURE2D_DESC textureDesc;
	textureDesc.Width  = BackBufferWidth;  // Match views to viewport size
	textureDesc.Height = BackBufferHeight;
	textureDesc.MipLevels = 1; // No mip-maps when rendering to textures (or we will have to render every level)
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // RGBA texture (8-bits each)
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Usage = D3D10_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE; // Indicate we will use texture as render target, and pass it to shaders
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = 0;
	if (FAILED(g_pd3dDevice->CreateTexture2D( &textureDesc, NULL, &SceneTexture ))) return false;

	// Get a "view" of the texture as a render target - giving us an interface for rendering to the texture
	if (FAILED(g_pd3dDevice->CreateRenderTargetView( SceneTexture, NULL, &SceneRenderTarget ))) return false;

	// And get a shader-resource "view" - giving us an interface for passing the texture to shaders
	D3D10_SHADER_RESOURCE_VIEW_DESC srDesc;
	srDesc.Format = textureDesc.Format;
	srDesc.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
	srDesc.Texture2D.MostDetailedMip = 0;
	srDesc.Texture2D.MipLevels = 1;
	if (FAILED(g_pd3dDevice->CreateShaderResourceView( SceneTexture, &srDesc, &SceneShaderResource ))) return false;

	// Load post-processing support textures
	if (FAILED( D3DX10CreateShaderResourceViewFromFile( g_pd3dDevice, (MediaFolder + "Noise.png").c_str() ,   NULL, NULL, &NoiseMap,   NULL ) )) return false;
	if (FAILED( D3DX10CreateShaderResourceViewFromFile( g_pd3dDevice, (MediaFolder + "Burn.png").c_str() ,    NULL, NULL, &BurnMap,    NULL ) )) return false;
	if (FAILED( D3DX10CreateShaderResourceViewFromFile( g_pd3dDevice, (MediaFolder + "Distort.png").c_str() , NULL, NULL, &DistortMap, NULL ) )) return false;


	// Load and compile a separate effect file for post-processes.
	ID3D10Blob* pErrors;
	DWORD dwShaderFlags = D3D10_SHADER_ENABLE_STRICTNESS; // These "flags" are used to set the compiler options

	string fullFileName = ShaderFolder + "PostProcess.fx";
	if( FAILED( D3DX10CreateEffectFromFile( fullFileName.c_str(), NULL, NULL, "fx_4_0", dwShaderFlags, 0, g_pd3dDevice, NULL, NULL, &PPEffect, &pErrors, NULL ) ))
	{
		if (pErrors != 0)  MessageBox( NULL, reinterpret_cast<char*>(pErrors->GetBufferPointer()), "Error", MB_OK ); // Compiler error: display error message
		else               MessageBox( NULL, "Error loading FX file. Ensure your FX file is in the same folder as this executable.", "Error", MB_OK );  // No error message - probably file not found
		return false;
	}

	// There's an array of post-processing technique names above - get array of post-process techniques matching those names from the compiled effect file
	for (int pp = 0; pp < NumPostProcesses; pp++)
	{
		PPTechniques[pp] = PPEffect->GetTechniqueByName( PPTechniqueNames[pp].c_str() );
	}

	// Link to HLSL variables in post-process shaders
	SceneTextureVar   = PPEffect->GetVariableByName( "SceneTexture" )->AsShaderResource();
	PostProcessMapVar = PPEffect->GetVariableByName( "PostProcessMap" )->AsShaderResource();
	TintColourVar   = PPEffect->GetVariableByName( "TintColour" )->AsVector();
	NoiseScaleVar   = PPEffect->GetVariableByName( "NoiseScale" )->AsVector();
	NoiseOffsetVar  = PPEffect->GetVariableByName( "NoiseOffset" )->AsVector();
	DistortLevelVar = PPEffect->GetVariableByName( "DistortLevel" )->AsScalar();
	BurnLevelVar    = PPEffect->GetVariableByName( "BurnLevel" )->AsScalar();
	WiggleVar       = PPEffect->GetVariableByName( "Wiggle" )->AsScalar();

	return true;
}

void PostProcessShutdown()
{
	if (PPEffect)            PPEffect->Release();
    if (DistortMap)          DistortMap->Release();
    if (BurnMap)             BurnMap->Release();
    if (NoiseMap)            NoiseMap->Release();
	if (SceneShaderResource) SceneShaderResource->Release();
	if (SceneRenderTarget)   SceneRenderTarget->Release();
	if (SceneTexture)        SceneTexture->Release();
}

//*****************************************************************************


//-----------------------------------------------------------------------------
// Game loop functions
//-----------------------------------------------------------------------------

// Draw one frame of the scene
void RenderScene( float updateTime )
{
	// Setup the viewport - defines which part of the back-buffer we will render to (usually all of it)
	D3D10_VIEWPORT vp;
	vp.Width  = BackBufferWidth;
	vp.Height = BackBufferHeight;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	g_pd3dDevice->RSSetViewports( 1, &vp );


	//************************************************
	// FIRST RENDER PASS - Render scene to texture

	// Specify that the scene texture will be the render target in this first pass (rather than the backbuffer), will share the depth/stencil buffer with the backbuffer though
	g_pd3dDevice->OMSetRenderTargets( 1, /*MISSING - specify scene texture as render target (variables near top of file)*/, DepthStencilView );

	// Clear the texture and the depth buffer
	g_pd3dDevice->ClearRenderTargetView( SceneRenderTarget, &AmbientColour.r );
	g_pd3dDevice->ClearDepthStencilView( DepthStencilView, D3D10_CLEAR_DEPTH, 1.0f, 0 );

	// Prepare camera
	MainCamera->SetAspect( static_cast<TFloat32>(BackBufferWidth) / BackBufferHeight );
	MainCamera->CalculateMatrices();
	MainCamera->CalculateFrustrumPlanes();

	// Set camera and light data in shaders
	SetCamera( MainCamera );
	SetAmbientLight( AmbientColour );
	SetLights( &Lights[0] );

	// Render entities and draw on-screen text
	EntityManager.RenderAllEntities( MainCamera );

	//************************************************


	//************************************************
	// PREPARE INDIVIDUAL POST-PROCESS SETTINGS

	switch (CurrentPostProcess)
	{
		case Tint:
		{
			// Set the colour used to tint the scene
			D3DXCOLOR TintColour/* = ?? FILTER - Make a nice colour*/;
			TintColourVar->SetRawValue( &TintColour, 0, 12 );
		}
		break;

		case GreyNoise:
		{
			const float GrainSize = 140; // Fineness of the noise grain

			// Set shader constants - scale and offset for noise. Scaling adjusts how fine the noise is.
			CVector2 NoiseScale = CVector2( BackBufferWidth / GrainSize, BackBufferHeight / GrainSize );
			NoiseScaleVar->SetRawValue( &NoiseScale, 0, 8 );

			// The offset is randomised to give a constantly changing noise effect (like tv static)
			CVector2 RandomUVs = CVector2( /*FILTER - 2 random UVs please*/ );
			NoiseOffsetVar->SetRawValue( &RandomUVs, 0, 8 );

			// Set noise texture
			PostProcessMapVar->SetResource( NoiseMap );
		}
		break;

		case Burn:
		{
			static float BurnLevel = 0.0f;
			const float BurnSpeed = 0.2f;

			// Set and increase the burn level (cycling back to 0 when it reaches 1.0f)
			BurnLevelVar->SetFloat( BurnLevel );
			BurnLevel = Mod( BurnLevel + BurnSpeed * updateTime, 1.0f );

			// Set burn texture
			PostProcessMapVar->SetResource( BurnMap );
		}
		break;


		case Distort:
		{
			// Set the level of distortion
			const float DistortLevel = 0.03f;
			DistortLevelVar->SetFloat( DistortLevel );

			// Set distort texture
			PostProcessMapVar->SetResource( DistortMap );
		}
		break;


		case Spiral:
		{
			static float Wiggle = 0.0f;
			const float WiggleSpeed = 1.0f;

			// Set and increase the amount of spiral - use a tweaked cos wave to animate
			WiggleVar->SetFloat( (1.0f - Cos(Wiggle)) * 4.0f );
			Wiggle += WiggleSpeed * updateTime;
			break;
		}
	}

	//************************************************


	//************************************************
	// SECOND RENDER PASS - Render full screen quad on the back-buffer mapped with the scene texture, the post-process technique will process the scene pixels in some way during the copy

	// Select the back buffer to use for rendering (will ignore depth-buffer for full-screen quad) and select scene texture for use in shader
	// Not going to clear the back-buffer, we're going to overwrite it all
	g_pd3dDevice->OMSetRenderTargets( 1, /*MISSING, 2nd pass specify back buffer as render target*/, DepthStencilView ); 
	SceneTextureVar->SetResource( /*MISSING, 2nd pass, will use scene texture in shaders - i.e. as a shader resource, again check available variables*/ );

	// Using special vertex shader than creates its own data for a full screen quad (see .fx file). No need to set vertex/index buffer, just draw 4 vertices of quad
	// Select technique to match currently selected post-process
	g_pd3dDevice->IASetInputLayout( NULL );
	g_pd3dDevice->IASetPrimitiveTopology( D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
	PPTechniques[CurrentPostProcess]->GetPassByIndex(0)->Apply(0);
	g_pd3dDevice->Draw( /*MISSING - Post-process pass renderes a quad*/, 0 );

	// These two lines unbind the scene texture from the shader to stop DirectX issuing a warning when we try to render to it again next frame
	SceneTextureVar->SetResource( 0 );
	PPTechniques[CurrentPostProcess]->GetPassByIndex(0)->Apply(0);

	//************************************************
	

	// Render UI elements last - don't want them post-processed
	RenderSceneText( updateTime );

	// Present the backbuffer contents to the display
	SwapChain->Present( 0, 0 );
}


// Render a single text string at the given position in the given colour, may optionally centre it
void RenderText( const string& text, int X, int Y, float r, float g, float b, bool centre = false )
{
	RECT rect;
	if (!centre)
	{
		SetRect( &rect, X, Y, 0, 0 );
		OSDFont->DrawText( NULL, text.c_str(), -1, &rect, DT_NOCLIP, D3DXCOLOR( r, g, b, 1.0f ) );
	}
	else
	{
		SetRect( &rect, X - 100, Y, X + 100, 0 );
		OSDFont->DrawText( NULL, text.c_str(), -1, &rect, DT_CENTER | DT_NOCLIP, D3DXCOLOR( r, g, b, 1.0f ) );
	}
}

// Render on-screen text each frame
void RenderSceneText( float updateTime )
{
	// Accumulate update times to calculate the average over a given period
	SumUpdateTimes += updateTime;
	++NumUpdateTimes;
	if (SumUpdateTimes >= UpdateTimePeriod)
	{
		AverageUpdateTime = SumUpdateTimes / NumUpdateTimes;
		SumUpdateTimes = 0.0f;
		NumUpdateTimes = 0;
	}

	// Write FPS text string
	stringstream outText;
	if (AverageUpdateTime >= 0.0f)
	{
		outText << "Frame Time: " << AverageUpdateTime * 1000.0f << "ms" << endl << "FPS:" << 1.0f / AverageUpdateTime;
		RenderText( outText.str(), 2, 2, 0.0f, 0.0f, 0.0f );
		RenderText( outText.str(), 0, 0, 1.0f, 1.0f, 0.0f );
		outText.str("");
	}

	// Output post-process name
	outText << "Post-Process: ";
	switch (CurrentPostProcess)
	{
	case Copy: 
		outText << "Copy";
		break;
	case Tint: 
		outText << "Tint";
		break;
	case GreyNoise: 
		outText << "Grey Noise";
		break;
	case Burn: 
		outText << "Burn";
		break;
	case Distort: 
		outText << "Distort";
		break;
	case Spiral: 
		outText << "Spiral";
		break;
	}
	RenderText( outText.str(),  0, 32,  1.0f, 1.0f, 1.0f );
}


// Update the scene between rendering
void UpdateScene( float updateTime )
{
	// Call all entity update functions
	EntityManager.UpdateAllEntities( updateTime );

	// Set camera speeds
	// Key F1 used for full screen toggle
	if (KeyHit( Key_F2 )) CameraMoveSpeed = 5.0f;
	if (KeyHit( Key_F3 )) CameraMoveSpeed = 40.0f;
	if (KeyHit( Key_F4 )) CameraMoveSpeed = 160.0f;
	if (KeyHit( Key_F5 )) CameraMoveSpeed = 640.0f;

	// Choose post-process
	if (KeyHit( Key_1 )) CurrentPostProcess = Copy;
	if (KeyHit( Key_2 )) CurrentPostProcess = Tint;
	if (KeyHit( Key_3 )) CurrentPostProcess = GreyNoise;
	if (KeyHit( Key_4 )) CurrentPostProcess = Burn;
	if (KeyHit( Key_5 )) CurrentPostProcess = Distort;
	if (KeyHit( Key_6 )) CurrentPostProcess = Spiral;

	// Rotate cube and attach light to it
	CEntity* cubey = EntityManager.GetEntity( "Cubey" );
	cubey->Matrix().RotateX( ToRadians(53.0f) * updateTime );
	cubey->Matrix().RotateZ( ToRadians(42.0f) * updateTime );
	cubey->Matrix().RotateWorldY( ToRadians(12.0f) * updateTime );
	Lights[1]->SetPosition( cubey->Position() );
	
	// Move the camera
	MainCamera->Control( Key_Up, Key_Down, Key_Left, Key_Right, Key_W, Key_S, Key_A, Key_D, 
	                     CameraMoveSpeed * updateTime, CameraRotSpeed * updateTime );
}


} // namespace gen
