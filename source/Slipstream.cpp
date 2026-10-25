#include "Slipstream.h"

#include "Blackbody.h"
#include "Diag.h"
#include "GLState.h"
#include "Geodesic.h"
#include "Shaders.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

using namespace ffglex;

namespace slipstream
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

constexpr int kClockVotes = 4;
/// Host seconds. A bigger forward step, or any backward one, is a jump -- a
/// clip trigger or a scrub -- and no time passes across it.
constexpr double kMaxFrameDelta = 1.0;

/// Light levels, at Exposure 0 EV. A star's whole light is its luminosity
/// times its flux, so these are the only free scales in the picture.
constexpr double kStarGain    = 1.0;
constexpr double kSkyDepth2   = 16.0;///< the sky's stars read as if at the far end of the box
constexpr double kTunnelGain  = 0.5;
constexpr double kCloudGain   = 1.0;
constexpr double kDiscGain    = 1.4;
constexpr double kCoronaGain  = 0.06;
/// The disc's clock: GM/c^3 per second, so the inner edge (r = 6, one turn
/// in 2 pi 6^1.5 = 92 M) goes round once in two seconds.
constexpr double kDiscRate   = 46.0;
constexpr double kDiscPeriod = 400.0;
/// The sky's grid: cells per cube face's edge.
constexpr double kSkyCells = 600.0;
/// The sky's mean luminosity where the hole smears it past resolving: the
/// Pareto tail starLuminosity() draws from (3 u^-1.5) without its few
/// blinding stars (above 300), whose own smears would stand out of a haze.
constexpr double kMeanLuminosity = 36.0;

void setUint( FFGLShader& shader, const char* name, uint32_t value )
{
	glUniform1ui( glGetUniformLocation( shader.GetGLID(), name ), value );
}

/// A hue wheel from clamped triangles, lifted towards white and scaled to
/// unit luminance: Tint's colour.
void tintOf( double hue, float out[ 3 ] )
{
	const double offsets[ 3 ] = { 0.0, 2.0 / 3.0, 1.0 / 3.0 };
	double rgb[ 3 ];
	for( int i = 0; i < 3; ++i )
	{
		double f = hue + offsets[ i ];
		f -= std::floor( f );
		rgb[ i ] = 0.3 + 0.7 * std::clamp( std::fabs( f * 6.0 - 3.0 ) - 1.0, 0.0, 1.0 );
	}
	const double lum = 0.2126 * rgb[ 0 ] + 0.7152 * rgb[ 1 ] + 0.0722 * rgb[ 2 ];
	for( int i = 0; i < 3; ++i )
		out[ i ] = static_cast< float >( rgb[ i ] / lum );
}

/// The solid angle of a rectangle seen from its centre's normal, full
/// angles a and b: 4 asin( sin( a/2 ) sin( b/2 ) ).
double rectangleSolidAngle( double a, double b )
{
	return 4.0 * std::asin( std::sin( 0.5 * a ) * std::sin( 0.5 * b ) );
}

int flashKind( Look look )
{
	return static_cast< int >( look );
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount,
               "the About run no longer matches StoatworksAbout.h" );

//---------------------------------------------------------------------------
SlipstreamPlugin::SlipstreamPlugin( bool effect ) :
	isEffect( effect ),
	hostOrder( HostOrder( effect ) )
{
	SetMinInputs( isEffect ? 1 : 0 );
	SetMaxInputs( isEffect ? 1 : 0 );
	SetTimeSupported( true );

	std::fill( std::begin( idToHost ), std::end( idToHost ), -1 );
	for( size_t i = 0; i < hostOrder.size(); ++i )
		idToHost[ hostOrder[ i ] ] = static_cast< int >( i );

	//-------------------------------------------------------------------
	// Defaults: Hyperspace, round trips for ever, so dropping it on a layer
	// shows what it does inside ten seconds.
	//-------------------------------------------------------------------
	params[ PT_LOOK ]          = static_cast< float >( Look::Hyperspace );
	params[ PT_DRIVE ]         = static_cast< float >( Drive::Loop );
	params[ PT_ENGAGE ]        = 0.0f;
	params[ PT_JOURNEY ]       = 0.0f;
	params[ PT_ENTRY_TIME ]    = ParamFromSeconds( 2.0 );
	params[ PT_TRANSIT_TIME ]  = ParamFromSeconds( 3.0 );
	params[ PT_EXIT_TIME ]     = ParamFromSeconds( 1.5 );
	params[ PT_CRUISE_TIME ]   = ParamFromSeconds( 3.0 );
	params[ PT_CRUISE_SPEED ]  = ParamFromCruise( 0.08 );
	params[ PT_TOP_SPEED ]     = ParamFromTop( 1.0 );
	params[ PT_HEADING_X ]     = 0.5f;
	params[ PT_HEADING_Y ]     = 0.5f;
	params[ PT_FIELD_OF_VIEW ] = ParamFromFieldOfView( 60.0 );
	params[ PT_SHUTTER ]       = 0.5f;
	params[ PT_STARS ]         = 4000.0f;
	params[ PT_STAR_COLOUR ]   = 0.6f;
	params[ PT_EXPOSURE ]      = ParamFromExposure( 0.0 );
	params[ PT_FLASH ]         = 0.7f;
	params[ PT_TINT ]          = 0.6f;
	params[ PT_TUNNEL ]        = 0.6f;
	params[ PT_STRETCH ]       = 0.75f;
	params[ PT_DISPERSION ]    = 0.4f;
	params[ PT_SWIRL ]         = 0.5f;
	params[ PT_CLOUDS ]        = 0.75f;
	params[ PT_HOLE_SIZE ]     = 0.3f;
	params[ PT_DISC ]          = 0.8f;
	params[ PT_DISC_TILT ]     = 0.93f;
	params[ PT_DISC_SIZE ]     = 0.4f;
	params[ PT_DISC_HEAT ]     = ParamFromDiscHeat( 4500.0 );
	params[ PT_DOPPLER ]       = 1.0f;
	params[ PT_CORONA ]        = 0.35f;
	params[ PT_CLIP_DEPTH ]    = ParamFromClipDepth( 0.3 );
	params[ PT_CLIP_THROUGH ]  = 0.25f;
	params[ PT_MIX ]           = 1.0f;

	for( unsigned int host = 0; host < hostOrder.size(); ++host )
	{
		const unsigned int id = hostOrder[ host ];
		const char* name      = NameOf( id );
		switch( id )
		{
		case PT_LOOK:
		{
			const int count = static_cast< int >( Look::Count );
			SetOptionParamInfo( host, name, static_cast< unsigned int >( count ), params[ id ] );
			for( int i = 0; i < count; ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ), LookName( static_cast< Look >( i ) ),
				                     static_cast< float >( i ) );
			break;
		}
		case PT_DRIVE:
		{
			const int count = static_cast< int >( Drive::Count );
			SetOptionParamInfo( host, name, static_cast< unsigned int >( count ), params[ id ] );
			for( int i = 0; i < count; ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ), DriveName( static_cast< Drive >( i ) ),
				                     static_cast< float >( i ) );
			break;
		}
		case PT_STARS:
			//Only FF_TYPE_STANDARD has its default clamped into 0..1, so an
			//integer is declared with its real default and range.
			SetParamInfo( host, name, FF_TYPE_INTEGER, params[ id ] );
			SetParamRange( host, 0.0f, static_cast< float >( kMaxStars ) );
			break;
		case PT_JUMP:
			SetParamInfo( host, name, FF_TYPE_EVENT, false );
			break;
		case PT_ENGAGE:
			SetParamInfo( host, name, FF_TYPE_BOOLEAN, params[ id ] > 0.5f );
			break;
		case PT_ABOUT_TEXT:
			SetParamInfo( host, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
			break;
		default:
			if( id > PT_ABOUT_TEXT )
				SetParamInfo( host, stoatworks::about::buttons()[ id - PT_ABOUT_TEXT - 1 ].label, FF_TYPE_EVENT, false );
			else
				SetParamInfo( host, name, FF_TYPE_STANDARD, params[ id ] );
			break;
		}
		SetParamGroup( host, GroupOf( id ) );
	}
}

SlipstreamPlugin::~SlipstreamPlugin() = default;

//---------------------------------------------------------------------------
FFResult SlipstreamPlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer="
	            + glStringOrUnknown( GL_RENDERER ) + " version=" + glStringOrUnknown( GL_VERSION ) );

	using namespace shaders;
	const std::string quadVertex = std::string( kVersion ) + kQuadVertex;
	struct Stage
	{
		FFGLShader* shader;
		std::string vertex, fragment;
		const char* name;
	};
	const Stage stages[] = {
		{ &intakeShader, quadVertex, Assemble( kIntakeFragment ), "intake" },
		{ &starShader, Assemble( kStarVertex ), Assemble( kStarFragment ), "stars" },
		{ &sceneShader, quadVertex, Assemble( kSceneFragment ), "scene" },
		{ &compositeShader, quadVertex, Assemble( kCompositeFragment ), "composite" },
	};
	for( const Stage& stage : stages )
	{
		if( stage.shader->Compile( stage.vertex.c_str(), stage.fragment.c_str() ) )
			continue;
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Slipstream: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}
	//The stars draw from gl_InstanceID and gl_VertexID alone, but a core
	//profile still wants a vertex array bound to draw at all.
	glGenVertexArrays( 1, &starArray );

	//A sampler bound to texture 0 is "unloadable" to Apple's GL (boreal's
	//trap): the source has no clip, so its clip samplers get this.
	glGenTextures( 1, &blankClip );
	glBindTexture( GL_TEXTURE_2D, blankClip );
	const float black[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, black );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glBindTexture( GL_TEXTURE_2D, 0 );

	uploadTables();
	diag::info( isEffect ? "initialised (Over)" : "initialised (source)" );
	return CFFGLPlugin::InitGL( vp );
}

void SlipstreamPlugin::uploadTables()
{
	auto makeTable = []( GLuint& texture, int width, int height, const float* data, GLint filter ) {
		glGenTextures( 1, &texture );
		glBindTexture( GL_TEXTURE_2D, texture );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, data );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		glBindTexture( GL_TEXTURE_2D, 0 );
	};
	{
		const auto& table = blackbody::Table();
		std::vector< float > texels( blackbody::kEntries * 4 );
		for( int i = 0; i < blackbody::kEntries; ++i )
		{
			for( int c = 0; c < 3; ++c )
				texels[ i * 4 + c ] = table[ i ].rgb[ c ];
			texels[ i * 4 + 3 ] = table[ i ].log2Y;
		}
		makeTable( blackbodyTexture, blackbody::kEntries, 1, texels.data(), GL_LINEAR );
	}
	const geodesic::Tables& orbits = geodesic::Get();
	makeTable( orbitTexture, geodesic::kT, geodesic::kB, orbits.orbits.data(), GL_NEAREST );
	makeTable( endTexture, 1, geodesic::kB, orbits.ends.data(), GL_NEAREST );
}

bool SlipstreamPlugin::ensureClipBuffer( int width, int height )
{
	if( clipTexture != 0 && clipWidth == width && clipHeight == height )
		return true;
	if( clipFramebuffer )
		glDeleteFramebuffers( 1, &clipFramebuffer );
	if( clipTexture )
		glDeleteTextures( 1, &clipTexture );
	clipFramebuffer = clipTexture = 0;
	clipWidth  = width;
	clipHeight = height;
	clipLevels = 1;
	for( int size = std::max( width, height ); size > 1; size /= 2 )
		++clipLevels;

	GLint previousTexture = 0, previousFramebuffer = 0;
	glGetIntegerv( GL_TEXTURE_BINDING_2D, &previousTexture );
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &previousFramebuffer );
	glGenTextures( 1, &clipTexture );
	glBindTexture( GL_TEXTURE_2D, clipTexture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr );
	glGenerateMipmap( GL_TEXTURE_2D );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glGenFramebuffers( 1, &clipFramebuffer );
	glBindFramebuffer( GL_FRAMEBUFFER, clipFramebuffer );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, clipTexture, 0 );
	const bool complete = glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
	glBindFramebuffer( GL_FRAMEBUFFER, static_cast< GLuint >( previousFramebuffer ) );
	glBindTexture( GL_TEXTURE_2D, static_cast< GLuint >( previousTexture ) );
	return complete;
}

//---------------------------------------------------------------------------
void SlipstreamPlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending seconds and milliseconds through SetTime:
	//vote on the unit against the wall clock (rosette's code, via langton).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;
			}
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;
	now          = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
}

//---------------------------------------------------------------------------
FFResult SlipstreamPlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = nullptr;
	if( isEffect )
	{
		if( pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
			return FF_FAIL;
		input = pgl->inputTextures[ 0 ];
	}

	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	const int width           = input ? static_cast< int >( input->Width ) : hostViewport[ 2 ];
	const int height          = input ? static_cast< int >( input->Height ) : hostViewport[ 3 ];
	if( width <= 0 || height <= 0 )
		return FF_FAIL;
	glDisable( GL_BLEND );

	//-------------------------------------------------------------------
	// Time: the host's, as real elapsed seconds, frame to frame, in double.
	//-------------------------------------------------------------------
	UpdateClock();
	if( settledJump )
	{
		lastNow     = -1.0;
		settledJump = false;
	}
	double dt = 0.0;
	if( lastNow >= 0.0 )
	{
		const double stepTime = floatClockForTest
		                            ? static_cast< double >( static_cast< float >( now ) - static_cast< float >( lastNow ) )
		                            : now - lastNow;
		if( stepTime >= 0.0 && stepTime <= kMaxFrameDelta )
			dt = stepTime;
	}
	lastNow = now;
	frameDt = dt;

	//-------------------------------------------------------------------
	// The journey and the camera.
	//-------------------------------------------------------------------
	const Look look = static_cast< Look >( OptionIndex( params[ PT_LOOK ], static_cast< int >( Look::Count ) ) );
	DriveInputs drive;
	drive.drive   = static_cast< Drive >( OptionIndex( params[ PT_DRIVE ], static_cast< int >( Drive::Count ) ) );
	drive.engage  = params[ PT_ENGAGE ] > 0.5f;
	const bool jumpDown = params[ PT_JUMP ] > 0.5f;
	drive.jumpPress = jumpDown && !jumpHeld;
	jumpHeld        = jumpDown;
	drive.fader     = std::clamp( static_cast< double >( params[ PT_JOURNEY ] ), 0.0, 1.0 );
	drive.entry     = SecondsFromParam( params[ PT_ENTRY_TIME ] );
	drive.transit   = SecondsFromParam( params[ PT_TRANSIT_TIME ] );
	drive.exit      = SecondsFromParam( params[ PT_EXIT_TIME ] );
	drive.cruise    = SecondsFromParam( params[ PT_CRUISE_TIME ] );
	journey.Advance( dt, drive );
	const double j0 = journey.Start(), j1 = journey.End();

	const double fov        = FieldOfViewFromParam( params[ PT_FIELD_OF_VIEW ] ) * kPi / 180.0;
	focal                   = 0.5 * height / std::tan( 0.5 * fov );
	ScriptSettings settings;
	settings.stretch  = StretchFromParam( params[ PT_STRETCH ] );
	settings.holeHold = DistanceForShadow( HoleSizeFromParam( params[ PT_HOLE_SIZE ] ) * 0.5 * fov );
	if( cameraForTest >= 0.0 )
	{
		camera.SetForTest( cameraForTest );
		cameraForTest = -1.0;
	}
	camera.Advance( look, j0, j1, dt, CruiseFromParam( params[ PT_CRUISE_SPEED ] ), TopFromParam( params[ PT_TOP_SPEED ] ),
	                settings );

	const double shutter = ShutterFromParam( params[ PT_SHUTTER ] );
	shutterZ             = std::max( 0.0, -camera.OffsetAt( 1.0 - shutter ) );
	const Pose open      = PoseAt( look, j0 + ( j1 - j0 ) * ( 1.0 - shutter ), settings );
	const Pose close     = PoseAt( look, j1, settings );
	holeD                = look == Look::BlackHole ? close.holeD : 0.0;

	//-------------------------------------------------------------------
	// The flash: a crossing of one of the look's flash points this frame,
	// then an exponential decay, averaged over the open shutter in closed
	// form (a flash that starts mid-shutter is a part of it).
	//-------------------------------------------------------------------
	{
		bool crossed       = false;
		double sinceCross  = 0.0;
		for( double f : FlashPoints( look ) )
		{
			double at = -1.0;
			if( j1 > j0 )
			{
				const double m = std::floor( j1 - f );
				if( f + m > j0 )
					at = ( f + m - j0 ) / ( j1 - j0 );
			}
			else if( j1 < j0 )
			{
				const double m = std::ceil( j1 - f );
				if( f + m < j0 )
					at = ( j0 - ( f + m ) ) / ( j0 - j1 );
			}
			if( at >= 0.0 )
			{
				crossed    = true;
				sinceCross = ( 1.0 - at ) * dt;
			}
		}
		flashAge = crossed ? sinceCross : flashAge + dt;
		if( look != static_cast< Look >( lastLook ) )
			flashAge = crossed ? flashAge : 1.0e9;
		lastLook = static_cast< int >( look );
		const double tau  = FlashDecay( look );
		const double open = shutter * ( dt > 0.0 ? dt : 1.0 / 60.0 );
		const double hi   = flashAge, lo = std::max( flashAge - open, 0.0 );
		double average    = 0.0;
		if( hi >= 0.0 && hi < 1.0e8 )
			average = open > 1.0e-9 ? tau * ( std::exp( -lo / tau ) - std::exp( -hi / tau ) ) / open : std::exp( -hi / tau );
		flashLight = params[ PT_FLASH ] * average;
	}

	//The whirlpool's spin and the disc's clock: phases kept small in double.
	spinClock = std::fmod( spinClock + dt * ( 0.4 + 1.6 * close.speed ) * close.twist, 18.0 * kPi );
	discClock = std::fmod( discClock + dt * kDiscRate, kDiscPeriod );

	//-------------------------------------------------------------------
	// Everything allocated before anything is bound.
	//-------------------------------------------------------------------
	bool ok = starBuffer.Ensure( width, height, GL_RGBA16F ) && sceneBuffer.Ensure( width, height, GL_RGBA16F );
	if( isEffect )
		ok = ok && ensureClipBuffer( width, height );
	if( !ok )
	{
		diag::error( "could not allocate the buffers" );
		return FF_FAIL;
	}

	const double exposure = ExposureFromParam( params[ PT_EXPOSURE ] );
	const float vanish[ 2 ] = { static_cast< float >( 0.5 * width * ( 1.0 + HeadingFromParam( params[ PT_HEADING_X ] ) ) ),
		                        static_cast< float >( 0.5 * height * ( 1.0 + HeadingFromParam( params[ PT_HEADING_Y ] ) ) ) };
	const float sigma   = static_cast< float >( std::max( 0.6, 0.8 * height / 1080.0 ) );
	const float stretch = static_cast< float >( settings.stretch );
	const float pinch   = static_cast< float >( 0.3 * height ), swirl = static_cast< float >( 0.25 * height );
	const double swirlTurns = SwirlTurnsFromParam( params[ PT_SWIRL ] ) * 2.0 * kPi;
	const float twist0 = static_cast< float >( open.twist * swirlTurns ), twist1 = static_cast< float >( close.twist * swirlTurns );
	float tint[ 3 ];
	tintOf( params[ PT_TINT ], tint );

	//-------------------------------------------------------------------
	// Intake (Over): the clip into exposure, and its mipmaps.
	//-------------------------------------------------------------------
	if( isEffect )
	{
		glBindFramebuffer( GL_FRAMEBUFFER, clipFramebuffer );
		glViewport( 0, 0, width, height );
		{
			ScopedShaderBinding shader( intakeShader.GetGLID() );
			bindUnit( 0, input->Handle );
			intakeShader.Set( "ClipIn", 0 );
			intakeShader.Set( "RoundTripForTest", roundTrip ? 1.0f : 0.0f );
			quad.Draw();
			unbindTextureUnits( 1 );
		}
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		glBindTexture( GL_TEXTURE_2D, clipTexture );
		glGenerateMipmap( GL_TEXTURE_2D );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	//-------------------------------------------------------------------
	// Stars: the streaks, additive, into their own exposure.
	//-------------------------------------------------------------------
	starBuffer.BindForDrawing();
	{
		const GLfloat zero[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
		glClearBufferfv( GL_COLOR, 0, zero );
	}
	const bool testStarOn = testStar[ 3 ] > 0.0f;
	const int starCount   = testStarOn ? 1 : std::clamp( static_cast< int >( std::lround( params[ PT_STARS ] ) ), 0, kMaxStars );
	if( look != Look::BlackHole && starCount > 0 )
	{
		ScopedShaderBinding shader( starShader.GetGLID() );
		bindUnit( 0, blackbodyTexture );
		starShader.Set( "Blackbody", 0 );
		setUint( starShader, "StarSeed", 0x51f15e5du );
		//The farthest corner of the frame from the vanishing point, carried
		//out to the far end of the box.
		const double reach = std::hypot( std::max( vanish[ 0 ], width - vanish[ 0 ] ), std::max( vanish[ 1 ], height - vanish[ 1 ] ) );
		starShader.Set( "TubeHalf", static_cast< float >( reach * ( kNear + kDepth ) / focal ) );
		starShader.Set( "PhaseZ", static_cast< float >( camera.PhaseZ() ) );
		starShader.Set( "ShutterZ", static_cast< float >( shutterZ ) );
		starShader.Set( "Focal", static_cast< float >( focal ) );
		starShader.Set( "Vanish", vanish[ 0 ], vanish[ 1 ] );
		starShader.Set( "Raster", static_cast< float >( width ), static_cast< float >( height ) );
		starShader.Set( "Sigma", sigma );
		starShader.Set( "StarGain", static_cast< float >( exposure * kStarGain ) );
		starShader.Set( "StarColour", testStarOn ? 0.0f : std::clamp( params[ PT_STAR_COLOUR ], 0.0f, 1.0f ) );
		starShader.Set( "TestStar", testStar[ 0 ], testStar[ 1 ], testStar[ 2 ], testStar[ 3 ] );
		starShader.Set( "Pull0", static_cast< float >( open.pull ) );
		starShader.Set( "Pull1", static_cast< float >( close.pull ) );
		starShader.Set( "Twist0", look == Look::Wormhole ? twist0 : 0.0f );
		starShader.Set( "Twist1", look == Look::Wormhole ? twist1 : 0.0f );
		starShader.Set( "Stretch", stretch );
		starShader.Set( "PinchRadius", pinch );
		starShader.Set( "SwirlRadius", swirl );
		starShader.Set( "Dispersion", look == Look::Warp ? std::clamp( params[ PT_DISPERSION ], 0.0f, 1.0f ) : 0.0f );
		starShader.Set( "CopyLaw", copyLaw ? 1.0f : 0.0f );
		starShader.Set( "FluxLaw", fluxLaw ? 1.0f : 0.0f );
		starShader.Set( "LengthLaw", lengthLaw ? 1.0f : 0.0f );
		setAdditiveBlend();
		glBindVertexArray( starArray );
		glDrawArraysInstanced( GL_TRIANGLE_STRIP, 0, 4, starCount );
		glBindVertexArray( 0 );
		glDisable( GL_BLEND );
		unbindTextureUnits( 1 );
	}

	//-------------------------------------------------------------------
	// The scene.
	//-------------------------------------------------------------------
	sceneBuffer.BindForDrawing();
	{
		ScopedShaderBinding shader( sceneShader.GetGLID() );
		bindUnit( 0, starBuffer.TextureID() );
		bindUnit( 1, isEffect ? clipTexture : blankClip );
		bindUnit( 2, orbitTexture );
		bindUnit( 3, endTexture );
		bindUnit( 4, blackbodyTexture );
		sceneShader.Set( "Stars", 0 );
		sceneShader.Set( "ClipE", 1 );
		sceneShader.Set( "Orbits", 2 );
		sceneShader.Set( "Ends", 3 );
		sceneShader.Set( "Blackbody", 4 );
		sceneShader.Set( "LookId", static_cast< int >( look ) );
		sceneShader.Set( "IsEffect", isEffect ? 1 : 0 );
		sceneShader.Set( "Raster", static_cast< float >( width ), static_cast< float >( height ) );
		sceneShader.Set( "ViewOrigin", 0.0f, 0.0f );
		sceneShader.Set( "Vanish", vanish[ 0 ], vanish[ 1 ] );
		sceneShader.Set( "Focal", static_cast< float >( focal ) );
		sceneShader.Set( "ClipLevels", static_cast< float >( clipLevels - 1 ) );
		const float through = isEffect ? std::clamp( params[ PT_CLIP_THROUGH ], 0.0f, 1.0f ) : 0.0f;
		sceneShader.Set( "ClipThrough", through );
		sceneShader.Set( "LightGain", static_cast< float >( exposure ) );
		sceneShader.Set( "TintColour", tint[ 0 ], tint[ 1 ], tint[ 2 ] );

		//Hyperspace: the clip's smear, k = 1 + dZ / Clip Depth, and its share
		//fading to Clip Through as the tunnel comes up.
		const double clipDepth = ClipDepthFromParam( params[ PT_CLIP_DEPTH ] );
		sceneShader.Set( "SmearK", static_cast< float >( 1.0 + shutterZ / ( clipDepth * kDepth ) ) );
		float clipShare = 1.0f;
		if( look == Look::Hyperspace )
			clipShare = static_cast< float >( 1.0 + ( through - 1.0 ) * close.tunnel );
		else if( look == Look::Warp )
			clipShare = static_cast< float >( through * ( 1.0 - std::clamp( close.clipScale, 0.0, 1.0 ) ) );
		sceneShader.Set( "ClipShare", clipShare );
		sceneShader.Set( "TunnelLight",
		                 static_cast< float >( std::clamp( params[ PT_TUNNEL ], 0.0f, 1.0f ) * close.tunnel * exposure * kTunnelGain ) );
		sceneShader.Set( "LongZ", static_cast< float >( camera.LongZ() ) );
		sceneShader.Set( "ShutterZ", static_cast< float >( shutterZ ) );

		//Warp
		sceneShader.Set( "Stretch", stretch );
		sceneShader.Set( "PinchRadius", pinch );
		sceneShader.Set( "SwirlRadius", swirl );
		sceneShader.Set( "Pull0", static_cast< float >( open.pull ) );
		sceneShader.Set( "Pull1", static_cast< float >( close.pull ) );
		sceneShader.Set( "Scale0", static_cast< float >( open.clipScale ) );
		sceneShader.Set( "Scale1", static_cast< float >( close.clipScale ) );

		//Wormhole
		sceneShader.Set( "Twist0", twist0 );
		sceneShader.Set( "Twist1", twist1 );
		sceneShader.Set( "MouthRadius", static_cast< float >( close.mouth * height ) );
		sceneShader.Set( "EndLight", static_cast< float >( close.endLight * height ) );
		sceneShader.Set( "CloudLight", static_cast< float >( exposure * kCloudGain ) );
		sceneShader.Set( "CloudDensity", std::clamp( params[ PT_CLOUDS ], 0.0f, 1.0f ) );
		sceneShader.Set( "Spin", static_cast< float >( spinClock ) );

		//Black Hole
		const double tilt = DiscTiltFromParam( params[ PT_DISC_TILT ] ) * kPi / 180.0;
		sceneShader.Set( "HoleD", static_cast< float >( holeD ) );
		sceneShader.Set( "DiscNormal", 0.0f, static_cast< float >( std::sin( tilt ) ), static_cast< float >( -std::cos( tilt ) ) );
		sceneShader.Set( "DiscInner", 6.0f );
		sceneShader.Set( "DiscOuter", static_cast< float >( DiscOuterFromParam( params[ PT_DISC_SIZE ] ) ) );
		sceneShader.Set( "DiscKelvin", static_cast< float >( DiscHeatFromParam( params[ PT_DISC_HEAT ] ) ) );
		discLight   = std::clamp( params[ PT_DISC ], 0.0f, 1.0f ) * close.disc * exposure * kDiscGain;
		coronaLight = std::clamp( params[ PT_CORONA ], 0.0f, 1.0f ) * close.disc * exposure * kCoronaGain;
		sceneShader.Set( "DiscLight", static_cast< float >( discLight ) );
		sceneShader.Set( "DopplerAmount", std::clamp( params[ PT_DOPPLER ], 0.0f, 1.0f ) );
		sceneShader.Set( "CoronaLight", static_cast< float >( coronaLight ) );
		const double timeA = discClock, timeB = std::fmod( discClock + 0.5 * kDiscPeriod, kDiscPeriod );
		sceneShader.Set( "DiscTimeA", static_cast< float >( timeA ) );
		sceneShader.Set( "DiscTimeB", static_cast< float >( timeB ) );
		//A's weight peaks halfway through its period and is zero where it
		//restarts; B's restart falls where A's weight is whole.
		sceneShader.Set( "DiscBlend", static_cast< float >( std::fabs( 2.0 * timeA / kDiscPeriod - 1.0 ) ) );
		const double aspect     = static_cast< double >( width ) / height;
		const double refFov     = 60.0 * kPi / 180.0;
		const double frameSolid = rectangleSolidAngle( 2.0 * std::atan( aspect * std::tan( 0.5 * refFov ) ), refFov );
		const double cellSolid  = ( 2.0 / kSkyCells ) * ( 2.0 / kSkyCells );
		//The sky shows a third as many stars as the box does at rest: the
		//box's are spread through its depth and most are faint.
		const double probability = std::clamp( starCount / 3.0 * cellSolid / frameSolid, 0.0, 1.0 );
		sceneShader.Set( "SkyCells", static_cast< float >( kSkyCells ) );
		sceneShader.Set( "SkyProbability", static_cast< float >( probability ) );
		sceneShader.Set( "SkyGain", static_cast< float >( exposure * kStarGain / ( kSkyDepth2 * focal * focal ) ) );
		sceneShader.Set( "SkyMean", static_cast< float >( probability * kMeanLuminosity / cellSolid ) );
		sceneShader.Set( "SkySigma", static_cast< float >( sigma / focal ) );
		sceneShader.Set( "StarColour", std::clamp( params[ PT_STAR_COLOUR ], 0.0f, 1.0f ) );
		setUint( sceneShader, "StarSeed", 0x51f15e5du );
		sceneShader.Set( "BendLaw", bendLaw ? 1.0f : 0.0f );
		sceneShader.Set( "CaptureLaw", captureLaw ? 1.0f : 0.0f );
		sceneShader.Set( "SpinSign", spinSign );
		sceneShader.Set( "DiscTexture", discTexture ? 1.0f : 0.0f );
		sceneShader.Set( "SeamForTest", seamForTest ? 1.0f : 0.0f );
		quad.Draw();
		unbindTextureUnits( 5 );
	}

	//-------------------------------------------------------------------
	// Composite: develop, into the host's framebuffer.
	//-------------------------------------------------------------------
	glBindFramebuffer( GL_FRAMEBUFFER, pgl->HostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	{
		ScopedShaderBinding shader( compositeShader.GetGLID() );
		bindUnit( 0, sceneBuffer.TextureID() );
		bindUnit( 1, input ? input->Handle : blankClip );
		compositeShader.Set( "SceneE", 0 );
		compositeShader.Set( "ClipIn", 1 );
		compositeShader.Set( "IsEffect", isEffect ? 1 : 0 );
		compositeShader.Set( "MixAmount", isEffect ? std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) : 1.0f );
		compositeShader.Set( "Blackout", static_cast< float >( close.blackout ) );
		compositeShader.Set( "FlashLight", static_cast< float >( flashLight ) );
		compositeShader.Set( "FlashKind", flashKind( look ) );
		compositeShader.Set( "FlashAge", static_cast< float >( std::min( std::max( flashAge - 0.5 * shutter * dt, 0.0 ), 1.0e4 ) ) );
		compositeShader.Set( "Vanish", vanish[ 0 ], vanish[ 1 ] );
		compositeShader.Set( "Raster", static_cast< float >( width ), static_cast< float >( height ) );
		compositeShader.Set( "ViewOrigin", static_cast< float >( hostViewport[ 0 ] ), static_cast< float >( hostViewport[ 1 ] ) );
		compositeShader.Set( "TintColour", tint[ 0 ], tint[ 1 ], tint[ 2 ] );
		quad.Draw();
		unbindTextureUnits( 2 );
	}
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult SlipstreamPlugin::DeInitGL()
{
	for( FFGLShader* shader : { &intakeShader, &starShader, &sceneShader, &compositeShader } )
		shader->FreeGLResources();
	quad.Release();
	starBuffer.Destroy();
	sceneBuffer.Destroy();
	if( starArray )
		glDeleteVertexArrays( 1, &starArray );
	if( clipFramebuffer )
		glDeleteFramebuffers( 1, &clipFramebuffer );
	for( GLuint* texture : { &clipTexture, &blackbodyTexture, &orbitTexture, &endTexture, &blankClip } )
		if( *texture )
		{
			glDeleteTextures( 1, texture );
			*texture = 0;
		}
	starArray = clipFramebuffer = 0;
	clipWidth = clipHeight = 0;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult SlipstreamPlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

char* SlipstreamPlugin::GetTextParameter( unsigned int index )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult SlipstreamPlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult SlipstreamPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= hostOrder.size() )
		return FF_FAIL;
	const unsigned int id = hostOrder[ index ];
	if( id >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( id - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;
	params[ id ] = value;
	return FF_SUCCESS;
}

float SlipstreamPlugin::GetFloatParameter( unsigned int index )
{
	return index < hostOrder.size() ? params[ hostOrder[ index ] ] : 0.0f;
}

void SlipstreamPlugin::SetById( unsigned int id, float value )
{
	const int host = HostIndexOf( id );
	if( host >= 0 )
		SetFloatParameter( static_cast< unsigned int >( host ), value );
}

} // namespace slipstream
