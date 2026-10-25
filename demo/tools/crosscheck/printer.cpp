// The C++ half of the browser demo's cross-check of its CPU port: the plugin's
// own Controls.cpp, Flight.cpp, Blackbody.cpp and Geodesic.cpp, driven through a
// fixed scenario and printed, floats and doubles as their bits. run.mjs prints
// the same scenario from the PORT block of demo/plugin.js; crosscheck.sh diffs
// the two. Read this beside run.mjs: each block mirrors one there.
//
// Built by crosscheck.sh, twice (see there for why): with -ffp-contract=off,
// which must match JavaScript line for line, and as the plugin is built.

#include "Blackbody.h"
#include "Controls.h"
#include "Flight.h"
#include "Geodesic.h"

#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
std::string f( float v )
{
	uint32_t bits;
	std::memcpy( &bits, &v, sizeof bits );
	char buffer[ 16 ];
	std::snprintf( buffer, sizeof buffer, "%08" PRIx32, bits );
	return buffer;
}
/// FNV-1a over 32-bit words.
uint32_t fnv( uint32_t h, uint32_t word )
{
	return ( h ^ word ) * 16777619u;
}
uint32_t bitsOf( float v )
{
	uint32_t bits;
	std::memcpy( &bits, &v, sizeof bits );
	return bits;
}

const char* kPoseFields = "speed tunnel pull clipScale twist mouth endLight holeD disc blackout scene";
std::string pose( const Pose& p )
{
	return d( p.speed ) + " " + d( p.tunnel ) + " " + d( p.pull ) + " " + d( p.clipScale ) + " " + d( p.twist ) + " "
	       + d( p.mouth ) + " " + d( p.endLight ) + " " + d( p.holeD ) + " " + d( p.disc ) + " " + d( p.blackout ) + " "
	       + d( p.scene );
}

/// The drive scenario's events, the same on both sides: what the host holds
/// at each frame. Integer arithmetic only, so both sides agree exactly.
struct Host
{
	int drive;
	bool engage, jump;
	double fader;
};
Host hostAt( int scenario, int frame )
{
	Host h { 0, false, false, 0.0 };
	switch( scenario % 4 )
	{
	case 0://Hold: on, off mid-transit, on again halfway out (carries on), off
		h.drive  = static_cast< int >( Drive::Hold );
		h.engage = ( frame >= 10 && frame < 230 ) || ( frame >= 262 && frame < 300 ) || ( frame >= 520 && frame < 900 );
		break;
	case 1://Trip: presses, some while already tripping
		h.drive = static_cast< int >( Drive::Trip );
		h.jump  = frame == 5 || frame == 6 || frame == 40 || frame == 420 || frame == 421 || frame == 700 || frame == 1100;
		break;
	case 2://Loop
		h.drive = static_cast< int >( Drive::Loop );
		break;
	default://Fader: forward, back, past the ends, and held
		h.drive = static_cast< int >( Drive::Fader );
		if( frame < 300 )
			h.fader = frame / 300.0;
		else if( frame < 500 )
			h.fader = ( 600 - frame ) / 300.0;
		else if( frame < 700 )
			h.fader = ( frame - 450 ) / 150.0 - 0.25;
		else
			h.fader = ( frame % 7 ) / 6.0;
		break;
	}
	return h;
}
} // namespace

int main()
{
	//-----------------------------------------------------------------------
	// Controls: every slider position (and a little either side), every mapping.
	//-----------------------------------------------------------------------
	for( int k = -10; k <= 1010; ++k )
	{
		const float v = static_cast< float >( k / 1000.0 );
		std::printf( "ctl %d %s sec=%s cru=%s top=%s head=%s fov=%s shu=%s exp=%s str=%s swi=%s hole=%s tilt=%s outer=%s heat=%s clip=%s\n", k,
		             f( v ).c_str(), d( SecondsFromParam( v ) ).c_str(), d( CruiseFromParam( v ) ).c_str(), d( TopFromParam( v ) ).c_str(),
		             d( HeadingFromParam( v ) ).c_str(), d( FieldOfViewFromParam( v ) ).c_str(), d( ShutterFromParam( v ) ).c_str(),
		             d( ExposureFromParam( v ) ).c_str(), d( StretchFromParam( v ) ).c_str(), d( SwirlTurnsFromParam( v ) ).c_str(),
		             d( HoleSizeFromParam( v ) ).c_str(), d( DiscTiltFromParam( v ) ).c_str(), d( DiscOuterFromParam( v ) ).c_str(),
		             d( DiscHeatFromParam( v ) ).c_str(), d( ClipDepthFromParam( v ) ).c_str() );
	}
	for( int k = -40; k <= 440; ++k )
	{
		const float v = static_cast< float >( k / 100.0 );
		std::printf( "opt %d %d %d %d\n", k, OptionIndex( v, 4 ), OptionIndex( v, 2 ), OptionIndex( -v, 4 ) );
	}
	for( int k = -5; k <= 260; ++k )
	{
		const double x = k / 10.0;
		std::printf( "inv %d sec=%s cru=%s top=%s fov=%s exp=%s heat=%s clip=%s\n", k, f( ParamFromSeconds( x ) ).c_str(),
		             f( ParamFromCruise( x / 100.0 ) ).c_str(), f( ParamFromTop( x / 2.0 ) ).c_str(),
		             f( ParamFromFieldOfView( 5.0 * x ) ).c_str(), f( ParamFromExposure( x / 2.0 - 5.0 ) ).c_str(),
		             f( ParamFromDiscHeat( 1600.0 * x ) ).c_str(), f( ParamFromClipDepth( x / 10.0 ) ).c_str() );
	}
	//The constructor's defaults, by their inverse mappings.
	std::printf( "defaults entry=%s transit=%s exit=%s cruise=%s cruiseSpeed=%s top=%s fov=%s exposure=%s heat=%s clipDepth=%s\n",
	             f( ParamFromSeconds( 2.0 ) ).c_str(), f( ParamFromSeconds( 3.0 ) ).c_str(), f( ParamFromSeconds( 1.5 ) ).c_str(),
	             f( ParamFromSeconds( 3.0 ) ).c_str(), f( ParamFromCruise( 0.08 ) ).c_str(), f( ParamFromTop( 1.0 ) ).c_str(),
	             f( ParamFromFieldOfView( 60.0 ) ).c_str(), f( ParamFromExposure( 0.0 ) ).c_str(),
	             f( ParamFromDiscHeat( 4500.0 ) ).c_str(), f( ParamFromClipDepth( 0.3 ) ).c_str() );
	for( int effect = 0; effect < 2; ++effect )
	{
		std::printf( "order %d", effect );
		for( unsigned int id : HostOrder( effect == 1 ) )
			if( id < PT_ABOUT_TEXT )
				std::printf( " %u", id );
		std::printf( "\n" );
	}
	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		std::printf( "param %u %s|%s\n", id, NameOf( id ), GroupOf( id ) );
	for( int i = 0; i < 4; ++i )
		std::printf( "names %d %s|%s transit=%s decay=%s\n", i, LookName( static_cast< Look >( i ) ),
		             DriveName( static_cast< Drive >( i ) ), d( TransitSpeed( static_cast< Look >( i ) ) ).c_str(),
		             d( FlashDecay( static_cast< Look >( i ) ) ).c_str() );
	for( int i = 0; i < 4; ++i )
	{
		std::printf( "flashes %d", i );
		for( double p : FlashPoints( static_cast< Look >( i ) ) )
			std::printf( " %s", d( p ).c_str() );
		std::printf( "\n" );
	}

	//-----------------------------------------------------------------------
	// The black hole's distance for a shadow, and the shadow for a distance.
	//-----------------------------------------------------------------------
	for( int k = -2; k <= 400; ++k )
	{
		const double distance = 2.5 + k * k * 0.25;
		std::printf( "shadow %d %s\n", k, d( ShadowAngle( distance ) ).c_str() );
	}
	for( int k = 0; k <= 400; ++k )
	{
		const double angle = 1.0e-6 + k * ( 1.5707963 / 400.0 );
		std::printf( "dist %d %s\n", k, d( DistanceForShadow( angle ) ).c_str() );
	}

	//-----------------------------------------------------------------------
	// Each look's script, all through J (and either side of it).
	//-----------------------------------------------------------------------
	std::printf( "posefields %s\n", kPoseFields );
	const double holds[ 3 ] = { 1.0, DistanceForShadow( 0.3 * 0.5 * 60.0 * 3.14159265358979323846 / 180.0 ), 4000.0 };
	for( int look = 0; look < 4; ++look )
		for( int s = 0; s < 3; ++s )
		{
			ScriptSettings settings;
			settings.stretch  = 0.2 * s + 0.1;
			settings.holeHold = holds[ s ];
			for( int k = -100; k <= 1100; ++k )
			{
				const double J = k / 1000.0;
				std::printf( "pose %d %d %d %s\n", look, s, k, pose( PoseAt( static_cast< Look >( look ), J, settings ) ).c_str() );
			}
			//The rests, the flash points and the halves' edges, exactly.
			const double exact[] = { 0.0, 0.5, 1.0, 0.25, 0.75, 0.4, 0.575, 0.325, 0.65, 0.8, 0.999999999, 0.5000000000000001 };
			for( double J : exact )
				std::printf( "posex %d %d %s %s\n", look, s, d( J ).c_str(), pose( PoseAt( static_cast< Look >( look ), J, settings ) ).c_str() );
		}

	//-----------------------------------------------------------------------
	// The drive and the camera, frame by frame: every drive, at 60, 50 and 30
	// fps, every look, through presses abandoned halfway and a fader scrubbed
	// both ways. A host-style clock: each dt a difference of two readings.
	//-----------------------------------------------------------------------
	const double rates[ 3 ] = { 60.0, 50.0, 30.0 };
	for( int scenario = 0; scenario < 4 * 4 * 3; ++scenario )
	{
		const Look look   = static_cast< Look >( ( scenario / 4 ) % 4 );
		const double fps  = rates[ scenario / 16 ];
		Journey journey;
		Camera camera;
		ScriptSettings settings;
		settings.stretch  = 0.7125;
		settings.holeHold = holds[ 1 ];
		DriveInputs in;
		in.entry   = SecondsFromParam( static_cast< float >( 0.2 + 0.05 * ( scenario % 5 ) ) );
		in.transit = SecondsFromParam( static_cast< float >( 0.25 + 0.04 * ( scenario % 7 ) ) );
		in.exit    = SecondsFromParam( static_cast< float >( 0.15 + 0.06 * ( scenario % 3 ) ) );
		in.cruise  = SecondsFromParam( static_cast< float >( 0.2 + 0.03 * ( scenario % 4 ) ) );
		const double cruise = CruiseFromParam( static_cast< float >( 0.05 * ( scenario % 6 ) ) );
		const double top    = TopFromParam( static_cast< float >( 0.3 + 0.1 * ( scenario % 5 ) ) );
		bool held           = false;
		for( int frame = 0; frame < 1300; ++frame )
		{
			const Host h = hostAt( scenario, frame );
			in.drive     = static_cast< Drive >( h.drive );
			in.engage    = h.engage;
			in.jumpPress = h.jump && !held;
			held         = h.jump;
			in.fader     = h.fader;
			const double dt = frame == 0 ? 0.0 : ( frame / fps ) - ( ( frame - 1 ) / fps );
			journey.Advance( dt, in );
			camera.Advance( look, journey.Start(), journey.End(), dt, cruise, top, settings );
			std::printf( "fly %d %d %s %s %s %s %s %s %s %s %s %s\n", scenario, frame, d( journey.Start() ).c_str(),
			             d( journey.End() ).c_str(), d( journey.Phase() ).c_str(), d( camera.Total() ).c_str(),
			             d( camera.PhaseZ() ).c_str(), d( camera.LongZ() ).c_str(), d( camera.Speed() ).c_str(),
			             d( camera.OffsetAt( 0.0 ) ).c_str(), d( camera.OffsetAt( 0.37 ) ).c_str(),
			             d( camera.OffsetAt( 1.0 - 0.5 ) ).c_str() );
		}
	}

	//The first trap: a Trip whose transit ends on a frame's boundary must end.
	//Every whole-frame Transit Time from 0.2 to 5 s at 60, 50 and 30 fps.
	for( int r = 0; r < 3; ++r )
	{
		const double fps = rates[ r ];
		for( int n = static_cast< int >( std::ceil( 0.2 * fps ) ); n <= static_cast< int >( 5.0 * fps ); ++n )
		{
			Journey journey;
			DriveInputs in;
			in.drive    = Drive::Trip;
			in.entry    = 0.5;
			in.exit     = 0.5;
			in.transit  = n / fps;
			int back    = -1;
			uint32_t h  = 2166136261u;
			for( int frame = 0; frame < 2000 && back < 0; ++frame )
			{
				in.jumpPress = frame == 1;
				const double dt = frame == 0 ? 0.0 : ( frame / fps ) - ( ( frame - 1 ) / fps );
				journey.Advance( dt, in );
				uint64_t bits;
				const double p = journey.Phase();
				std::memcpy( &bits, &p, sizeof bits );
				h = fnv( fnv( h, static_cast< uint32_t >( bits ) ), static_cast< uint32_t >( bits >> 32 ) );
				if( frame > 1 && journey.Phase() == 0.0 )
					back = frame;
			}
			std::printf( "trip %g %d back=%d h=%08" PRIx32 "\n", fps, n, back, h );
		}
	}

	//-----------------------------------------------------------------------
	// The blackbody table, every texel the GPU reads.
	//-----------------------------------------------------------------------
	const auto& table = blackbody::Table();
	for( int i = 0; i < blackbody::kEntries; ++i )
		std::printf( "bb %d %s %s %s %s\n", i, f( table[ i ].rgb[ 0 ] ).c_str(), f( table[ i ].rgb[ 1 ] ).c_str(),
		             f( table[ i ].rgb[ 2 ] ).c_str(), f( table[ i ].log2Y ).c_str() );

	//-----------------------------------------------------------------------
	// The orbit tables: every row's b, its ends in double, its ends as the GPU
	// reads them, and a digest of its 2,048 floats in the orbit texture.
	//-----------------------------------------------------------------------
	const geodesic::Tables& t = geodesic::Get();
	uint32_t all = 2166136261u;
	for( int i = 0; i < geodesic::kB; ++i )
	{
		uint32_t row = 2166136261u;
		for( int j = 0; j < geodesic::kT * 4; ++j )
			row = fnv( row, bitsOf( t.orbits[ static_cast< size_t >( i ) * geodesic::kT * 4 + j ] ) );
		all = fnv( all, row );
		std::printf( "geo %d b=%s %s uEnd=%s phiEnd=%s phiEndCap=%s ends=%s,%s,%s,%s row=%08" PRIx32 "\n", i,
		             d( geodesic::EscapeB( i ) ).c_str(), d( geodesic::CaptureB( i ) ).c_str(), d( t.uEndEscape[ i ] ).c_str(),
		             d( t.phiEndEscape[ i ] ).c_str(), d( t.phiEndCapture[ i ] ).c_str(), f( t.ends[ i * 4 + 0 ] ).c_str(),
		             f( t.ends[ i * 4 + 1 ] ).c_str(), f( t.ends[ i * 4 + 2 ] ).c_str(), f( t.ends[ i * 4 + 3 ] ).c_str(), row );
	}
	//A sample of single texels, so a digest mismatch has somewhere to look.
	for( int k = 0; k < 4000; ++k )
	{
		const size_t at = ( static_cast< size_t >( k ) * 2654435761u ) % ( static_cast< size_t >( geodesic::kB ) * geodesic::kT * 4 );
		std::printf( "orb %zu %s\n", at, f( t.orbits[ at ] ).c_str() );
	}
	std::printf( "geoall %08" PRIx32 "\n", all );

	//Every float the GPU reads from the tables, raw, for compare.py's texel
	//count: the orbit table, its ends, the blackbody table, in that order.
	if( const char* path = std::getenv( "TABLES_OUT" ) )
	{
		if( FILE* file = std::fopen( path, "wb" ) )
		{
			std::fwrite( t.orbits.data(), sizeof( float ), t.orbits.size(), file );
			std::fwrite( t.ends.data(), sizeof( float ), t.ends.size(), file );
			for( const auto& entry : table )
			{
				std::fwrite( entry.rgb, sizeof( float ), 3, file );
				std::fwrite( &entry.log2Y, sizeof( float ), 1, file );
			}
			std::fclose( file );
		}
	}
	return 0;
}
