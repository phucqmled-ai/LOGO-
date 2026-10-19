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
	0,                   // Plugin minor version number
	FF_SOURCE,           // Plugin type
	"Chay logo PNG va chu theo duong ngang, doc, tron, elip, vien chu nhat",
	"QM LED" );

static const int ROW_H   = 256;// pixel height of every item in the atlas
static const int PAD     = 8;  // transparent padding around each item
static const int MAX_W   = 4096;
static const float PI_F  = 3.14159265358979f;

static const char vertexShaderCode[] = R"(#version 410 core
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
void main()
{
	gl_Position = vPosition;
	uv = vUV;
}
)";

static const char fragmentShaderCode[] = R"(#version 410 core
#define MAXI 16
in vec2 uv;
out vec4 fragColor;

uniform vec2 res;
uniform int shape;          // 0 ngang, 1 doc, 2 tron, 3 elip, 4 vien chu nhat
uniform vec2 center;        // px
uniform vec2 size;          // px (outer)
uniform float thick;        // px
uniform float pathLen;      // px
uniform int closedPath;
uniform int flipSide;
uniform float scrollPos;    // px
uniform float cycleLen;     // px
uniform int count;
uniform float itemScale;
uniform float starts[ MAXI ];
uniform float cells[ MAXI ];
uniform float lens[ MAXI ];
uniform vec4 rects[ MAXI ];
uniform int isText[ MAXI ];
uniform sampler2D atlas;
uniform vec4 textColor;
uniform vec4 borderColor;   // a = 0 means off
uniform float borderW;      // px
uniform float glowR;        // px
uniform float bandFill;

// premultiplied "over"
vec4 over( vec4 top, vec4 bottom ) { return top + bottom * ( 1.0 - top.a ); }

void main()
{
	vec2 p = uv * res;
	vec2 d = p - center;
	float s = 0.0;   // distance along path (px)
	float n = 0.0;   // 0..1 across band, 1 = top of item
	bool along = true;

	if( shape == 0 )
	{
		s = p.x - ( center.x - size.x * 0.5 );
		n = d.y / thick + 0.5;
		along = s >= 0.0 && s <= pathLen;
	}
	else if( shape == 1 )
	{
		s = ( center.y + size.y * 0.5 ) - p.y;
		n = d.x / thick + 0.5;
		along = s >= 0.0 && s <= pathLen;
	}
	else if( shape == 2 || shape == 3 )
	{
		vec2 r = shape == 2 ? vec2( size.y * 0.5 - thick * 0.5 ) : max( size * 0.5 - thick * 0.5, vec2( 1.0 ) );
		r = max( r, vec2( 1.0 ) );
		float t = atan( d.y / r.y, d.x / r.x );
		vec2 m = vec2( r.x * cos( t ), r.y * sin( t ) );
		float dist = length( d ) - length( m );
		n = dist / thick + 0.5;
		float u = fract( 0.25 - t / 6.28318530718 );// 0 at top, clockwise
		s = u * pathLen;
	}
	else
	{
		float W = max( size.x - thick, 1.0 );
		float H = max( size.y - thick, 1.0 );
		float hw = W * 0.5, hh = H * 0.5;
		float ex = abs( d.x ) - hw;
		float ey = abs( d.y ) - hh;
		if( ey > ex )
		{
			if( d.y > 0.0 ) { s = d.x + hw;                 n = ( d.y - hh ) / thick + 0.5; }
			else            { s = W + H + ( hw - d.x );     n = ( -d.y - hh ) / thick + 0.5; }
		}
		else
		{
			if( d.x > 0.0 ) { s = W + ( hh - d.y );         n = ( d.x - hw ) / thick + 0.5; }
			else            { s = 2.0 * W + H + ( d.y + hh ); n = ( -d.x - hw ) / thick + 0.5; }
		}
	}

	if( flipSide == 1 )
	{
		n = 1.0 - n;
		s = pathLen - s;
	}

	vec4 col = vec4( 0.0 );
	if( !along )
	{
		fragColor = col;
		return;
	}

	// band background
	if( n >= 0.0 && n <= 1.0 )
		col = vec4( 0.0, 0.0, 0.0, bandFill );

	// items
	if( count > 0 && cycleLen > 0.0 && n >= 0.0 && n <= 1.0 )
	{
		float x = mod( s + scrollPos, cycleLen );
		for( int i = 0; i < MAXI; ++i )
		{
			if( i >= count ) break;
			if( x >= starts[ i ] && x < starts[ i ] + cells[ i ] )
			{
				float xi = x - starts[ i ];
				float u  = ( xi - ( cells[ i ] - lens[ i ] ) * 0.5 ) / lens[ i ];
				float v  = ( n - 0.5 ) / itemScale + 0.5;
				if( u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0 )
				{
					vec4 r  = rects[ i ];
					vec4 tx = texture( atlas, r.xy + vec2( u, v ) * r.zw );
					if( isText[ i ] == 1 )
						tx = vec4( textColor.rgb, 1.0 ) * tx.a * textColor.a;
					col = over( tx, col );
				}
				break;
			}
		}
	}

	// border lines + glow at both band edges
	if( borderColor.a > 0.0 )
	{
		float dpx = min( abs( n ) , abs( n - 1.0 ) ) * thick;
		float line = 1.0 - smoothstep( borderW * 0.5 - 0.75, borderW * 0.5 + 0.75, dpx );
		float glow = glowR > 0.5 ? exp( -max( dpx - borderW * 0.5, 0.0 ) / glowR ) * 0.6 : 0.0;
		float a = clamp( max( line, glow ), 0.0, 1.0 ) * borderColor.a;
		col = over( vec4( borderColor.rgb * a, a ), col );
	}

	// output straight (non premultiplied) alpha
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
	if( !quad.Initialise() )
	{
		DeInitGL();
		return FF_FAIL;
	}
	contentDirty = true;
	hasLastTime  = false;
	return CFFGLPlugin::InitGL( vp );
}

FFResult LogoRunner::DeInitGL()
{
	shader.FreeGLResources();
	quad.Release();
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

float LogoRunner::PathLength() const
{
	float rw    = (float)currentViewport.width;
	float rh    = (float)currentViewport.height;
	float w     = values[ P_WIDTH ] * rw;
	float h     = values[ P_HEIGHT ] * rh;
	float thick = std::max( 2.0f, values[ P_THICK ] * 0.5f * std::min( rw, rh ) );
	int shape   = (int)std::lround( values[ P_SHAPE ] );
	switch( shape )
	{
	case 0:
		return std::max( w, 1.0f );
	case 1:
		return std::max( h, 1.0f );
	case 2:
	{
		float r = std::max( h * 0.5f - thick * 0.5f, 1.0f );
		return 2.0f * PI_F * r;
	}
	case 3:
	{
		float a = std::max( w * 0.5f - thick * 0.5f, 1.0f );
		float b = std::max( h * 0.5f - thick * 0.5f, 1.0f );
		return PI_F * ( 3.0f * ( a + b ) - std::sqrt( ( 3.0f * a + b ) * ( a + 3.0f * b ) ) );
	}
	default:
		return 2.0f * ( std::max( w - thick, 1.0f ) + std::max( h - thick, 1.0f ) );
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

	auto now = std::chrono::steady_clock::now();
	float dt = 0.0f;
	if( hasLastTime )
		dt = std::chrono::duration< float >( now - lastTime ).count();
	lastTime    = now;
	hasLastTime = true;
	dt          = std::min( std::max( dt, 0.0f ), 0.1f );

	float rw    = (float)currentViewport.width;
	float rh    = (float)currentViewport.height;
	int shape   = (int)std::lround( values[ P_SHAPE ] );
	float thick = std::max( 2.0f, values[ P_THICK ] * 0.5f * std::min( rw, rh ) );
	float scale = 0.1f + 0.9f * values[ P_ITEM_SIZE ];
	float gapPx = values[ P_GAP ] * thick * 2.0f;
	float L     = PathLength();
	bool closed = shape >= 2;

	int count = (int)itemAspect.size();
	float lens[ MAX_ITEMS ], cells[ MAX_ITEMS ], starts[ MAX_ITEMS ];
	float sum = 0.0f;
	for( int i = 0; i < count; ++i )
	{
		lens[ i ] = std::max( 1.0f, thick * scale * itemAspect[ i ] );
		sum += lens[ i ] + gapPx;
	}
	float cycle = sum, extraPer = 0.0f;
	if( closed && count > 0 )
	{
		float rep = std::max( 1.0f, std::round( L / sum ) );
		cycle     = L / rep;
		extraPer  = ( cycle - sum ) / count;
		if( lens[ 0 ] + gapPx + extraPer < 1.0f )
			extraPer = 0.0f, cycle = sum;
	}
	float acc = 0.0f;
	for( int i = 0; i < count; ++i )
	{
		starts[ i ] = acc;
		cells[ i ]  = std::max( lens[ i ], lens[ i ] + gapPx + extraPer );
		acc += cells[ i ];
	}
	if( count > 0 )
		cycle = acc;

	float sp = ( values[ P_SPEED ] - 0.5f ) * 2.0f;
	float pxPerSec = sp * std::fabs( sp ) * 600.0f * ( rh / 1080.0f );
	scroll += pxPerSec * dt;
	if( cycle > 0.0f )
		scroll = std::fmod( scroll, (double)cycle );
	float scrollPos = (float)scroll + values[ P_OFFSET ] * cycle;
	if( cycle > 0.0f )
	{
		scrollPos = std::fmod( scrollPos, cycle );
		if( scrollPos < 0.0f )
			scrollPos += cycle;
	}

	float tr, tg, tb, br, bg, bb;
	HsvToRgb( values[ P_TEXT_HUE ], values[ P_TEXT_SAT ], values[ P_TEXT_BRI ], tr, tg, tb );
	HsvToRgb( values[ P_BORDER_HUE ], values[ P_BORDER_SAT ], values[ P_BORDER_BRI ], br, bg, bb );
	float borderA = values[ P_BORDER ] > 0.5f ? values[ P_BORDER_ALPHA ] : 0.0f;
	float k       = rh / 1080.0f;

	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	GLuint prog = shader.GetGLID();
	shader.Set( "res", rw, rh );
	shader.Set( "shape", shape );
	shader.Set( "center", values[ P_POS_X ] * rw, ( 1.0f - values[ P_POS_Y ] ) * rh );
	shader.Set( "size", values[ P_WIDTH ] * rw, values[ P_HEIGHT ] * rh );
	shader.Set( "thick", thick );
	shader.Set( "pathLen", L );
	shader.Set( "closedPath", closed ? 1 : 0 );
	shader.Set( "flipSide", values[ P_FLIP ] > 0.5f ? 1 : 0 );
	shader.Set( "scrollPos", scrollPos );
	shader.Set( "cycleLen", cycle );
	shader.Set( "count", count );
	shader.Set( "itemScale", scale );
	shader.Set( "textColor", tr, tg, tb, values[ P_TEXT_ALPHA ] );
	shader.Set( "borderColor", br, bg, bb, borderA );
	shader.Set( "borderW", std::max( 0.5f, values[ P_BORDER_WIDTH ] * 16.0f * k ) );
	shader.Set( "glowR", values[ P_GLOW ] * 30.0f * k );
	shader.Set( "bandFill", values[ P_BAND_FILL ] );
	if( count > 0 )
	{
		glUniform1fv( glGetUniformLocation( prog, "starts" ), count, starts );
		glUniform1fv( glGetUniformLocation( prog, "cells" ), count, cells );
		glUniform1fv( glGetUniformLocation( prog, "lens" ), count, lens );
		glUniform4fv( glGetUniformLocation( prog, "rects" ), count, itemRect.data() );
		glUniform1iv( glGetUniformLocation( prog, "isText" ), count, itemIsText.data() );
	}
	shader.Set( "atlas", 0 );

	ScopedSamplerActivation activateSampler( 0 );
	Scoped2DTextureBinding textureBinding( atlasTex );
	quad.Draw();
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
	case P_SPEED:
	{
		float sp = ( v - 0.5f ) * 2.0f;
		snprintf( displayBuf, sizeof( displayBuf ), "%+d%%", (int)std::lround( sp * 100.0f ) );
		return displayBuf;
	}
	}
	return CFFGLPlugin::GetParameterDisplay( index );
}
