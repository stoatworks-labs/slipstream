// The checks. Included into main.cpp's anonymous namespace.
//
// Each takes a Perturb. With every field at its default the check scores the
// plugin against the physics it claims, measured out of the picture or the
// exposure buffers and compared with something computed ANOTHER way: a
// simulation of the definition (a camera's shutter open while a star moves),
// a second integrator, a closed form, the literature. `--negative` sets one
// field at a time to a deliberately wrong MODEL (a test hook in the plugin,
// or the harness's own expectation made wrong) and requires the check to
// FAIL.

struct Perturb
{
	bool constantFlux     = false;///< --streak: the star's flux not inverse-square (plugin)
	bool unspread         = false;///< --energy: a streak's light not spread over its length (plugin)
	bool noCopies         = false;///< --copies: the field's periodic copies not counted (plugin)
	bool noRoundTrip      = false;///< --identity: the clip's way into exposure skipped (plugin)
	bool smearOutward     = false;///< --smear: expect the smear outward from the point (harness)
	bool abandonReturns   = false;///< --journey: expect an abandoned jump to run back (harness)
	bool floatClock       = false;///< --clock: elapsed time from the host clock in float (plugin)
	bool newtonian        = false;///< --geodesic: the orbit without its 3u^2 (harness's integration)
	bool flatCapture      = false;///< --shadow: capture below b = 2 (plugin)
	bool straightRays     = false;///< --einstein and --ring: no bending (plugin)
	bool spinFlipped      = false;///< --disc: the spin's sign flipped in the shift only (plugin)
	bool noGravityShift   = false;///< --disc: expect no gravitational shift (harness)
	bool hotterLocus      = false;///< --planck: compare against the locus 20% hotter (harness)
	bool seam             = false;///< --seam: the clouds' lattice one cell short (plugin)
	bool resizeLate       = false;///< --resize: compare with a run one frame on (harness)
	bool cuesRamp         = false;///< --cues: ramp every control (harness)
};

using CheckFn = int ( * )( const Perturb& );

struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
	const char* what;
};

//---------------------------------------------------------------------------
// Helpers.
//---------------------------------------------------------------------------
double smoothstepD( double a, double b, double x )
{
	const double t = std::clamp( ( x - a ) / ( b - a ), 0.0, 1.0 );
	return t * t * ( 3.0 - 2.0 * t );
}

/// The far end's fade, as the star shader draws it: part of the model (the
/// stars come out of the dark), so the simulation applies it too.
double farFade( double z )
{
	return 1.0 - smoothstepD( kNear + 0.8 * kDepth, kNear + kDepth, z );
}

double luminance( const float* e )
{
	return 0.2126 * e[ 0 ] + 0.7152 * e[ 1 ] + 0.0722 * e[ 2 ];
}

/// Light binned along a line out of (vx, vy) in direction (dx, dy): each
/// pixel within `halfWidth` of the line shares its light among the bins its
/// square covers. A unit square projected on a line is a trapezoid of width
/// |dx| + |dy|; binning pixel CENTRES instead counts a diagonal line's pixels
/// unevenly (a 20% ripple at 2 px bins), which is the measurement and not
/// the streak. Channel mean.
struct Profile
{
	double s0 = 0.0, bin = 1.0;
	std::vector< double > light;
	double total = 0.0;
};

/// The share of a unit square's area, projected on a unit direction with
/// components of magnitude a and b, that lies below x (the square centred
/// on 0): the CDF of U( -a/2, a/2 ) + U( -b/2, b/2 ).
double squareShare( double x, double a, double b )
{
	if( a < b )
		std::swap( a, b );
	const double half = 0.5 * ( a + b );
	if( x <= -half )
		return 0.0;
	if( x >= half )
		return 1.0;
	if( b < 1e-9 )
		return ( x + 0.5 * a ) / a;
	//The density rises over [ -half, -half + b ], is flat to half - b, falls.
	auto rising = [ & ]( double y ) { return y * y / ( 2.0 * a * b ); };
	const double y = x + half;
	if( y <= b )
		return rising( y );
	if( y <= a )
		return b / ( 2.0 * a ) + ( y - b ) / a;
	return 1.0 - rising( a + b - y );
}
Profile measureLine( const Floats& e, int w, int h, double vx, double vy, double dx, double dy, double s0, double bin, int bins,
                     double halfWidth )
{
	Profile p;
	p.s0  = s0;
	p.bin = bin;
	p.light.assign( static_cast< size_t >( bins ), 0.0 );
	for( int j = 0; j < h; ++j )
		for( int i = 0; i < w; ++i )
		{
			const double px = i + 0.5 - vx, py = j + 0.5 - vy;
			const double s = px * dx + py * dy, across = std::fabs( px * dy - py * dx );
			if( across > halfWidth )
				continue;
			const float* v = &e[ ( static_cast< size_t >( j ) * w + i ) * 4 ];
			const double light = ( v[ 0 ] + v[ 1 ] + v[ 2 ] ) / 3.0;
			if( light == 0.0 )
				continue;
			const double a = std::fabs( dx ), c = std::fabs( dy ), reach = 0.5 * ( a + c );
			const int first = static_cast< int >( std::floor( ( s - reach - s0 ) / bin ) );
			const int last  = static_cast< int >( std::floor( ( s + reach - s0 ) / bin ) );
			for( int k = std::max( first, 0 ); k <= std::min( last, bins - 1 ); ++k )
			{
				const double lo = s0 + k * bin - s, hi = lo + bin;
				p.light[ static_cast< size_t >( k ) ] += light * ( squareShare( hi, a, c ) - squareShare( lo, a, c ) );
			}
			p.total += light;
		}
	return p;
}

/// The definition, simulated: a camera's shutter open while the field flies
/// past at constant speed. 400,000 instants through the shutter; at each,
/// every copy of the star's column inside the box [ near, near + D ) is at
/// depth z, lands on the screen at f rho / z, and lays down L x flux( z ) /
/// N. Nothing here is the shader's closed form.
Profile simulateStreak( double rho, double zc, double dz, double focal, double luminosity, double s0, double bin, int bins )
{
	Profile p;
	p.s0  = s0;
	p.bin = bin;
	p.light.assign( static_cast< size_t >( bins ), 0.0 );
	constexpr int N = 400000;
	for( int i = 0; i < N; ++i )
	{
		const double t = ( i + 0.5 ) / N;
		for( int k = -12; k <= 12; ++k )
		{
			const double z = zc + k * kDepth + dz * ( 1.0 - t );
			if( z < kNear || z >= kNear + kDepth )
				continue;
			const double light = luminosity / ( rho * rho + z * z ) * farFade( z ) / N;
			const double s     = focal * rho / z;
			p.total += light;
			const int b = static_cast< int >( std::floor( ( s - s0 ) / bin ) );
			if( b >= 0 && b < bins )
				p.light[ static_cast< size_t >( b ) ] += light;
		}
	}
	return p;
}

/// A rig on Warp in transit with one white star: the star's speed set by Top
/// Speed alone (Warp's transit has no pull, no tunnel, no flash in the
/// exposure), placed so that at the next frame's shutter close it sits at
/// depth zc. Returns the star buffer.
struct StarShot
{
	Floats exposure;
	double zc, dz, focal, vx, vy;
};
bool shootStar( Rig& rig, double x, double y, double zcTarget, double top, double luminosity, StarShot& out )
{
	rig.Set( PT_LOOK, static_cast< float >( Look::Warp ) );
	rig.Set( PT_DRIVE, static_cast< float >( Drive::Fader ) );
	rig.Set( PT_JOURNEY, top > 0.0 ? 0.5f : 0.0f );
	rig.Set( PT_TOP_SPEED, top > 0.0 ? ParamFromTop( top ) : 0.5f );
	rig.Set( PT_CRUISE_SPEED, 0.0f );
	rig.Set( PT_DISPERSION, 0.0f );
	rig.Set( PT_SHUTTER, 0.5f );
	rig.Set( PT_EXPOSURE, ParamFromExposure( 0.0 ) );
	rig.Set( PT_FLASH, 0.0f );
	rig.plugin.SetTestStarForTest( x, y, 1.0, luminosity );
	//Settle at speed: the fader's step to 0.5 is inside the first frame.
	if( !rig.Render( 4 ) )
		return false;
	//The next frame's camera, to the double: speed is constant now.
	const double perFrame = ( top > 0.0 ? top * TransitSpeed( Look::Warp ) * kDepth : 0.0 ) / rig.fps;
	const double next     = std::fmod( rig.plugin.CameraState().PhaseZ() + perFrame, kDepth );
	double z0             = std::fmod( next + zcTarget, kDepth );
	if( z0 < 0.0 )
		z0 += kDepth;
	rig.plugin.SetTestStarForTest( x, y, z0, luminosity );
	if( !rig.Render( 1 ) )
		return false;
	const double phase = rig.plugin.CameraState().PhaseZ();
	out.zc             = kNear + std::fmod( std::fmod( z0 - phase - kNear, kDepth ) + kDepth, kDepth );
	out.dz             = rig.plugin.LastShutterZ();
	out.focal          = rig.plugin.LastFocal();
	out.vx             = 0.5 * rig.width;
	out.vy             = 0.5 * rig.height;
	out.exposure       = rig.StarExposure();
	return true;
}

//===========================================================================
// --streak: a star's streak has the length the camera flew and the light
// a shutter would lay down along it, out of the picture.
//===========================================================================
int runStreak( const Perturb& perturb )
{
	std::printf( "\n=== streak: one star in flight, against a simulated shutter (every instant of it, every copy)\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig( false );
		if( !rig.Init( raster.w, raster.h ) )
			return 1;
		rig.plugin.SetFluxLawForTest( !perturb.constantFlux );
		const double x = 0.28, y = 0.21, rho = std::hypot( x, y );
		StarShot shot;
		if( !shootStar( rig, x, y, 0.9, 1.0, 50.0, shot ) )
			return 1;
		const double sigma = std::max( 0.6, 0.8 * raster.h / 1080.0 );
		const double sIn = shot.focal * rho / std::min( shot.zc + shot.dz, kNear + kDepth ), sOut = shot.focal * rho / shot.zc;
		const double bin = std::max( 2.0, 2.0 * sigma );
		const double s0  = sIn - 6.0 * sigma;
		const int bins   = static_cast< int >( std::ceil( ( sOut - sIn + 12.0 * sigma ) / bin ) );
		const Profile got  = measureLine( shot.exposure, raster.w, raster.h, shot.vx, shot.vy, x / rho, y / rho, s0, bin, bins, 5.0 * sigma + 1.0 );
		const Profile want = simulateStreak( rho, shot.zc, shot.dz, shot.focal, 50.0, s0, bin, bins );
		//Interior bins: three sigmas and a bin clear of both ends, where the
		//blur along the line is all light from inside the streak.
		double worst = 0.0, lo = 1e30, hi = 0.0;
		int used     = 0;
		for( int b = 0; b < bins; ++b )
		{
			const double a = s0 + b * bin, c = a + bin;
			if( a < sIn + 3.0 * sigma || c > sOut - 3.0 * sigma )
				continue;
			worst = std::max( worst, std::fabs( got.light[ b ] / want.light[ b ] - 1.0 ) );
			lo    = std::min( lo, got.light[ b ] / bin );
			hi    = std::max( hi, got.light[ b ] / bin );
			++used;
		}
		if( std::getenv( "SLIPTEST_TRACE" ) )
			for( int b = 0; b < bins; ++b )
				std::printf( "    bin %3d  s %7.1f  got %.4f  want %.4f\n", b, s0 + ( b + 0.5 ) * bin, got.light[ b ], want.light[ b ] );
		std::printf( "  %dx%d: star at rho %.3f, depth %.3f -> %.3f over the shutter; streak %.1f px (%.1f to %.1f from the vanishing point)\n",
		             raster.w, raster.h, rho, shot.zc + shot.dz, shot.zc, sOut - sIn, sIn, sOut );
		//2%: half float (0.05%), the sampled Gaussian across (0.2% at sigma
		//0.6 px), light leaking between neighbouring bins where the level
		//changes (sigma / bin of a bin's change, under 1%).
		Check( used >= 4 && worst < 0.02,
		       fmt( "light per unit length matches the simulated shutter in all %d interior bins: worst %.2f%% (2%%); the level runs "
		            "%.3f to %.3f per pixel, a %.1f%% spread", used, 100.0 * worst, lo, hi, 100.0 * ( hi / lo - 1.0 ) ) );
		Check( std::fabs( got.total / want.total - 1.0 ) < 0.01,
		       fmt( "the streak's whole light %.4f, the simulation's %.4f (1%%)", got.total, want.total ) );
	}
	return Verdict();
}

//===========================================================================
// --energy: a faster star lays the same light over a longer streak.
//===========================================================================
int runEnergy( const Perturb& perturb )
{
	std::printf( "\n=== energy: one star's whole light at rest and at three speeds, against its flux through the shutter\n" );
	for( const Raster& raster : kRasters )
	{
		const double x = -0.31, y = 0.12, rho = std::hypot( x, y );
		for( double top : { 0.0, 0.2, 0.6, 1.5 } )
		{
			Rig rig( false );
			if( !rig.Init( raster.w, raster.h ) )
				return 1;
			rig.plugin.SetLengthLawForTest( !perturb.unspread );
			StarShot shot;
			if( !shootStar( rig, x, y, 1.2, top, 20.0, shot ) )
				return 1;
			double total = 0.0;
			for( size_t i = 0; i < shot.exposure.size(); i += 4 )
				total += ( shot.exposure[ i ] + shot.exposure[ i + 1 ] + shot.exposure[ i + 2 ] ) / 3.0;
			const Profile want = simulateStreak( rho, shot.zc, shot.dz, shot.focal, 20.0, 0.0, 1.0, 0 );
			const double rest  = 20.0 / ( rho * rho + shot.zc * shot.zc ) * farFade( shot.zc );
			const double length = shot.focal * rho * ( 1.0 / shot.zc - 1.0 / std::min( shot.zc + shot.dz, kNear + kDepth ) );
			//0.5%: the sampled Gaussian sums to 1 within 0.2% at sigma 0.6
			//px, and half float adds 0.05% a pixel.
			Check( std::fabs( total / want.total - 1.0 ) < 0.005,
			       fmt( "%dx%d, Top Speed %.1f: streak %.1f px, whole light %.4f against the flux through the shutter %.4f (at rest it "
			            "would be %.4f) (0.5%%)", raster.w, raster.h, top, length, total, want.total, rest ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --copies: past one pass of the field per shutter, the light along a
// star's line stops depending on speed.
//===========================================================================
int runCopies( const Perturb& perturb )
{
	std::printf( "\n=== copies: hyperspace, the periodic field passing more than once a shutter\n" );
	for( const Raster& raster : kRasters )
	{
		const double x = 0.22, y = -0.26, rho = std::hypot( x, y );
		double totals[ 2 ] = {};
		int index          = 0;
		for( double top : { 4.5, 7.5 } )
		{
			Rig rig( false );
			if( !rig.Init( raster.w, raster.h ) )
				return 1;
			rig.plugin.SetCopyLawForTest( !perturb.noCopies );
			StarShot shot;
			if( !shootStar( rig, x, y, 1.0, top, 50.0, shot ) )
				return 1;
			const double sigma = std::max( 0.6, 0.8 * raster.h / 1080.0 );
			const double sIn   = shot.focal * rho / ( kNear + kDepth );
			const double sMax  = std::hypot( 0.5 * raster.w, 0.5 * raster.h );
			const double bin   = std::max( 4.0, 4.0 * sigma );
			const double s0    = sIn - 6.0 * sigma;
			const int bins     = static_cast< int >( std::ceil( ( sMax - s0 ) / bin ) );
			const Profile got  = measureLine( shot.exposure, raster.w, raster.h, shot.vx, shot.vy, x / rho, y / rho, s0, bin, bins, 5.0 * sigma + 1.0 );
			const Profile want = simulateStreak( rho, shot.zc, shot.dz, shot.focal, 50.0, s0, bin, bins );
			double worst = 0.0;
			int used     = 0;
			//Inside the frame only, and clear of the far end and of the step
			//where the count of copies changes ( zc + k D ).
			for( int b = 0; b < bins; ++b )
			{
				const double a = s0 + b * bin, c = a + bin;
				//Clear of the far end's fade too: the stars coming out of the
				//dark is a choice of the model, not the physics on trial.
				const double fadeEdge = shot.focal * rho / ( kNear + 0.8 * kDepth );
				if( a < std::max( sIn, fadeEdge ) + 3.0 * sigma || c > 0.5 * raster.h - 2.0 || want.light[ b ] <= 0.0 )
					continue;
				bool step = false;
				for( int k = -4; k <= 4; ++k )
					for( double edge : { shot.zc + k * kDepth, shot.zc + k * kDepth + shot.dz } )
						if( edge > kNear && edge < kNear + kDepth )
						{
							const double s = shot.focal * rho / edge;
							step           = step || ( s > a - 3.0 * sigma && s < c + 3.0 * sigma );
						}
				if( step )
					continue;
				worst = std::max( worst, std::fabs( got.light[ b ] / want.light[ b ] - 1.0 ) );
				if( std::getenv( "SLIPTEST_TRACE" ) )
					std::printf( "    bin %3d  s %7.1f  got %.4f  want %.4f\n", b, a + 0.5 * bin, got.light[ b ], want.light[ b ] );
				++used;
			}
			std::printf( "  %dx%d, Top Speed %.1f: %.2f passes of the field a shutter\n", raster.w, raster.h, top, shot.dz / kDepth );
			Check( used >= 3 && worst < 0.02,
			       fmt( "light along the line matches the simulated periodic field in %d bins: worst %.2f%% (2%%, as --streak)", used,
			            100.0 * worst ) );
			totals[ index++ ] = want.total;
		}
		//The claim itself: once the field passes more than once a shutter
		//the column's light no longer depends on speed. Both totals are the
		//time-average flux of whichever copy is in the box, so they differ
		//only by the partial pass at the end, under 1/1.5 of a pass's share.
		std::printf( "  the column's whole light at 1.5 and 2.5 passes a shutter: %.4f and %.4f (the simulation's)\n", totals[ 0 ], totals[ 1 ] );
	}
	return Verdict();
}

//===========================================================================
// --identity: the Over at rest gives back the clip, every look.
//===========================================================================
int runIdentity( const Perturb& perturb )
{
	std::printf( "\n=== identity: the Over at rest returns the clip to the bit (sRGB -> exposure -> film -> sRGB)\n" );
	for( const Raster& raster : kRasters )
		for( int look = 0; look < static_cast< int >( Look::Count ); ++look )
		{
			Rig rig( true );
			const Floats card = buildCard( raster.w, raster.h );
			if( !rig.Init( raster.w, raster.h, &card ) )
				return 1;
			rig.plugin.SetRoundTripForTest( !perturb.noRoundTrip );
			rig.Set( PT_LOOK, static_cast< float >( look ) );
			rig.Set( PT_DRIVE, static_cast< float >( Drive::Fader ) );
			rig.Set( PT_JOURNEY, 0.0f );
			rig.Set( PT_CRUISE_SPEED, 0.0f );
			rig.Set( PT_STARS, 0.0f );
			if( !rig.Render( 3 ) )
				return 1;
			const Floats out = rig.Output();
			int worst = 0, off = 0;
			for( size_t i = 0; i < out.size(); ++i )
			{
				const int a = static_cast< int >( std::lround( std::clamp( out[ i ], 0.0f, 1.0f ) * 255.0f ) );
				const int b = static_cast< int >( std::lround( card[ i ] * 255.0f ) );
				worst       = std::max( worst, std::abs( a - b ) );
				off += a != b;
			}
			Check( worst == 0, fmt( "%dx%d, %s: %d of %zu channels differ, by at most %d in 255", raster.w, raster.h,
			                        LookName( static_cast< Look >( look ) ), off, out.size(), worst ) );
		}
	return Verdict();
}

//===========================================================================
// --smear: one point of the clip becomes the inward segment, evenly lit.
//
// Pixel p averages the clip over p u, u uniform in [ 1, k ]. A point of
// light A at p0 is on the ray of every pixel between p0 / k and p0, and a
// pixel at r on that line meets it once, crossing it at speed r per unit u:
// light per unit length across the line, A / ( ( k - 1 ) r0 ), the same
// all along, and A / k in all. (The picture keeps its surface brightness and
// was smaller while it was further off: its light is not conserved, a
// star's is.)
//===========================================================================
int runSmear( const Perturb& perturb )
{
	std::printf( "\n=== smear: a point of the Over's clip, under hyperspace's smear, out of the scene's exposure\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		//A dot of 3 x 3 white pixels at ( 0.12 w, 0.08 h ) from the middle:
		//close enough in that its mirror images beyond the frame's edges
		//smear along other lines, far from this one.
		Floats card( static_cast< size_t >( w ) * h * 4, 0.0f );
		const int cx = w / 2 + static_cast< int >( 0.12 * w ), cy = h / 2 + static_cast< int >( 0.08 * h );
		for( int j = cy - 1; j <= cy + 1; ++j )
			for( int i = cx - 1; i <= cx + 1; ++i )
				for( int c = 0; c < 4; ++c )
					card[ ( static_cast< size_t >( j ) * w + i ) * 4 + c ] = 1.0f;
		for( size_t i = 3; i < card.size(); i += 4 )
			card[ i ] = 1.0f;
		Rig rig( true );
		if( !rig.Init( w, h, &card ) )
			return 1;
		rig.Set( PT_LOOK, static_cast< float >( Look::Hyperspace ) );
		rig.Set( PT_DRIVE, static_cast< float >( Drive::Fader ) );
		rig.Set( PT_JOURNEY, 0.0f );
		rig.Set( PT_CRUISE_SPEED, 1.0f );
		rig.Set( PT_CLIP_DEPTH, 0.0f );
		rig.Set( PT_SHUTTER, 1.0f );
		rig.Set( PT_STARS, 0.0f );
		if( !rig.Render( 4 ) )
			return 1;
		const double k     = 1.0 + rig.plugin.LastShutterZ() / ( ClipDepthFromParam( 0.0f ) * kDepth );
		const Floats scene = rig.SceneExposure();
		const double vx = 0.5 * w, vy = 0.5 * h;
		const double px = cx + 0.5 - vx, py = cy + 0.5 - vy, r0 = std::hypot( px, py );
		//The dot's light in exposure: nine pixels at the white clamp.
		const double dotE = 9.0 * std::log( 4096.0 );
		const double bin  = std::max( 2.0, r0 / 40.0 );
		const int bins    = static_cast< int >( std::ceil( r0 * ( k + 0.3 ) / bin ) );
		//Wide enough for the mip pyramid's blur: the level read is 1.4 tap
		//spacings a texel, trilinear reaches into the next (2.8), and
		//bilinear spreads a texel either side.
		const double spacing = ( k - 1.0 ) / 32.0 * r0;
		const Profile got = measureLine( scene, w, h, vx, vy, px / r0, py / r0, 0.0, bin, bins, 6.0 * spacing + 4.0 );
		//Where the light should be: from r0 / k to r0 (in), or from r0 to r0 k
		//(the wrong way out).
		const double a = perturb.smearOutward ? r0 : r0 / k, b = perturb.smearOutward ? r0 * k : r0;
		double inside = 0.0;
		for( int i = 0; i < bins; ++i )
		{
			const double c = ( i + 0.5 ) * bin;
			if( c > a - 6.0 * spacing && c < b + 6.0 * spacing )
				inside += got.light[ i ];
		}
		if( std::getenv( "SLIPTEST_TRACE" ) )
			for( int i = 0; i < bins; ++i )
				std::printf( "    bin %3d  r %7.1f  light %.4f\n", i, ( i + 0.5 ) * bin, got.light[ i ] );
		//Evenly lit: the mean level over the inner half of the segment against
		//the outer half's, each end cleared of the mip pyramid's blur there
		//(the coarser of the two levels trilinear reads: 2.8 tap spacings,
		//and a texel of bilinear either side) and of two bins.
		const double du = ( k - 1.0 ) / 32.0;
		const double lo = a + std::max( 4.0 * du * a, 2.0 * bin ), hi = b - std::max( 4.0 * du * b, 2.0 * bin );
		const double mid = 0.5 * ( lo + hi );
		double innerSum = 0.0, outerSum = 0.0;
		int innerBins = 0, outerBins = 0;
		for( int i = 0; i < bins; ++i )
		{
			const double c = ( i + 0.5 ) * bin;
			if( c > lo && c < mid )
			{
				innerSum += got.light[ i ];
				++innerBins;
			}
			else if( c >= mid && c < hi )
			{
				outerSum += got.light[ i ];
				++outerBins;
			}
		}
		const double innerLevel = innerBins ? innerSum / innerBins : 0.0, outerLevel = outerBins ? outerSum / outerBins : 1.0;
		const double wantTotal = dotE / k;
		std::printf( "  %dx%d: k = %.3f, the dot %.1f px out; segment %.1f to %.1f px\n", w, h, k, r0, a, b );
		//3%: 32 taps through a mip level half a step coarser than their
		//spacing, a blur of about 0.57 spacings: a sampled Gaussian's sum
		//aliases by 2 exp( -2 pi^2 0.57^2 ) = 0.3%, and bilinear is not quite
		//a Gaussian.
		Check( std::fabs( inside / wantTotal - 1.0 ) < 0.03,
		       fmt( "the segment holds %.2f of exposure; the dot's %.2f over k gives %.2f (3%%)", inside, dotE, wantTotal ) );
		//5%: the bins' own sampling of a line blurred by a pyramid (half
		//float and bilinear weights are far below it).
		Check( innerBins >= 2 && outerBins >= 2 && std::fabs( innerLevel / outerLevel - 1.0 ) < 0.05,
		       fmt( "evenly lit along it: the inner half's level over the outer half's %.3f (%d and %d bins) (5%%)", innerLevel / outerLevel,
		            innerBins, outerBins ) );
	}
	return Verdict();
}

//===========================================================================
// --journey (no GL): the drives' timings, and J only ever forward.
//===========================================================================
int runJourney( const Perturb& perturb )
{
	std::printf( "\n=== journey: Hold, Trip, Loop and Fader, at 60 fps, Entry 2 s, Transit 3 s, Exit 1.5 s, Cruise 4 s\n" );
	const double dt = 1.0 / 60.0;
	DriveInputs in;
	in.entry   = 2.0;
	in.transit = 3.0;
	in.exit    = 1.5;
	in.cruise  = 4.0;
	auto frames = []( double seconds ) { return static_cast< int >( std::lround( seconds * 60.0 ) ); };
	bool forward = true;
	auto step = [ & ]( Journey& j ) {
		j.Advance( dt, in );
		forward = forward && j.End() >= j.Start();
		in.jumpPress = false;
	};
	//Hold.
	{
		Journey j;
		in.drive  = Drive::Hold;
		in.engage = true;
		int f     = 0;
		while( j.Phase() != 0.5 && f < 10000 )
		{
			step( j );
			++f;
		}
		int held = 0;
		for( int i = 0; i < 120; ++i )
		{
			step( j );
			held += j.Phase() == 0.5;
		}
		in.engage = false;
		int back  = 0;
		while( j.Phase() != 0.0 && back < 10000 )
		{
			step( j );
			++back;
		}
		Check( std::abs( f - frames( 2.0 ) ) <= 1 && held == 120 && std::abs( back - frames( 1.5 ) ) <= 1,
		       fmt( "Hold: in transit after %d frames (Entry %d), held %d of 120 with Engage on, back in %d (Exit %d)", f, frames( 2.0 ), held,
		            back, frames( 1.5 ) ) );
	}
	//Hold, abandoned: Engage on for half a second, then off. The jump carries
	//on through and out.
	{
		Journey j;
		in.drive  = Drive::Hold;
		in.engage = true;
		for( int i = 0; i < 30; ++i )
			step( j );
		in.engage     = false;
		const double at = j.Phase();
		int f         = 30;
		double peak   = at;
		bool fell     = false;
		while( j.Phase() != 0.0 && f < 10000 )
		{
			const double before = j.Phase();
			step( j );
			fell = fell || ( j.Phase() < before && j.Phase() != 0.0 );
			peak = std::max( peak, j.Phase() );
			++f;
		}
		const bool through = peak > 0.99 && !fell;
		Check( perturb.abandonReturns ? ( !through && f < frames( 2.0 ) ) : ( through && std::abs( f - frames( 3.5 ) ) <= 2 ),
		       fmt( "Hold, Engage off at J %.3f: J went on to %.4f and came back to 0 after %d frames (Entry + Exit %d)%s", at, peak, f,
		            frames( 3.5 ), perturb.abandonReturns ? " -- expected to run back" : "" ) );
	}
	//Trip: one press, a round trip; a second press mid-trip changes nothing.
	{
		Journey j;
		in.drive     = Drive::Trip;
		in.engage    = false;
		in.jumpPress = true;
		int f        = 0;
		bool left    = false;
		while( f < 10000 )
		{
			if( f == 100 )
				in.jumpPress = true;
			step( j );
			++f;
			left = left || j.Phase() != 0.0;
			if( left && j.Phase() == 0.0 )
				break;
		}
		Check( std::abs( f - frames( 6.5 ) ) <= 2, fmt( "Trip: one press is a round trip of %d frames (Entry + Transit + Exit %d), a second "
		                                                "press at frame 100 ignored", f, frames( 6.5 ) ) );
	}
	//Loop: the period.
	{
		Journey j;
		in.drive = Drive::Loop;
		std::vector< int > departures;
		double last = 0.0;
		for( int f = 0; f < 60 * 40 && departures.size() < 3; ++f )
		{
			step( j );
			if( last == 0.0 && j.Phase() != 0.0 )
				departures.push_back( f );
			last = j.Phase();
		}
		const bool ok = departures.size() == 3 && std::abs( departures[ 1 ] - departures[ 0 ] - frames( 10.5 ) ) <= 2
		                && std::abs( departures[ 2 ] - departures[ 1 ] - frames( 10.5 ) ) <= 2 && std::abs( departures[ 0 ] - frames( 4.0 ) ) <= 1;
		Check( ok, fmt( "Loop: leaves at frames %d, %d, %d: Cruise %d first, then every %d (Cruise + Entry + Transit + Exit)",
		                departures.size() > 0 ? departures[ 0 ] : -1, departures.size() > 1 ? departures[ 1 ] : -1,
		                departures.size() > 2 ? departures[ 2 ] : -1, frames( 4.0 ), frames( 10.5 ) ) );
	}
	//Trip over every Transit Time of a whole number of frames from 0.2 to 5 s,
	//at 60, 50 and 30 fps, each frame's dt the difference of two readings
	//of a clock (as the host's is), pressed on frame 3. A transit that ends
	//on a frame's boundary leaves only rounding for the next step, and that
	//once rounded J back onto the rest with its timer reset: a transit that
	//never ended (Transit 2 s at 60 fps did it).
	{
		int stuck = 0, tried = 0;
		double worstLate = 0.0;
		for( double fps : { 60.0, 50.0, 30.0 } )
			for( int frames = static_cast< int >( 0.2 * fps ); frames <= static_cast< int >( 5.0 * fps ); ++frames )
			{
				Journey j;
				DriveInputs trip = in;
				trip.drive       = Drive::Trip;
				trip.entry       = SecondsFromParam( 0.5f );
				trip.exit        = SecondsFromParam( 0.4375f );
				trip.transit     = frames / fps;
				const double total = trip.entry + trip.transit + trip.exit;
				int f = 0;
				bool left = false, back = false;
				for( ; f < static_cast< int >( ( total + 2.0 ) * fps ); ++f )
				{
					trip.jumpPress = f == 3;
					j.Advance( ( f + 1 ) / fps - f / fps, trip );
					left = left || j.Phase() != 0.0;
					if( left && j.Phase() == 0.0 )
					{
						back = true;
						break;
					}
				}
				++tried;
				if( !back )
					++stuck;
				else
					worstLate = std::max( worstLate, std::fabs( ( f - 3 ) / fps - total ) );
			}
		Check( stuck == 0 && worstLate <= 2.0 / 30.0,
		       fmt( "Trip, %d Transit Times of whole frames at 60, 50 and 30 fps: %d never came back; the rest within %.3f s of "
		            "Entry + Transit + Exit", tried, stuck, worstLate ) );
	}
	Check( forward, "Hold, Trip and Loop never moved J backwards" );
	//Fader: J is the fader, both ways.
	{
		Journey j;
		in.drive = Drive::Fader;
		bool ok  = true;
		for( double v : { 0.0, 0.3, 0.7, 0.2, 1.0, 0.5 } )
		{
			in.fader = v;
			j.Advance( dt, in );
			ok = ok && std::fabs( j.Phase() - ( v >= 1.0 ? 0.0 : v ) ) < 1e-12;
		}
		Check( ok, "Fader: J follows Journey, forwards and back (1 is normal space again)" );
	}
	return Verdict();
}

//===========================================================================
// --clock: Resolume's clock at 499 million ms does not stop the camera.
//===========================================================================
int runClock( const Perturb& perturb )
{
	std::printf( "\n=== clock: the host's clock at 499,000 s, sent in ms (where a float resolves 0.03 s)\n" );
	Rig rig( false );
	if( !rig.Init( 320, 180 ) )
		return 1;
	rig.plugin.SetClockScaleForTest( 0.001 );
	rig.plugin.SetFloatClockForTest( perturb.floatClock );
	rig.hostUnit    = 1000.0;
	rig.clockOffset = 499000.0;
	rig.Set( PT_DRIVE, static_cast< float >( Drive::Hold ) );
	rig.Set( PT_CRUISE_SPEED, ParamFromCruise( 0.5 ) );
	//Frame by frame: the ends of a run (499,000 s and 499,002 s) are exact in
	//float, so a float clock's errors cancel over it. What an operator sees
	//is each frame's step: a float clock stalls on some and doubles on
	//others.
	if( !rig.Render( 1 ) )
		return 1;
	const double speed = CruiseFromParam( ParamFromCruise( 0.5 ) ) * kDepth;
	double before = rig.plugin.CameraState().Total(), worst = 0.0;
	for( int f = 0; f < 120; ++f )
	{
		if( !rig.Render( 1 ) )
			return 1;
		const double step = rig.plugin.CameraState().Total() - before;
		before            = rig.plugin.CameraState().Total();
		worst             = std::max( worst, std::fabs( step / ( speed / 60.0 ) - 1.0 ) );
	}
	Check( worst < 1e-6, fmt( "120 frames at Cruise %.3f D/s: every frame's step within %.1e of 1/60 s of flight (1e-6)",
	                          speed / kDepth, worst ) );
	return Verdict();
}

//===========================================================================
// --geodesic (no GL): the orbit table against the literature and a second
// integration.
//===========================================================================
int runGeodesic( const Perturb& perturb )
{
	std::printf( "\n=== geodesic: Schwarzschild's light, the table against weak field, strong field and a second integration\n" );
	const geodesic::Tables& t = geodesic::Get();
	constexpr double pi = kPi;
	//Weak field: 4/b + 15 pi / ( 4 b^2 ) + 128 / ( 3 b^3 ) (Keeton and
	//Petters 2005, M = 1).
	for( double b : { 200.0, 1000.0, 5000.0 } )
	{
		double deflection;
		if( perturb.newtonian )
		{
			double phiEnd, uEnd;
			geodesic::Integrate( b, 2.0e-3, phiEnd, uEnd, true );
			deflection = 2.0 * phiEnd - pi;
		}
		else
			deflection = geodesic::Deflection( b );
		const double want = 4.0 / b + 15.0 * pi / ( 4.0 * b * b ) + 128.0 / ( 3.0 * b * b * b );
		//5e-4: the table's rows are linear in ln( b - b_c ), 0.047 apart;
		//the curvature of 2/b over that is 5.5e-4 / b of phi.
		Check( std::fabs( deflection / want - 1.0 ) < 5e-4,
		       fmt( "b = %.0f: deflection %.9f rad, weak field to third order %.9f (5e-4)", b, deflection, want ) );
	}
	//Strong field (Bozza 2002): -ln( b / b_c - 1 ) + ln( 216 ( 7 - 4 sqrt 3 ) ) - pi,
	//good to O( b / b_c - 1 ).
	for( double e : { 1e-6, 1e-5, 1e-4 } )
	{
		const double b    = geodesic::kCritical * ( 1.0 + e );
		const double want = -std::log( e ) + std::log( 216.0 * ( 7.0 - 4.0 * std::sqrt( 3.0 ) ) ) - pi;
		double got;
		if( perturb.newtonian )
		{
			double phiEnd, uEnd;
			geodesic::Integrate( b, 2.0e-3, phiEnd, uEnd, true );
			got = 2.0 * phiEnd - pi;
		}
		else
			got = geodesic::Deflection( b );
		Check( std::fabs( got - want ) < 0.01 + 10.0 * e,
		       fmt( "b / b_c - 1 = %.0e: deflection %.6f rad (%.2f turns), Bozza's strong-deflection limit %.6f", e, got, got / ( 2 * pi ), want ) );
	}
	//The table's rows against the first integral by quadrature.
	double worstEscape = 0.0, worstCapture = 0.0;
	for( int i = 0; i < geodesic::kB; i += 37 )
	{
		const double bE = geodesic::EscapeB( i ), bC = geodesic::CaptureB( i );
		worstEscape  = std::max( worstEscape, std::fabs( t.phiEndEscape[ i ] - geodesic::QuadraturePhiEnd( bE ) ) );
		worstCapture = std::max( worstCapture, std::fabs( t.phiEndCapture[ i ] - geodesic::QuadraturePhiEnd( bC ) ) );
	}
	//1e-5 rad: RK4 at 2e-3 rad, its error grown e^phi on the near-critical
	//rows (the photon sphere is unstable at one e-fold a radian).
	Check( worstEscape < 1e-5 && worstCapture < 1e-5,
	       fmt( "14 rows a branch, RK4 in phi against quadrature of dphi = du / sqrt( F ): worst %.1e (escape), %.1e (capture) rad (1e-5)",
	            worstEscape, worstCapture ) );
	//The critical impact parameter.
	{
		double phiA, uA, phiB, uB;
		geodesic::Integrate( geodesic::kCritical * ( 1.0 + 1e-7 ), 2.0e-3, phiA, uA, perturb.newtonian );
		geodesic::Integrate( geodesic::kCritical * ( 1.0 - 1e-7 ), 2.0e-3, phiB, uB, perturb.newtonian );
		Check( uA < 1.0 / 3.0 + 1e-3 && uB == 0.5,
		       fmt( "b = sqrt 27 (1 +- 1e-7): turns at r = %.4f after %.2f rad / falls in after %.2f rad", 1.0 / uA, phiA, phiB ) );
	}
	return Verdict();
}

//===========================================================================
// --shadow: the black disc is asin( sqrt 27 sqrt( 1 - 2/d ) / d ) across.
//===========================================================================
int runShadow( const Perturb& perturb )
{
	std::printf( "\n=== shadow: the black disc's size in the picture, against Synge's closed form\n" );
	for( const Raster& raster : kRasters )
		for( float size : { 0.25f, 0.6f } )
		{
			Rig rig( false );
			if( !rig.Init( raster.w, raster.h ) )
				return 1;
			rig.plugin.SetCaptureLawForTest( !perturb.flatCapture );
			rig.Set( PT_LOOK, static_cast< float >( Look::BlackHole ) );
			rig.Set( PT_DRIVE, static_cast< float >( Drive::Fader ) );
			rig.Set( PT_JOURNEY, 0.5f );
			rig.Set( PT_DISC, 0.0f );
			rig.Set( PT_CORONA, 0.0f );
			rig.Set( PT_STARS, 0.0f );
			rig.Set( PT_HOLE_SIZE, size );
			if( !rig.Render( 3 ) )
				return 1;
			const Floats scene = rig.SceneExposure();
			long count         = 0;
			for( size_t i = 3; i < scene.size(); i += 4 )
				count += scene[ i ] > 0.5f;
			const double d      = rig.plugin.HoleDistance();
			const double r      = std::sqrt( count / kPi );
			const double angle  = ShadowAngle( d );
			const double want   = rig.plugin.LastFocal() * std::tan( angle );
			const double horizon = std::asin( 2.0 / d );
			//Half a pixel: the area of a disc on a pixel grid, as a radius.
			Check( std::fabs( r - want ) < 0.5,
			       fmt( "%dx%d, d = %.3f: shadow %.2f px across, asin( sqrt 27 sqrt( 1 - 2/d ) / d ) gives %.2f px (0.5 px); %.2f times "
			            "the horizon's own angle", raster.w, raster.h, d, r, want, angle / horizon ) );
		}
	return Verdict();
}

/// The harness's own light ray, by RK4 in alpha from the camera at u = 1/d
/// with impact parameter b, inward: u'' = -u + 3u^2. Calls `visit( alpha, u,
/// du )` each step; stops when visit returns false, at escape (u <= 0), at
/// the horizon (u >= 1/2) or at alphaMax. Returns how it ended: 1 escaped,
/// 2 captured, 0 stopped.
template< typename Visit >
int traceRay( double d, double b, bool inward, double alphaMax, double h, Visit visit, double* uAtEnd = nullptr )
{
	double u = 1.0 / d;
	const double f0 = 1.0 / ( b * b ) - u * u + 2.0 * u * u * u;
	double w = ( inward ? 1.0 : -1.0 ) * std::sqrt( std::max( f0, 0.0 ) );
	double alpha = 0.0;
	auto acc = []( double x ) { return -x + 3.0 * x * x; };
	while( alpha < alphaMax )
	{
		if( !visit( alpha, u, w ) )
			return 0;
		const double k1u = w, k1w = acc( u );
		const double k2u = w + 0.5 * h * k1w, k2w = acc( u + 0.5 * h * k1u );
		const double k3u = w + 0.5 * h * k2w, k3w = acc( u + 0.5 * h * k2u );
		const double k4u = w + h * k3w, k4w = acc( u + h * k3u );
		u += h / 6.0 * ( k1u + 2 * k2u + 2 * k3u + k4u );
		w += h / 6.0 * ( k1w + 2 * k2w + 2 * k3w + k4w );
		alpha += h;
		if( u <= 0.0 )
			return 1;
		if( u >= 0.5 )
			return 2;
	}
	if( uAtEnd )
		*uAtEnd = u;
	return 0;
}

//===========================================================================
// --einstein: a point behind the hole becomes a ring.
//===========================================================================
int runEinstein( const Perturb& perturb )
{
	std::printf( "\n=== einstein: a dot straight behind the hole (the Over's clip) and the ring it becomes\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		Floats card( static_cast< size_t >( w ) * h * 4, 0.0f );
		const double dotSigma = 1.5;
		for( int j = 0; j < h; ++j )
			for( int i = 0; i < w; ++i )
			{
				const double x = i + 0.5 - 0.5 * w, y = j + 0.5 - 0.5 * h;
				const float v  = static_cast< float >( std::exp( -0.5 * ( x * x + y * y ) / ( dotSigma * dotSigma ) ) );
				float* o       = &card[ ( static_cast< size_t >( j ) * w + i ) * 4 ];
				o[ 0 ] = o[ 1 ] = o[ 2 ] = v;
				o[ 3 ]                   = 1.0f;
			}
		Rig rig( true );
		if( !rig.Init( w, h, &card ) )
			return 1;
		rig.plugin.SetBendLawForTest( !perturb.straightRays );
		rig.Set( PT_LOOK, static_cast< float >( Look::BlackHole ) );
		rig.Set( PT_DRIVE, static_cast< float >( Drive::Fader ) );
		rig.Set( PT_JOURNEY, 0.5f );
		rig.Set( PT_DISC, 0.0f );
		rig.Set( PT_CORONA, 0.0f );
		rig.Set( PT_STARS, 0.0f );
		rig.Set( PT_HOLE_SIZE, 0.06f );
		if( !rig.Render( 3 ) )
			return 1;
		const double d = rig.plugin.HoleDistance(), f = rig.plugin.LastFocal();
		//The ring: the angle psi whose ray leaves straight away from the
		//camera, behind the hole. The harness's own ray, to infinity.
		auto escapeAngle = [ & ]( double psi ) {
			const double b = d * std::sin( psi ) / std::sqrt( 1.0 - 2.0 / d );
			double last    = 0.0;
			const int end  = traceRay( d, b, true, 20.0, 2.0e-4, [ & ]( double a, double, double ) {
                last = a;
                return true;
            } );
			return end == 1 ? last : -1.0;
		};
		double lo = std::asin( std::min( 1.0, 1.01 * std::sqrt( 27.0 ) * std::sqrt( 1.0 - 2.0 / d ) / d ) ), hi = 0.6;
		for( int i = 0; i < 50; ++i )
		{
			const double mid = 0.5 * ( lo + hi );
			( escapeAngle( mid ) > kPi ? lo : hi ) = mid;
		}
		const double want = f * std::tan( 0.5 * ( lo + hi ) );
		const Floats scene = rig.SceneExposure();
		double sum = 0.0, moment = 0.0;
		for( int j = 0; j < h; ++j )
			for( int i = 0; i < w; ++i )
			{
				const double r = std::hypot( i + 0.5 - 0.5 * w, j + 0.5 - 0.5 * h );
				if( r < 0.75 * want || r > 1.25 * want )
					continue;
				const double v = luminance( &scene[ ( static_cast< size_t >( j ) * w + i ) * 4 ] );
				sum += v;
				moment += v * r;
			}
		const double got = sum > 0.0 ? moment / sum : 0.0;
		std::printf( "  %dx%d: d = %.2f; weak field puts the ring at sqrt( 4/d ) = %.2f px\n", w, h, d, f * std::tan( std::sqrt( 4.0 / d ) ) );
		//Half a pixel: a ring's centroid on a pixel grid.
		Check( sum > 1.0 && std::fabs( got - want ) < 0.5,
		       fmt( "the ring's radius %.2f px; the harness's own ray (RK4 from the camera, out to infinity) puts it at %.2f px (0.5 px)",
		            got, want ) );
	}
	return Verdict();
}

//===========================================================================
// --disc: the shift, the side that approaches, the face-on profile.
//===========================================================================
int runDisc( const Perturb& perturb )
{
	std::printf( "\n=== disc: the accretion disc's light, its Doppler side and its gravitational shift\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		auto shoot = [ & ]( float doppler, float tiltParam, Floats& scene, Rig*& keep ) -> bool {
			keep = new Rig( false );
			Rig& rig = *keep;
			if( !rig.Init( w, h ) )
				return false;
			rig.plugin.SetDiscTextureForTest( false );
			rig.plugin.SetSpinSignForTest( perturb.spinFlipped ? -1.0f : 1.0f );
			rig.Set( PT_LOOK, static_cast< float >( Look::BlackHole ) );
			rig.Set( PT_DRIVE, static_cast< float >( Drive::Fader ) );
			rig.Set( PT_JOURNEY, 0.5f );
			rig.Set( PT_CORONA, 0.0f );
			rig.Set( PT_STARS, 0.0f );
			rig.Set( PT_DOPPLER, doppler );
			rig.Set( PT_DISC_TILT, tiltParam );
			if( !rig.Render( 3 ) )
				return false;
			scene = rig.SceneExposure();
			return true;
		};
		//1. Doppler 0: left and right mirror each other.
		{
			Floats scene;
			Rig* rig = nullptr;
			if( !shoot( 0.0f, 0.9f, scene, rig ) )
				return 1;
			double worst = 0.0, peak = 0.0;
			for( int j = 0; j < h; ++j )
				for( int i = 0; i < w / 2; ++i )
				{
					const double a = luminance( &scene[ ( static_cast< size_t >( j ) * w + i ) * 4 ] );
					const double b = luminance( &scene[ ( static_cast< size_t >( j ) * w + ( w - 1 - i ) ) * 4 ] );
					worst          = std::max( worst, std::fabs( a - b ) );
					peak           = std::max( peak, std::max( a, b ) );
				}
			delete rig;
			//Half float's 1/1024 of the brightest pixel.
			Check( peak > 0.05 && worst < peak / 1024.0 + 1e-6,
			       fmt( "%dx%d, Doppler 0: mirror-symmetric, worst %.2e against a brightest %.3f", w, h, worst, peak ) );
		}
		//2. Doppler 1: the side moving towards the camera, from the geometry
		//alone, is the brighter and the bluer.
		{
			Floats scene;
			Rig* rig = nullptr;
			if( !shoot( 1.0f, 0.9f, scene, rig ) )
				return 1;
			delete rig;
			const double tilt = DiscTiltFromParam( 0.9f ) * kPi / 180.0;
			//The disc turns anticlockwise about its normal ( 0, sin i, -cos i );
			//the point on its +x side moves along normal x x-hat; the camera is
			//at -z from the hole.
			const double nx = 0.0, ny = std::sin( tilt ), nz = -std::cos( tilt );
			const double vx = ny * 0.0 - nz * 0.0, vy = nz * 1.0 - nx * 0.0, vz = nx * 0.0 - ny * 1.0;
			( void )vx;
			( void )vy;
			const bool rightApproaches = -vz > 0.0;
			double lum[ 2 ] = {}, blue[ 2 ] = {}, red[ 2 ] = {};
			for( int j = 0; j < h; ++j )
				for( int i = 0; i < w; ++i )
				{
					const float* e = &scene[ ( static_cast< size_t >( j ) * w + i ) * 4 ];
					const int side = i < w / 2 ? 0 : 1;
					lum[ side ] += luminance( e );
					blue[ side ] += e[ 2 ];
					red[ side ] += e[ 0 ];
				}
			const int bright = rightApproaches ? 1 : 0, dim = 1 - bright;
			Check( lum[ bright ] > 1.3 * lum[ dim ] && blue[ bright ] / red[ bright ] > blue[ dim ] / red[ dim ],
			       fmt( "%dx%d, Doppler 1: the %s side approaches (from the geometry); it holds %.2f times the other's light, blue / red "
			            "%.3f against %.3f", w, h, rightApproaches ? "right" : "left", lum[ bright ] / std::max( lum[ dim ], 1e-9 ),
			            blue[ bright ] / red[ bright ], blue[ dim ] / red[ dim ] ) );
		}
		//3. Face on: the profile along a radius against the harness's own
		//rays, the Shakura-Sunyaev temperatures and the shift sqrt( 1 - 3/r ).
		{
			Floats scene;
			Rig* rig = nullptr;
			if( !shoot( 1.0f, 0.0f, scene, rig ) )
				return 1;
			const double d = rig->plugin.HoleDistance(), f = rig->plugin.LastFocal(), light = rig->plugin.LastDiscLight();
			const double tMax = DiscHeatFromParam( rig->plugin.GetById( PT_DISC_HEAT ) );
			const double outer = DiscOuterFromParam( rig->plugin.GetById( PT_DISC_SIZE ) );
			delete rig;
			const double lapse = std::sqrt( 1.0 - 2.0 / d );
			const double peak  = 49.0 / 36.0 * 6.0;
			auto profile = [ & ]( double r ) {
				const double fr = ( 1.0 - std::sqrt( 6.0 / r ) ) / ( r * r * r );
				const double fm = ( 1.0 - std::sqrt( 6.0 / peak ) ) / ( peak * peak * peak );
				return std::pow( std::max( fr / fm, 0.0 ), 0.25 );
			};
			const double y0 = blackbody::RelativeLuminance( tMax );
			double worst = 0.0;
			int used     = 0;
			const int rows = static_cast< int >( 0.5 * h );
			for( int k = 2; k < rows; ++k )
			{
				const double rpx = k + 0.5;
				const double psi = std::atan( rpx / f );
				const double b   = d * std::sin( psi ) / lapse;
				//Face on, the disc's plane is the one at right angles to the line
				//of sight: the ray meets it a quarter turn round the hole.
				double rHit = -1.0, uEnd = 0.0;
				const int steps = 16000;
				if( traceRay( d, b, true, kPi / 2.0 - 0.5 * ( kPi / 2.0 ) / steps, ( kPi / 2.0 ) / steps,
				              []( double, double, double ) { return true; }, &uEnd ) == 0 )
					rHit = 1.0 / uEnd;
				if( std::getenv( "SLIPTEST_TRACE" ) && k % 20 == 0 )
					std::printf( "    %.1f px: psi %.4f b %.3f -> r %.3f\n", rpx, psi, b, rHit );
				if( rHit < 7.0 || rHit > 0.5 * outer )
					continue;
				const double g    = perturb.noGravityShift ? 1.0 / lapse : std::sqrt( 1.0 - 3.0 / rHit ) / lapse;
				const double want = light * blackbody::RelativeLuminance( g * tMax * profile( rHit ) ) / y0;
				//The azimuthal mean at this radius.
				double sum = 0.0;
				int n      = 0;
				for( int j = 0; j < h; ++j )
					for( int i = 0; i < w; ++i )
					{
						const double r = std::hypot( i + 0.5 - 0.5 * w, j + 0.5 - 0.5 * h );
						if( std::fabs( r - rpx ) < 0.5 )
						{
							sum += luminance( &scene[ ( static_cast< size_t >( j ) * w + i ) * 4 ] );
							++n;
						}
					}
				if( n == 0 || want <= 0.0 )
					continue;
				worst = std::max( worst, std::fabs( sum / n / want - 1.0 ) );
				++used;
			}
			//3%: the table's linear steps in log T (256 entries over a factor
			//150), half float, and the ring of pixels averaged over a radius
			//that changes across a pixel.
			Check( used >= 3 && worst < 0.03,
			       fmt( "%dx%d, face on: the disc's light along a radius at %d radii (r 7 to %.0f) against the harness's own rays, "
			            "Shakura-Sunyaev and sqrt( 1 - 3/r ): worst %.2f%% (3%%)", w, h, used, 0.5 * outer, 100.0 * worst ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --ring: the corona, integrated along the bent rays, and the photon ring
// it makes without being drawn.
//===========================================================================
int runRing( const Perturb& perturb )
{
	std::printf( "\n=== ring: the corona's glow along each bent ray, and the photon ring that falls out\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		Rig rig( false );
		if( !rig.Init( w, h ) )
			return 1;
		rig.plugin.SetBendLawForTest( !perturb.straightRays );
		rig.Set( PT_LOOK, static_cast< float >( Look::BlackHole ) );
		rig.Set( PT_DRIVE, static_cast< float >( Drive::Fader ) );
		rig.Set( PT_JOURNEY, 0.5f );
		rig.Set( PT_DISC, 0.0f );
		rig.Set( PT_CORONA, 1.0f );
		rig.Set( PT_STARS, 0.0f );
		rig.Set( PT_HOLE_SIZE, 0.3f );
		if( !rig.Render( 3 ) )
			return 1;
		//Disc 0 leaves the corona's light: the script's disc share is 1 in
		//transit, so the plugin's CoronaLight is Corona x exposure x gain.
		const double d = rig.plugin.HoleDistance(), f = rig.plugin.LastFocal(), light = rig.plugin.LastCoronaLight();
		const Floats scene = rig.SceneExposure();
		const double lapse = std::sqrt( 1.0 - 2.0 / d );
		const double shadowPx = f * std::tan( ShadowAngle( d ) );
		std::vector< double > sum( static_cast< size_t >( h ), 0.0 );
		std::vector< int > count( static_cast< size_t >( h ), 0 );
		for( int j = 0; j < h; ++j )
			for( int i = 0; i < w; ++i )
			{
				const int r = static_cast< int >( std::hypot( i + 0.5 - 0.5 * w, j + 0.5 - 0.5 * h ) );
				if( r < h )
				{
					sum[ static_cast< size_t >( r ) ] += luminance( &scene[ ( static_cast< size_t >( j ) * w + i ) * 4 ] );
					++count[ static_cast< size_t >( r ) ];
				}
			}
		double worst = 0.0;
		int used     = 0, peakAt = 0;
		double peak  = 0.0;
		for( int k = 1; k < h / 2; ++k )
		{
			const double mean = count[ k ] ? sum[ k ] / count[ k ] : 0.0;
			if( mean > peak )
			{
				peak   = mean;
				peakAt = k;
			}
			const double rpx = k + 0.5;
			if( std::fabs( rpx - shadowPx ) < 4.0 || rpx > 2.5 * shadowPx || rpx < 0.3 * shadowPx )
				continue;
			const double psi = std::atan( rpx / f );
			const double b   = d * std::sin( psi ) / lapse;
			//The harness's integral: j = 27 u^3, each emitter's light shifted
			//by ( ( 1 - 2u ) / lapse^2 )^2, along dl = sqrt( u'^2 + u^2 ) / u^2,
			//to escape, the horizon or four turns.
			double glow = 0.0;
			const double step = 1.0e-3;
			traceRay( d, b, true, 8.0 * kPi, step, [ & ]( double, double u, double du ) {
				const double gg = std::max( 1.0 - 2.0 * u, 0.0 ) / ( lapse * lapse );
				glow += 27.0 * u * std::sqrt( du * du + u * u ) * gg * gg * step;
				return true;
			} );
			const double want = light * glow;
			const double got  = sum[ k ] / count[ k ];
			worst = std::max( worst, std::fabs( got / want - 1.0 ) );
			++used;
		}
		//4%: the plugin's 32-point midpoint rule along the ray against a
		//1e-3 rad RK4, averaged over a ring of pixels.
		Check( used > 5 && worst < 0.04,
		       fmt( "%dx%d: the glow at %d radii against the harness's own integral along its own rays: worst %.2f%% (4%%)", w, h, used,
		            100.0 * worst ) );
		Check( std::fabs( peakAt + 0.5 - shadowPx ) < 3.0,
		       fmt( "the brightest ring is %d px from the vanishing point, the shadow's edge %.1f px: the photon ring, drawn by nothing but "
		            "the path length", peakAt, shadowPx ) );
	}
	return Verdict();
}

//===========================================================================
// --planck (no GL): the blackbody table's colours on the Planckian locus.
//===========================================================================
int runPlanck( const Perturb& perturb )
{
	std::printf( "\n=== planck: Planck x CIE 1931 (Wyman, Sloan and Shirley's fit) against Kang et al.'s Planckian locus\n" );
	auto locus = []( double t, double& x, double& y ) {
		if( t <= 4000.0 )
			x = -0.2661239e9 / ( t * t * t ) - 0.2343589e6 / ( t * t ) + 0.8776956e3 / t + 0.179910;
		else
			x = -3.0258469e9 / ( t * t * t ) + 2.1070379e6 / ( t * t ) + 0.2226347e3 / t + 0.240390;
		if( t <= 2222.0 )
			y = -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - 0.20219683;
		else if( t <= 4000.0 )
			y = -0.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - 0.16748867;
		else
			y = 3.0817580 * x * x * x - 5.87338670 * x * x + 3.75112997 * x - 0.37001483;
	};
	//Distance in CIE 1960 uv, the plane lighting measures "on the locus" in.
	auto uv = []( double x, double y, double& u, double& v ) {
		const double den = -2.0 * x + 12.0 * y + 3.0;
		u                = 4.0 * x / den;
		v                = 6.0 * y / den;
	};
	double worst = 0.0;
	std::string what;
	for( double t : { 2000.0, 3000.0, 4000.0, 5000.0, 6500.0, 10000.0, 20000.0 } )
	{
		double x, y, kx, ky, u, v, ku, kv;
		blackbody::Chromaticity( t, x, y );
		locus( perturb.hotterLocus ? 1.2 * t : t, kx, ky );
		uv( x, y, u, v );
		uv( kx, ky, ku, kv );
		const double e = std::hypot( u - ku, v - kv );
		worst          = std::max( worst, e );
		what += fmt( " %.0fK %.4f", t, e );
	}
	//0.006 in uv: the +-0.006 Duv ANSI C78.377 allows a lamp sold as a
	//colour temperature (Duv is the distance across the locus; this is the
	//whole distance, so it is the stricter). Kang et al.'s spline is good to 1e-4;
	//the error is Wyman et al.'s fit, whose x-bar is half the CIE table's at
	//700 nm, which is where a 2000 K source's colour is.
	Check( worst < 0.006, fmt( "distance from the locus in CIE 1960 uv:%s (0.006, ANSI C78.377's Duv)", what.c_str() ) );
	return Verdict();
}

//===========================================================================
// --seam: no seam where atan's branch cut runs (Wormhole).
//===========================================================================
int runSeam( const Perturb& perturb )
{
	std::printf( "\n=== seam: the clouds across the branch cut (left of the vanishing point) and across the right\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		Rig rig( false );
		if( !rig.Init( w, h ) )
			return 1;
		rig.plugin.SetSeamForTest( perturb.seam );
		rig.Set( PT_LOOK, static_cast< float >( Look::Wormhole ) );
		rig.Set( PT_DRIVE, static_cast< float >( Drive::Fader ) );
		rig.Set( PT_JOURNEY, 0.5f );
		rig.Set( PT_STARS, 0.0f );
		if( !rig.Render( 3 ) )
			return 1;
		const Floats scene = rig.SceneExposure();
		const int above = h / 2, below = h / 2 - 1;
		double left = 0.0, right = 0.0, scale = 0.0;
		int n = 0;
		for( int i = w / 8; i < w / 2 - w / 16; ++i )
		{
			const int mirror = w - 1 - i;
			auto at = [ & ]( int x, int y ) { return luminance( &scene[ ( static_cast< size_t >( y ) * w + x ) * 4 ] ); };
			left += std::fabs( at( i, above ) - at( i, below ) );
			right += std::fabs( at( mirror, above ) - at( mirror, below ) );
			scale += at( i, above ) + at( mirror, above );
			++n;
		}
		//Neighbouring rows differ by the clouds' own gradient on both sides;
		//a seam is a jump. Three times the right side's difference is room
		//for the texture's own variation from one side to the other.
		Check( left < 3.0 * right + 1e-3 * scale,
		       fmt( "%dx%d: mean step between the rows either side of the cut %.4f, of the same rows on the right %.4f", w, h, left / n,
		            right / n ) );
	}
	return Verdict();
}

//===========================================================================
// --resize: the flight carries on through a change of raster.
//===========================================================================
int runResize( const Perturb& perturb )
{
	std::printf( "\n=== resize: the host's raster changes under a running instance (no InitGL, as Arena does)\n" );
	Rig a( false ), b( false );
	if( !a.Init( 640, 360 ) || !b.Init( 320, 180 ) )
		return 1;
	if( !a.Render( 150 ) || !a.Resize( 320, 180 ) || !a.Render( 1 ) )
		return 1;
	if( !b.Render( perturb.resizeLate ? 152 : 151 ) )
		return 1;
	const Floats x = a.Output(), y = b.Output();
	double worst = 0.0;
	for( size_t i = 0; i < x.size(); ++i )
		worst = std::max( worst, static_cast< double >( std::fabs( x[ i ] - y[ i ] ) ) );
	Check( worst == 0.0 && a.plugin.JourneyState().Phase() == b.plugin.JourneyState().Phase(),
	       fmt( "150 frames at 640x360 then one at 320x180, against 151 at 320x180 from the start: J %.4f and %.4f, worst pixel "
	            "difference %.2e", a.plugin.JourneyState().Phase(), b.plugin.JourneyState().Phase(), worst ) );
	return Verdict();
}

//===========================================================================
// --state: the GL state the host hands over is the state it gets back.
//===========================================================================
int runState( const Perturb& )
{
	std::printf( "\n=== state: the GL state the host hands over is the state it gets back\n" );
	for( bool effect : { false, true } )
		for( int look = 0; look < static_cast< int >( Look::Count ); ++look )
		{
			Rig rig( effect );
			if( !rig.Init( 320, 180 ) )
				return 1;
			rig.Set( PT_LOOK, static_cast< float >( look ) );
			rig.Set( PT_DRIVE, static_cast< float >( Drive::Fader ) );
			rig.Set( PT_JOURNEY, 0.4f );
			GLuint hostArray = 0, hostBuffer = 0;
			glGenVertexArrays( 1, &hostArray );
			glGenBuffers( 1, &hostBuffer );
			int problems = 0;
			std::string what;
			for( int frame = 0; frame < 3; ++frame )
			{
				glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
				glViewport( 7, 5, 300, 170 );
				glBindVertexArray( hostArray );
				glBindBuffer( GL_ARRAY_BUFFER, hostBuffer );
				glEnable( GL_BLEND );
				glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO );
				glClearColor( 0.2f, 0.3f, 0.4f, 0.5f );
				glEnable( GL_SCISSOR_TEST );
				glScissor( 0, 0, 320, 180 );
				glActiveTexture( GL_TEXTURE0 );
				glUseProgram( 0 );
				rig.plugin.SetTime( frame / 60.0 );
				if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
					return 1;
				GLint viewport[ 4 ] = {}, array = 0, buffer = 0, program = 0, unit = 0, fbo = 0, src = 0, dst = 0;
				GLfloat clear[ 4 ]   = {};
				GLboolean mask[ 4 ]  = {};
				glGetIntegerv( GL_VIEWPORT, viewport );
				glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &array );
				glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &buffer );
				glGetIntegerv( GL_CURRENT_PROGRAM, &program );
				glGetIntegerv( GL_ACTIVE_TEXTURE, &unit );
				glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fbo );
				glGetIntegerv( GL_BLEND_SRC_RGB, &src );
				glGetIntegerv( GL_BLEND_DST_RGB, &dst );
				glGetFloatv( GL_COLOR_CLEAR_VALUE, clear );
				glGetBooleanv( GL_COLOR_WRITEMASK, mask );
				auto expect = [ & ]( bool ok, const char* name ) {
					if( !ok )
					{
						++problems;
						what += std::string( " " ) + name;
					}
				};
				expect( viewport[ 0 ] == 7 && viewport[ 1 ] == 5 && viewport[ 2 ] == 300 && viewport[ 3 ] == 170, "viewport" );
				expect( array == static_cast< GLint >( hostArray ), "vertex-array" );
				expect( buffer == static_cast< GLint >( hostBuffer ), "array-buffer" );
				expect( program == 0, "program" );
				expect( unit == GL_TEXTURE0, "active-unit" );
				expect( fbo == static_cast< GLint >( rig.outputFBO ), "framebuffer" );
				expect( glIsEnabled( GL_BLEND ) && src == GL_SRC_ALPHA && dst == GL_ONE_MINUS_SRC_ALPHA, "blend" );
				expect( glIsEnabled( GL_SCISSOR_TEST ), "scissor" );
				expect( clear[ 0 ] == 0.2f && clear[ 1 ] == 0.3f && clear[ 2 ] == 0.4f && clear[ 3 ] == 0.5f, "clear-colour" );
				expect( mask[ 0 ] && mask[ 1 ] && mask[ 2 ] && mask[ 3 ], "colour-mask" );
				for( int u = 0; u < 10; ++u )
				{
					GLint bound = 0;
					glActiveTexture( static_cast< GLenum >( GL_TEXTURE0 + u ) );
					glGetIntegerv( GL_TEXTURE_BINDING_2D, &bound );
					expect( bound == 0, "texture-unit" );
				}
				glActiveTexture( GL_TEXTURE0 );
			}
			glDisable( GL_SCISSOR_TEST );
			glDisable( GL_BLEND );
			glBindVertexArray( 0 );
			glBindBuffer( GL_ARRAY_BUFFER, 0 );
			glDeleteVertexArrays( 1, &hostArray );
			glDeleteBuffers( 1, &hostBuffer );
			Check( problems == 0, fmt( "%s, %s, three frames: viewport, vertex array, array buffer, program, active unit, framebuffer, "
			                           "blend, scissor, clear colour, colour mask, ten texture units (%d wrong:%s)",
			                           effect ? "Over" : "source", LookName( static_cast< Look >( look ) ), problems,
			                           what.empty() ? " none" : what.c_str() ) );
		}
	return Verdict();
}

//===========================================================================
// --names (no GL)
//===========================================================================
int runNames( const Perturb& )
{
	std::printf( "\n=== names: every parameter unique (as Arena addresses them, too) and within FFGL's 16 characters\n" );
	for( bool effect : { false, true } )
	{
		SlipstreamPlugin plugin( effect );
		std::map< std::string, int > seen, address;
		int longNames = 0, dupes = 0, clashes = 0;
		for( unsigned int i = 0; i < plugin.ParamCount(); ++i )
		{
			const std::string name = plugin.GetParamName( i ) ? plugin.GetParamName( i ) : "";
			if( name.size() > 16 )
			{
				std::printf( "    too long: %s\n", name.c_str() );
				++longNames;
			}
			if( seen[ name ]++ > 0 )
				++dupes;
			std::string key;
			for( char c : name )
				if( c != ' ' )
					key += static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) );
			if( address[ key ]++ > 0 )
				++clashes;
		}
		const unsigned int aboutFirst = plugin.ParamCount() - stoatworks::about::kParamCount;
		const bool aboutLast          = std::string( plugin.GetParamName( aboutFirst ) ) == "About";
		Check( longNames == 0 && dupes == 0 && clashes == 0 && aboutLast,
		       fmt( "%s: %u parameters, %d too long, %d duplicated, %d clashing addresses; the About block is last (%s)",
		            effect ? "SW Slip Over" : "SW Slipstream", plugin.ParamCount(), longNames, dupes, clashes, aboutLast ? "yes" : "NO" ) );
	}
	return Verdict();
}

//===========================================================================
// --cues (no GL)
//===========================================================================
int runCues( const Perturb& perturb )
{
	std::printf( "\n=== cues: a cue sheet steps options, booleans, events and integers, and ramps standard controls\n" );
	std::istringstream sheet( "0 Look 0\n60 Look 3\n0 Engage 0\n30 Engage 1\n"
	                          "0 Stars 100\n60 Stars 9000\n0 Journey 0\n60 Journey 1\n0 Jump 0\n30 Jump 1\n" );
	std::string error;
	const auto tracks = loadScript( sheet, "cues", error );
	SlipstreamPlugin plugin( false );
	int bad = 0;
	std::string what;
	for( const NamedParameter& p : listParameters( plugin ) )
	{
		const auto found = tracks.find( p.name );
		if( found == tracks.end() )
			continue;
		const bool ramp       = perturb.cuesRamp ? true : !stepsBetweenCues( p.type );
		const float mid       = valueAt( found->second, 29, ramp );
		const float a         = found->second.front().second, b = found->second.back().second;
		const int bFrame      = found->second.back().first;
		const float atB       = valueAt( found->second, bFrame, ramp );
		const bool expectRamp = p.type == FF_TYPE_STANDARD;
		const bool ok = expectRamp ? ( mid > a && mid < b && atB == b ) : ( mid == a && valueAt( found->second, bFrame - 1, ramp ) == a && atB == b );
		bad += !ok;
		what += fmt( " %s %s(%g at frame 29)", p.name.c_str(), expectRamp ? "ramps " : "steps ", mid );
	}
	Check( error.empty() && bad == 0, fmt( "%d wrong:%s", bad, what.c_str() ) );
	return Verdict();
}

//---------------------------------------------------------------------------
const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "streak", runStreak, false, "a streak's length and light, against a simulated shutter" },
		{ "energy", runEnergy, false, "a star's whole light at any speed" },
		{ "copies", runCopies, false, "hyperspace's steady lines: the periodic field" },
		{ "identity", runIdentity, false, "the Over at rest returns the clip" },
		{ "smear", runSmear, false, "a point of the clip, smeared inward, 1/r" },
		{ "journey", runJourney, true, "the drives' timings; J only forward" },
		{ "clock", runClock, false, "Resolume's clock at 499 million ms" },
		{ "geodesic", runGeodesic, true, "the orbit table against the literature" },
		{ "shadow", runShadow, false, "the shadow's size in the picture" },
		{ "einstein", runEinstein, false, "the Einstein ring of a point behind the hole" },
		{ "disc", runDisc, false, "the disc's Doppler side and gravitational shift" },
		{ "ring", runRing, false, "the corona along bent rays; the photon ring" },
		{ "planck", runPlanck, true, "the blackbody colours on the Planckian locus" },
		{ "seam", runSeam, false, "no seam at the branch cut" },
		{ "resize", runResize, false, "the flight through a change of raster" },
		{ "state", runState, false, "the host's GL state put back" },
		{ "names", runNames, true, "parameter names" },
		{ "cues", runCues, true, "the cue sheet" },
	};
	return list;
}

std::string CheckList()
{
	std::string out = "  checks:";
	int column      = 10;
	for( const CheckEntry& c : checks() )
	{
		const std::string flag = std::string( " --" ) + c.flag + ( c.offline ? "*" : "" );
		if( column + flag.size() > 90 )
		{
			out += "\n         ";
			column = 9;
		}
		out += flag;
		column += static_cast< int >( flag.size() );
	}
	return out + "\n          (* needs no GL)\n";
}

bool isOffline( const std::string& flag )
{
	for( const CheckEntry& c : checks() )
		if( flag == c.flag )
			return c.offline;
	return false;
}

//===========================================================================
// --negative
//===========================================================================
int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	add( "streak", runStreak, "a star's flux held at its shutter-close value (no inverse square)", []( Perturb& p ) { p.constantFlux = true; } );
	add( "energy", runEnergy, "a streak's light not spread over its length", []( Perturb& p ) { p.unspread = true; } );
	add( "copies", runCopies, "the field's periodic copies not counted", []( Perturb& p ) { p.noCopies = true; } );
	add( "identity", runIdentity, "the clip's way into exposure skipped", []( Perturb& p ) { p.noRoundTrip = true; } );
	add( "smear", runSmear, "expect the smear to run outward from the point", []( Perturb& p ) { p.smearOutward = true; } );
	add( "journey", runJourney, "expect an abandoned jump to run back to normal space", []( Perturb& p ) { p.abandonReturns = true; } );
	add( "clock", runClock, "elapsed time from the host clock in float", []( Perturb& p ) { p.floatClock = true; } );
	add( "geodesic", runGeodesic, "the orbit equation without its 3u^2 (Newton's straight line)", []( Perturb& p ) { p.newtonian = true; } );
	add( "shadow", runShadow, "capture below b = 2, flat space's horizon", []( Perturb& p ) { p.flatCapture = true; } );
	add( "einstein", runEinstein, "straight rays", []( Perturb& p ) { p.straightRays = true; } );
	add( "disc", runDisc, "the disc's spin flipped in its Doppler shift only", []( Perturb& p ) { p.spinFlipped = true; } );
	add( "disc", runDisc, "expect no gravitational shift", []( Perturb& p ) { p.noGravityShift = true; } );
	add( "ring", runRing, "straight rays through the corona", []( Perturb& p ) { p.straightRays = true; } );
	add( "planck", runPlanck, "compare against the locus 20% hotter", []( Perturb& p ) { p.hotterLocus = true; } );
	add( "seam", runSeam, "the clouds' lattice one cell short of closing round", []( Perturb& p ) { p.seam = true; } );
	add( "resize", runResize, "compare with a run one frame on", []( Perturb& p ) { p.resizeLate = true; } );
	add( "cues", runCues, "ramp every control between keys", []( Perturb& p ) { p.cuesRamp = true; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}

/// A DRAFT of the fleet Arena gate's expectation (plugin-bench arena/expect),
/// from what the plugins really declare: defaults are the floats the
/// constructors set, not rounded literals (containment's trap). The `needs`,
/// `inert` and `probe` fields are added by hand in plugin-bench, from
/// tools/sweep.py's contexts.
int runExpect()
{
	auto escape = []( const std::string& s ) {
		std::string out;
		for( char c : s )
		{
			if( c == '"' || c == '\\' )
				out += '\\';
			out += c;
		}
		return out;
	};
	std::printf( "{\n  \"plugin\": \"slipstream\",\n  \"dlls\": [\"Slipstream.dll\", \"Slipstream Over.dll\"],\n"
	             "  \"register\": [\n    {\"name\": \"SW Slipstream\", \"uid\": \"SR01\", \"kind\": \"source\"},\n"
	             "    {\"name\": \"SW Slip Over\", \"uid\": \"SR02\", \"kind\": \"effect\"}\n  ],\n  \"params\": {\n" );
	for( bool over : { false, true } )
	{
		SlipstreamPlugin plugin( over );
		std::printf( "    \"%s\": [\n", over ? "SW Slip Over" : "SW Slipstream" );
		const std::vector< NamedParameter > list = listParameters( plugin );
		for( size_t i = 0; i < list.size(); ++i )
		{
			const NamedParameter& p = list[ i ];
			std::string line        = "      {\"name\": \"" + escape( p.name ) + "\", ";
			if( p.type == FF_TYPE_OPTION )
				line += "\"type\": \"ParamChoice\", \"default\": \""
				        + escape( plugin.GetParamElementName( p.index, static_cast< unsigned int >( std::lround( p.value ) ) ) ) + "\"";
			else if( p.type == FF_TYPE_BOOLEAN )
				line += std::string( "\"type\": \"ParamBoolean\", \"default\": " ) + ( p.value > 0.5f ? "true" : "false" );
			else if( p.type == FF_TYPE_EVENT )
			{
				line += "\"type\": \"ParamEvent\"";
				//FFGL's name field is 16 characters and Arena shows what it is
				//given: a name of exactly 16 is declared as itself.
				if( p.name.size() >= 16 )
					line += ", \"declared\": \"" + escape( p.name ) + "\"";
			}
			else if( p.type == FF_TYPE_TEXT )
				line += "\"type\": \"ParamString\", \"default_pattern\": \"^Slipstream\\\\ v{version}\\\\ \\\\-\\\\ MIT\\\\ \\\\-\\\\ Stoatworks\\\\ Labs,\\\\ stoatworks\\\\-labs\\\\.com$\"";
			else
			{
				const float lo = p.type == FF_TYPE_INTEGER ? plugin.GetParamRange( p.index ).min : 0.0f;
				const float hi = p.type == FF_TYPE_INTEGER ? plugin.GetParamRange( p.index ).max : 1.0f;
				line += fmt( "\"type\": \"ParamRange\", \"min\": %.1f, \"max\": %.1f, \"default\": %.17g", lo, hi, static_cast< double >( p.value ) );
			}
			line += i + 1 < list.size() ? "},\n" : "}\n";
			std::printf( "%s", line.c_str() );
		}
		std::printf( over ? "    ]\n" : "    ],\n" );
	}
	std::printf( "  }\n}\n" );
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
