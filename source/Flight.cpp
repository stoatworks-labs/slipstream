#include "Flight.h"

#include <algorithm>
#include <cmath>

namespace slipstream
{
namespace
{
double clamp01( double x )
{
	return std::clamp( x, 0.0, 1.0 );
}
double smoothstep( double a, double b, double x )
{
	const double t = clamp01( ( x - a ) / ( b - a ) );
	return t * t * ( 3.0 - 2.0 * t );
}
/// An exponential ramp from 0 (s = 0) to 1 (s = 1): slow, then a rush. The
/// way the films accelerate, and the way anything does that doubles its
/// speed in equal times.
double expRamp( double s, double k )
{
	s = clamp01( s );
	return std::expm1( k * s ) / std::expm1( k );
}
/// Read J mod 1 into the entry half (p, from 0 to 1 over J in [0, 0.5)) or
/// the exit half (q, over J in [0.5, 1)).
struct Half
{
	bool exiting;
	double t;
};
Half halfOf( double J )
{
	double j = J - std::floor( J );
	if( j < 0.5 )
		return { false, 2.0 * j };
	return { true, 2.0 * j - 1.0 };
}
constexpr double kHoleFar = 1.0e5;///< where a hole comes in from, GM/c^2
constexpr double kPlunge  = 3.05; ///< the deepest the plunge goes (the photon sphere is 3)
} // namespace

double TransitSpeed( Look look )
{
	switch( look )
	{
	//1.5 D per half-open frame at 60 fps: past one pass of the field per
	//shutter, so transit is the films' steady radial lines.
	case Look::Hyperspace: return 180.0;
	//A third of the field per shutter: stars fly past one by one.
	case Look::Warp: return 40.0;
	case Look::Wormhole: return 30.0;
	//The black hole's camera holds still: a static observer.
	case Look::BlackHole: return 0.0;
	default: return 0.0;
	}
}

Pose PoseAt( Look look, double J, const ScriptSettings& settings )
{
	Pose pose;
	const Half h = halfOf( J );
	switch( look )
	{
	case Look::Hyperspace:
		if( !h.exiting )
		{
			pose.speed  = expRamp( ( h.t - 0.1 ) / 0.7, 7.0 );
			pose.tunnel = smoothstep( 0.75, 1.0, h.t );
		}
		else
		{
			pose.tunnel = 1.0 - smoothstep( 0.0, 0.15, h.t );
			//Dropping out: the speed falls at once, then the last of it
			//bleeds off, and the streaks shrink back into points.
			pose.speed = h.t < 0.15 ? 1.0 : expRamp( 1.0 - ( h.t - 0.15 ) / 0.4, 7.0 );
		}
		break;

	case Look::Warp:
		if( !h.exiting )
		{
			//The rubber band, then the snap at 0.65.
			if( h.t < 0.65 )
			{
				const double s = h.t / 0.65;
				pose.pull      = s * s * s;
				pose.clipLead  = pose.pull;
			}
			pose.speed     = expRamp( ( h.t - 0.65 ) / 0.05, 5.0 );
			pose.clipScale = 1.0 - smoothstep( 0.65, 0.69, h.t );
		}
		else if( h.t < 0.3 )
		{
			pose.speed     = 1.0;
			pose.clipScale = 0.0;
		}
		else
		{
			//Arriving: the clip bursts out of the vanishing point and the
			//stars snap back, both overshooting and settling: a damped
			//spring, released at 0.3.
			const double s = ( h.t - 0.3 ) / 0.7;
			const double decay = std::exp( -6.0 * s );
			pose.speed     = 1.0 - smoothstep( 0.3, 0.33, h.t );
			pose.clipScale = 1.0 - decay * std::cos( 9.0 * s );
			pose.pull      = -0.35 * decay * std::sin( 9.0 * s );
		}
		break;

	case Look::Wormhole:
		if( !h.exiting )
		{
			pose.twist = smoothstep( 0.0, 0.6, h.t );
			pose.mouth = 2.0 * std::pow( smoothstep( 0.15, 0.65, h.t ), 1.5 );
			pose.speed = expRamp( ( h.t - 0.3 ) / 0.5, 4.0 );
		}
		else if( h.t < 0.5 )
		{
			pose.twist    = 1.0;
			pose.mouth    = 2.0;
			pose.speed    = 1.0;
			const double grow = smoothstep( 0.15, 0.5, h.t );
			pose.endLight = 2.0 * grow * grow * grow;
		}
		else
		{
			//Through: the white-out flashes at 0.5 and the scene unwinds
			//out of it.
			pose.twist = 1.0 - smoothstep( 0.5, 1.0, h.t );
			pose.speed = 1.0 - smoothstep( 0.5, 0.8, h.t );
		}
		break;

	case Look::BlackHole:
	{
		const double hold = std::max( settings.holeHold, kPlunge + 0.1 );
		if( !h.exiting )
		{
			if( J - std::floor( J ) > 0.0 )
			{
				const double s = smoothstep( 0.0, 1.0, h.t );
				pose.holeD     = std::exp( std::log( kHoleFar ) + ( std::log( hold ) - std::log( kHoleFar ) ) * s );
				pose.disc      = smoothstep( 0.3, 0.9, h.t );
			}
		}
		else if( h.t < 0.6 )
		{
			//The plunge: the shadow grows until it is the whole sky.
			const double s  = smoothstep( 0.0, 0.6, h.t );
			pose.holeD      = kPlunge + std::exp( std::log( hold - kPlunge ) + ( std::log( 0.02 ) - std::log( hold - kPlunge ) ) * s );
			pose.disc       = 1.0;
			pose.blackout   = smoothstep( 0.42, 0.6, h.t );
		}
		//and out of a white hole at 0.6 (FlashPoints), into normal space.
		break;
	}

	default: break;
	}
	return pose;
}

const std::vector< double >& FlashPoints( Look look )
{
	static const std::vector< double > hyperspace = { 0.4, 0.575 };
	static const std::vector< double > warp       = { 0.325, 0.65 };
	static const std::vector< double > wormhole   = { 0.75 };
	static const std::vector< double > blackHole  = { 0.8 };
	static const std::vector< double > none;
	switch( look )
	{
	case Look::Hyperspace: return hyperspace;
	case Look::Warp: return warp;
	case Look::Wormhole: return wormhole;
	case Look::BlackHole: return blackHole;
	default: return none;
	}
}

double FlashDecay( Look look )
{
	switch( look )
	{
	case Look::Hyperspace: return 0.12;
	case Look::Warp: return 0.25;
	case Look::Wormhole: return 0.6;
	case Look::BlackHole: return 0.8;
	default: return 0.2;
	}
}

double ShadowAngle( double d )
{
	d = std::max( d, 3.0 );
	return std::asin( std::min( 1.0, std::sqrt( 27.0 ) * std::sqrt( 1.0 - 2.0 / d ) / d ) );
}

double DistanceForShadow( double angle )
{
	//ShadowAngle falls monotonically from pi/2 at d = 3: bisect in log d.
	double lo = std::log( 3.0 ), hi = std::log( 1.0e9 );
	for( int i = 0; i < 200; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		if( ShadowAngle( std::exp( mid ) ) > angle )
			lo = mid;
		else
			hi = mid;
	}
	return std::exp( 0.5 * ( lo + hi ) );
}

//---------------------------------------------------------------------------
double Journey::Phase() const
{
	return phase;
}

void Journey::Reset()
{
	start = end = phase = 0.0;
	timer       = 0.0;
	tripping    = false;
}

void Journey::Advance( double dt, const DriveInputs& in )
{
	if( in.drive == Drive::Fader )
	{
		//Scrubbing: J is the fader, wherever it was.
		start     = lastFader ? end : phase;
		end       = std::clamp( in.fader, 0.0, 1.0 );
		phase     = end >= 1.0 ? 0.0 : end;
		lastFader = true;
		return;
	}
	lastFader = false;

	double j = Phase();
	start    = j;
	double moved = 0.0;
	if( in.drive == Drive::Trip && in.jumpPress && j == 0.0 )
	{
		tripping = true;
		timer    = 0.0;
	}
	const double rateIn  = 0.5 / std::max( in.entry, 1.0e-3 );
	const double rateOut = 0.5 / std::max( in.exit, 1.0e-3 );

	double remaining = std::max( dt, 0.0 );
	for( int guard = 0; remaining > 0.0 && guard < 16; ++guard )
	{
		if( j == 0.0 )
		{
			bool leave = false;
			switch( in.drive )
			{
			case Drive::Hold: leave = in.engage; break;
			case Drive::Trip: leave = tripping; break;
			case Drive::Loop:
			{
				const double wait = in.cruise - timer;
				if( wait > remaining )
				{
					timer += remaining;
					remaining = 0.0;
				}
				else
				{
					remaining -= std::max( wait, 0.0 );
					leave = true;
				}
				break;
			}
			default: break;
			}
			if( !leave )
				break;
			timer = 0.0;
			const double step = std::min( remaining * rateIn, 0.5 );
			j += step;
			moved += step;
			remaining -= step / rateIn;
			if( j >= 0.5 )
				j = 0.5;
		}
		else if( j < 0.5 )
		{
			//Entering: whatever the controls say now, the jump carries on.
			const double step = std::min( remaining * rateIn, 0.5 - j );
			j += step;
			moved += step;
			remaining -= step / rateIn;
			if( 0.5 - j <= 1.0e-15 )
			{
				moved += 0.5 - j;
				j = 0.5;
			}
		}
		else if( j == 0.5 )
		{
			bool leave = false;
			switch( in.drive )
			{
			case Drive::Hold: leave = !in.engage; break;
			case Drive::Trip:
			case Drive::Loop:
			{
				const double wait = in.transit - timer;
				if( wait > remaining )
				{
					timer += remaining;
					remaining = 0.0;
				}
				else
				{
					remaining -= std::max( wait, 0.0 );
					leave = true;
				}
				break;
			}
			default: break;
			}
			if( !leave )
				break;
			timer = 0.0;
			const double step = std::min( remaining * rateOut, 0.5 );
			j += step;
			moved += step;
			remaining -= step / rateOut;
			if( j >= 1.0 )
			{
				j        = 0.0;
				tripping = false;
			}
		}
		else
		{
			//Exiting: carries on to normal space.
			const double step = std::min( remaining * rateOut, 1.0 - j );
			j += step;
			moved += step;
			remaining -= step / rateOut;
			if( 1.0 - j <= 1.0e-15 )
			{
				moved += 1.0 - j;
				j        = 0.0;
				tripping = false;
				timer    = 0.0;
			}
		}
	}
	end   = start + moved;
	phase = j;
}

//---------------------------------------------------------------------------
void Camera::Advance( Look look, double j0, double j1, double dt, double cruise, double top, const ScriptSettings& settings )
{
	//Speeds are in D per second; distance is in the field's own units.
	const double transit = top * TransitSpeed( look ) * kDepth;
	std::array< double, kSubsteps + 1 > speed {};
	for( int i = 0; i <= kSubsteps; ++i )
	{
		const double J = j0 + ( j1 - j0 ) * i / kSubsteps;
		speed[ i ]     = cruise * kDepth + transit * PoseAt( look, J, settings ).speed;
	}
	//Trapezoids: distance at each substep, from the start of the frame.
	std::array< double, kSubsteps + 1 > along {};
	const double h = std::max( dt, 0.0 ) / kSubsteps;
	for( int i = 1; i <= kSubsteps; ++i )
		along[ i ] = along[ i - 1 ] + 0.5 * h * ( speed[ i - 1 ] + speed[ i ] );
	for( int i = 0; i <= kSubsteps; ++i )
		samples[ i ] = along[ i ] - along[ kSubsteps ];
	total += along[ kSubsteps ];
	zMod     = std::fmod( zMod + along[ kSubsteps ], kDepth );
	zLong    = std::fmod( zLong + along[ kSubsteps ], kLongPeriod );
	speedNow = speed[ kSubsteps ];
}

double Camera::OffsetAt( double f ) const
{
	f              = std::clamp( f, 0.0, 1.0 ) * kSubsteps;
	const int i    = std::min( static_cast< int >( f ), kSubsteps - 1 );
	const double t = f - i;
	return samples[ i ] + ( samples[ i + 1 ] - samples[ i ] ) * t;
}

} // namespace slipstream
