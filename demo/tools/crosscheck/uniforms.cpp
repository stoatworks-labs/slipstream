// The C++ half of the browser demo's FRAME cross-check: the REAL plugin class,
// SlipstreamPlugin from Slipstream.cpp, in a headless OpenGL 4.1 context, driven
// through scenario.mjs's frames the way a host drives it (SetFloatParameter,
// SetTime, ProcessOpenGL into a framebuffer of its own), and after every frame
// every active uniform of its four shader programs read back out of GL
// (glGetUniform*), with its CPU state. frames.mjs prints the same from the
// FRAME block of demo/plugin.js; crosscheck.sh compares the two.
//
//     uniforms < scenario.txt > cpp-frames.txt
//
// Nothing here computes a uniform: what is printed is what the plugin handed
// to GL. Floats print as their bits, ints and uints as 32-bit words.

#include "Controls.h"
#include "Flight.h"
#include "Slipstream.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace slipstream;

namespace
{
std::string d( double v )
{
	uint64_t bits;
	std::memcpy( &bits, &v, sizeof bits );
	char buffer[ 24 ];
	std::snprintf( buffer, sizeof buffer, "%016" PRIx64, bits );
	return buffer;
}
std::string w32( uint32_t v )
{
	char buffer[ 12 ];
	std::snprintf( buffer, sizeof buffer, "%08" PRIx32, v );
	return buffer;
}
uint64_t hex64( const std::string& s )
{
	return std::stoull( s, nullptr, 16 );
}

CGLContextObj createContext()
{
	const CGLPixelFormatAttribute attributes[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	CGLPixelFormatObj format = nullptr;
	GLint count              = 0;
	if( CGLChoosePixelFormat( attributes, &format, &count ) != kCGLNoError || format == nullptr )
		return nullptr;
	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;
	CGLSetCurrentContext( context );
	return context;
}

/// The host's side: an output framebuffer and, for the Over, a clip texture.
struct Host
{
	std::unique_ptr< SlipstreamPlugin > plugin;
	int width = 0, height = 0;
	GLuint output = 0, fbo = 0, clip = 0;
	ProcessOpenGLStruct process    = {};
	FFGLTextureStruct input        = {};
	FFGLTextureStruct* inputs[ 1 ] = { nullptr };
	std::map< std::string, GLuint > programs;

	void release()
	{
		if( fbo )
			glDeleteFramebuffers( 1, &fbo );
		if( output )
			glDeleteTextures( 1, &output );
		if( clip )
			glDeleteTextures( 1, &clip );
		fbo = output = clip = 0;
	}

	GLuint texture( GLint format, GLenum type )
	{
		GLuint t = 0;
		glGenTextures( 1, &t );
		glBindTexture( GL_TEXTURE_2D, t );
		glTexImage2D( GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, type, nullptr );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glBindTexture( GL_TEXTURE_2D, 0 );
		return t;
	}

	bool attach( int w, int h )
	{
		release();
		width  = w;
		height = h;
		output = texture( GL_RGBA8, GL_UNSIGNED_BYTE );
		glGenFramebuffers( 1, &fbo );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0 );
		const bool complete = glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		process.HostFBO = fbo;
		if( plugin->IsEffect() )
		{
			//The host's clip is 8 bit, as Resolume's are. Its pixels decide no uniform.
			clip        = texture( GL_RGBA8, GL_UNSIGNED_BYTE );
			input.Width = input.HardwareWidth = static_cast< FFUInt32 >( width );
			input.Height = input.HardwareHeight = static_cast< FFUInt32 >( height );
			input.Handle                        = clip;
			inputs[ 0 ]                         = &input;
			process.numInputTextures            = 1;
			process.inputTextures               = inputs;
		}
		return complete;
	}

	/// The plugin's four programs, told apart by a uniform only each one has.
	void findPrograms()
	{
		programs.clear();
		const std::pair< const char*, const char* > marks[] = {
			{ "intake", "RoundTripForTest" }, { "stars", "TubeHalf" }, { "scene", "LookId" }, { "composite", "SceneE" }
		};
		for( GLuint id = 1; id < 4096; ++id )
		{
			if( !glIsProgram( id ) )
				continue;
			for( const auto& mark : marks )
				if( glGetUniformLocation( id, mark.second ) >= 0 )
					programs[ mark.first ] = id;
		}
	}

	void printUniforms( int frame )
	{
		for( const char* name : { "intake", "stars", "scene", "composite" } )
		{
			const auto found = programs.find( name );
			if( found == programs.end() )
			{
				std::printf( "U %d %s MISSING\n", frame, name );
				continue;
			}
			const GLuint program = found->second;
			GLint count          = 0;
			glGetProgramiv( program, GL_ACTIVE_UNIFORMS, &count );
			std::vector< std::string > fields;
			for( GLint i = 0; i < count; ++i )
			{
				char uniform[ 128 ];
				GLsizei length = 0;
				GLint size     = 0;
				GLenum type    = 0;
				glGetActiveUniform( program, static_cast< GLuint >( i ), sizeof uniform, &length, &size, &type, uniform );
				const GLint location = glGetUniformLocation( program, uniform );
				std::string field    = std::string( uniform ) + "=";
				int components       = 1;
				char kind            = 'f';
				switch( type )
				{
				case GL_FLOAT: break;
				case GL_FLOAT_VEC2: components = 2; break;
				case GL_FLOAT_VEC3: components = 3; break;
				case GL_FLOAT_VEC4: components = 4; break;
				case GL_INT:
				case GL_SAMPLER_2D: kind = 'i'; break;
				case GL_UNSIGNED_INT: kind = 'u'; break;
				default: kind = '?'; break;
				}
				uint32_t words[ 4 ] = { 0, 0, 0, 0 };
				if( kind == 'f' )
				{
					GLfloat v[ 4 ] = { 0, 0, 0, 0 };
					glGetUniformfv( program, location, v );
					std::memcpy( words, v, sizeof v );
				}
				else if( kind == 'i' )
				{
					GLint v[ 4 ] = { 0, 0, 0, 0 };
					glGetUniformiv( program, location, v );
					std::memcpy( words, v, sizeof v );
				}
				else if( kind == 'u' )
				{
					GLuint v[ 4 ] = { 0, 0, 0, 0 };
					glGetUniformuiv( program, location, v );
					std::memcpy( words, v, sizeof v );
				}
				else
				{
					field += "type" + std::to_string( type );
					fields.push_back( field );
					continue;
				}
				field += std::string( 1, kind );
				for( int c = 0; c < components; ++c )
					field += ( c ? "," : ":" ) + w32( words[ c ] );
				fields.push_back( field );
			}
			std::sort( fields.begin(), fields.end() );
			std::printf( "U %d %s", frame, name );
			for( const std::string& field : fields )
				std::printf( " %s", field.c_str() );
			std::printf( "\n" );
		}
	}
};
} // namespace

int main()
{
	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "uniforms: no OpenGL 4.1 core context\n" );
		return 1;
	}
	Host host;
	std::string line;
	int lineNumber = 0;
	while( std::getline( std::cin, line ) )
	{
		++lineNumber;
		std::istringstream words( line );
		std::string verb;
		words >> verb;
		if( verb == "plugin" )
		{
			std::string kind, size;
			words >> kind >> size;
			int w = 0, h = 0;
			std::sscanf( size.c_str(), "%dx%d", &w, &h );
			if( host.plugin )
			{
				host.plugin->DeInitGL();
				host.release();
			}
			host.plugin.reset( new SlipstreamPlugin( kind == "over" ) );
			FFGLViewportStruct viewport = {};
			viewport.width              = static_cast< FFUInt32 >( w );
			viewport.height             = static_cast< FFUInt32 >( h );
			if( host.plugin->InitGL( &viewport ) != FF_SUCCESS )
			{
				std::fprintf( stderr, "uniforms: InitGL failed\n" );
				return 1;
			}
			host.plugin->SetClockScaleForTest( 1.0 );
			host.findPrograms();
			if( !host.attach( w, h ) )
				return 1;
			std::printf( "P %s %dx%d\n", kind.c_str(), w, h );
		}
		else if( verb == "raster" )
		{
			std::string size;
			words >> size;
			int w = 0, h = 0;
			std::sscanf( size.c_str(), "%dx%d", &w, &h );
			if( !host.attach( w, h ) )
				return 1;
			std::printf( "R %dx%d\n", w, h );
		}
		else if( verb == "set" )
		{
			unsigned int id = 0;
			std::string bits;
			words >> id >> bits;
			const uint32_t word = static_cast< uint32_t >( hex64( bits ) );
			float value;
			std::memcpy( &value, &word, sizeof value );
			host.plugin->SetById( id, value );
		}
		else if( verb == "frame" )
		{
			int frame = 0;
			std::string bits;
			words >> frame >> bits;
			const uint64_t word = hex64( bits );
			double time;
			std::memcpy( &time, &word, sizeof time );
			host.plugin->SetTime( time );
			glBindFramebuffer( GL_FRAMEBUFFER, host.fbo );
			glViewport( 0, 0, host.width, host.height );
			if( host.plugin->ProcessOpenGL( &host.process ) != FF_SUCCESS )
			{
				std::fprintf( stderr, "uniforms: ProcessOpenGL failed at frame %d\n", frame );
				return 1;
			}
			const SlipstreamPlugin& p = *host.plugin;
			std::printf( "C %d dt=%s J=%s start=%s end=%s total=%s pz=%s lz=%s speed=%s shutter=%s focal=%s flash=%s hole=%s disc=%s corona=%s\n",
			             frame, d( p.LastDt() ).c_str(), d( p.JourneyState().Phase() ).c_str(), d( p.JourneyState().Start() ).c_str(),
			             d( p.JourneyState().End() ).c_str(), d( p.CameraState().Total() ).c_str(), d( p.CameraState().PhaseZ() ).c_str(),
			             d( p.CameraState().LongZ() ).c_str(), d( p.CameraState().Speed() ).c_str(), d( p.LastShutterZ() ).c_str(),
			             d( p.LastFocal() ).c_str(), d( p.LastFlash() ).c_str(), d( p.HoleDistance() ).c_str(),
			             d( p.LastDiscLight() ).c_str(), d( p.LastCoronaLight() ).c_str() );
			host.printUniforms( frame );
		}
		else if( !verb.empty() )
		{
			std::fprintf( stderr, "uniforms: line %d: unknown '%s'\n", lineNumber, verb.c_str() );
			return 2;
		}
	}
	if( host.plugin )
		host.plugin->DeInitGL();
	host.release();
	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	return 0;
}
