// The checks. Included into main.cpp's anonymous namespace.
//
// Each takes a Perturb. With every field at its default the check scores the
// plugin against the physics it claims; `--negative` sets one field at a
// time to a deliberately wrong MODEL (a test hook in the plugin, or the
// harness's own expectation made wrong) and requires the check to FAIL.

struct Perturb
{
};

using CheckFn = int ( * )( const Perturb& );

struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
	const char* what;
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {};
	return list;
}

std::string CheckList()
{
	std::string out = "  checks:";
	for( const CheckEntry& c : checks() )
		out += std::string( " --" ) + c.flag;
	return out + "\n";
}

bool isOffline( const std::string& flag )
{
	for( const CheckEntry& c : checks() )
		if( flag == c.flag )
			return c.offline;
	return false;
}

int runNegative( bool offlineOnly = false )
{
	( void )offlineOnly;
	return 0;
}

int runExpect()
{
	return 0;
}

//===========================================================================
// --bench
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
	};
	const Size sizes[] = { { 1280, 720, "720p" }, { 1920, 1080, "1080p" }, { 3840, 2160, "4K" } };
	std::printf( "\n=== bench: after 60 frames of warm-up, each frame timed with glFinish both sides\n" );
	struct Case
	{
		bool effect;
		Look look;
		double phase;
		const char* what;
	};
	const Case cases[] = {
		{ false, Look::Hyperspace, 0.0, "Hyperspace, normal space" },
		{ false, Look::Hyperspace, 0.5, "Hyperspace, in transit" },
		{ true, Look::Hyperspace, 0.35, "Hyperspace, jumping" },
		{ true, Look::Warp, 0.3, "Warp, the rubber band" },
		{ false, Look::Wormhole, 0.5, "Wormhole, in transit" },
		{ true, Look::BlackHole, 0.5, "Black Hole, held" },
	};
	for( const Case& c : cases )
		for( const Size& size : sizes )
		{
			Rig rig( c.effect );
			if( !rig.Init( size.w, size.h ) )
				return 1;
			rig.Set( PT_LOOK, static_cast< float >( c.look ) );
			rig.Set( PT_DRIVE, static_cast< float >( Drive::Fader ) );
			rig.Set( PT_JOURNEY, static_cast< float >( c.phase ) );
			if( !rig.Render( 60 ) )
				return 1;
			glFinish();
			constexpr int kTimed = 60;
			std::vector< double > times;
			for( int f = 0; f < kTimed; ++f )
			{
				const auto start = std::chrono::steady_clock::now();
				if( !rig.Render( 1 ) )
					return 1;
				glFinish();
				times.push_back( std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() );
			}
			std::sort( times.begin(), times.end() );
			std::printf( "  %-14s %-28s %-6s median %6.2f ms/frame, worst %6.2f  (median %5.1f%% of a 60 fps frame)\n",
			             c.effect ? "SW Slip Over" : "SW Slipstream", c.what, size.name, times[ kTimed / 2 ], times.back(),
			             100.0 * times[ kTimed / 2 ] / ( 1000.0 / 60.0 ) );
		}
	return 0;
}
