#pragma once
#include <FFGLSDK.h>
#include <string>
#include <vector>
#include <chrono>

// QM Logo Runner - FFGL source plugin for Resolume Arena/Avenue 7+
// Chay logo PNG va chu theo duong: ngang, doc, tron/elip, vien chu nhat.

class LogoRunner : public CFFGLPlugin
{
public:
	LogoRunner();
	~LogoRunner() override;

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	char* GetTextParameter( unsigned int index ) override;
	char* GetParameterDisplay( unsigned int index ) override;

	enum Param : unsigned int
	{
		P_LOGO1,
		P_LOGO2,
		P_LOGO3,
		P_TEXT,
		P_FONT,
		P_CONTENT,
		P_TEXT_HUE,
		P_TEXT_SAT,
		P_TEXT_BRI,
		P_TEXT_ALPHA,
		P_SHAPE,
		P_POS_X,
		P_POS_Y,
		P_WIDTH,
		P_HEIGHT,
		P_THICK,
		P_ITEM_SIZE,
		P_GAP,
		P_FLIP,
		P_SPEED,
		P_OFFSET,
		P_BORDER,
		P_BORDER_HUE,
		P_BORDER_SAT,
		P_BORDER_BRI,
		P_BORDER_ALPHA,
		P_BORDER_WIDTH,
		P_GLOW,
		P_BAND_FILL,
		P_COUNT
	};

	static const int MAX_ITEMS = 16;

private:
	struct Item
	{
		int w = 0, h = 0;
		std::vector< unsigned char > rgba;// premultiplied, top row first
		bool isText = false;
	};

	void RebuildAtlas();
	bool LoadLogo( const std::string& path, Item& out );
	bool RenderText( const std::string& text, Item& out );
	bool LoadFont();
	float PathLength() const;

	float values[ P_COUNT ];
	std::string logoPath[ 3 ];
	std::string text;
	std::string fontPath;
	std::string loadedFontPath;
	std::vector< unsigned char > fontData;
	bool contentDirty = true;

	std::vector< float > itemAspect;
	std::vector< int > itemIsText;
	std::vector< float > itemRect;// x,y,w,h in atlas uv
	GLuint atlasTex = 0;

	ffglex::FFGLShader shader;
	ffglex::FFGLScreenQuad quad;

	double scroll = 0.0;
	std::chrono::steady_clock::time_point lastTime;
	bool hasLastTime = false;
	char displayBuf[ 64 ];
};
