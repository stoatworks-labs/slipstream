#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace slipstream
{
namespace
{
double clamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}
constexpr double kSecondsLow = 0.2, kSecondsHigh = 20.0;
constexpr double kCruiseLow = 0.005, kCruiseHigh = 2.0;
constexpr double kTopLow = 0.1, kTopHigh = 10.0;
constexpr double kHeatLow = 2000.0, kHeatHigh = 40000.0;
constexpr double kClipDepthLow = 0.02, kClipDepthHigh = 2.0;
} // namespace

const std::vector< unsigned int >& HostOrder( bool effect )
{
	auto build = []( bool over ) {
		std::vector< unsigned int > order;
		for( unsigned int id = PT_LOOK; id <= PT_CORONA; ++id )
			order.push_back( id );
		if( over )
			order.insert( order.end(), { PT_CLIP_DEPTH, PT_CLIP_THROUGH, PT_MIX } );
		for( unsigned int id = PT_ABOUT_TEXT; id < PT_COUNT; ++id )
			order.push_back( id );
		return order;
	};
	static const std::vector< unsigned int > source = build( false ), over = build( true );
	return effect ? over : source;
}

const char* GroupOf( unsigned int id )
{
	if( id <= PT_CRUISE_TIME )
		return "Journey";
	if( id <= PT_SHUTTER )
		return "Flight";
	if( id <= PT_TINT )
		return "Light";
	if( id <= PT_TUNNEL )
		return "Hyperspace";
	if( id <= PT_DISPERSION )
		return "Warp";
	if( id <= PT_CLOUDS )
		return "Wormhole";
	if( id <= PT_CORONA )
		return "Black Hole";
	if( id <= PT_MIX )
		return "Clip";
	return "About";
}

const char* NameOf( unsigned int id )
{
	switch( id )
	{
	case PT_LOOK: return "Look";
	case PT_DRIVE: return "Drive";
	case PT_ENGAGE: return "Engage";
	case PT_JUMP: return "Jump";
	case PT_JOURNEY: return "Journey";
	case PT_ENTRY_TIME: return "Entry Time";
	case PT_TRANSIT_TIME: return "Transit Time";
	case PT_EXIT_TIME: return "Exit Time";
	case PT_CRUISE_TIME: return "Cruise Time";
	case PT_CRUISE_SPEED: return "Cruise Speed";
	case PT_TOP_SPEED: return "Top Speed";
	case PT_HEADING_X: return "Heading X";
	case PT_HEADING_Y: return "Heading Y";
	case PT_FIELD_OF_VIEW: return "Field of View";
	case PT_SHUTTER: return "Shutter";
	case PT_STARS: return "Stars";
	case PT_STAR_COLOUR: return "Star Colour";
	case PT_EXPOSURE: return "Exposure";
	case PT_FLASH: return "Flash";
	case PT_TINT: return "Tint";
	case PT_TUNNEL: return "Tunnel";
	case PT_STRETCH: return "Stretch";
	case PT_DISPERSION: return "Dispersion";
	case PT_SWIRL: return "Swirl";
	case PT_CLOUDS: return "Clouds";
	case PT_HOLE_SIZE: return "Hole Size";
	case PT_DISC: return "Disc";
	case PT_DISC_TILT: return "Disc Tilt";
	case PT_DISC_SIZE: return "Disc Size";
	case PT_DISC_HEAT: return "Disc Heat";
	case PT_DOPPLER: return "Doppler";
	case PT_CORONA: return "Corona";
	case PT_CLIP_DEPTH: return "Clip Depth";
	case PT_CLIP_THROUGH: return "Clip Through";
	case PT_MIX: return "Mix";
	default: return "?";
	}
}

const char* LookName( Look look )
{
	switch( look )
	{
	case Look::Hyperspace: return "Hyperspace";
	case Look::Warp: return "Warp";
	case Look::Wormhole: return "Wormhole";
	case Look::BlackHole: return "Black Hole";
	default: return "?";
	}
}

const char* DriveName( Drive drive )
{
	switch( drive )
	{
	case Drive::Hold: return "Hold";
	case Drive::Trip: return "Trip";
	case Drive::Loop: return "Loop";
	case Drive::Fader: return "Fader";
	default: return "?";
	}
}

int OptionIndex( float value, int count )
{
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

double SecondsFromParam( float v )
{
	return geometric( v, kSecondsLow, kSecondsHigh );
}
float ParamFromSeconds( double seconds )
{
	return inverseGeometric( seconds, kSecondsLow, kSecondsHigh );
}
double CruiseFromParam( float v )
{
	//Exactly zero at the bottom: the stars stand still, and a jump still
	//works, from rest.
	if( v <= 0.0f )
		return 0.0;
	return geometric( v, kCruiseLow, kCruiseHigh );
}
float ParamFromCruise( double depthsPerSecond )
{
	return depthsPerSecond <= 0.0 ? 0.0f : inverseGeometric( depthsPerSecond, kCruiseLow, kCruiseHigh );
}
double TopFromParam( float v )
{
	return geometric( v, kTopLow, kTopHigh );
}
float ParamFromTop( double multiplier )
{
	return inverseGeometric( multiplier, kTopLow, kTopHigh );
}
double HeadingFromParam( float v )
{
	return 2.0 * clamp01( v ) - 1.0;
}
double FieldOfViewFromParam( float v )
{
	return 20.0 + 100.0 * clamp01( v );
}
float ParamFromFieldOfView( double degrees )
{
	return static_cast< float >( std::clamp( ( degrees - 20.0 ) / 100.0, 0.0, 1.0 ) );
}
double ShutterFromParam( float v )
{
	return clamp01( v );
}
double ExposureFromParam( float v )
{
	return std::exp2( 8.0 * clamp01( v ) - 4.0 );
}
float ParamFromExposure( double ev )
{
	return static_cast< float >( std::clamp( ( ev + 4.0 ) / 8.0, 0.0, 1.0 ) );
}
double StretchFromParam( float v )
{
	return 0.95 * clamp01( v );
}
double SwirlTurnsFromParam( float v )
{
	return 6.0 * clamp01( v );
}
double HoleSizeFromParam( float v )
{
	return 0.05 + 0.85 * clamp01( v );
}
double DiscTiltFromParam( float v )
{
	return 90.0 * clamp01( v );
}
double DiscOuterFromParam( float v )
{
	return 8.0 + 32.0 * clamp01( v );
}
double DiscHeatFromParam( float v )
{
	return geometric( v, kHeatLow, kHeatHigh );
}
float ParamFromDiscHeat( double kelvin )
{
	return inverseGeometric( kelvin, kHeatLow, kHeatHigh );
}
double ClipDepthFromParam( float v )
{
	return geometric( v, kClipDepthLow, kClipDepthHigh );
}
float ParamFromClipDepth( double depths )
{
	return inverseGeometric( depths, kClipDepthLow, kClipDepthHigh );
}

} // namespace slipstream
