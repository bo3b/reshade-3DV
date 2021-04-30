// SBS_to_Double.fx
//
// Reshade.fx shader to stretch the backbuffer across a double width texture.


// Global textures and samplers
texture BackBufferTex : COLOR;
texture DepthBufferTex : DEPTH;

sampler sBackBuffer { Texture = BackBufferTex; };
sampler sDepthBuffer { Texture = DepthBufferTex; };

// Vertex shader generating a triangle covering the entire screen
void PostProcessVS(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD)
{
	texcoord.x = (id == 2) ? 2.0 : 0.0;
	texcoord.y = (id == 1) ? 2.0 : 0.0;
	position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

// Pixel shader copying the backbuffer to 2x larger, shows stretched view.
float4 PS_Stretch(float4 vpos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
	float2 stretchXY;
	stretchXY.x = texcoord.x;
	stretchXY.y = texcoord.y;
	
	return tex2D(sBackBuffer, stretchXY);
}

technique Prestretch <ui_tooltip = "Stretch to double width."; >
{
	pass StretchPass
	{
		VertexShader = PostProcessVS;
		PixelShader = PS_Stretch;
	}
}
