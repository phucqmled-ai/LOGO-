#include "LogoRunner.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "../third_party/stb/stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb/stb_truetype.h"

using namespace ffglex;

static CFFGLPluginInfo PluginInfo(
	PluginFactory< LogoRunner >,
	"QMLR",              // Plugin unique ID (4 chars)
	"QM Logo Runner",    // Plugin name
	2,                   // API major version number
	1,                   // API minor version number
	1,                   // Plugin major version number
	5,                   // Plugin minor version number
	FF_SOURCE,           // Plugin type
	"Chay logo PNG va chu theo duong ngang, doc, tron, elip, vien chu nhat",
	"QM LED" );

static const int ROW_H   = 256;// pixel height of every item in the atlas
static const int PAD     = 8;  // transparent padding around each item
static const int MAX_W   = 4096;
static const float PI_F  = 3.14159265358979f;

static const char vertexShaderCode[] = R"(#version 410 core
layout( location = 0 ) in vec2 aPos; // pixels, y up
layout( location = 1 ) in float aPath;
uniform vec2 res;
out vec2 vPix;
flat out int vPath;
void main()
{
	vPix  = aPos;
	vPath = int( floor( aPath + 0.5 ) );
	gl_Position = vec4( aPos / res * 2.0 - 1.0, 0.0, 1.0 );
}
)";

static const char fragmentShaderCode[] = R"(#version 410 core
#define MAXI 16
#define MAXP 16
in vec2 vPix;
flat in int vPath;
out vec4 fragColor;

uniform vec2 res;
uniform int pathCount;
uniform vec4 pRect[ MAXP ];   // cx, cy, w, h (px, y up)
uniform vec4 pInfo[ MAXP ];   // type, length, thickness, clip
uniform vec4 pMotion[ MAXP ]; // extra gap, chain offset, scroll phase, clip inset
uniform vec4 pOuter[ MAXP * 2 ]; // quad corners 0,1 then 2,3 (px, y up)
uniform vec4 pInner[ MAXP * 2 ]; // inward offset quad for the band

float cross2( vec2 a, vec2 b ) { return a.x * b.y - a.y * b.x; }

vec2 corner( vec4 pair[ MAXP * 2 ], int k, int v )
{
	vec4 p = pair[ k * 2 + ( v >> 1 ) ];
	return ( v & 1 ) == 0 ? p.xy : p.zw;
}

// inverse bilinear: where is p inside quad a,b,c,d (cyclic order)
vec2 invBilinear( vec2 p, vec2 a, vec2 b, vec2 c, vec2 d )
{
	vec2 e = b - a, f = d - a, g = a - b + c - d, h = p - a;
	float k2 = cross2( g, f );
	float k1 = cross2( e, f ) + cross2( h, g );
	float k0 = cross2( h, e );
	float v;
	if( abs( k2 ) < 0.0001 * abs( k1 ) )
	{
		if( abs( k1 ) < 1e-9 ) return vec2( -1.0 );
		v = -k0 / k1;
	}
	else
	{
		float w = k1 * k1 - 4.0 * k0 * k2;
		if( w < 0.0 ) return vec2( -1.0 );
		w = sqrt( w );
		v = ( -k1 - w ) / ( 2.0 * k2 );
		if( v < 0.0 || v > 1.0 )
			v = ( -k1 + w ) / ( 2.0 * k2 );
	}
	vec2 den = e + g * v;
	vec2 num = h - f * v;
	float u  = abs( den.x ) > abs( den.y ) ? num.x / den.x : num.y / den.y;
	return vec2( u, v );
}
uniform int flipSide;
uniform int count;
uniform float itemScale;
uniform float gapFactor;
uniform float aspects[ MAXI ];
uniform float cumAspect[ MAXI ];
uniform vec4 rects[ MAXI ];
uniform int isText[ MAXI ];
uniform sampler2D atlas;
uniform vec4 textColor;
uniform vec4 borderColor;   // a = 0 means off
uniform float borderW;      // px
uniform float glowR;        // px
uniform float bandFill;

vec4 over( vec4 top, vec4 bottom ) { return top + bottom * ( 1.0 - top.a ); }

void main()
{
	if( vPath < 0 )
	{
		fragColor = vec4( 0.0 );
		return;
	}
	vec2 p   = vPix;
	vec4 col = vec4( 0.0 );

	{
		int k  = vPath;
		vec4 R = pRect[ k ];
		vec4 I = pInfo[ k ];
		vec4 M = pMotion[ k ];
		int shape   = int( I.x + 0.5 );
		float L     = I.y;
		float thick = I.z;
		vec2 center = R.xy;
		vec2 size   = R.zw;
		vec2 d      = p - center;


		float s = 0.0, n = 0.0, nb = -1e9;
		bool along = true;
		if( shape == 0 )
		{
			s = p.x - ( center.x - size.x * 0.5 );
			n = d.y / thick + 0.5;
			along = s >= 0.0 && s <= L;
		}
		else if( shape == 1 )
		{
			s = ( center.y + size.y * 0.5 ) - p.y;
			n = d.x / thick + 0.5;
			along = s >= 0.0 && s <= L;
		}
		else if( shape == 2 || shape == 3 )
		{
			vec2 r = shape == 2 ? vec2( size.y * 0.5 - thick * 0.5 ) : size * 0.5 - thick * 0.5;
			r = max( r, vec2( 1.0 ) );
			float t = atan( d.y / r.y, d.x / r.x );
			vec2 m = vec2( r.x * cos( t ), r.y * sin( t ) );
			n = ( length( d ) - length( m ) ) / thick + 0.5;
			s = fract( 0.25 - t / 6.28318530718 ) * L;
		}
		else if( shape == 4 )
		{
			float W  = max( size.x - thick, 1.0 );
			float H  = max( size.y - thick, 1.0 );
			float rc = min( thick * 0.5, min( W, H ) * 0.5 );
			float a  = W * 0.5 - rc, b = H * 0.5 - rc;
			float A  = 1.57079632679 * rc;// quarter arc length
			vec2 q   = abs( d ) - vec2( a, b );
			// square edges for band + border lines so they reach the slice corners
			vec2 e = abs( d ) - vec2( W, H ) * 0.5;
			nb = max( e.x, e.y ) / thick + 0.5;
			if( q.x > 0.0 && q.y > 0.0 )
			{
				vec2 cc = sign( d ) * vec2( a, b );
				vec2 v  = d - cc;
				float th = atan( v.y, v.x );
				n = ( length( v ) - rc ) / thick + 0.5;
				if( d.x > 0.0 && d.y > 0.0 )      s = 2.0 * a + ( 1.57079632679 - th ) * rc;
				else if( d.x > 0.0 )              s = 2.0 * a + 2.0 * b + 2.0 * A + ( -th ) * rc;
				else if( d.y < 0.0 )              s = 4.0 * a + 2.0 * b + 4.0 * A + ( -1.57079632679 - th ) * rc;
				else                              s = 4.0 * a + 4.0 * b + 6.0 * A + ( th - 1.57079632679 ) * rc;
			}
			else if( q.y > q.x )
			{
				n = ( abs( d.y ) - H * 0.5 ) / thick + 0.5;
				if( d.y > 0.0 ) s = d.x + a;
				else            s = 2.0 * a + 2.0 * b + 4.0 * A + ( a - d.x );
			}
			else
			{
				n = ( abs( d.x ) - W * 0.5 ) / thick + 0.5;
				if( d.x > 0.0 ) s = 2.0 * a + 2.0 * A + ( b - d.y );
				else            s = 4.0 * a + 2.0 * b + 6.0 * A + ( d.y + b );
			}
		}
		else if( shape == 5 )
		{
			// band hugging the quad outline
			float cum = 0.0;
			bool hit  = false;
			for( int e = 0; e < 4; ++e )
			{
				vec2 A  = corner( pOuter, k, e );
				vec2 B  = corner( pOuter, k, ( e + 1 ) & 3 );
				vec2 Ai = corner( pInner, k, e );
				vec2 Bi = corner( pInner, k, ( e + 1 ) & 3 );
				vec2 ed = B - A;
				float eL = length( ed );
				if( eL < 0.001 ) continue;
				vec2 nrm = vec2( -ed.y, ed.x ) / eL;// inward for CCW winding
				float nn = dot( p - A, nrm ) / thick;
				if( nn >= 0.0 && nn <= 1.0 )
				{
					vec2 P0 = mix( A, Ai, nn );
					vec2 P1 = mix( B, Bi, nn );
					vec2 dd = P1 - P0;
					float tt = dot( p - P0, dd ) / max( dot( dd, dd ), 1e-6 );
					if( tt >= 0.0 && tt <= 1.0 )
					{
						n   = nn;
						s   = cum + tt * eL;
						hit = true;
						break;
					}
				}
				cum += eL;
			}
			if( !hit )
			{
				fragColor = vec4( 0.0 );
				return;
			}
			nb = n;
		}
		else
		{
			// one straight band filling the quad
			vec2 A = corner( pOuter, k, 0 );
			vec2 B = corner( pOuter, k, 1 );
			vec2 C = corner( pOuter, k, 2 );
			vec2 D = corner( pOuter, k, 3 );
			vec2 uv2 = invBilinear( p, A, B, C, D );
			if( uv2.x < 0.0 || uv2.x > 1.0 || uv2.y < 0.0 || uv2.y > 1.0 )
			{
				fragColor = vec4( 0.0 );
				return;
			}
			s = uv2.x * L;
			float across = distance( A, D );
			n  = ( uv2.y - 0.5 ) * across / max( thick, 1.0 ) + 0.5;
			nb = n;
		}
		if( nb < -1e8 )
			nb = n;
		if( flipSide == 1 )
		{
			n  = 1.0 - n;
			nb = 1.0 - nb;
			s  = L - s;
		}
		if( !along )
		{
			fragColor = vec4( 0.0 );
			return;
		}

		vec4 c = vec4( 0.0 );
		bool inBand = nb >= 0.0 && nb <= 1.0;
		if( inBand )
			c = vec4( 0.0, 0.0, 0.0, bandFill );

		float cycle = M.w;
		if( count > 0 && cycle > 0.0 && n >= 0.0 && n <= 1.0 )
		{
			float gap = gapFactor * thick + M.x;
			float x   = mod( s + M.y + M.z, cycle );
			for( int i = 0; i < MAXI; ++i )
			{
				if( i >= count ) break;
				float acc = thick * itemScale * cumAspect[ i ] + gap * float( i );
				if( x < acc ) break;
				float len  = max( 1.0, thick * itemScale * aspects[ i ] );
				float cell = len + gap;
				if( x < acc + cell )
				{
					float u = ( x - acc - ( cell - len ) * 0.5 ) / len;
					float v = ( n - 0.5 ) / itemScale + 0.5;
					if( u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0 )
					{
						vec4 r  = rects[ i ];
						vec4 tx = texture( atlas, r.xy + vec2( u, v ) * r.zw );
						if( isText[ i ] == 1 )
							tx = vec4( textColor.rgb, 1.0 ) * tx.a * textColor.a;
						c = over( tx, c );
					}
					break;
				}
			}
		}

		if( borderColor.a > 0.0 )
		{
			float dpx  = min( abs( nb ), abs( nb - 1.0 ) ) * thick;
			float line = 1.0 - smoothstep( borderW * 0.5 - 0.75, borderW * 0.5 + 0.75, dpx );
			float glow = glowR > 0.5 ? exp( -max( dpx - borderW * 0.5, 0.0 ) / glowR ) * 0.6 : 0.0;
			float a    = clamp( max( line, glow ), 0.0, 1.0 ) * borderColor.a;
			c = over( vec4( borderColor.rgb * a, a ), c );
		}
		col = over( c, col );
	}

	if( col.a > 0.0001 )
		col.rgb /= col.a;
	fragColor = col;
}
)";

// ---------------------------------------------------------------------------

LogoRunner::LogoRunner()
{
	SetMinInputs( 0 );
	SetMaxInputs( 0 );

	for( float& v : values )
		v = 0.0f;

	const std::vector< std::string > imgExt = { "png", "jpg", "jpeg" };
	const std::vector< std::string > fontExt = { "ttf", "otf", "ttc" };

	SetFileParamInfo( P_LOGO1, "Logo 1", imgExt, "" );
	SetFileParamInfo( P_LOGO2, "Logo 2", imgExt, "" );
	SetFileParamInfo( P_LOGO3, "Logo 3", imgExt, "" );
	text = "QM LED | RESOLUME";
	SetParamInfo( P_TEXT, "Text", FF_TYPE_TEXT, text.c_str() );
	SetFileParamInfo( P_FONT, "Font", fontExt, "" );

	SetOptionParamInfo( P_CONTENT, "Content", 4, 0.0f );
	SetParamElementInfo( P_CONTENT, 0, "Logo + Text xen ke", 0.0f );
	SetParamElementInfo( P_CONTENT, 1, "Lan luot (logo roi text)", 1.0f );
	SetParamElementInfo( P_CONTENT, 2, "Chi logo", 2.0f );
	SetParamElementInfo( P_CONTENT, 3, "Chi text", 3.0f );

	SetParamInfo( P_TEXT_HUE, "Text Color", FF_TYPE_HUE, 0.0f );
	SetParamInfo( P_TEXT_SAT, "Text Saturation", FF_TYPE_SATURATION, 0.0f );
	SetParamInfo( P_TEXT_BRI, "Text Brightness", FF_TYPE_BRIGHTNESS, 1.0f );
	SetParamInfo( P_TEXT_ALPHA, "Text Alpha", FF_TYPE_ALPHA, 1.0f );
	values[ P_TEXT_HUE ]   = 0.0f;
	values[ P_TEXT_SAT ]   = 0.0f;
	values[ P_TEXT_BRI ]   = 1.0f;
	values[ P_TEXT_ALPHA ] = 1.0f;

	SetOptionParamInfo( P_SHAPE, "Shape", 5, 2.0f );
	SetParamElementInfo( P_SHAPE, 0, "Ngang (Horizontal)", 0.0f );
	SetParamElementInfo( P_SHAPE, 1, "Doc (Vertical)", 1.0f );
	SetParamElementInfo( P_SHAPE, 2, "Tron (Circle)", 2.0f );
	SetParamElementInfo( P_SHAPE, 3, "Elip (Ellipse)", 3.0f );
	SetParamElementInfo( P_SHAPE, 4, "Vien chu nhat (Rect border)", 4.0f );
	values[ P_SHAPE ] = 2.0f;

	SetParamInfo( P_POS_X, "Position X", FF_TYPE_STANDARD, 0.5f );
	SetParamInfo( P_POS_Y, "Position Y", FF_TYPE_STANDARD, 0.5f );
	SetParamInfo( P_WIDTH, "Width", FF_TYPE_STANDARD, 0.9f );
	SetParamInfo( P_HEIGHT, "Height", FF_TYPE_STANDARD, 0.9f );
	SetParamInfo( P_THICK, "Thickness", FF_TYPE_STANDARD, 0.2f );
	SetParamInfo( P_ITEM_SIZE, "Item Size", FF_TYPE_STANDARD, 0.8f );
	SetParamInfo( P_GAP, "Gap", FF_TYPE_STANDARD, 0.25f );
	SetParamInfo( P_FLIP, "Flip Side", FF_TYPE_BOOLEAN, false );
	values[ P_POS_X ]     = 0.5f;
	values[ P_POS_Y ]     = 0.5f;
	values[ P_WIDTH ]     = 0.9f;
	values[ P_HEIGHT ]    = 0.9f;
	values[ P_THICK ]     = 0.2f;
	values[ P_ITEM_SIZE ] = 0.8f;
	values[ P_GAP ]       = 0.25f;

	SetParamInfo( P_SPEED, "Speed", FF_TYPE_STANDARD, 0.7f );
	SetParamInfo( P_OFFSET, "Offset", FF_TYPE_STANDARD, 0.0f );
	values[ P_SPEED ] = 0.7f;

	SetParamInfo( P_BORDER, "Border", FF_TYPE_BOOLEAN, true );
	SetParamInfo( P_BORDER_HUE, "Border Color", FF_TYPE_HUE, 0.17f );
	SetParamInfo( P_BORDER_SAT, "Border Saturation", FF_TYPE_SATURATION, 0.8f );
	SetParamInfo( P_BORDER_BRI, "Border Brightness", FF_TYPE_BRIGHTNESS, 1.0f );
	SetParamInfo( P_BORDER_ALPHA, "Border Alpha", FF_TYPE_ALPHA, 1.0f );
	SetParamInfo( P_BORDER_WIDTH, "Border Width", FF_TYPE_STANDARD, 0.25f );
	SetParamInfo( P_GLOW, "Glow", FF_TYPE_STANDARD, 0.3f );
	SetParamInfo( P_BAND_FILL, "Band Fill", FF_TYPE_STANDARD, 0.0f );
	values[ P_BORDER ]       = 1.0f;
	values[ P_BORDER_HUE ]   = 0.17f;// yellow
	values[ P_BORDER_SAT ]   = 0.8f;
	values[ P_BORDER_BRI ]   = 1.0f;
	values[ P_BORDER_ALPHA ] = 1.0f;
	values[ P_BORDER_WIDTH ] = 0.25f;
	values[ P_GLOW ]         = 0.3f;

	SetOptionParamInfo( P_SLICE_MODE, "Slice Mode", 4, 0.0f );
	SetParamElementInfo( P_SLICE_MODE, 0, "Tat - dung Shape", 0.0f );
	SetParamElementInfo( P_SLICE_MODE, 1, "Vien quanh tung slice", 1.0f );
	SetParamElementInfo( P_SLICE_MODE, 2, "Dai chay tung slice", 2.0f );
	SetParamElementInfo( P_SLICE_MODE, 3, "Noi lien cac slice", 3.0f );
	SetParamInfo( P_SLICE_SELECT, "Slice", FF_TYPE_STANDARD, 0.0f );
	SetFileParamInfo( P_SLICE_FILE, "Slice File", { "xml" }, "" );
	SetParamInfo( P_SLICE_MARGIN, "Slice Margin", FF_TYPE_STANDARD, 0.2f );
	values[ P_SLICE_MARGIN ] = 0.2f;

	for( unsigned int i = P_SLICE_MODE; i <= P_SLICE_MARGIN; ++i )
		SetParamGroup( i, "Slices" );
	for( unsigned int i = P_LOGO1; i <= P_TEXT_ALPHA; ++i )
		SetParamGroup( i, "Content" );
	for( unsigned int i = P_SHAPE; i <= P_FLIP; ++i )
		SetParamGroup( i, "Shape" );
	for( unsigned int i = P_SPEED; i <= P_OFFSET; ++i )
		SetParamGroup( i, "Motion" );
	for( unsigned int i = P_BORDER; i <= P_BAND_FILL; ++i )
		SetParamGroup( i, "Border" );
}

LogoRunner::~LogoRunner()
{
}

FFResult LogoRunner::InitGL( const FFGLViewportStruct* vp )
{
	if( !shader.Compile( vertexShaderCode, fragmentShaderCode ) )
	{
		FFGLLog::LogToHost( "QM Logo Runner: shader compile failed" );
		DeInitGL();
		return FF_FAIL;
	}
	glGenVertexArrays( 1, &vao );
	glGenBuffers( 1, &vbo );
	{
		ScopedVAOBinding vaoBinding( vao );
		glBindBuffer( GL_ARRAY_BUFFER, vbo );
		glEnableVertexAttribArray( 0 );
		glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, 3 * sizeof( float ), (void*)0 );
		glEnableVertexAttribArray( 1 );
		glVertexAttribPointer( 1, 1, GL_FLOAT, GL_FALSE, 3 * sizeof( float ), (void*)( 2 * sizeof( float ) ) );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
	}
	contentDirty = true;
	hasLastTime  = false;
	return CFFGLPlugin::InitGL( vp );
}

FFResult LogoRunner::DeInitGL()
{
	shader.FreeGLResources();
	if( vbo ) { glDeleteBuffers( 1, &vbo ); vbo = 0; }
	if( vao ) { glDeleteVertexArrays( 1, &vao ); vao = 0; }
	if( atlasTex )
	{
		glDeleteTextures( 1, &atlasTex );
		atlasTex = 0;
	}
	return FF_SUCCESS;
}

// --------------------------------------------------------------------------- content

static void PremultiplyAndResize( const unsigned char* src, int sw, int sh, int dh, std::vector< unsigned char >& out, int& dw )
{
	dw = std::max( 1, (int)std::lround( (double)sw * dh / sh ) );
	if( dw > MAX_W - 2 * PAD )
		dw = MAX_W - 2 * PAD;
	out.assign( (size_t)dw * dh * 4, 0 );
	// box-ish bilinear sampling on premultiplied colors
	for( int y = 0; y < dh; ++y )
	{
		float fy = ( y + 0.5f ) * sh / (float)dh - 0.5f;
		int y0   = std::max( 0, std::min( sh - 1, (int)std::floor( fy ) ) );
		int y1   = std::min( sh - 1, y0 + 1 );
		float ty = std::max( 0.0f, std::min( 1.0f, fy - y0 ) );
		for( int x = 0; x < dw; ++x )
		{
			float fx = ( x + 0.5f ) * sw / (float)dw - 0.5f;
			int x0   = std::max( 0, std::min( sw - 1, (int)std::floor( fx ) ) );
			int x1   = std::min( sw - 1, x0 + 1 );
			float tx = std::max( 0.0f, std::min( 1.0f, fx - x0 ) );
			float acc[ 4 ] = { 0, 0, 0, 0 };
			const int xs[ 2 ] = { x0, x1 };
			const int ys[ 2 ] = { y0, y1 };
			const float wx[ 2 ] = { 1 - tx, tx };
			const float wy[ 2 ] = { 1 - ty, ty };
			for( int j = 0; j < 2; ++j )
				for( int i = 0; i < 2; ++i )
				{
					const unsigned char* px = src + ( (size_t)ys[ j ] * sw + xs[ i ] ) * 4;
					float w = wx[ i ] * wy[ j ];
					float a = px[ 3 ] / 255.0f;
					acc[ 0 ] += px[ 0 ] * a * w;
					acc[ 1 ] += px[ 1 ] * a * w;
					acc[ 2 ] += px[ 2 ] * a * w;
					acc[ 3 ] += px[ 3 ] * w;
				}
			unsigned char* o = &out[ ( (size_t)y * dw + x ) * 4 ];
			for( int c = 0; c < 4; ++c )
				o[ c ] = (unsigned char)std::max( 0.0f, std::min( 255.0f, acc[ c ] + 0.5f ) );
		}
	}
}

bool LogoRunner::LoadLogo( const std::string& path, Item& out )
{
	if( path.empty() )
		return false;
	int w = 0, h = 0, comp = 0;
	unsigned char* data = stbi_load( path.c_str(), &w, &h, &comp, 4 );
	if( !data )
	{
		FFGLLog::LogToHost( ( "QM Logo Runner: cannot load image " + path ).c_str() );
		return false;
	}
	// crop to the visible (non transparent) area so logos sit tight on the path
	int minX = w, minY = h, maxX = -1, maxY = -1;
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
			if( data[ ( (size_t)y * w + x ) * 4 + 3 ] > 8 )
			{
				minX = std::min( minX, x );
				maxX = std::max( maxX, x );
				minY = std::min( minY, y );
				maxY = std::max( maxY, y );
			}
	if( maxX < 0 )
	{
		stbi_image_free( data );
		return false;
	}
	int cw = maxX - minX + 1, ch = maxY - minY + 1;
	std::vector< unsigned char > crop( (size_t)cw * ch * 4 );
	for( int y = 0; y < ch; ++y )
		memcpy( &crop[ (size_t)y * cw * 4 ], data + ( (size_t)( y + minY ) * w + minX ) * 4, (size_t)cw * 4 );
	stbi_image_free( data );

	PremultiplyAndResize( crop.data(), cw, ch, ROW_H, out.rgba, out.w );
	out.h      = ROW_H;
	out.isText = false;
	return true;
}

static bool ReadFile( const std::string& path, std::vector< unsigned char >& out )
{
	std::ifstream f( std::filesystem::u8path( path ), std::ios::binary );
	if( !f )
		return false;
	out.assign( std::istreambuf_iterator< char >( f ), std::istreambuf_iterator< char >() );
	return !out.empty();
}

bool LogoRunner::LoadFont()
{
	std::vector< std::string > candidates;
	if( !fontPath.empty() )
		candidates.push_back( fontPath );
#if defined( _WIN32 )
	candidates.push_back( "C:\\Windows\\Fonts\\arialbd.ttf" );
	candidates.push_back( "C:\\Windows\\Fonts\\segoeuib.ttf" );
	candidates.push_back( "C:\\Windows\\Fonts\\arial.ttf" );
#elif defined( __APPLE__ )
	candidates.push_back( "/System/Library/Fonts/Supplemental/Arial Bold.ttf" );
	candidates.push_back( "/Library/Fonts/Arial Bold.ttf" );
	candidates.push_back( "/System/Library/Fonts/Supplemental/Arial.ttf" );
	candidates.push_back( "/System/Library/Fonts/Helvetica.ttc" );
#else
	candidates.push_back( "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" );
	candidates.push_back( "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf" );
#endif
	for( const std::string& c : candidates )
	{
		if( c == loadedFontPath && !fontData.empty() )
			return true;
		std::vector< unsigned char > data;
		if( ReadFile( c, data ) )
		{
			stbtt_fontinfo info;
			int offset = stbtt_GetFontOffsetForIndex( data.data(), 0 );
			if( offset >= 0 && stbtt_InitFont( &info, data.data(), offset ) )
			{
				fontData       = std::move( data );
				loadedFontPath = c;
				return true;
			}
		}
	}
	return false;
}

static std::vector< int > DecodeUtf8( const std::string& s )
{
	std::vector< int > cps;
	size_t i = 0;
	while( i < s.size() )
	{
		unsigned char c = (unsigned char)s[ i ];
		int cp = 0, extra = 0;
		if( c < 0x80 ) { cp = c; extra = 0; }
		else if( ( c >> 5 ) == 0x6 ) { cp = c & 0x1F; extra = 1; }
		else if( ( c >> 4 ) == 0xE ) { cp = c & 0x0F; extra = 2; }
		else if( ( c >> 3 ) == 0x1E ) { cp = c & 0x07; extra = 3; }
		else { ++i; continue; }
		for( int k = 1; k <= extra; ++k )
		{
			if( i + k >= s.size() )
				return cps;
			cp = ( cp << 6 ) | ( (unsigned char)s[ i + k ] & 0x3F );
		}
		cps.push_back( cp );
		i += extra + 1;
	}
	return cps;
}

bool LogoRunner::RenderText( const std::string& str, Item& out )
{
	if( fontData.empty() )
		return false;
	std::vector< int > cps = DecodeUtf8( str );
	if( cps.empty() )
		return false;

	stbtt_fontinfo font;
	stbtt_InitFont( &font, fontData.data(), stbtt_GetFontOffsetForIndex( fontData.data(), 0 ) );
	int ascent, descent, lineGap;
	stbtt_GetFontVMetrics( &font, &ascent, &descent, &lineGap );
	float scale = (float)ROW_H / (float)( ascent - descent );

	// measure
	float widthF = 0;
	for( size_t i = 0; i < cps.size(); ++i )
	{
		int adv, lsb;
		stbtt_GetCodepointHMetrics( &font, cps[ i ], &adv, &lsb );
		widthF += adv * scale;
		if( i + 1 < cps.size() )
			widthF += stbtt_GetCodepointKernAdvance( &font, cps[ i ], cps[ i + 1 ] ) * scale;
	}
	int maxW = MAX_W - 2 * PAD;
	if( widthF > maxW )
	{
		scale *= maxW / widthF;
		widthF = (float)maxW;
	}
	int w = std::max( 1, (int)std::ceil( widthF ) + 2 );
	int h = ROW_H;
	std::vector< unsigned char > alpha( (size_t)w * h, 0 );
	float baseline = ascent * scale + ( h - ( ascent - descent ) * scale ) * 0.5f;

	float x = 1.0f;
	for( size_t i = 0; i < cps.size(); ++i )
	{
		int adv, lsb;
		stbtt_GetCodepointHMetrics( &font, cps[ i ], &adv, &lsb );
		int x0, y0, x1, y1;
		float shiftX = x - std::floor( x );
		stbtt_GetCodepointBitmapBoxSubpixel( &font, cps[ i ], scale, scale, shiftX, 0, &x0, &y0, &x1, &y1 );
		int gw = x1 - x0, gh = y1 - y0;
		if( gw > 0 && gh > 0 )
		{
			std::vector< unsigned char > g( (size_t)gw * gh );
			stbtt_MakeCodepointBitmapSubpixel( &font, g.data(), gw, gh, gw, scale, scale, shiftX, 0, cps[ i ] );
			int ox = (int)std::floor( x ) + x0;
			int oy = (int)std::lround( baseline ) + y0;
			for( int yy = 0; yy < gh; ++yy )
				for( int xx = 0; xx < gw; ++xx )
				{
					int px = ox + xx, py = oy + yy;
					if( px >= 0 && px < w && py >= 0 && py < h )
					{
						unsigned char& dst = alpha[ (size_t)py * w + px ];
						dst                = std::max( dst, g[ (size_t)yy * gw + xx ] );
					}
				}
		}
		x += adv * scale;
		if( i + 1 < cps.size() )
			x += stbtt_GetCodepointKernAdvance( &font, cps[ i ], cps[ i + 1 ] ) * scale;
	}

	out.w = w;
	out.h = h;
	out.rgba.assign( (size_t)w * h * 4, 0 );
	for( size_t i = 0; i < alpha.size(); ++i )
	{
		unsigned char a = alpha[ i ];
		out.rgba[ i * 4 + 0 ] = a;
		out.rgba[ i * 4 + 1 ] = a;
		out.rgba[ i * 4 + 2 ] = a;
		out.rgba[ i * 4 + 3 ] = a;
	}
	out.isText = true;
	return true;
}

static std::vector< std::string > SplitTexts( const std::string& s )
{
	std::vector< std::string > parts;
	std::string cur;
	auto push = [ & ]() {
		size_t b = cur.find_first_not_of( " \t\r\n" );
		size_t e = cur.find_last_not_of( " \t\r\n" );
		if( b != std::string::npos )
			parts.push_back( cur.substr( b, e - b + 1 ) );
		cur.clear();
	};
	for( char c : s )
	{
		if( c == '|' )
			push();
		else
			cur += c;
	}
	push();
	return parts;
}

void LogoRunner::RebuildAtlas()
{
	contentDirty = false;

	std::vector< Item > logos, texts;
	for( int i = 0; i < 3; ++i )
	{
		Item it;
		if( LoadLogo( logoPath[ i ], it ) )
			logos.push_back( std::move( it ) );
	}
	if( LoadFont() )
	{
		for( const std::string& t : SplitTexts( text ) )
		{
			Item it;
			if( RenderText( t, it ) )
				texts.push_back( std::move( it ) );
		}
	}
	else
	{
		FFGLLog::LogToHost( "QM Logo Runner: no font found, choose a .ttf in the Font parameter" );
	}

	int mode = (int)std::lround( values[ P_CONTENT ] );
	std::vector< Item* > seq;
	if( mode == 2 )
	{
		for( auto& l : logos ) seq.push_back( &l );
	}
	else if( mode == 3 )
	{
		for( auto& t : texts ) seq.push_back( &t );
	}
	else if( mode == 1 )
	{
		for( auto& l : logos ) seq.push_back( &l );
		for( auto& t : texts ) seq.push_back( &t );
	}
	else
	{
		size_t n = std::max( logos.size(), texts.size() );
		for( size_t i = 0; i < n; ++i )
		{
			if( !logos.empty() ) seq.push_back( &logos[ i % logos.size() ] );
			if( !texts.empty() ) seq.push_back( &texts[ i % texts.size() ] );
		}
	}
	if( seq.size() > (size_t)MAX_ITEMS )
		seq.resize( MAX_ITEMS );

	itemAspect.clear();
	itemIsText.clear();
	itemRect.clear();
	if( atlasTex )
	{
		glDeleteTextures( 1, &atlasTex );
		atlasTex = 0;
	}
	if( seq.empty() )
		return;

	int atlasW = 1, atlasH = 0;
	for( Item* it : seq )
	{
		atlasW = std::max( atlasW, it->w + 2 * PAD );
		atlasH += it->h + 2 * PAD;
	}
	std::vector< unsigned char > atlas( (size_t)atlasW * atlasH * 4, 0 );
	// rows are written bottom-up so that v = 1 is the top of every item
	int rowBottom = 0;
	for( Item* it : seq )
	{
		int y0 = rowBottom + PAD;
		for( int y = 0; y < it->h; ++y )
		{
			int dstRow = y0 + ( it->h - 1 - y );
			memcpy( &atlas[ ( (size_t)dstRow * atlasW + PAD ) * 4 ], &it->rgba[ (size_t)y * it->w * 4 ], (size_t)it->w * 4 );
		}
		itemRect.push_back( (float)PAD / atlasW );
		itemRect.push_back( (float)y0 / atlasH );
		itemRect.push_back( (float)it->w / atlasW );
		itemRect.push_back( (float)it->h / atlasH );
		itemAspect.push_back( (float)it->w / (float)it->h );
		itemIsText.push_back( it->isText ? 1 : 0 );
		rowBottom += it->h + 2 * PAD;
	}

	glGenTextures( 1, &atlasTex );
	Scoped2DTextureBinding texBinding( atlasTex );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, atlasW, atlasH, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas.data() );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 4 );
	glGenerateMipmap( GL_TEXTURE_2D );
}

// --------------------------------------------------------------------------- render

float LogoRunner::PathLen( int type, float w, float h, float thick )
{
	switch( type )
	{
	case 0:
		return std::max( w, 1.0f );
	case 1:
		return std::max( h, 1.0f );
	case 2:
		return 2.0f * PI_F * std::max( h * 0.5f - thick * 0.5f, 1.0f );
	case 3:
	{
		float a = std::max( w * 0.5f - thick * 0.5f, 1.0f );
		float b = std::max( h * 0.5f - thick * 0.5f, 1.0f );
		return PI_F * ( 3.0f * ( a + b ) - std::sqrt( ( 3.0f * a + b ) * ( a + 3.0f * b ) ) );
	}
	default:
	{
		float W  = std::max( w - thick, 1.0f ), H = std::max( h - thick, 1.0f );
		float rc = std::min( thick * 0.5f, std::min( W, H ) * 0.5f );
		return 2.0f * ( W + H ) - 8.0f * rc + 2.0f * PI_F * rc;
	}
	}
}

// ------------------------------------------------------------------ slices

static long long FileStamp( const std::string& path )
{
	std::error_code ec;
	auto p = std::filesystem::u8path( path );
	auto t = std::filesystem::last_write_time( p, ec );
	if( ec )
		return -1;
	auto sz = std::filesystem::file_size( p, ec );
	return (long long)t.time_since_epoch().count() ^ ( ec ? 0 : (long long)sz << 1 );
}

std::string LogoRunner::DefaultSliceFile() const
{
	std::vector< std::string > cands;
#if defined( _WIN32 )
	const char* up = getenv( "USERPROFILE" );
	if( up )
	{
		std::string u = up;
		cands.push_back( u + "\\Documents\\Resolume Arena\\Preferences\\AdvancedOutput.xml" );
		cands.push_back( u + "\\OneDrive\\Documents\\Resolume Arena\\Preferences\\AdvancedOutput.xml" );
		cands.push_back( u + "\\OneDrive\\Tai lieu\\Resolume Arena\\Preferences\\AdvancedOutput.xml" );
	}
#else
	const char* home = getenv( "HOME" );
	if( home )
	{
		std::string h = home;
		cands.push_back( h + "/Documents/Resolume Arena/Preferences/AdvancedOutput.xml" );
		cands.push_back( h + "/Documents/Resolume Avenue/Preferences/AdvancedOutput.xml" );
	}
#endif
	for( const std::string& c : cands )
		if( FileStamp( c ) != -1 )
			return c;
	return cands.empty() ? std::string() : cands[ 0 ];
}

static std::string Attr( const std::string& tag, const std::string& name )
{
	std::string key = " " + name + "=\"";
	size_t p        = tag.find( key );
	if( p == std::string::npos )
		return "";
	p += key.size();
	size_t e = tag.find( '"', p );
	return e == std::string::npos ? "" : tag.substr( p, e - p );
}

static std::string XmlUnescape( std::string s )
{
	const char* from[] = { "&amp;", "&lt;", "&gt;", "&quot;", "&apos;" };
	const char* to[]   = { "&", "<", ">", "\"", "'" };
	for( int i = 0; i < 5; ++i )
	{
		size_t p = 0;
		while( ( p = s.find( from[ i ], p ) ) != std::string::npos )
		{
			s.replace( p, strlen( from[ i ] ), to[ i ] );
			p += 1;
		}
	}
	return s;
}

bool LogoRunner::LoadSlices( const std::string& path )
{
	std::ifstream f( std::filesystem::u8path( path ), std::ios::binary );
	if( !f )
		return false;
	std::string xml( ( std::istreambuf_iterator< char >( f ) ), std::istreambuf_iterator< char >() );

	std::vector< SliceRect > found;
	size_t cs = xml.find( "<CurrentCompositionTextureSize" );
	if( cs != std::string::npos )
	{
		std::string tag = xml.substr( cs, xml.find( '>', cs ) - cs );
		float w = (float)atof( Attr( tag, "width" ).c_str() );
		float h = (float)atof( Attr( tag, "height" ).c_str() );
		if( w > 0 && h > 0 )
			compW = w, compH = h;
	}

	const char* kinds[] = { "Slice", "Polygon" };
	for( const char* kind : kinds )
	{
		std::string open  = std::string( "<" ) + kind + " ";
		std::string close = std::string( "</" ) + kind + ">";
		size_t pos        = 0;
		while( ( pos = xml.find( open, pos ) ) != std::string::npos )
		{
			size_t end = xml.find( close, pos );
			if( end == std::string::npos )
				break;
			std::string block = xml.substr( pos, end - pos );
			pos               = end;

			SliceRect r;
			r.name    = kind;
			size_t np = block.find( "<Param name=\"Name\"" );
			if( np != std::string::npos )
				r.name = XmlUnescape( Attr( block.substr( np, block.find( '>', np ) - np ), "value" ) );

			size_t ir = block.find( "<InputRect" );
			if( ir == std::string::npos )
				ir = block.find( "<InputContour" );
			if( ir == std::string::npos )
				continue;
			size_t irEnd = block.find( "</Input", ir );
			std::string rectBlock = block.substr( ir, irEnd == std::string::npos ? std::string::npos : irEnd - ir );
			float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
			std::vector< float > vxs, vys;
			size_t vp = 0;
			while( ( vp = rectBlock.find( "<v ", vp ) ) != std::string::npos )
			{
				std::string vt = rectBlock.substr( vp, rectBlock.find( '>', vp ) - vp );
				float x        = (float)atof( Attr( vt, "x" ).c_str() );
				float y        = (float)atof( Attr( vt, "y" ).c_str() );
				x0 = std::min( x0, x ), x1 = std::max( x1, x );
				y0 = std::min( y0, y ), y1 = std::max( y1, y );
				vxs.push_back( x );
				vys.push_back( y );
				vp += 3;
			}
			int nv = (int)vxs.size();
			if( nv < 2 || x1 - x0 < 2 || y1 - y0 < 2 )
				continue;
			r.x0 = x0, r.y0 = y0, r.x1 = x1, r.y1 = y1;
			if( nv == 4 )
			{
				for( int k = 0; k < 4; ++k )
					r.qx[ k ] = vxs[ k ], r.qy[ k ] = vys[ k ];
			}
			else
			{
				// not a quad: fall back to the bounding box
				const float bx[ 4 ] = { x0, x1, x1, x0 };
				const float by[ 4 ] = { y0, y0, y1, y1 };
				for( int k = 0; k < 4; ++k )
					r.qx[ k ] = bx[ k ], r.qy[ k ] = by[ k ];
			}
			bool dup = false;
			for( const SliceRect& o : found )
				if( std::fabs( o.x0 - r.x0 ) < 1 && std::fabs( o.y0 - r.y0 ) < 1 && std::fabs( o.x1 - r.x1 ) < 1 && std::fabs( o.y1 - r.y1 ) < 1 )
					dup = true;
			if( !dup )
				found.push_back( r );
		}
	}

	// order: left -> right, then top -> bottom
	float tol = compW * 0.03f;
	std::stable_sort( found.begin(), found.end(), [ tol ]( const SliceRect& a, const SliceRect& b ) {
		float ax = ( a.x0 + a.x1 ) * 0.5f, bx = ( b.x0 + b.x1 ) * 0.5f;
		if( std::fabs( ax - bx ) > tol )
			return ax < bx;
		return a.y0 < b.y0;
	} );
	if( found.size() > (size_t)MAX_PATHS )
		found.resize( MAX_PATHS );
	slices = found;
	return true;
}

void LogoRunner::CheckSliceFile()
{
	auto now = std::chrono::steady_clock::now();
	if( sliceCheckedOnce && std::chrono::duration< float >( now - lastSliceCheck ).count() < 1.0f )
		return;
	lastSliceCheck   = now;
	sliceCheckedOnce = true;

	std::string path = sliceFile.empty() ? DefaultSliceFile() : sliceFile;
	long long stamp  = FileStamp( path );
	if( path == loadedSliceFile && stamp == sliceFileStamp )
		return;
	loadedSliceFile = path;
	sliceFileStamp  = stamp;
	if( stamp == -1 || !LoadSlices( path ) )
	{
		slices.clear();
		FFGLLog::LogToHost( ( "QM Logo Runner: cannot read slices from " + path ).c_str() );
	}
}

// Shrink a convex quad inward by d along the vertex bisectors.
static void OffsetQuad( const float* qx, const float* qy, float d, float* ox, float* oy )
{
	// inward normals per edge, assuming counter-clockwise winding in GL space
	float nx[ 4 ], ny[ 4 ];
	for( int e = 0; e < 4; ++e )
	{
		int f     = ( e + 1 ) & 3;
		float ex  = qx[ f ] - qx[ e ], ey = qy[ f ] - qy[ e ];
		float len = std::sqrt( ex * ex + ey * ey );
		if( len < 1e-4f )
			len = 1e-4f;
		nx[ e ] = -ey / len;// rotate +90 deg => points inside for CCW winding
		ny[ e ] = ex / len;
	}
	for( int v = 0; v < 4; ++v )
	{
		int e0 = ( v + 3 ) & 3, e1 = v;// edges meeting at vertex v
		// intersect the two offset lines
		float a1 = nx[ e0 ], b1 = ny[ e0 ], c1 = nx[ e0 ] * qx[ v ] + ny[ e0 ] * qy[ v ] + d;
		float a2 = nx[ e1 ], b2 = ny[ e1 ], c2 = nx[ e1 ] * qx[ v ] + ny[ e1 ] * qy[ v ] + d;
		float det = a1 * b2 - a2 * b1;
		if( std::fabs( det ) < 1e-5f )
		{
			ox[ v ] = qx[ v ] + nx[ e1 ] * d;
			oy[ v ] = qy[ v ] + ny[ e1 ] * d;
		}
		else
		{
			ox[ v ] = ( c1 * b2 - c2 * b1 ) / det;
			oy[ v ] = ( a1 * c2 - a2 * c1 ) / det;
		}
	}
}

static float QuadInradius( const float* qx, const float* qy )
{
	// distance from the centroid to the nearest edge
	float cx = 0, cy = 0;
	for( int i = 0; i < 4; ++i )
		cx += qx[ i ] * 0.25f, cy += qy[ i ] * 0.25f;
	float best = 1e9f;
	for( int e = 0; e < 4; ++e )
	{
		int f     = ( e + 1 ) & 3;
		float ex  = qx[ f ] - qx[ e ], ey = qy[ f ] - qy[ e ];
		float len = std::max( 1e-4f, std::sqrt( ex * ex + ey * ey ) );
		float d   = std::fabs( ( -ey * ( cx - qx[ e ] ) + ex * ( cy - qy[ e ] ) ) / len );
		best      = std::min( best, d );
	}
	return best;
}

void LogoRunner::BuildPaths( std::vector< PathDef >& out ) const
{
	out.clear();
	float rw    = (float)currentViewport.width;
	float rh    = (float)currentViewport.height;
	float thick = std::max( 2.0f, values[ P_THICK ] * 0.5f * std::min( rw, rh ) );
	int mode    = (int)std::lround( values[ P_SLICE_MODE ] );

	if( mode == 0 )
	{
		PathDef p;
		p.type     = (int)std::lround( values[ P_SHAPE ] );
		p.cx       = values[ P_POS_X ] * rw;
		p.cy       = ( 1.0f - values[ P_POS_Y ] ) * rh;
		p.w        = values[ P_WIDTH ] * rw;
		p.h        = values[ P_HEIGHT ] * rh;
		p.thick    = thick;
		p.len      = PathLen( p.type, p.w, p.h, thick );
		p.clip     = false;
		p.chainOff = 0;
		p.inset    = 0;
		for( int k = 0; k < 4; ++k )
			p.ox[ k ] = p.oy[ k ] = p.ix[ k ] = p.iy[ k ] = 0.0f;
		out.push_back( p );
		return;
	}

	int sel  = (int)std::lround( values[ P_SLICE_SELECT ] * slices.size() );
	float sx = rw / compW, sy = rh / compH;
	float borderPx = values[ P_BORDER ] > 0.5f ? std::max( 0.5f, values[ P_BORDER_WIDTH ] * 16.0f * ( rh / 1080.0f ) ) : 0.0f;
	float inset    = borderPx * 0.5f + 1.0f + ( values[ P_SLICE_MARGIN ] - 0.2f ) * 100.0f * ( rh / 1080.0f );

	// thinnest slice decides the common thickness in chain mode
	float chainThick = thick;
	for( size_t i = 0; i < slices.size(); ++i )
	{
		if( sel > 0 && (int)i != sel - 1 )
			continue;
		float qx[ 4 ], qy[ 4 ];
		for( int k = 0; k < 4; ++k )
			qx[ k ] = slices[ i ].qx[ k ] * sx, qy[ k ] = rh - slices[ i ].qy[ k ] * sy;
		chainThick = std::min( chainThick, QuadInradius( qx, qy ) * 2.0f );
	}

	float chain = 0.0f;
	for( size_t i = 0; i < slices.size(); ++i )
	{
		if( sel > 0 && (int)i != sel - 1 )
			continue;
		const SliceRect& s = slices[ i ];
		float qx[ 4 ], qy[ 4 ];
		for( int k = 0; k < 4; ++k )
			qx[ k ] = s.qx[ k ] * sx, qy[ k ] = rh - s.qy[ k ] * sy;

		// make the winding counter-clockwise in GL space
		float area = 0;
		for( int k = 0; k < 4; ++k )
		{
			int f = ( k + 1 ) & 3;
			area += qx[ k ] * qy[ f ] - qx[ f ] * qy[ k ];
		}
		if( area < 0 )
		{
			std::swap( qx[ 1 ], qx[ 3 ] );
			std::swap( qy[ 1 ], qy[ 3 ] );
		}

		PathDef p;
		p.clip  = false;// quad shapes bound themselves
		p.inset = inset;
		float inr = QuadInradius( qx, qy );
		float in  = std::min( inset, inr - 2.0f );
		if( in > 0.0f )
			OffsetQuad( qx, qy, in, p.ox, p.oy );
		else
			for( int k = 0; k < 4; ++k )
				p.ox[ k ] = qx[ k ], p.oy[ k ] = qy[ k ];

		float edge[ 4 ];
		for( int e = 0; e < 4; ++e )
		{
			int f     = ( e + 1 ) & 3;
			edge[ e ] = std::sqrt( ( p.ox[ f ] - p.ox[ e ] ) * ( p.ox[ f ] - p.ox[ e ] ) + ( p.oy[ f ] - p.oy[ e ] ) * ( p.oy[ f ] - p.oy[ e ] ) );
		}
		float inr2 = QuadInradius( p.ox, p.oy );

		if( mode == 1 )
		{
			p.type  = 5;// band hugging the quad outline
			p.thick = std::max( 2.0f, std::min( thick, inr2 * 0.95f ) );
			p.len   = edge[ 0 ] + edge[ 1 ] + edge[ 2 ] + edge[ 3 ];
			OffsetQuad( p.ox, p.oy, p.thick, p.ix, p.iy );
		}
		else
		{
			p.type  = 6;// one straight band filling the quad
			float along = ( edge[ 0 ] + edge[ 2 ] ) * 0.5f;
			float cross = ( edge[ 1 ] + edge[ 3 ] ) * 0.5f;
			if( cross > along )
			{
				// rotate the corner order so that edge 0->1 is the long side
				float tx[ 4 ], ty[ 4 ];
				for( int k = 0; k < 4; ++k )
					tx[ k ] = p.ox[ ( k + 1 ) & 3 ], ty[ k ] = p.oy[ ( k + 1 ) & 3 ];
				for( int k = 0; k < 4; ++k )
					p.ox[ k ] = tx[ k ], p.oy[ k ] = ty[ k ];
				std::swap( along, cross );
			}
			p.thick = mode == 3 ? std::min( chainThick, cross ) : std::max( 2.0f, std::min( thick, cross ) );
			p.len   = along;
			for( int k = 0; k < 4; ++k )
				p.ix[ k ] = p.ox[ k ], p.iy[ k ] = p.oy[ k ];
		}

		float cx = 0, cy = 0;
		for( int k = 0; k < 4; ++k )
			cx += p.ox[ k ] * 0.25f, cy += p.oy[ k ] * 0.25f;
		p.cx       = cx;
		p.cy       = cy;
		p.w        = ( s.x1 - s.x0 ) * sx;
		p.h        = ( s.y1 - s.y0 ) * sy;
		p.chainOff = mode == 3 ? chain : 0.0f;
		chain += p.len;
		out.push_back( p );
	}
}

// Build only the triangles the band actually covers, so the GPU never shades
// the empty parts of the frame.
void LogoRunner::BuildGeometry( const std::vector< PathDef >& paths, float pad, float rw, float rh )
{
	verts.clear();
	auto pushTri = [ & ]( float ax, float ay, float bx, float by, float cx, float cy, float idx ) {
		verts.push_back( ax ); verts.push_back( ay ); verts.push_back( idx );
		verts.push_back( bx ); verts.push_back( by ); verts.push_back( idx );
		verts.push_back( cx ); verts.push_back( cy ); verts.push_back( idx );
	};
	auto pushQuad = [ & ]( const float* qx, const float* qy, float idx ) {
		pushTri( qx[ 0 ], qy[ 0 ], qx[ 1 ], qy[ 1 ], qx[ 2 ], qy[ 2 ], idx );
		pushTri( qx[ 0 ], qy[ 0 ], qx[ 2 ], qy[ 2 ], qx[ 3 ], qy[ 3 ], idx );
	};
	auto pushRing = [ & ]( const float* ox, const float* oy, const float* ix, const float* iy, float idx ) {
		for( int e = 0; e < 4; ++e )
		{
			int f = ( e + 1 ) & 3;
			pushTri( ox[ e ], oy[ e ], ox[ f ], oy[ f ], ix[ f ], iy[ f ], idx );
			pushTri( ox[ e ], oy[ e ], ix[ f ], iy[ f ], ix[ e ], iy[ e ], idx );
		}
	};

	// full frame pass that writes transparent, so nothing from the host shows through
	{
		float qx[ 4 ] = { 0, rw, rw, 0 };
		float qy[ 4 ] = { 0, 0, rh, rh };
		pushQuad( qx, qy, -1.0f );
	}

	for( size_t k = 0; k < paths.size(); ++k )
	{
		const PathDef& p = paths[ k ];
		float idx        = (float)k;
		float hw         = p.thick * 0.5f + pad;

		if( p.type == 5 )
		{
			float ox[ 4 ], oy[ 4 ], ix[ 4 ], iy[ 4 ];
			OffsetQuad( p.ox, p.oy, -pad, ox, oy );
			OffsetQuad( p.ox, p.oy, p.thick + pad, ix, iy );
			pushRing( ox, oy, ix, iy, idx );
		}
		else if( p.type == 6 )
		{
			float across = std::sqrt( ( p.ox[ 0 ] - p.ox[ 3 ] ) * ( p.ox[ 0 ] - p.ox[ 3 ] ) + ( p.oy[ 0 ] - p.oy[ 3 ] ) * ( p.oy[ 0 ] - p.oy[ 3 ] ) );
			float half   = std::min( 0.5f, hw / std::max( across, 1.0f ) );
			float v0 = 0.5f - half, v1 = 0.5f + half;
			auto bilerp = [ & ]( float u, float v, float& x, float& y ) {
				float tx = p.ox[ 0 ] + ( p.ox[ 1 ] - p.ox[ 0 ] ) * u;
				float ty = p.oy[ 0 ] + ( p.oy[ 1 ] - p.oy[ 0 ] ) * u;
				float bx = p.ox[ 3 ] + ( p.ox[ 2 ] - p.ox[ 3 ] ) * u;
				float by = p.oy[ 3 ] + ( p.oy[ 2 ] - p.oy[ 3 ] ) * u;
				x = tx + ( bx - tx ) * v;
				y = ty + ( by - ty ) * v;
			};
			float qx[ 4 ], qy[ 4 ];
			bilerp( 0, v0, qx[ 0 ], qy[ 0 ] );
			bilerp( 1, v0, qx[ 1 ], qy[ 1 ] );
			bilerp( 1, v1, qx[ 2 ], qy[ 2 ] );
			bilerp( 0, v1, qx[ 3 ], qy[ 3 ] );
			pushQuad( qx, qy, idx );
		}
		else if( p.type == 0 || p.type == 1 )
		{
			float halfW = p.type == 0 ? p.w * 0.5f : hw;
			float halfH = p.type == 0 ? hw : p.h * 0.5f;
			float qx[ 4 ] = { p.cx - halfW, p.cx + halfW, p.cx + halfW, p.cx - halfW };
			float qy[ 4 ] = { p.cy - halfH, p.cy - halfH, p.cy + halfH, p.cy + halfH };
			pushQuad( qx, qy, idx );
		}
		else if( p.type == 2 || p.type == 3 )
		{
			float rx = p.type == 2 ? p.h * 0.5f - p.thick * 0.5f : p.w * 0.5f - p.thick * 0.5f;
			float ry = p.h * 0.5f - p.thick * 0.5f;
			rx       = std::max( rx, 1.0f );
			ry       = std::max( ry, 1.0f );
			const int N = 72;
			for( int i = 0; i < N; ++i )
			{
				float a0 = 2.0f * PI_F * i / N, a1 = 2.0f * PI_F * ( i + 1 ) / N;
				float c0 = std::cos( a0 ), s0 = std::sin( a0 ), c1 = std::cos( a1 ), s1 = std::sin( a1 );
				float o0x = p.cx + ( rx + hw ) * c0, o0y = p.cy + ( ry + hw ) * s0;
				float o1x = p.cx + ( rx + hw ) * c1, o1y = p.cy + ( ry + hw ) * s1;
				float i0x = p.cx + std::max( rx - hw, 0.0f ) * c0, i0y = p.cy + std::max( ry - hw, 0.0f ) * s0;
				float i1x = p.cx + std::max( rx - hw, 0.0f ) * c1, i1y = p.cy + std::max( ry - hw, 0.0f ) * s1;
				pushTri( o0x, o0y, o1x, o1y, i1x, i1y, idx );
				pushTri( o0x, o0y, i1x, i1y, i0x, i0y, idx );
			}
		}
		else
		{
			float ow = p.w * 0.5f + pad, oh = p.h * 0.5f + pad;
			float iw = std::max( 0.0f, p.w * 0.5f - p.thick - pad ), ih = std::max( 0.0f, p.h * 0.5f - p.thick - pad );
			float ox[ 4 ] = { p.cx - ow, p.cx + ow, p.cx + ow, p.cx - ow };
			float oy[ 4 ] = { p.cy - oh, p.cy - oh, p.cy + oh, p.cy + oh };
			float ix[ 4 ] = { p.cx - iw, p.cx + iw, p.cx + iw, p.cx - iw };
			float iy[ 4 ] = { p.cy - ih, p.cy - ih, p.cy + ih, p.cy + ih };
			pushRing( ox, oy, ix, iy, idx );
		}
	}
}

static void HsvToRgb( float h, float s, float v, float& r, float& g, float& b )
{
	if( h >= 1.0f )
		h = 0.0f;
	HSVtoRGB( h, s, v, r, g, b );
}

FFResult LogoRunner::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( contentDirty )
		RebuildAtlas();
	if( std::lround( values[ P_SLICE_MODE ] ) != 0 )
		CheckSliceFile();

	auto now = std::chrono::steady_clock::now();
	float dt = 0.0f;
	if( hasLastTime )
		dt = std::chrono::duration< float >( now - lastTime ).count();
	lastTime    = now;
	hasLastTime = true;
	dt          = std::min( std::max( dt, 0.0f ), 0.1f );

	float rw    = (float)currentViewport.width;
	float rh    = (float)currentViewport.height;
	float scale = 0.1f + 0.9f * values[ P_ITEM_SIZE ];
	float gapF  = values[ P_GAP ] * 2.0f;
	int count   = (int)itemAspect.size();
	int mode    = (int)std::lround( values[ P_SLICE_MODE ] );

	float cumAspect[ MAX_ITEMS ] = {};
	float totalAspect            = 0.0f;
	for( int i = 0; i < count; ++i )
	{
		cumAspect[ i ] = totalAspect;
		totalAspect += itemAspect[ i ];
	}

	std::vector< PathDef > paths;
	BuildPaths( paths );
	int np = (int)paths.size();

	float sp       = ( values[ P_SPEED ] - 0.5f ) * 2.0f;
	float pxPerSec = sp * std::fabs( sp ) * 600.0f * ( rh / 1080.0f );

	float rectU[ MAX_PATHS * 4 ], infoU[ MAX_PATHS * 4 ], motionU[ MAX_PATHS * 4 ];
	float outerU[ MAX_PATHS * 8 ], innerU[ MAX_PATHS * 8 ];
	for( int k = 0; k < np; ++k )
	{
		const PathDef& p = paths[ k ];
		float base       = p.thick * ( scale * totalAspect + gapF * count );
		float extra      = 0.0f;
		bool closed      = p.type >= 2 && p.type != 6;
		if( closed && count > 0 && base > 1.0f && mode != 3 )
		{
			float rep = std::max( 1.0f, std::round( p.len / base ) );
			extra     = ( p.len / rep - base ) / count;
			if( base + extra * count < 1.0f )
				extra = 0.0f;
		}
		float cycle = base + extra * count;
		int pk      = mode == 3 ? 0 : k;// chain mode: one shared phase
		if( mode != 3 || k == 0 )
		{
			phase[ pk ] += pxPerSec * dt;
			if( cycle > 0.0f )
			{
				phase[ pk ] = std::fmod( phase[ pk ], (double)cycle );
				if( phase[ pk ] < 0 )
					phase[ pk ] += cycle;
			}
		}
		float bcx = p.cx, bcy = p.cy, bw = p.w, bh = p.h;
		bool clip = p.clip;
		if( p.type >= 5 )
		{
			float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
			for( int v = 0; v < 4; ++v )
			{
				x0 = std::min( x0, p.ox[ v ] ), x1 = std::max( x1, p.ox[ v ] );
				y0 = std::min( y0, p.oy[ v ] ), y1 = std::max( y1, p.oy[ v ] );
			}
			bcx = ( x0 + x1 ) * 0.5f, bcy = ( y0 + y1 ) * 0.5f;
			bw  = x1 - x0 + 4.0f, bh = y1 - y0 + 4.0f;
			clip = true;
		}
		rectU[ k * 4 + 0 ]   = bcx;
		rectU[ k * 4 + 1 ]   = bcy;
		rectU[ k * 4 + 2 ]   = bw;
		rectU[ k * 4 + 3 ]   = bh;
		infoU[ k * 4 + 0 ]   = (float)p.type;
		infoU[ k * 4 + 1 ]   = p.len;
		infoU[ k * 4 + 2 ]   = p.thick;
		infoU[ k * 4 + 3 ]   = clip ? 1.0f : 0.0f;
		motionU[ k * 4 + 0 ] = extra;
		motionU[ k * 4 + 1 ] = p.chainOff;
		motionU[ k * 4 + 2 ] = (float)phase[ pk ] + values[ P_OFFSET ] * cycle;
		motionU[ k * 4 + 3 ] = cycle;
		for( int v = 0; v < 4; ++v )
		{
			outerU[ k * 8 + v * 2 + 0 ] = p.ox[ v ];
			outerU[ k * 8 + v * 2 + 1 ] = p.oy[ v ];
			innerU[ k * 8 + v * 2 + 0 ] = p.ix[ v ];
			innerU[ k * 8 + v * 2 + 1 ] = p.iy[ v ];
		}
	}

	float tr, tg, tb, br, bg, bb;
	HsvToRgb( values[ P_TEXT_HUE ], values[ P_TEXT_SAT ], values[ P_TEXT_BRI ], tr, tg, tb );
	HsvToRgb( values[ P_BORDER_HUE ], values[ P_BORDER_SAT ], values[ P_BORDER_BRI ], br, bg, bb );
	float borderA = values[ P_BORDER ] > 0.5f ? values[ P_BORDER_ALPHA ] : 0.0f;
	float k       = rh / 1080.0f;

	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	GLuint prog = shader.GetGLID();
	shader.Set( "res", rw, rh );
	shader.Set( "pathCount", np );
	if( np > 0 )
	{
		glUniform4fv( glGetUniformLocation( prog, "pRect" ), np, rectU );
		glUniform4fv( glGetUniformLocation( prog, "pInfo" ), np, infoU );
		glUniform4fv( glGetUniformLocation( prog, "pMotion" ), np, motionU );
		glUniform4fv( glGetUniformLocation( prog, "pOuter" ), np * 2, outerU );
		glUniform4fv( glGetUniformLocation( prog, "pInner" ), np * 2, innerU );
	}
	shader.Set( "flipSide", values[ P_FLIP ] > 0.5f ? 1 : 0 );
	shader.Set( "count", count );
	shader.Set( "itemScale", scale );
	shader.Set( "gapFactor", gapF );
	shader.Set( "textColor", tr, tg, tb, values[ P_TEXT_ALPHA ] );
	shader.Set( "borderColor", br, bg, bb, borderA );
	shader.Set( "borderW", std::max( 0.5f, values[ P_BORDER_WIDTH ] * 16.0f * k ) );
	shader.Set( "glowR", values[ P_GLOW ] * 30.0f * k );
	shader.Set( "bandFill", values[ P_BAND_FILL ] );
	if( count > 0 )
	{
		glUniform1fv( glGetUniformLocation( prog, "aspects" ), count, itemAspect.data() );
		glUniform1fv( glGetUniformLocation( prog, "cumAspect" ), count, cumAspect );
		glUniform4fv( glGetUniformLocation( prog, "rects" ), count, itemRect.data() );
		glUniform1iv( glGetUniformLocation( prog, "isText" ), count, itemIsText.data() );
	}
	shader.Set( "atlas", 0 );

	float pad = std::max( 2.0f, values[ P_BORDER_WIDTH ] * 16.0f * k + values[ P_GLOW ] * 30.0f * k * 4.0f );
	BuildGeometry( paths, pad, rw, rh );

	ScopedSamplerActivation activateSampler( 0 );
	Scoped2DTextureBinding textureBinding( atlasTex );
	if( !verts.empty() )
	{
		GLboolean blendWas = glIsEnabled( GL_BLEND );
		if( blendWas )
			glDisable( GL_BLEND );
		ScopedVAOBinding vaoBinding( vao );
		glBindBuffer( GL_ARRAY_BUFFER, vbo );
		glBufferData( GL_ARRAY_BUFFER, verts.size() * sizeof( float ), verts.data(), GL_STREAM_DRAW );
		glDrawArrays( GL_TRIANGLES, 0, (GLsizei)( verts.size() / 3 ) );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		if( blendWas )
			glEnable( GL_BLEND );
	}
	return FF_SUCCESS;
}


// --------------------------------------------------------------------------- params

FFResult LogoRunner::SetFloatParameter( unsigned int index, float value )
{
	if( index >= P_COUNT )
		return FF_FAIL;
	if( index == P_CONTENT && std::lround( values[ index ] ) != std::lround( value ) )
		contentDirty = true;
	values[ index ] = value;
	return FF_SUCCESS;
}

float LogoRunner::GetFloatParameter( unsigned int index )
{
	if( index >= P_COUNT )
		return 0.0f;
	return values[ index ];
}

FFResult LogoRunner::SetTextParameter( unsigned int index, const char* value )
{
	std::string v = value ? value : "";
	switch( index )
	{
	case P_LOGO1:
	case P_LOGO2:
	case P_LOGO3:
		if( logoPath[ index - P_LOGO1 ] != v )
			contentDirty = true;
		logoPath[ index - P_LOGO1 ] = v;
		return FF_SUCCESS;
	case P_TEXT:
		if( text != v )
			contentDirty = true;
		text = v;
		return FF_SUCCESS;
	case P_FONT:
		if( fontPath != v )
			contentDirty = true;
		fontPath = v;
		return FF_SUCCESS;
	case P_SLICE_FILE:
		sliceFile        = v;
		sliceCheckedOnce = false;
		return FF_SUCCESS;
	}
	return FF_FAIL;
}

char* LogoRunner::GetTextParameter( unsigned int index )
{
	switch( index )
	{
	case P_LOGO1:
	case P_LOGO2:
	case P_LOGO3:
		return const_cast< char* >( logoPath[ index - P_LOGO1 ].c_str() );
	case P_TEXT:
		return const_cast< char* >( text.c_str() );
	case P_FONT:
		return const_cast< char* >( fontPath.c_str() );
	case P_SLICE_FILE:
		return const_cast< char* >( sliceFile.c_str() );
	}
	return (char*)FF_FAIL;
}

char* LogoRunner::GetParameterDisplay( unsigned int index )
{
	float rw = (float)currentViewport.width;
	float rh = (float)currentViewport.height;
	if( rw < 1 ) rw = 1920;
	if( rh < 1 ) rh = 1080;
	float v = index < P_COUNT ? values[ index ] : 0.0f;
	switch( index )
	{
	case P_POS_X:
	case P_WIDTH:
		snprintf( displayBuf, sizeof( displayBuf ), "%d px", (int)std::lround( v * rw ) );
		return displayBuf;
	case P_POS_Y:
	case P_HEIGHT:
		snprintf( displayBuf, sizeof( displayBuf ), "%d px", (int)std::lround( v * rh ) );
		return displayBuf;
	case P_THICK:
		snprintf( displayBuf, sizeof( displayBuf ), "%d px", (int)std::lround( std::max( 2.0f, v * 0.5f * std::min( rw, rh ) ) ) );
		return displayBuf;
	case P_SLICE_SELECT:
	{
		if( slices.empty() )
		{
			snprintf( displayBuf, sizeof( displayBuf ), "Tat ca (chua doc slice)" );
			return displayBuf;
		}
		int sel = (int)std::lround( v * slices.size() );
		if( sel <= 0 )
			snprintf( displayBuf, sizeof( displayBuf ), "Tat ca (%d slice)", (int)slices.size() );
		else
			snprintf( displayBuf, sizeof( displayBuf ), "%d: %s", sel, slices[ sel - 1 ].name.c_str() );
		return displayBuf;
	}
	case P_SLICE_MARGIN:
		snprintf( displayBuf, sizeof( displayBuf ), "%+d px", (int)std::lround( ( v - 0.2f ) * 100.0f * ( rh / 1080.0f ) ) );
		return displayBuf;
	case P_SPEED:
	{
		float sp = ( v - 0.5f ) * 2.0f;
		snprintf( displayBuf, sizeof( displayBuf ), "%+d%%", (int)std::lround( sp * 100.0f ) );
		return displayBuf;
	}
	}
	return CFFGLPlugin::GetParameterDisplay( index );
}
