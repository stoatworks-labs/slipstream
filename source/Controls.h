#pragma once

#include "StoatworksAboutLinks.h"

#include <vector>

/**
    The host's parameters, and what they mean in the flight's units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange` could
    widen it. The conversions live in Controls.cpp, one function per control.
    Option, boolean and event parameters hold the element value itself; Stars
    is a real integer (`FF_TYPE_INTEGER`).

    Units: the star field's depth D (the field repeats every D along the line
    of flight, and every speed is in D per second), seconds, degrees, kelvin,
    and, for the black hole, GM/c^2 (so the event horizon is at r = 2).
*/
namespace slipstream
{
/**
    Every control either plugin has, by what it is -- NOT by the index a host
    sees. Each plugin declares its own subset, densely, in the order of
    `HostOrder()`, with the About block LAST in both.
*/
enum ParamId : unsigned int
{
	// -- Journey -------------------------------------------------------------
	PT_LOOK = 0,
	PT_DRIVE,
	PT_ENGAGE,
	PT_JUMP,
	PT_JOURNEY,
	PT_ENTRY_TIME,
	PT_TRANSIT_TIME,
	PT_EXIT_TIME,
	PT_CRUISE_TIME,

	// -- Flight --------------------------------------------------------------
	PT_CRUISE_SPEED,
	PT_TOP_SPEED,
	PT_HEADING_X,
	PT_HEADING_Y,
	PT_FIELD_OF_VIEW,
	PT_SHUTTER,

	// -- Light ---------------------------------------------------------------
	PT_STARS,
	PT_STAR_COLOUR,
	PT_EXPOSURE,
	PT_FLASH,
	PT_TINT,

	// -- Hyperspace ----------------------------------------------------------
	PT_TUNNEL,

	// -- Warp ----------------------------------------------------------------
	PT_STRETCH,
	PT_DISPERSION,

	// -- Wormhole ------------------------------------------------------------
	PT_SWIRL,
	PT_CLOUDS,

	// -- Black Hole ----------------------------------------------------------
	PT_HOLE_SIZE,
	PT_DISC,
	PT_DISC_TILT,
	PT_DISC_SIZE,
	PT_DISC_HEAT,
	PT_DOPPLER,
	PT_CORONA,

	// -- Clip (Over only) ----------------------------------------------------
	PT_CLIP_DEPTH,
	PT_CLIP_THROUGH,
	PT_MIX,

	// -- The Stoatworks About block: a text line, then one button per link.
	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

/// The ids a plugin declares, in the order the host sees them. Host index i
/// is `HostOrder( effect )[ i ]`. The About block is last in both.
const std::vector< unsigned int >& HostOrder( bool effect );

/// The group each control is shown under.
const char* GroupOf( unsigned int id );

/// The display name of a control.
const char* NameOf( unsigned int id );

enum class Look
{
	Hyperspace = 0,///< points stretch to streaks; the tunnel
	Warp,          ///< the rubber band, the flash, warp streaks
	Wormhole,      ///< the whirlpool, then clouds
	BlackHole,     ///< Schwarzschild: shadow, lensing, disc, corona
	Count
};
const char* LookName( Look look );

enum class Drive
{
	Hold = 0,///< Engage on goes in and stays; off comes out
	Trip,    ///< each Jump press is one round trip
	Loop,    ///< round trips for ever, Cruise Time between
	Fader,   ///< Journey sets the phase
	Count
};
const char* DriveName( Drive drive );

int OptionIndex( float value, int count );

//---------------------------------------------------------------------------
// The mappings.
//---------------------------------------------------------------------------

/// Entry, Transit, Exit and Cruise Time: 0.2 to 20 seconds, geometrically.
double SecondsFromParam( float v );
float ParamFromSeconds( double seconds );

/// Cruise Speed in D per second: 0 at the bottom (the stars stand still),
/// then 0.005 to 2, geometrically.
double CruiseFromParam( float v );
float ParamFromCruise( double depthsPerSecond );
/// Top Speed: a multiplier on the look's own transit speed, 0.1 to 10.
double TopFromParam( float v );
float ParamFromTop( double multiplier );

/// Heading X and Y: where the vanishing point sits, -1 (left, bottom edge)
/// to +1 (right, top edge) of the frame's half-size.
double HeadingFromParam( float v );
/// The vertical field of view, 20 to 120 degrees.
double FieldOfViewFromParam( float v );
float ParamFromFieldOfView( double degrees );
/// The shutter's opening as a fraction of the frame: 0 to 1 (0 to 360 deg).
double ShutterFromParam( float v );

/// Stars: a real integer, 0 to kMaxStars.
constexpr int kMaxStars = 20000;
/// Exposure as a linear multiplier: -4 to +4 EV, 0 EV at 0.5.
double ExposureFromParam( float v );
float ParamFromExposure( double ev );

/// Stretch: the rubber band's pull towards the vanishing point, 0 to 0.95 of
/// the radius at the core.
double StretchFromParam( float v );
/// Swirl: the whirlpool's twist at its core, 0 to 6 turns.
double SwirlTurnsFromParam( float v );

/// Hole Size: the shadow's angular radius as a fraction of the vertical
/// half field of view, 0.05 to 0.9.
double HoleSizeFromParam( float v );
/// Disc Tilt: the disc's inclination, 0 (face on) to 90 degrees (edge on).
double DiscTiltFromParam( float v );
/// Disc Size: the disc's outer radius, 8 to 40 GM/c^2.
double DiscOuterFromParam( float v );
/// Disc Heat: the hottest the disc gets, 2000 to 40000 K, geometrically.
double DiscHeatFromParam( float v );
float ParamFromDiscHeat( double kelvin );

/// Clip Depth (Over): how far ahead the clip rides, 0.02 to 2 D, geometrically.
double ClipDepthFromParam( float v );
float ParamFromClipDepth( double depths );

} // namespace slipstream
