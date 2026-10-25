#include "Shaders.h"

namespace slipstream::shaders
{
const char* const kVersion = "#version 410 core\n";

//===========================================================================
// The library. No #version, no main.
//
// Randomness is integer: the PCG output mix, exact in 32 bits on every GPU.
// Noise lattices WRAP (a period per axis), so a coordinate handed over mod a
// period -- the camera's distance mod kLongPeriod, an angle round the
// vanishing point -- lands on the same lattice at both ends: no seam where
// atan's branch cut is, none where the distance wraps.
//===========================================================================
const char* const kCommon = R"(
//smoothstep is undefined for edge0 >= edge1 (GLSL 4.10, 8.3): every fade
//down is written 1 - smoothstep( low, high, x ).
const float PI  = 3.14159265358979;
const float TAU = 6.28318530717959;
const float FIELD_DEPTH = 4.0;
const float FIELD_NEAR  = 0.02;
const float LONG_PERIOD = 256.0;
const float ROOT_TWO_PI = 2.50662827463;

uniform sampler2D Blackbody;

uint pcg( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}
float unitFloat( uint h )
{
	return float( h >> 8u ) * ( 1.0 / 16777216.0 );
}
uint mixHash( uint a, uint b, uint c )
{
	return pcg( a ^ pcg( b ^ pcg( c ) ) );
}
/// a mod m for any sign of a (GLSL's % is undefined on negative operands).
int wrapIndex( int a, int m )
{
	return a - m * int( floor( float( a ) / float( m ) ) );
}

float lattice( int x, int y, ivec2 period, uint salt )
{
	return unitFloat( mixHash( uint( wrapIndex( x, period.x ) ), uint( wrapIndex( y, period.y ) ), salt ) );
}
/// Value noise on a wrapping lattice, smoothstep-interpolated, in [0, 1].
float valueNoise( vec2 p, ivec2 period, uint salt )
{
	vec2 i  = floor( p );
	vec2 f  = p - i;
	vec2 s  = f * f * ( 3.0 - 2.0 * f );
	int x   = int( i.x ), y = int( i.y );
	float a = lattice( x, y, period, salt );
	float b = lattice( x + 1, y, period, salt );
	float c = lattice( x, y + 1, period, salt );
	float d = lattice( x + 1, y + 1, period, salt );
	return mix( mix( a, b, s.x ), mix( c, d, s.x ), s.y );
}
/// Value noise smeared along y over a run of `run` cells: what a shutter
/// does to a wall moving past along y. Short runs are averaged in taps; a
/// long run's average is a function of x alone (a streak), its contrast
/// falling as the run grows.
float smearedNoise( vec2 p, float run, ivec2 period, uint salt )
{
	if( run < 0.25 )
		return valueNoise( p, period, salt );
	float taps = 0.0;
	for( int i = 0; i < 3; ++i )
		taps += valueNoise( vec2( p.x, p.y - run * ( float( i ) + 0.5 ) / 3.0 ), period, salt );
	taps *= 1.0 / 3.0;
	float streak = valueNoise( vec2( p.x, 0.5 ), ivec2( period.x, 1 ), salt ^ 0x5bd1e995u );
	streak = 0.5 + ( streak - 0.5 ) * min( 1.0, 2.0 / sqrt( run ) );
	return mix( taps, streak, smoothstep( 2.0, 8.0, run ) );
}
/// Octaves of smearedNoise: each doubles the frequency and the lattice's
/// period with it, so every octave wraps where the first does.
float smearedFbm( vec2 p, float run, ivec2 period, uint salt, int octaves )
{
	float sum = 0.0, weight = 0.5, total = 0.0;
	for( int o = 0; o < octaves; ++o )
	{
		sum += weight * smearedNoise( p, run, period, salt + uint( o ) * 0x9e3779b9u );
		total += weight;
		p *= 2.0;
		run *= 2.0;
		period *= 2;
		weight *= 0.5;
	}
	return sum / total;
}

/// Exposure E scaled as LIGHT, not as exposure: the developed value
/// 1 - exp( -E ) times k, taken back into exposure. Fading the clip by
/// scaling its exposure would leave its highlights at full brightness.
vec3 fadeLight( vec3 e, float k )
{
	return -log( max( vec3( 1.0 ) - k * ( vec3( 1.0 ) - exp( -e ) ), vec3( 1.0 / 4096.0 ) ) );
}

float linearFromSrgb( float c )
{
	c = clamp( c, 0.0, 1.0 );
	return c <= 0.04045 ? c / 12.92 : pow( ( c + 0.055 ) / 1.055, 2.4 );
}
float srgbFromLinear( float l )
{
	l = clamp( l, 0.0, 1.0 );
	return l <= 0.0031308 ? l * 12.92 : 1.055 * pow( l, 1.0 / 2.4 ) - 0.055;
}

/// erf, Abramowitz and Stegun 7.1.26: |error| < 1.5e-7.
float erfApprox( float x )
{
	float a = abs( x );
	float t = 1.0 / ( 1.0 + 0.3275911 * a );
	float y = 1.0 - ( ( ( ( 1.061405429 * t - 1.453152027 ) * t + 1.421413741 ) * t - 0.284496736 ) * t + 0.254829592 ) * t * exp( -a * a );
	return x < 0.0 ? -y : y;
}
float normalCdf( float x )
{
	return 0.5 * ( 1.0 + erfApprox( x * 0.70710678118 ) );
}

/// The blackbody table (Blackbody.cpp): rgb at unit luminance, log2 of the
/// luminance relative to 6500 K in alpha. 800 K to 120000 K, geometric.
float blackbodyCoordinate( float kelvin )
{
	return clamp( log( max( kelvin, 1.0 ) / 800.0 ) / log( 150.0 ), 0.0, 1.0 );
}
vec3 blackbodyColour( float kelvin )
{
	return texture( Blackbody, vec2( blackbodyCoordinate( kelvin ), 0.5 ) ).rgb;
}
float blackbodyLog2Y( float kelvin )
{
	return texture( Blackbody, vec2( blackbodyCoordinate( kelvin ), 0.5 ) ).a;
}

/// A star's temperature and luminosity from its hash: luminosity a Pareto
/// tail (most stars faint, a few blinding), temperature 3000 to 30000 K.
float starLuminosity( uint h )
{
	return min( 3.0 * pow( max( unitFloat( h ), 1.0e-6 ), -1.5 ), 1.0e5 );
}
float starKelvin( uint h )
{
	return 3000.0 * pow( 10.0, unitFloat( h ) );
}

/// Where a point at `rel` (pixels from the vanishing point) is drawn under
/// the Warp's rubber band (pull, a share of Stretch: the inner part of the
/// picture drawn in towards the vanishing point, the outer part held) and
/// the Wormhole's whirlpool (twist, radians at the core).
vec2 warpRel( vec2 rel, float pull, float twist, float stretch, float pinchRadius, float swirlRadius )
{
	float r = length( rel );
	float x = r / pinchRadius;
	vec2 p  = rel * ( 1.0 - stretch * pull * exp( -x * x ) );
	float q = r / swirlRadius;
	float a = twist / ( 1.0 + q * q );
	float c = cos( a ), s = sin( a );
	return vec2( c * p.x - s * p.y, s * p.x + c * p.y );
}
)";

const char* const kQuadVertex = R"(
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
)";

//===========================================================================
// Intake (Over): the clip into exposure. sRGB to linear light, then the
// inverse of the composite's film curve, E = -ln( 1 - L ), clamped where
// L reaches 1 - 1/4096 (pure white would be infinite exposure). The
// composite's 1 - exp( -E ) undoes it exactly.
//===========================================================================
const char* const kIntakeFragment = R"(
uniform sampler2D ClipIn;
uniform float RoundTripForTest;
out vec4 Exposure;
void main()
{
	vec4 c   = texelFetch( ClipIn, ivec2( gl_FragCoord.xy ), 0 );
	vec3 lin = vec3( linearFromSrgb( c.r ), linearFromSrgb( c.g ), linearFromSrgb( c.b ) );
	vec3 e   = -log( max( vec3( 1.0 ) - lin, vec3( 1.0 / 4096.0 ) ) );
	//--identity's negative control: the round trip's inverse skipped.
	if( RoundTripForTest < 0.5 )
		e = lin;
	Exposure = vec4( e, c.a );
}
)";

//===========================================================================
// Stars. One instance a star, four vertices a quad (a triangle strip) that
// covers the star's streak over the shutter and three and a half sigmas of
// its blur either side. The star lives in a box 2 x 2 across and D deep
// that repeats along the line of flight; at shutter close it is at depth
// zc in [ near, near + D ), and at shutter open it was zc + dZ.
//
// The box's cross-section is the camera's view at the far end of the box
// (TubeHalf: the farthest corner of the frame from the vanishing point, so a
// whirlpool's turn uncovers nothing): no star is ever outside the view at
// the far end, and nothing outside the box is ever in it. A field narrower
// than the view shows its own edge as a bright square at the vanishing
// point, which is the first thing this got wrong.
//
// The streak is drawn exactly, in closed form:
//
//   - On the screen it runs along a straight line out from the vanishing
//     point, from f rho / zInner to f rho / zOuter.
//   - Light per unit depth flown is L / ( rho^2 + z^2 ) / dZ (flux times the
//     time spent, the exposure normalised to the shutter as a camera with
//     auto exposure would), and depth maps to the screen as ds = f rho dz /
//     z^2, so light per unit LENGTH is L z^2 / ( ( rho^2 + z^2 ) f rho dZ):
//     uniform wherever z >> rho, whatever the depth. The inverse square and
//     the perspective cancel.
//   - The field repeats every D, so a long shutter catches the same column
//     of copies more than once: n( z ) of them pass depth z, counted exactly
//     ( floor and ceil below ). Past one pass per shutter the light per unit
//     length settles at L / ( f rho D ): the films' steady radial lines.
//   - The blur is a Gaussian of Sigma pixels: across the line exactly, and
//     along it the two ends' error functions, the light in between taken as
//     the local level.
//
// What the fragment shader gets is energy = the streak's whole light at the
// local level ( n z^2 / ( rho^2 + z^2 ) ) x ( span / dZ ) / ( zInner zOuter ),
// which is finite as dZ goes to 0: a star at rest is a Gaussian spot.
//===========================================================================
const char* const kStarVertex = R"(
uniform uint StarSeed;
uniform float TubeHalf;
uniform float PhaseZ;
uniform float ShutterZ;
uniform float Focal;
uniform vec2 Vanish;
uniform vec2 Raster;
uniform float Sigma;
uniform float StarGain;
uniform float StarColour;
uniform float Pull0, Pull1, Twist0, Twist1;
uniform float Stretch, PinchRadius, SwirlRadius;
uniform float Dispersion;
uniform float CopyLaw;
uniform vec4 TestStar;

flat out vec3 starLight;
flat out vec4 segment;
flat out vec4 depths;
out vec2 local;

void main()
{
	uint h   = pcg( uint( gl_InstanceID ) * 2654435761u ^ StarSeed );
	float x  = ( 2.0 * unitFloat( h ) - 1.0 ) * TubeHalf;
	h        = pcg( h );
	float y  = ( 2.0 * unitFloat( h ) - 1.0 ) * TubeHalf;
	h        = pcg( h );
	float z0 = unitFloat( h ) * FIELD_DEPTH;
	h        = pcg( h );
	float luminosity = starLuminosity( h );
	h                = pcg( h );
	float kelvin     = starKelvin( h );
	//The harness's one star, at a place it chose.
	if( TestStar.w > 0.0 )
	{
		x          = TestStar.x;
		y          = TestStar.y;
		z0         = TestStar.z;
		luminosity = TestStar.w;
	}

	float rho  = max( length( vec2( x, y ) ), 1.0e-4 );
	vec2 dir   = vec2( x, y ) / rho;
	float zc   = FIELD_NEAR + mod( z0 - PhaseZ - FIELD_NEAR, FIELD_DEPTH );
	float zo   = zc + ShutterZ;
	float zFar = FIELD_NEAR + FIELD_DEPTH;
	//An earlier copy of the column came into the box during the shutter
	//and left through the near plane: the streak reaches the near plane.
	bool earlier  = CopyLaw > 0.5 && zo - FIELD_DEPTH > FIELD_NEAR;
	float zInner  = min( zo, zFar );
	float zOuter  = earlier ? FIELD_NEAR : zc;
	float span    = earlier ? zInner - FIELD_NEAR : min( ShutterZ, zFar - zc );
	float ratio   = ShutterZ > 0.0 ? span / ShutterZ : 1.0;

	vec2 inner = warpRel( dir * ( Focal * rho / zInner ), Pull0, Twist0, Stretch, PinchRadius, SwirlRadius );
	vec2 outer = warpRel( dir * ( Focal * rho / zOuter ), Pull1, Twist1, Stretch, PinchRadius, SwirlRadius );
	vec2 axis  = outer - inner;
	float len  = length( axis );
	vec2 along = len > 1.0e-4 ? axis / len : vec2( 1.0, 0.0 );
	vec2 across = vec2( -along.y, along.x );

	float margin = 3.5 * Sigma + 1.0;
	float reach  = len * ( 1.0 + 0.25 * Dispersion );
	float a      = ( gl_VertexID == 0 || gl_VertexID == 2 ) ? -margin : reach + margin;
	float c      = gl_VertexID < 2 ? -margin : margin;
	vec2 pos     = Vanish + inner + along * a + across * c;
	gl_Position  = vec4( pos / Raster * 2.0 - 1.0, 0.0, 1.0 );
	local        = vec2( a, c );

	vec3 chroma = mix( vec3( 1.0 ), blackbodyColour( kelvin ), StarColour );
	starLight   = chroma * luminosity * StarGain;
	segment     = vec4( len, rho, 1.0 / zInner, 1.0 / zOuter );
	depths      = vec4( zc, ShutterZ, ratio, earlier ? 1.0 : 0.0 );
}
)";

const char* const kStarFragment = R"(
uniform float Sigma;
uniform float Dispersion;
uniform float FluxLaw;
uniform float CopyLaw;
uniform float LengthLaw;

flat in vec3 starLight;
flat in vec4 segment;
flat in vec4 depths;
in vec2 local;
out vec4 Exposure;

/// How many copies of the column passed depth z during the shutter: copy k
/// ran from zc + k D + dZ down to zc + k D.
float copiesAt( float z, float zc, float dz )
{
	if( CopyLaw < 0.5 )
		return 1.0;
	return floor( ( z - zc ) / FIELD_DEPTH ) - ceil( ( z - zc - dz ) / FIELD_DEPTH ) + 1.0;
}

void main()
{
	float len  = segment.x;
	float rho  = segment.y;
	float t    = local.x;
	float frac = len > 0.0 ? clamp( t / len, 0.001, 0.999 ) : 0.5;
	float z    = 1.0 / mix( segment.z, segment.w, frac );
	float n    = depths.y > 0.0 ? max( copiesAt( z, depths.x, depths.y ), 0.0 ) : 1.0;
	//--streak's negative control: the flux the star had at shutter close,
	//all the way along (no inverse square).
	float flux = FluxLaw > 0.5 ? 1.0 / ( rho * rho + z * z ) : 1.0 / ( rho * rho + depths.x * depths.x );
	//Stars come in out of the dark at the far end of the box.
	float fade   = 1.0 - smoothstep( FIELD_NEAR + 0.8 * FIELD_DEPTH, FIELD_NEAR + FIELD_DEPTH, z );
	float energy = flux * n * z * z * depths.z * segment.z * segment.w * fade;

	float across = local.y;
	float g      = exp( -0.5 * across * across / ( Sigma * Sigma ) ) / ( Sigma * ROOT_TWO_PI );
	vec3 light;
	for( int c = 0; c < 3; ++c )
	{
		//Dispersion: the blue end of the light runs ahead of the red.
		float lc = len * ( 1.0 + 0.25 * Dispersion * float( c - 1 ) );
		float alongTerm;
		if( lc < 0.05 * Sigma )
		{
			float m   = t - 0.5 * lc;
			alongTerm = exp( -0.5 * m * m / ( Sigma * Sigma ) ) / ( Sigma * ROOT_TWO_PI );
		}
		else
			alongTerm = ( normalCdf( ( lc - t ) / Sigma ) - normalCdf( -t / Sigma ) ) / lc;
		//--energy's negative control: the light not spread over the length.
		if( LengthLaw < 0.5 )
			alongTerm *= max( lc, Sigma * ROOT_TWO_PI ) / ( Sigma * ROOT_TWO_PI );
		light[ c ] = starLight[ c ] * energy * g * alongTerm;
	}
	Exposure = vec4( light, 0.0 );
}
)";

//===========================================================================
// The scene. One pass per look, everything in exposure, the stars laid in.
//===========================================================================
const char* const kSceneFragment = R"(
in vec2 uv;
uniform int LookId;
uniform int IsEffect;
uniform vec2 Raster;
uniform vec2 ViewOrigin;
uniform vec2 Vanish;
uniform float Focal;
uniform sampler2D Stars;
uniform sampler2D ClipE;
uniform float ClipLevels;
uniform float ClipThrough;
uniform float LightGain;
uniform vec3 TintColour;

//Hyperspace
uniform float SmearK;
uniform float ClipShare;
uniform float TunnelLight;
uniform float LongZ;
uniform float ShutterZ;
//Warp
uniform float Stretch, PinchRadius, SwirlRadius;
uniform float Pull0, Pull1, Scale0, Scale1;
//Wormhole
uniform float Twist0, Twist1;
uniform float MouthRadius;
uniform float EndLight;
uniform float CloudLight;
uniform float CloudDensity;
uniform float Spin;
//Black Hole
uniform float HoleD;
uniform sampler2D Orbits;
uniform sampler2D Ends;
uniform vec3 DiscNormal;
uniform float DiscInner, DiscOuter, DiscKelvin, DiscLight, DopplerAmount, CoronaLight;
uniform float DiscTimeA, DiscTimeB, DiscBlend;
uniform float SkyCells;
uniform float SkyProbability;
uniform float SkyGain;
uniform float SkyMean;
uniform float SkySigma;
uniform float StarColour;
uniform uint StarSeed;
uniform float BendLaw;
uniform float CaptureLaw;
uniform float SpinSign;
uniform float DiscTexture;
uniform float SeamForTest;

out vec4 Scene;

const float B_CRIT   = 5.196152422706632;
const float LOG_MIN  = -14.0;
const float ESC_MAX  = 10.0;
const float CAP_MAX  = 1.6447;
const int TABLE_B    = 512;
const int TABLE_T    = 512;

//---------------------------------------------------------------------------
vec3 clipAt( vec2 px, float lod )
{
	if( IsEffect == 0 || px.x < 0.0 || px.y < 0.0 || px.x >= Raster.x || px.y >= Raster.y )
		return vec3( 0.0 );
	return textureLod( ClipE, px / Raster, lod ).rgb;
}
/// The same, with the picture carried on beyond the frame in mirror image:
/// what the smear and the rubber band read, so the frame's own border is
/// never drawn into the streaks (as a nest of rectangles, one per tap) or
/// uncovered as black.
vec3 clipMirrored( vec2 px, float lod )
{
	if( IsEffect == 0 )
		return vec3( 0.0 );
	vec2 q = px / Raster;
	q      = vec2( 1.0 ) - abs( mod( q, 2.0 ) - vec2( 1.0 ) );
	return textureLod( ClipE, q, lod ).rgb;
}

//---------------------------------------------------------------------------
// Hyperspace. The clip rides ahead at Clip Depth; each of its points runs in
// towards the vanishing point over the shutter as a star does, so pixel p
// averages the clip over p u for u uniform in [ 1, k ], k = 1 + dZ / depth
// (uniform in u: the time a receding point spends at each scale, with the
// picture's surface brightness constant). A point of the clip becomes an
// evenly lit line from p0 / k to p0. Beyond the frame the picture carries on
// in mirror image.
//---------------------------------------------------------------------------
vec3 clipSmear( vec2 rel )
{
	if( SmearK <= 1.0 + 1.0e-6 )
		return clipAt( Vanish + rel, 0.0 );
	//The mip level half a step coarser than the taps' spacing: the taps'
	//sum then aliases by under 1% (a blur of 0.57 spacings), and the smear
	//shows no copies of itself.
	const int N = 32;
	float du  = ( SmearK - 1.0 ) / float( N );
	float lod = clamp( log2( max( du * length( rel ), 1.0 ) ) + 0.5, 0.0, ClipLevels );
	vec3 sum  = vec3( 0.0 );
	for( int i = 0; i < N; ++i )
		sum += clipMirrored( Vanish + rel * ( 1.0 + du * ( float( i ) + 0.5 ) ), lod );
	return sum / float( N );
}

/// The later films' tunnel: a cylinder just outside the star box, its wall
/// mottled light, smeared along the line of flight by the shutter (and by
/// the pixel's own footprint on the wall, which is what keeps the far end
/// from aliasing).
vec3 tunnel( vec2 rel )
{
	float r     = max( length( rel ), 0.5 );
	float theta = atan( rel.y, rel.x );
	const float R = 1.3, around = 48.0, perUnit = 3.0;
	float z     = Focal * R / r;
	vec2 wall   = vec2( theta / TAU * around, ( z + LongZ ) * perUnit );
	float run   = max( ShutterZ, Focal * R / ( r * r ) ) * perUnit;
	float n     = smearedFbm( wall, run, ivec2( int( around ), int( LONG_PERIOD * perUnit ) ), 0x2545f491u, 4 );
	float depth = ( 1.0 - exp( -z / 2.0 ) ) * exp( -z / 60.0 );
	float band  = smoothstep( 0.42, 0.78, n );
	vec3 colour = mix( TintColour, vec3( 1.0 ), 0.6 * band * band );
	return TunnelLight * depth * colour * ( 0.03 + 1.8 * band * band );
}

//---------------------------------------------------------------------------
// Warp. The clip under the rubber band and the snap: at each of eight
// instants through the shutter, the clip point whose pulled, scaled position
// is this pixel, by bisection (the map is monotone in radius).
//---------------------------------------------------------------------------
float unpinch( float r, float pull, float scale )
{
	float lo = 0.0, hi = r / ( scale * min( 1.0, 1.0 - Stretch * pull ) ) + 1.0;
	for( int i = 0; i < 22; ++i )
	{
		float mid = 0.5 * ( lo + hi );
		float x   = mid / PinchRadius;
		float at  = mid * scale * ( 1.0 - Stretch * pull * exp( -x * x ) );
		if( at < r )
			lo = mid;
		else
			hi = mid;
	}
	return 0.5 * ( lo + hi );
}
vec3 clipWarp( vec2 rel )
{
	if( Pull0 == 0.0 && Pull1 == 0.0 && Scale0 == 1.0 && Scale1 == 1.0 )
		return clipAt( Vanish + rel, 0.0 );
	float r  = length( rel );
	vec2 dir = r > 1.0e-6 ? rel / r : vec2( 0.0 );
	//Enough instants that the snap (the scale falling from 1 to 0 in a
	//couple of frames) smears rather than leaving copies of itself.
	int N = clamp( int( ceil( 48.0 * abs( Scale1 - Scale0 ) + 24.0 * abs( Pull1 - Pull0 ) ) ), 8, 48 );
	vec3 sum = vec3( 0.0 );
	for( int i = 0; i < N; ++i )
	{
		float ts    = ( float( i ) + 0.5 ) / float( N );
		float pull  = mix( Pull0, Pull1, ts );
		float scale = mix( Scale0, Scale1, ts );
		if( scale < 1.0e-4 )
			continue;
		float rc  = unpinch( r, pull, scale );
		float lod = clamp( log2( max( rc / max( r, 1.0 ), 1.0 ) ), 0.0, ClipLevels );
		//Shrinking into the vanishing point the clip is a picture in space:
		//black round it. Pulled by the band it is space itself: mirrored.
		sum += scale < 1.0 ? clipAt( Vanish + dir * rc, lod ) : clipMirrored( Vanish + dir * rc, lod );
	}
	return sum / float( N );
}

//---------------------------------------------------------------------------
// Wormhole. The scene twists into the whirlpool; inside the mouth, three
// concentric cylinders of domain-warped cloud swirl past (their bands
// pitched so the angle wraps a whole number of turns per LONG_PERIOD, so
// the distance's wrap is seamless), lit from the far end. Each cloud's
// edge is a threshold on the noise, so the gaps between them show the
// layer behind, and behind the last, the dark.
//---------------------------------------------------------------------------
vec3 clipSwirl( vec2 rel )
{
	if( Twist0 == 0.0 && Twist1 == 0.0 )
		return clipAt( Vanish + rel, 0.0 );
	vec3 sum = vec3( 0.0 );
	for( int i = 0; i < 4; ++i )
	{
		float twist = mix( Twist0, Twist1, ( float( i ) + 0.5 ) / 4.0 );
		sum += clipAt( Vanish + warpRel( rel, 0.0, -twist, 0.0, 1.0, SwirlRadius ), 0.0 );
	}
	return sum * 0.25;
}
/// Domain-warped noise: the warp is two octaves of plain noise (its own blur
/// would not show), the density four of smeared noise. A third of what it
/// was, for the same clouds.
float plainFbm2( vec2 p, ivec2 period, uint salt )
{
	return ( 2.0 * valueNoise( p, period, salt ) + valueNoise( 2.0 * p, 2 * period, salt + 0x9e3779b9u ) ) / 3.0;
}
float cloudDensity( vec2 wall, float run, ivec2 period, uint salt )
{
	float qx = plainFbm2( wall, period, salt );
	float qy = plainFbm2( wall + vec2( 5.2, 1.3 ), period, salt ^ 0x68e31da4u );
	return smearedFbm( wall + 4.0 * vec2( qx, qy ), run, period, salt ^ 0xb5297a4du, 4 );
}
vec3 cloudTunnel( vec2 rel, vec3 clipTint, float clipWeight )
{
	float r     = max( length( rel ), 0.5 );
	float theta = atan( rel.y, rel.x );
	const float around = 20.0, perUnit = 1.0;
	//8 turns of the bands over LONG_PERIOD: a whole number.
	const float PITCH = TAU * 8.0 / LONG_PERIOD;
	vec3 deep = TintColour * 0.25, mid = TintColour, high = mix( TintColour, vec3( 1.0 ), 0.75 );
	vec3 acc    = vec3( 0.0 );
	float cover = 0.0;
	for( int k = 0; k < 3; ++k )
	{
		float R   = k == 0 ? 1.0 : ( k == 1 ? 1.5 : 2.25 );
		float z   = Focal * R / r;
		float w   = z + LongZ;
		float ang = theta - Spin / R - w * PITCH;
		vec2 wall = vec2( ang / TAU * around, w * perUnit );
		float run = max( ShutterZ, Focal * R / ( r * r ) ) * perUnit;
		//--seam's negative control: a lattice that does not close round.
		ivec2 period = ivec2( int( around ) + ( SeamForTest > 0.5 ? 1 : 0 ), int( LONG_PERIOD * perUnit ) );
		float d   = cloudDensity( wall, run, period, 0x1b873593u + uint( k ) * 0x85ebca6bu );
		//Billows: the noise folded about its middle, so cloud edges are
		//ridged and rounded rather than a smooth wash.
		float a   = smoothstep( 0.46, 0.68, d ) * CloudDensity * ( k == 2 ? 1.0 : 0.85 );
		float lit = 0.15 + 2.2 * z / ( z + 8.0 );
		vec3 c    = mix( mix( deep, mid, smoothstep( 0.35, 0.65, d ) ), high, smoothstep( 0.62, 0.9, d ) );
		c         = mix( c, clipTint * ( 0.4 + 1.2 * d ), clipWeight );
		acc += ( 1.0 - cover ) * a * c * lit;
		cover += ( 1.0 - cover ) * a;
		//Nothing behind solid cloud shows: stop looking.
		if( cover > 0.995 )
			break;
	}
	vec3 glow = vec3( 1.0, 0.96, 0.88 ) * 5.0 * exp( -r / ( 0.05 * Raster.y ) );
	return CloudLight * ( acc + ( 1.0 - cover ) * glow );
}
vec3 wormhole( vec2 rel, vec3 starLight )
{
	vec3 clip  = clipSwirl( rel );
	vec3 scene = starLight + clip;
	float r    = length( rel );
	vec3 e     = scene;
	if( MouthRadius > 0.0 )
	{
		float theta  = atan( rel.y, rel.x );
		float ragged = valueNoise( vec2( theta / TAU * 9.0, LongZ * 0.0625 ), ivec2( 9, int( LONG_PERIOD * 0.0625 ) ), 0x27d4eb2fu );
		float inside = 1.0 - smoothstep( 0.55 * MouthRadius, MouthRadius, r * ( 0.8 + 0.4 * ragged ) );
		float lum    = dot( clip, vec3( 0.2126, 0.7152, 0.0722 ) );
		vec3 tint    = lum > 1.0e-5 ? clip / lum : TintColour;
		vec3 clouds  = cloudTunnel( rel, tint, IsEffect == 1 ? ClipThrough : 0.0 );
		e            = mix( scene, clouds, inside );
	}
	if( EndLight > 0.0 )
		e += vec3( 8.0 ) * ( 1.0 - smoothstep( 0.75 * EndLight, EndLight, r ) );
	return e;
}

//---------------------------------------------------------------------------
// (MSVC caps one string literal near 16 KB, C2026: the scene is two adjacent
// raw strings from here, which the compiler joins and tools/glslc.sh too.)
)" R"(
//---------------------------------------------------------------------------
// Black Hole. Tables: see Geodesic.h. A bilinear read by hand (texelFetch),
// so the interpolation is float and not the filter unit's fixed point.
//---------------------------------------------------------------------------
vec4 tableAt( sampler2D table, float column, float row, int columns )
{
	column = clamp( column, 0.0, float( columns - 1 ) );
	row    = clamp( row, 0.0, float( TABLE_B - 1 ) );
	int c0 = min( int( column ), columns - 2 ), r0 = min( int( row ), TABLE_B - 2 );
	float fc = column - float( c0 ), fr = row - float( r0 );
	vec4 a = texelFetch( table, ivec2( c0, r0 ), 0 ), b = texelFetch( table, ivec2( c0 + 1, r0 ), 0 );
	vec4 c = texelFetch( table, ivec2( c0, r0 + 1 ), 0 ), d = texelFetch( table, ivec2( c0 + 1, r0 + 1 ), 0 );
	return mix( mix( a, b, fc ), mix( c, d, fc ), fr );
}

struct Ray
{
	bool escape;  ///< the table branch: b above the critical
	bool inward;  ///< leaving the camera towards the hole
	float row;
	float uEnd, phiEnd, phiCam;
	float psi, b, p;///< p: the straight line's closest approach (BendLaw 0)
};

/// u = 1/r at position angle alpha from the camera along the ray.
float rayU( Ray ray, float alpha )
{
	if( BendLaw < 0.5 )
		return max( sin( alpha + ray.psi ), 0.0 ) / ray.p;
	float phi = ray.inward ? ray.phiCam + alpha : ray.phiCam - alpha;
	if( ray.escape && phi > ray.phiEnd )
		phi = 2.0 * ray.phiEnd - phi;
	float s   = clamp( phi / max( ray.phiEnd, 1.0e-9 ), 0.0, 1.0 );
	vec4 orb  = tableAt( Orbits, s * float( TABLE_T - 1 ), ray.row, TABLE_T );
	return ray.uEnd * ( ray.escape ? orb.g : orb.a );
}

/// The starfield at infinity, by direction: one star per cell of a cube
/// face's grid at most, each a Gaussian of SkySigma radians widened by the
/// pixel's footprint (so a ray the hole stretches is blurred, not aliased).
vec3 faceDirection( int face, vec2 st )
{
	if( face == 0 ) return normalize( vec3( 1.0, st.x, st.y ) );
	if( face == 1 ) return normalize( vec3( -1.0, st.x, st.y ) );
	if( face == 2 ) return normalize( vec3( st.x, 1.0, st.y ) );
	if( face == 3 ) return normalize( vec3( st.x, -1.0, st.y ) );
	if( face == 4 ) return normalize( vec3( st.x, st.y, 1.0 ) );
	return normalize( vec3( st.x, st.y, -1.0 ) );
}
vec3 skyStars( vec3 n, float extra )
{
	vec3 a = abs( n );
	int face;
	vec2 st;
	if( a.x >= a.y && a.x >= a.z )
	{
		face = n.x > 0.0 ? 0 : 1;
		st   = n.yz / a.x;
	}
	else if( a.y >= a.z )
	{
		face = n.y > 0.0 ? 2 : 3;
		st   = n.xz / a.y;
	}
	else
	{
		face = n.z > 0.0 ? 4 : 5;
		st   = n.xy / a.z;
	}
	float cellAngle = 2.0 / SkyCells;
	float sigma     = sqrt( SkySigma * SkySigma + extra * extra );
	//A blur wider than a cell sees the field's mean light. Below that the
	//neighbourhood reaches three sigmas past the pixel's own cell, so no
	//star's Gaussian is cut square at a cell's edge.
	float meanWeight = smoothstep( 0.8 * cellAngle, 1.0 * cellAngle, sigma );
	vec3 sum = vec3( 0.0 );
	if( meanWeight < 1.0 )
	{
		vec2 cell  = ( st * 0.5 + 0.5 ) * SkyCells;
		ivec2 c0   = ivec2( floor( cell ) );
		int cells  = int( SkyCells );
		int across = clamp( int( ceil( 3.0 * sigma / cellAngle ) ), 1, 3 );
		for( int dy = -across; dy <= across; ++dy )
			for( int dx = -across; dx <= across; ++dx )
			{
				ivec2 cc = c0 + ivec2( dx, dy );
				if( cc.x < 0 || cc.y < 0 || cc.x >= cells || cc.y >= cells )
					continue;
				uint h = mixHash( uint( cc.x ), uint( cc.y ), StarSeed ^ ( uint( face ) * 0x9e3779b9u ) );
				if( unitFloat( h ) >= SkyProbability )
					continue;
				h = pcg( h );
				float ox = unitFloat( h );
				h = pcg( h );
				float oy = unitFloat( h );
				h = pcg( h );
				float luminosity = starLuminosity( h );
				h = pcg( h );
				float kelvin = starKelvin( h );
				vec3 sdir = faceDirection( face, ( vec2( cc ) + vec2( ox, oy ) ) / SkyCells * 2.0 - 1.0 );
				vec3 off  = n - sdir;
				float g   = exp( -0.5 * dot( off, off ) / ( sigma * sigma ) ) / ( TAU * sigma * sigma );
				sum += luminosity * g * mix( vec3( 1.0 ), blackbodyColour( kelvin ), StarColour );
			}
	}
	return SkyGain * mix( sum, vec3( SkyMean ), meanWeight );
}

/// The sky along an escaping direction: the clip where the camera's own
/// frustum would have seen it (Over), the starfield everywhere else.
vec3 skyAlong( vec3 n, float footprint, float plain )
{
	if( IsEffect == 1 && n.z > 1.0e-4 )
	{
		vec2 px = Vanish + Focal * n.xy / n.z;
		if( px.x >= 0.0 && px.y >= 0.0 && px.x < Raster.x && px.y < Raster.y )
		{
			float lod = clamp( log2( max( footprint * Focal / ( n.z * n.z ), 1.0 ) ), 0.0, ClipLevels );
			return textureLod( ClipE, px / Raster, lod ).rgb;
		}
	}
	return skyStars( n, sqrt( max( footprint * footprint - plain * plain, 0.0 ) ) );
}

/// The Shakura-Sunyaev profile: T ~ ( ( 1 - sqrt( rin / r ) ) / r^3 )^( 1/4 ),
/// normalised to 1 at its hottest, r = 49/36 rin.
float discProfile( float r )
{
	float peak = 49.0 / 36.0 * DiscInner;
	float f    = ( 1.0 - sqrt( DiscInner / r ) ) / ( r * r * r );
	float fMax = ( 1.0 - sqrt( DiscInner / peak ) ) / ( peak * peak * peak );
	return pow( max( f / fMax, 0.0 ), 0.25 );
}

vec3 blackHole( vec2 rel, out float opaque )
{
	opaque = 0.0;
	vec3 k = normalize( vec3( rel, Focal ) );
	//The pixel's footprint before the hole, for the sky's blur.
	float plain = length( dFdx( k ) ) + length( dFdy( k ) );
	if( HoleD <= 0.0 )
		return IsEffect == 1 ? clipAt( Vanish + rel, 0.0 ) : skyStars( k, 0.0 );

	float d     = HoleD;
	vec3 er     = vec3( 0.0, 0.0, -1.0 );
	float sinP  = length( k.xy );
	vec3 et     = sinP > 1.0e-7 ? vec3( k.xy / sinP, 0.0 ) : vec3( 1.0, 0.0, 0.0 );
	float lapse = sqrt( 1.0 - 2.0 / d );

	Ray ray;
	ray.psi    = atan( sinP, k.z );
	ray.b      = d * sinP / lapse;
	ray.p      = max( d * sinP, 1.0e-6 );
	ray.inward = k.z > 0.0;
	float bCrit = CaptureLaw > 0.5 ? B_CRIT : 2.0;
	ray.escape  = ray.b > B_CRIT;
	bool capture = ray.inward && ray.b < bCrit;

	float uCam = 1.0 / d;
	float alphaEnd;
	if( BendLaw < 0.5 )
	{
		//Straight rays (--ring's negative): a line past the hole.
		ray.row = 0.0;
		ray.uEnd = 0.5;
		ray.phiEnd = 0.0;
		ray.phiCam = 0.0;
		alphaEnd   = capture ? max( asin( clamp( ray.p * 0.5, 0.0, 1.0 ) ) - ray.psi, 0.0 ) : PI - ray.psi;
	}
	else
	{
		if( ray.escape )
		{
			ray.row = clamp( ( log( max( ray.b - B_CRIT, 1.0e-30 ) ) - LOG_MIN ) / ( ESC_MAX - LOG_MIN ), 0.0, 1.0 ) * float( TABLE_B - 1 );
			vec4 e  = tableAt( Ends, 0.0, ray.row, 1 );
			ray.uEnd   = e.r;
			ray.phiEnd = e.g;
		}
		else
		{
			ray.row = clamp( ( log( max( B_CRIT - ray.b, 1.0e-30 ) ) - LOG_MIN ) / ( CAP_MAX - LOG_MIN ), 0.0, 1.0 ) * float( TABLE_B - 1 );
			vec4 e  = tableAt( Ends, 0.0, ray.row, 1 );
			ray.uEnd   = e.b;
			ray.phiEnd = e.a;
		}
		float t    = sqrt( max( 1.0 - uCam / ray.uEnd, 0.0 ) );
		vec4 orb   = tableAt( Orbits, t * float( TABLE_T - 1 ), ray.row, TABLE_T );
		ray.phiCam = ray.escape ? orb.r : orb.b;
		if( !ray.inward )
			alphaEnd = ray.phiCam;
		else if( capture )
			alphaEnd = ray.phiEnd - ray.phiCam;
		else
			alphaEnd = 2.0 * ray.phiEnd - ray.phiCam;
		//Flat-space capture (--shadow's negative) takes the escape branch's
		//path for the rays it captures below sqrt 27; they are cut at the
		//hole by the capture flag.
	}

	//Where the ray goes if it gets away.
	vec3 n = cos( alphaEnd ) * er + sin( alphaEnd ) * et;
	float footprint = length( dFdx( n ) ) + length( dFdy( n ) );

	//The disc: where the ray's orbit plane crosses the disc's.
	vec3 no    = cross( er, k );
	float lno  = length( no );
	bool hit   = false;
	float alphaHit = 0.0, rHit = 0.0;
	if( lno > 1.0e-7 && DiscLight > 0.0 )
	{
		no /= lno;
		vec3 line = cross( no, DiscNormal );
		float ll  = length( line );
		if( ll > 1.0e-6 )
		{
			line /= ll;
			float a0 = atan( dot( line, et ), dot( line, er ) );
			float a1 = mod( a0, PI );
			for( int c = 0; c < 5; ++c )
			{
				float alpha = a1 + PI * float( c );
				if( alpha >= alphaEnd )
					break;
				float rr = 1.0 / max( rayU( ray, alpha ), 1.0e-9 );
				if( rr >= DiscInner && rr <= DiscOuter )
				{
					hit      = true;
					alphaHit = alpha;
					rHit     = rr;
					break;
				}
			}
		}
	}

	vec3 light = vec3( 0.0 );
	if( hit )
	{
		vec3 pos   = rHit * ( cos( alphaHit ) * er + sin( alphaHit ) * et );
		vec3 e1    = normalize( abs( DiscNormal.x ) < 0.9 ? cross( DiscNormal, vec3( 1.0, 0.0, 0.0 ) ) : cross( DiscNormal, vec3( 0.0, 1.0, 0.0 ) ) );
		vec3 e2    = cross( DiscNormal, e1 );
		float az   = atan( dot( pos, e2 ), dot( pos, e1 ) );
		float omega  = pow( rHit, -1.5 );
		//The real photon runs from the disc to the camera, against the
		//traced ray: its angular momentum about the disc's axis is -b n.n_d.
		float lambda = -ray.b * dot( no, DiscNormal ) * SpinSign;
		float shift  = sqrt( 1.0 - 3.0 / rHit ) / ( ( 1.0 - DopplerAmount * omega * lambda ) * lapse );
		float kelvin = DiscKelvin * discProfile( rHit );
		float seen   = shift * kelvin;
		float level  = exp2( blackbodyLog2Y( seen ) - blackbodyLog2Y( DiscKelvin ) );
		//Turbulence sheared by Keplerian rotation, renewed by crossfading
		//two copies a half period apart (time stays small and bounded).
		vec2 at     = vec2( 0.0, log( rHit ) * 9.0 );
		float na    = valueNoise( vec2( ( az - omega * DiscTimeA ) / TAU * 48.0, at.y ), ivec2( 48, 1 << 20 ), 0x3c6ef372u );
		float nb    = valueNoise( vec2( ( az - omega * DiscTimeB ) / TAU * 48.0 + 17.0, at.y + 5.0 ), ivec2( 48, 1 << 20 ), 0xa54ff53au );
		float fine  = valueNoise( vec2( ( az - omega * mix( DiscTimeA, DiscTimeB, DiscBlend ) ) / TAU * 192.0, at.y * 4.0 ), ivec2( 192, 1 << 20 ), 0x510e527fu );
		float swirl = mix( na, nb, DiscBlend ) * 0.75 + 0.25 * fine;
		float edge  = 1.0 - smoothstep( DiscOuter * 0.55, DiscOuter, rHit );
		float grain = DiscTexture > 0.5 ? 0.35 + 1.3 * swirl * swirl : 1.0;
		light += DiscLight * level * blackbodyColour( seen ) * grain * edge;
		opaque = 1.0;
	}

	//The corona: optically thin, emissivity ~ r^-3 (1 at r = 3), each
	//static emitter's light shifted by g^4, g = sqrt( 1 - 2/r ) / lapse;
	//integrated along the bent ray to the disc, the horizon or 4 turns.
	if( CoronaLight > 0.0 )
	{
		float stop = hit ? alphaHit : min( alphaEnd, 4.0 * TAU );
		const int NC = 32;
		float da    = stop / float( NC );
		float glow  = 0.0;
		for( int i = 0; i < NC; ++i )
		{
			float u  = rayU( ray, da * ( float( i ) + 0.5 ) );
			float gg = max( 1.0 - 2.0 * u, 0.0 ) / ( lapse * lapse );
			//j dl / d alpha = 27 u^3 x sqrt( u'^2 + u^2 ) / u^2, with
			//u'^2 + u^2 = 1/b^2 + 2u^3 on a geodesic (1/p^2 on a line).
			float speed = BendLaw > 0.5 ? sqrt( 1.0 / ( ray.b * ray.b ) + 2.0 * u * u * u ) : 1.0 / ray.p;
			glow += 27.0 * u * speed * gg * gg;
		}
		light += CoronaLight * glow * da * blackbodyColour( 2.0 * DiscKelvin );
	}

	if( capture )
	{
		opaque = 1.0;
		return light;
	}
	if( !hit )
		light += skyAlong( n, 0.5 * footprint, 0.5 * plain );
	return light;
}

//---------------------------------------------------------------------------
void main()
{
	vec2 p      = gl_FragCoord.xy - ViewOrigin;
	vec2 rel    = p - Vanish;
	vec3 stars  = texelFetch( Stars, ivec2( p ), 0 ).rgb;
	vec3 e      = vec3( 0.0 );
	float opaque = 0.0;
	if( LookId == 0 )
		e = fadeLight( clipSmear( rel ), ClipShare ) + stars + tunnel( rel );
	else if( LookId == 1 )
		e = clipWarp( rel ) + fadeLight( clipAt( p, 0.0 ), ClipShare ) + stars;
	else if( LookId == 2 )
		e = wormhole( rel, stars );
	else
		e = blackHole( rel, opaque );
	Scene = vec4( e, opaque );
}
)";

//===========================================================================
// Composite: the flash, the plunge's blackout, then development (film:
// 1 - exp( -E ) per channel), sRGB, and Mix against the clip (Over). The
// source is premultiplied, over black, its alpha the brightest channel (the
// black hole's shadow and disc opaque).
//===========================================================================
const char* const kCompositeFragment = R"(
uniform sampler2D SceneE;
uniform sampler2D ClipIn;
uniform int IsEffect;
uniform float MixAmount;
uniform float Blackout;
uniform float FlashLight;
uniform int FlashKind;
uniform float FlashAge;
uniform vec2 Vanish;
uniform vec2 Raster;
uniform vec2 ViewOrigin;
uniform vec3 TintColour;
out vec4 fragColour;

vec3 flash( vec2 p )
{
	if( FlashLight <= 0.0 )
		return vec3( 0.0 );
	vec2 rel = p - Vanish;
	float r  = length( rel );
	float H  = Raster.y;
	if( FlashKind == 0 )
		return FlashLight * vec3( 0.9, 0.95, 1.0 ) * ( 0.5 + 3.0 * exp( -r / ( 0.12 * H ) ) );
	if( FlashKind == 1 )
	{
		//Star Trek's: a core, a horizontal anamorphic streak, a ring
		//running out from the vanishing point.
		float core   = 14.0 * exp( -0.5 * r * r / ( 0.02 * H * 0.02 * H ) );
		float sx     = rel.x / ( 0.55 * Raster.x ), sy = rel.y / ( 0.006 * H );
		float streak = 5.0 * exp( -0.5 * ( sx * sx + sy * sy ) );
		float ring   = 3.0 * exp( -0.5 * pow( ( r - FlashAge * 1.6 * H ) / ( 0.012 * H ), 2.0 ) );
		return FlashLight * mix( vec3( 1.0 ), TintColour, 0.35 ) * ( core + streak + ring + 0.12 );
	}
	if( FlashKind == 2 )
		return FlashLight * vec3( 3.0 );
	//The white hole: a disc of light opening out of the vanishing point.
	float open = FlashAge * 3.0 * H;
	return FlashLight * vec3( 1.0, 0.98, 0.95 ) * ( 1.5 + 5.0 * ( 1.0 - smoothstep( 0.8 * open, open, r ) ) );
}

void main()
{
	vec2 p   = gl_FragCoord.xy - ViewOrigin;
	vec4 s   = texelFetch( SceneE, ivec2( p ), 0 );
	vec3 e   = s.rgb * ( 1.0 - Blackout ) + flash( p );
	vec3 lin = vec3( 1.0 ) - exp( -max( e, vec3( 0.0 ) ) );
	vec3 col = vec3( srgbFromLinear( lin.r ), srgbFromLinear( lin.g ), srgbFromLinear( lin.b ) );
	if( IsEffect == 1 )
	{
		vec4 clip  = texelFetch( ClipIn, ivec2( p ), 0 );
		fragColour = vec4( mix( clip.rgb, col, MixAmount ), mix( clip.a, 1.0, MixAmount ) );
	}
	else
	{
		float alpha = max( max( col.r, col.g ), max( col.b, s.a * ( 1.0 - Blackout ) + Blackout ) );
		fragColour  = vec4( col, alpha );
	}
}
)";

std::string Assemble( const char* piece )
{
	return std::string( kVersion ) + kCommon + piece;
}

} // namespace slipstream::shaders
