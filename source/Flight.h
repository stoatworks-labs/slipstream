#pragma once

#include "Controls.h"

#include <array>
#include <cmath>
#include <vector>

/**
    The flight, on the CPU, in double: where the journey is (the phase J),
    how fast the camera is going, how far it has gone, and what each look's
    script says the picture is doing at any instant of the frame.

    J is in [0, 1): 0 is normal space, 0.5 is in transit, 1 is normal space
    again and wraps to 0. Hold, Trip and Loop only ever move J forward, so a
    jump abandoned halfway carries on through and out; Fader scrubs it.

    The camera's distance flown, Z, is integrated in double and handed to the
    GPU only as Z mod the field's period: the star field repeats every D along
    the line of flight, so nothing is lost, and Resolume's clock (measured at
    ~499 million ms, where a float resolves 0.03 s) never reaches a shader.
*/
namespace slipstream
{
/// The star field's period along the line of flight. Every speed is in units
/// of D per second. The box's width follows the view (Shaders.cpp).
constexpr double kDepth = 4.0;
/// The nearest a star is drawn: closer, it has left the frame anyway.
constexpr double kNear = 0.02;
/// The tunnel and the clouds repeat every kLongPeriod along the line of
/// flight (their noise lattices wrap there), so the distance handed to the
/// GPU for them never grows either.
constexpr double kLongPeriod = 64.0 * kDepth;

/// Each look's transit speed at Top Speed 1, in D per second.
double TransitSpeed( Look look );

/// Where a black hole comes in from, in GM/c^2: far enough that its shadow is
/// a fraction of a pixel. Geodesic.h's table must reach every ray from here.
constexpr double kHoleFar = 1.0e5;

/// What a look's script says at one instant. Everything the renderer needs
/// that changes over a journey.
struct Pose
{
	double speed     = 0.0;///< 0 (cruise) .. 1 (transit): the share of Top Speed
	double tunnel    = 0.0;///< Hyperspace: the tunnel's light, 0..1
	double pull      = 0.0;///< Warp: the rubber band, a share of Stretch (negative overshoots)
	double clipScale = 1.0;///< Warp: the clip's size (1 in place, 0 gone into the vanishing point)
	double twist     = 0.0;///< Wormhole: the whirlpool's twist, a share of Swirl
	double mouth     = 0.0;///< Wormhole: the cloud mouth's radius, in frame heights (0 shut)
	double endLight  = 0.0;///< Wormhole: the light at the end, in frame heights
	double endGlow   = 0.0;///< Wormhole: that light's brightness, 0 .. 1
	double holeD     = 0.0;///< Black Hole: the camera's distance in GM/c^2; 0 = no hole
	double disc      = 0.0;///< Black Hole: how much of the disc's light, 0..1
	double blackout  = 0.0;///< the picture faded to black (the plunge), 0..1
	double scene     = 1.0;///< how much of normal space (stars, clip) shows, 0..1
};

/// The values a script needs from the controls.
struct ScriptSettings
{
	double stretch  = 0.4;///< Warp
	double holeHold = 12.0;///< Black Hole: the distance held in transit
};

/// The look's script at phase J (any real; read mod 1).
Pose PoseAt( Look look, double J, const ScriptSettings& settings );

/// The phases at which a look flashes, in [0, 1).
const std::vector< double >& FlashPoints( Look look );
/// A look's flash: its time constant in seconds.
double FlashDecay( Look look );

/// Where a camera at distance d sees the shadow's edge: the angle from the
/// hole's centre, asin( sqrt( 27 ) sqrt( 1 - 2/d ) / d ), d >= 3.
double ShadowAngle( double d );
/// The distance at which the shadow's angular radius is `angle` radians
/// (0 < angle <= pi/2), by bisection: the inverse of ShadowAngle.
double DistanceForShadow( double angle );

/**
    The drive: J, by Hold, Trip, Loop or Fader.
*/
struct DriveInputs
{
	Drive drive    = Drive::Loop;
	bool engage    = false;
	bool jumpPress = false;///< a rising edge this frame
	double fader   = 0.0;  ///< Journey, for Drive Fader
	double entry = 2.0, transit = 3.0, exit = 1.5, cruise = 4.0;///< seconds
};

class Journey
{
public:
	/// Advance by `dt` seconds. Returns J at the end of the frame, UNWRAPPED
	/// relative to the start (J0 + the distance moved, or the fader's value).
	void Advance( double dt, const DriveInputs& in );

	/// J at the start and end of the last frame: end is start plus the move
	/// (it may pass 1; read mod 1). In Fader mode, end may be below start.
	double Start() const
	{
		return start;
	}
	double End() const
	{
		return end;
	}
	/// J now, in [0, 1).
	double Phase() const;
	/// Back to normal space, no timers.
	void Reset();

	/// For the harness: the phase as a fresh drive would hold it.
	void SetPhaseForTest( double j )
	{
		start = end = phase = j;
		timer               = 0.0;
	}

private:
	double start = 0.0, end = 0.0;
	double phase = 0.0;///< J now, in [0, 1), landed exactly on the rests
	double timer = 0.0;///< Trip and Loop: time held in transit, or cruising
	bool tripping = false;
	bool lastFader = false;
};

/**
    The camera along the frame: distance flown at substeps through the frame,
    so the shutter's opening can be read anywhere in it.
*/
class Camera
{
public:
	static constexpr int kSubsteps = 16;

	/// One frame: J moved from j0 to j1 over dt; speed is cruise + top x the
	/// script's share, in D per second.
	void Advance( Look look, double j0, double j1, double dt, double cruise, double top, const ScriptSettings& settings );

	/// Distance flown at fraction `f` of the last frame (0 = its start, 1 =
	/// its end), relative to the end of the frame: always <= 0.
	double OffsetAt( double f ) const;
	/// The end of the frame's distance, mod the field's period D: what the
	/// stars are placed by.
	double PhaseZ() const
	{
		return zMod;
	}
	/// The same, mod kLongPeriod: what the tunnel's noise is placed by.
	double LongZ() const
	{
		return zLong;
	}
	/// The speed at the end of the frame, D per second.
	double Speed() const
	{
		return speedNow;
	}
	/// The whole distance flown (the harness reads it).
	double Total() const
	{
		return total;
	}
	/// For the harness: the camera put at distance z (mod the periods).
	void SetForTest( double z )
	{
		Reset();
		zMod  = std::fmod( z, kDepth );
		zLong = std::fmod( z, kLongPeriod );
	}
	void Reset()
	{
		total = 0.0;
		zMod  = 0.0;
		zLong = 0.0;
		samples.fill( 0.0 );
		speedNow = 0.0;
	}

private:
	double total = 0.0, zMod = 0.0, zLong = 0.0;
	/// samples[ i ]: distance at fraction i / kSubsteps of the frame, minus
	/// the distance at its end.
	std::array< double, kSubsteps + 1 > samples {};
	double speedNow = 0.0;
};

} // namespace slipstream
