/**
    sliptest -- render Slipstream offline, and measure what its camera saw.

    It drives the REAL plugin class, through the same ProcessOpenGL a host
    calls, on a synthetic clock, in a headless CGL context. The checks read
    the picture, the exposure buffers the plugin draws into (the stars' and
    the scene's, half float, before development), and the plugin's own CPU
    state (Flight.h, Geodesic.h, Blackbody.h).

        sliptest --out /tmp/s.png         the source, the defaults
        sliptest --over --out /tmp/o.png  the Over on the harness's card
        sliptest --list                   every parameter and its default
        sliptest --set "Look=Black Hole"  a control by name (an option by its
                                          element's name), repeatable
        sliptest --at 90 "Jump=1"         a setting from frame 90 on
        sliptest --phase 0.5              start the journey at J = 0.5
        sliptest --pipe [--frames N]      the source's frames, raw RGBA on stdout
        sliptest --over --pipe            raw frames in, raw frames out
        sliptest --film N                 N frames, raw RGBA on stdout
        sliptest --offline                the checks that need no GL context (CI)

    `--script` is the fleet's cue format: `frame  Parameter Name  value` lines,
    held before the first key and after the last. A STANDARD (0..1) control is
    linear between its keys; an option, a boolean, an event or an integer
    STEPS: it holds each key's value until the next key's frame.

    SLIPTEST_RENDERER=software asks for Apple's software renderer by id, on a
    Mac with a GPU: what a GPU-less CI runner falls back to.
*/

#include "Blackbody.h"
#include "Controls.h"
#include "Flight.h"
#include "Geodesic.h"
#include "Hash.h"
#include "Shaders.h"
#include "Slipstream.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace slipstream;

namespace
{
using Floats = std::vector< float >;
using Bytes  = std::vector< unsigned char >;

constexpr double kPi = 3.14159265358979323846;

//---------------------------------------------------------------------------
// Reporting.
//---------------------------------------------------------------------------
int g_failures = 0;
int g_checks   = 0;

std::string fmt( const char* format, ... )
{
	char buffer[ 4096 ];
	va_list args;
	va_start( args, format );
	std::vsnprintf( buffer, sizeof( buffer ), format, args );
	va_end( args );
	return buffer;
}

void Check( bool condition, const std::string& message )
{
	++g_checks;
	std::printf( "  %s  %s\n", condition ? "ok  " : "FAIL", message.c_str() );
	if( !condition )
		++g_failures;
}

int Verdict()
{
	std::printf( "\n  %s\n", g_failures == 0 ? "PASS" : "FAIL" );
	return g_failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// PNG. zlib ships with the OS.
//---------------------------------------------------------------------------
void putU32( Bytes& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( Bytes& out, const char* type, const Bytes& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

/// `rgba` is floats, row 0 at the BOTTOM (GL's order); the file is written top
/// row first. The source is premultiplied: written over black, alpha opaque.
bool writePng( const std::string& path, int width, int height, const Floats& rgba )
{
	Bytes raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = height - 1; y >= 0; --y )
	{
		raw.push_back( 0 );
		for( int x = 0; x < width; ++x )
			for( int c = 0; c < 4; ++c )
			{
				const float v = c == 3 ? 1.0f : rgba[ ( static_cast< size_t >( y ) * width + x ) * 4 + c ];
				raw.push_back( static_cast< unsigned char >( std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f ) ) );
			}
	}
	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	Bytes compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );

	Bytes png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	Bytes ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.insert( ihdr.end(), { 8, 6, 0, 0, 0 } );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );
	FILE* file = std::fopen( path.c_str(), "wb" );
	if( !file )
		return false;
	const size_t written = std::fwrite( png.data(), 1, png.size(), file );
	std::fclose( file );
	return written == png.size();
}

//---------------------------------------------------------------------------
// The context.
//---------------------------------------------------------------------------
bool g_software = false;

CGLContextObj createContext()
{
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute fallback[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute generic[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFARendererID, static_cast< CGLPixelFormatAttribute >( kCGLRendererGenericFloatID ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};

	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	const char* renderer     = std::getenv( "SLIPTEST_RENDERER" );
	if( renderer != nullptr && std::strcmp( renderer, "software" ) == 0 )
	{
		if( CGLChoosePixelFormat( generic, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
		g_software = true;
		std::fprintf( stderr, "sliptest: SLIPTEST_RENDERER=software, Apple's software renderer\n" );
	}
	else if( CGLChoosePixelFormat( accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
	{
		if( CGLChoosePixelFormat( fallback, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
	}
	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;
	CGLSetCurrentContext( context );
	return context;
}

const char* kindName( unsigned int type )
{
	switch( type )
	{
	case FF_TYPE_BOOLEAN: return "bool";
	case FF_TYPE_EVENT: return "event";
	case FF_TYPE_OPTION: return "option";
	case FF_TYPE_BUFFER: return "buffer";
	case FF_TYPE_STANDARD: return "standard";
	case FF_TYPE_TEXT: return "text";
	case FF_TYPE_INTEGER: return "integer";
	default: return "other";
	}
}

/// A control whose value is a choice, a switch, a press or a count: cues
/// STEP between keys for these, and only a standard control ramps.
bool stepsBetweenCues( unsigned int type )
{
	return type == FF_TYPE_OPTION || type == FF_TYPE_BOOLEAN || type == FF_TYPE_EVENT || type == FF_TYPE_INTEGER;
}

//---------------------------------------------------------------------------
// Pictures.
//---------------------------------------------------------------------------
double hash01( uint32_t a, uint32_t b = 0 )
{
	return Pcg( a * 2654435761u ^ Pcg( b + 0x9e3779b9u ) ) * ( 1.0 / 4294967296.0 );
}

/// The Over's card: a night city -- a deep blue sky graded to violet, a
/// skyline of lit windows, a ring, a saturated red sign, a white grid on the
/// ground plane. Rows bottom first, sRGB values.
Floats buildCard( int width, int height )
{
	Floats card( static_cast< size_t >( width ) * height * 4 );
	const double s = std::min( width, height );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 - 0.5 * width ) / s, v = ( y + 0.5 - 0.5 * height ) / s;
			double r = 0.10 + 0.25 * ( 0.5 + v ), g = 0.06 + 0.10 * ( 0.5 + v ), b = 0.30 + 0.35 * ( 0.5 + v );
			if( v < -0.12 )
			{
				//The ground: a grid in perspective.
				const double depth = 0.08 / ( -v - 0.04 );
				const double gx = u * depth * 6.0, gz = depth * 3.0;
				const bool line = std::fabs( gx - std::round( gx ) ) < 0.04 || std::fabs( gz - std::round( gz ) ) < 0.04 * depth;
				r = g = b = line ? 0.85 : 0.05;
			}
			else if( v < 0.18 )
			{
				const int block = static_cast< int >( std::floor( ( u + 2.0 ) * 11.0 ) );
				const double top = 0.02 + 0.16 * hash01( static_cast< uint32_t >( block ), 7 );
				if( v < top )
				{
					r = g = b = 0.07;
					const int wx = static_cast< int >( std::floor( ( u + 2.0 ) * 66.0 ) );
					const int wy = static_cast< int >( std::floor( ( v + 1.0 ) * 50.0 ) );
					if( ( wx % 2 ) == 0 && ( wy % 2 ) == 0 && hash01( static_cast< uint32_t >( wx * 131 + wy ), 3 ) > 0.4 )
					{
						r = 1.0;
						g = 0.85;
						b = 0.45;
					}
				}
			}
			const double rr = std::sqrt( ( u + 0.45 ) * ( u + 0.45 ) + ( v - 0.28 ) * ( v - 0.28 ) );
			if( std::fabs( rr - 0.09 ) < 0.012 )
				r = g = b = 0.97;
			if( std::fabs( u - 0.42 ) < 0.16 && std::fabs( v - 0.3 ) < 0.05 )
			{
				r = 0.95;
				g = 0.08;
				b = 0.12;
			}
			float* o = &card[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			o[ 0 ]   = static_cast< float >( std::clamp( r, 0.0, 1.0 ) );
			o[ 1 ]   = static_cast< float >( std::clamp( g, 0.0, 1.0 ) );
			o[ 2 ]   = static_cast< float >( std::clamp( b, 0.0, 1.0 ) );
			o[ 3 ]   = 1.0f;
		}
	return card;
}

GLuint makeTexture( int width, int height, const float* pixels, GLint format = GL_RGBA32F )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, GL_FLOAT, pixels );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

//---------------------------------------------------------------------------
// A rig: the real plugin, a float output framebuffer, a synthetic clock.
//---------------------------------------------------------------------------
struct Rig
{
	SlipstreamPlugin plugin;
	int width = 0, height = 0;
	GLuint sourceTexture = 0, outputTexture = 0, outputFBO = 0, readFBO = 0;
	int frame          = 0;
	double fps         = 60.0;
	double clockOffset = 0.0;
	/// The host's clock unit: 1 sends seconds, 1000 milliseconds.
	double hostUnit = 1.0;

	ProcessOpenGLStruct process    = {};
	FFGLTextureStruct inputStruct  = {};
	FFGLTextureStruct* inputs[ 1 ] = { nullptr };

	explicit Rig( bool effect = false ) : plugin( effect )
	{
	}

	~Rig()
	{
		plugin.DeInitGL();
		release();
		if( readFBO )
			glDeleteFramebuffers( 1, &readFBO );
	}

	void release()
	{
		if( outputFBO )
			glDeleteFramebuffers( 1, &outputFBO );
		if( outputTexture )
			glDeleteTextures( 1, &outputTexture );
		if( sourceTexture )
			glDeleteTextures( 1, &sourceTexture );
		outputFBO = outputTexture = sourceTexture = 0;
	}

	bool attach( int w, int h, const Floats* picture )
	{
		width         = w;
		height        = h;
		outputTexture = makeTexture( width, height, nullptr );
		glGenFramebuffers( 1, &outputFBO );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0 );
		if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
			return false;
		process.HostFBO = outputFBO;
		if( plugin.IsEffect() )
		{
			const Floats card = picture ? *picture : buildCard( width, height );
			//The host's clip is 8 bit, as Resolume's are.
			sourceTexture     = makeTexture( width, height, card.data(), GL_RGBA8 );
			inputStruct.Width = inputStruct.HardwareWidth = static_cast< FFUInt32 >( width );
			inputStruct.Height = inputStruct.HardwareHeight = static_cast< FFUInt32 >( height );
			inputStruct.Handle                              = sourceTexture;
			inputs[ 0 ]                                     = &inputStruct;
			process.numInputTextures                        = 1;
			process.inputTextures                           = inputs;
		}
		return true;
	}

	bool Init( int w, int h, const Floats* picture = nullptr )
	{
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( w );
		viewport.height             = static_cast< FFUInt32 >( h );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see ~/Library/Logs/slipstream for which shader\n" );
			return false;
		}
		plugin.SetClockScaleForTest( 1.0 );
		return attach( w, h, picture );
	}

	/// The host's raster changes under a running instance: no InitGL.
	bool Resize( int w, int h, const Floats* picture = nullptr )
	{
		release();
		return attach( w, h, picture );
	}

	void Upload( const Floats& picture )
	{
		glBindTexture( GL_TEXTURE_2D, sourceTexture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, picture.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	void Set( unsigned int id, float value )
	{
		plugin.SetById( id, value );
	}

	double TimeOf( int f ) const
	{
		return clockOffset + static_cast< double >( f ) / fps;
	}

	bool Render( int frames = 1 )
	{
		for( int i = 0; i < frames; ++i )
		{
			plugin.SetTime( TimeOf( frame ) * hostUnit );
			++frame;
			glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
			glViewport( 0, 0, width, height );
			glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
			glClear( GL_COLOR_BUFFER_BIT );
			if( plugin.ProcessOpenGL( &process ) != FF_SUCCESS )
			{
				std::fprintf( stderr, "ProcessOpenGL failed\n" );
				return false;
			}
		}
		return true;
	}

	Floats Output() const
	{
		Floats pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		return pixels;
	}

	/// One of the plugin's exposure buffers (half float), row 0 at the bottom.
	Floats Exposure( GLuint texture )
	{
		Floats pixels( static_cast< size_t >( width ) * height * 4 );
		if( !readFBO )
			glGenFramebuffers( 1, &readFBO );
		glBindFramebuffer( GL_FRAMEBUFFER, readFBO );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0 );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		return pixels;
	}
	Floats StarExposure()
	{
		return Exposure( plugin.StarBufferID() );
	}
	Floats SceneExposure()
	{
		return Exposure( plugin.SceneBufferID() );
	}
};

//---------------------------------------------------------------------------
// Parameters by name.
//---------------------------------------------------------------------------
struct NamedParameter
{
	std::string name;
	unsigned int index;
	float value;
	unsigned int type;
};

std::vector< NamedParameter > listParameters( SlipstreamPlugin& plugin )
{
	std::vector< NamedParameter > list;
	for( unsigned int i = 0; i < plugin.ParamCount(); ++i )
	{
		const char* const name = plugin.GetParamName( i );
		list.push_back( NamedParameter { name ? name : "?", i, plugin.GetFloatParameter( i ), plugin.GetParamType( i ) } );
	}
	return list;
}

/// `Name=Value`. An option takes its element's name or its index; anything
/// that is neither, or not wholly a number, is refused (polyhedral's trap:
/// strtof( "Fixed" ) is 0, silently).
bool applySetting( SlipstreamPlugin& plugin, const std::string& assignment, std::string& error )
{
	const size_t equals = assignment.find( '=' );
	if( equals == std::string::npos )
	{
		error = "expected Name=Value";
		return false;
	}
	const std::string name  = assignment.substr( 0, equals );
	const std::string value = assignment.substr( equals + 1 );
	for( const NamedParameter& parameter : listParameters( plugin ) )
		if( parameter.name == name )
		{
			if( parameter.type == FF_TYPE_OPTION )
			{
				const unsigned int count = plugin.GetNumParamElements( parameter.index );
				for( unsigned int e = 0; e < count; ++e )
					if( value == plugin.GetParamElementName( parameter.index, e ) )
					{
						plugin.SetFloatParameter( parameter.index, static_cast< float >( e ) );
						return true;
					}
			}
			char* end       = nullptr;
			const float got = std::strtof( value.c_str(), &end );
			if( value.empty() || end == nullptr || *end != '\0' )
			{
				error = "'" + value + "' is not a number" + ( parameter.type == FF_TYPE_OPTION ? " or an option of " + name : "" );
				return false;
			}
			plugin.SetFloatParameter( parameter.index, got );
			return true;
		}
	error = "no parameter called '" + name + "'";
	return false;
}

bool applyAll( SlipstreamPlugin& plugin, const std::vector< std::string >& settings )
{
	for( const std::string& setting : settings )
	{
		std::string error;
		if( !applySetting( plugin, setting, error ) )
		{
			std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
			return false;
		}
	}
	return true;
}

//---------------------------------------------------------------------------
// The cue sheet.
//---------------------------------------------------------------------------
using Track = std::vector< std::pair< int, float > >;

std::map< std::string, Track > loadScript( std::istream& in, const std::string& path, std::string& error )
{
	std::map< std::string, Track > tracks;
	std::string line;
	int lineNumber = 0;
	while( std::getline( in, line ) )
	{
		++lineNumber;
		const size_t hash = line.find( '#' );
		if( hash != std::string::npos )
			line.erase( hash );
		std::istringstream words( line );
		int frame = 0;
		if( !( words >> frame ) )
			continue;
		std::vector< std::string > parts;
		std::string word;
		while( words >> word )
			parts.push_back( word );
		if( parts.size() < 2 )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": expected `frame Parameter Name value`";
			return {};
		}
		char* end         = nullptr;
		const float value = std::strtof( parts.back().c_str(), &end );
		if( end == nullptr || *end != '\0' )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": '" + parts.back() + "' is not a number";
			return {};
		}
		parts.pop_back();
		std::string name = parts.front();
		for( size_t i = 1; i < parts.size(); ++i )
			name += " " + parts[ i ];
		tracks[ name ].emplace_back( frame, value );
	}
	for( auto& entry : tracks )
		std::stable_sort( entry.second.begin(), entry.second.end(),
		                  []( const std::pair< int, float >& a, const std::pair< int, float >& b ) { return a.first < b.first; } );
	return tracks;
}

float valueAt( const Track& track, int frame, bool ramp )
{
	if( track.empty() )
		return 0.0f;
	if( frame <= track.front().first )
		return track.front().second;
	if( frame >= track.back().first )
		return track.back().second;
	for( size_t i = 0; i + 1 < track.size(); ++i )
	{
		const auto& a = track[ i ];
		const auto& b = track[ i + 1 ];
		if( frame >= a.first && frame < b.first )
		{
			if( !ramp )
				return a.second;
			const float t = static_cast< float >( frame - a.first ) / static_cast< float >( b.first - a.first );
			return a.second + ( b.second - a.second ) * t;
		}
	}
	return track.back().second;
}

struct Cue
{
	Track track;
	bool ramp;
};

bool bindScript( SlipstreamPlugin& plugin, const std::string& path, std::map< unsigned int, Cue >& out, std::string& error )
{
	std::ifstream file( path );
	if( !file )
	{
		error = "cannot open " + path;
		return false;
	}
	const std::map< std::string, Track > tracks = loadScript( file, path, error );
	if( !error.empty() )
		return false;
	const std::vector< NamedParameter > known = listParameters( plugin );
	for( const auto& entry : tracks )
	{
		bool found = false;
		for( const NamedParameter& parameter : known )
			if( parameter.name == entry.first )
			{
				out[ parameter.index ] = Cue { entry.second, !stepsBetweenCues( parameter.type ) };
				found                  = true;
			}
		if( !found )
		{
			error = "script names '" + entry.first + "', which is not a parameter (try --list)";
			return false;
		}
	}
	return true;
}

/// `--at FRAME "Name=Value"`: a setting applied from that frame on.
struct Timed
{
	int frame;
	std::string setting;
};

//===========================================================================
// --pipe and --film. Raw RGBA, top row first, on the synthetic clock.
//===========================================================================
int runPipe( bool effect, int width, int height, double fps, const std::string& scriptPath, int count, bool readStdin,
             const std::vector< std::string >& settings, double phase )
{
	Rig rig( effect );
	rig.fps = fps;
	if( !rig.Init( width, height ) )
		return 1;
	if( !applyAll( rig.plugin, settings ) )
		return 2;
	if( phase >= 0.0 )
		rig.plugin.SetPhaseForTest( phase );
	std::map< unsigned int, Cue > automation;
	if( !scriptPath.empty() )
	{
		std::string error;
		if( !bindScript( rig.plugin, scriptPath, automation, error ) )
		{
			std::fprintf( stderr, "%s\n", error.c_str() );
			return 2;
		}
	}

	Bytes in( static_cast< size_t >( width ) * height * 4 );
	Floats picture( in.size() );
	for( int index = 0; readStdin || count <= 0 || index < count; ++index )
	{
		if( readStdin )
		{
			size_t filled = 0;
			while( filled < in.size() )
			{
				const ssize_t got = read( STDIN_FILENO, in.data() + filled, in.size() - filled );
				if( got <= 0 )
					break;
				filled += static_cast< size_t >( got );
			}
			//A partial frame is the end of the stream, never a frame.
			if( filled < in.size() )
			{
				if( filled > 0 )
					std::fprintf( stderr, "partial frame at the end (%zu of %zu bytes): dropped\n", filled, in.size() );
				break;
			}
			for( int y = 0; y < height; ++y )
				for( int x = 0; x < width * 4; ++x )
					picture[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ] = in[ static_cast< size_t >( y ) * width * 4 + x ] / 255.0f;
			if( effect )
				rig.Upload( picture );
		}
		for( const auto& cue : automation )
			rig.plugin.SetFloatParameter( cue.first, valueAt( cue.second.track, index, cue.second.ramp ) );
		if( !rig.Render( 1 ) )
			return 1;

		const Floats out = rig.Output();
		Bytes bytes( in.size() );
		for( int y = 0; y < height; ++y )
			for( int x = 0; x < width * 4; ++x )
			{
				const float v = ( !effect && x % 4 == 3 ) ? 1.0f : out[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ];
				bytes[ static_cast< size_t >( y ) * width * 4 + x ] = static_cast< unsigned char >( std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f ) );
			}
		size_t written = 0;
		while( written < bytes.size() )
		{
			const ssize_t put = write( STDOUT_FILENO, bytes.data() + written, bytes.size() - written );
			if( put <= 0 )
			{
				std::fprintf( stderr, "stdout closed at frame %d\n", index );
				return 1;
			}
			written += static_cast< size_t >( put );
		}
	}
	return 0;
}

//===========================================================================
// The checks live in checks.inc, each a function of a Perturb.
//===========================================================================
struct Raster
{
	int w, h;
};
std::vector< Raster > kRasters = { { 1280, 720 }, { 320, 180 } };

#include "checks.inc"

} // namespace

//---------------------------------------------------------------------------
int main( int argc, char** argv )
{
	std::string outPath = "/tmp/slipstream.png";
	std::vector< std::string > settings;
	std::vector< Timed > timed;
	int width = 1280, height = 720, frames = 180;
	double fps = 60.0, phase = -1.0;
	bool effect = false;
	std::string mode, scriptPath, clipPath;
	int filmFrames = -1;
	bool sizeGiven = false, framesGiven = false;

	for( int i = 1; i < argc; ++i )
	{
		const std::string argument = argv[ i ];
		const bool hasNext         = i + 1 < argc;
		if( argument == "--help" || argument == "-h" )
		{
			std::printf( "sliptest -- render Slipstream offline and measure it\n\n"
			             "  --out PATH        render and write a PNG (default /tmp/slipstream.png)\n"
			             "  --over            the Over effect, on the harness's night-city card\n"
			             "  --clip FILE       (with --over --out) a raw RGBA frame of --size to use as the clip\n"
			             "  --size WxH        render size (default 1280x720)\n"
			             "  --frames N        frames before reading back (default 180: 3 s)\n"
			             "  --fps N           the synthetic clock's rate (default 60)\n"
			             "  --set \"Name=V\"    set a parameter by its display name (an option by its element's\n"
			             "                    name). Repeatable.\n"
			             "  --at N \"Name=V\"   the same, from frame N on. Repeatable.\n"
			             "  --phase J         start the journey at J (0 normal space, 0.5 in transit)\n"
			             "  --list            every parameter and its default\n"
			             "  --pipe            raw RGBA out: the source makes --frames N (0/absent: until the\n"
			             "                    reader hangs up); the Over effect takes frames in on stdin\n"
			             "  --film N          N frames, raw RGBA on stdout (the Over on its card)\n"
			             "  --script PATH     cues for --pipe/--film: 'frame Name value'\n\n"
			             "%s"
			             "  --negative        every check against a deliberately wrong model: each must FAIL\n"
			             "  --offline         the checks and negative controls that need no GL context (CI)\n"
			             "  --bench           ms per frame at 720p, 1080p and 4K\n",
			             CheckList().c_str() );
			return 0;
		}
		else if( argument == "--out" && hasNext )
			outPath = argv[ ++i ];
		else if( argument == "--set" && hasNext )
			settings.push_back( argv[ ++i ] );
		else if( argument == "--at" && i + 2 < argc )
		{
			const int at = std::atoi( argv[ ++i ] );
			timed.push_back( { at, argv[ ++i ] } );
		}
		else if( argument == "--phase" && hasNext )
			phase = std::strtod( argv[ ++i ], nullptr );
		else if( argument == "--frames" && hasNext )
		{
			frames      = std::atoi( argv[ ++i ] );
			framesGiven = true;
		}
		else if( argument == "--fps" && hasNext )
			fps = std::strtod( argv[ ++i ], nullptr );
		else if( argument == "--over" )
			effect = true;
		else if( argument == "--clip" && hasNext )
			clipPath = argv[ ++i ];
		else if( argument == "--pipe" )
			mode = "pipe";
		else if( argument == "--film" && hasNext )
		{
			mode       = "film";
			filmFrames = std::max( 1, std::atoi( argv[ ++i ] ) );
		}
		else if( argument == "--script" && hasNext )
			scriptPath = argv[ ++i ];
		else if( argument == "--list" )
			mode = "list";
		else if( argument == "--size" && hasNext )
		{
			const std::string value = argv[ ++i ];
			const size_t cross      = value.find( 'x' );
			if( cross != std::string::npos )
			{
				width  = std::atoi( value.substr( 0, cross ).c_str() );
				height = std::atoi( value.substr( cross + 1 ).c_str() );
			}
			sizeGiven = true;
		}
		else if( argument.rfind( "--", 0 ) == 0 )
			mode = argument.substr( 2 );
		else
		{
			std::fprintf( stderr, "unknown argument '%s' (try --help)\n", argument.c_str() );
			return 2;
		}
	}
	if( width <= 0 || height <= 0 || !( fps > 0.0 ) )
	{
		std::fprintf( stderr, "--size and --fps must be positive\n" );
		return 2;
	}
	//A check given --size runs at that raster alone (the software pass).
	if( sizeGiven )
		kRasters = { { width, height } };

	if( mode == "list" )
	{
		SlipstreamPlugin plugin( effect );
		std::printf( "%-3s %-16s %-9s %s\n", "id", "name", "kind", "default" );
		for( const NamedParameter& parameter : listParameters( plugin ) )
			std::printf( "%-3u %-16s %-9s %.4f\n", parameter.index, parameter.name.c_str(), kindName( parameter.type ), parameter.value );
		return 0;
	}
	if( mode == "expect" )
		return runExpect();

	std::signal( SIGPIPE, SIG_IGN );

	if( mode == "offline" )
	{
		int failed = 0;
		for( const CheckEntry& check : checks() )
			if( check.offline )
				failed |= check.run( Perturb {} );
		failed |= runNegative( true );
		std::printf( "\n  offline: the checks that need no GL context. The picture checks were NOT run --\n"
		             "  tools/verify.sh runs them against a real driver, at 320x180 and above, and again on the software renderer.\n"
		             "\n  %s\n", failed == 0 ? "PASS" : "FAIL" );
		return failed == 0 ? 0 : 1;
	}
	for( const CheckEntry& check : checks() )
		if( mode == check.flag && check.offline )
			return check.run( Perturb {} );

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "could not create an OpenGL 4.1 core context\n" );
		return 1;
	}

	int result = 0;
	bool ran   = false;
	for( const CheckEntry& check : checks() )
		if( mode == check.flag )
		{
			result = check.run( Perturb {} );
			ran    = true;
		}

	if( ran )
		;
	else if( mode == "pipe" )
		result = runPipe( effect, width, height, fps, scriptPath, framesGiven ? frames : 0, effect, settings, phase );
	else if( mode == "film" )
		result = runPipe( effect, width, height, fps, scriptPath, filmFrames, false, settings, phase );
	else if( mode == "negative" )
		result = runNegative();
	else if( mode == "bench" )
		result = runBench();
	else if( !mode.empty() )
	{
		std::fprintf( stderr, "unknown mode --%s (try --help)\n", mode.c_str() );
		result = 2;
	}
	else
	{
		Rig rig( effect );
		rig.fps = fps;
		Floats card;
		if( effect && !clipPath.empty() )
		{
			std::ifstream file( clipPath, std::ios::binary );
			Bytes raw( static_cast< size_t >( width ) * height * 4 );
			if( !file.read( reinterpret_cast< char* >( raw.data() ), static_cast< std::streamsize >( raw.size() ) ) )
			{
				std::fprintf( stderr, "--clip %s: not %dx%d RGBA\n", clipPath.c_str(), width, height );
				return 2;
			}
			card.resize( raw.size() );
			for( int y = 0; y < height; ++y )
				for( int x = 0; x < width * 4; ++x )
					card[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ] = raw[ static_cast< size_t >( y ) * width * 4 + x ] / 255.0f;
		}
		else if( effect )
			card = buildCard( width, height );
		if( !rig.Init( width, height, effect ? &card : nullptr ) )
			result = 1;
		else if( !applyAll( rig.plugin, settings ) )
			result = 2;
		else
		{
			if( phase >= 0.0 )
				rig.plugin.SetPhaseForTest( phase );
			for( int f = 0; f < std::max( frames, 1 ) && result == 0; ++f )
			{
				for( const Timed& t : timed )
					if( t.frame == f && !applyAll( rig.plugin, { t.setting } ) )
						result = 2;
				if( result == 0 && !rig.Render( 1 ) )
					result = 1;
			}
			if( result == 0 )
			{
				if( writePng( outPath, width, height, rig.Output() ) )
					std::printf( "wrote %s -- %dx%d, %d frames at %g fps (%.2f s); J %.4f, camera %.3f D, shutter %.4f D, flash %.3f, hole %.2f\n",
					             outPath.c_str(), width, height, frames, fps, frames / fps, rig.plugin.JourneyState().Phase(),
					             rig.plugin.CameraState().Total() / kDepth, rig.plugin.LastShutterZ() / kDepth,
					             rig.plugin.LastFlash(), rig.plugin.HoleDistance() );
				else
					result = 1;
			}
		}
	}

	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	return result;
}
