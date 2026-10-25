#include "Slipstream.h"

/**
    The effect: the clip is the view ahead. It smears into the streaks,
    is pulled in by the rubber band and snaps away, twists into the
    whirlpool, or hangs behind the black hole, lensed.

    The name is the fleet's "Over", and yes, it is also a knitted tank top.
    See SourcePlugin.cpp for why this file is listed in its own target.
*/
namespace
{
class SlipstreamEffect : public slipstream::SlipstreamPlugin
{
public:
	SlipstreamEffect() :
		SlipstreamPlugin( true )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< SlipstreamEffect >,// Create method
	"SR02",                           // Plugin unique ID of maximum length 4
	"SW Slip Over",                   // Plugin name
	2,                                // API major version number
	1,                                // API minor version number
	0,                                // Plugin major version number
	1,                                // Plugin minor version number
	FF_EFFECT,                        // Plugin type
	"Your clip as the view ahead of a ship: it smears into hyperspace streaks, stretches and snaps away at warp, "
	"twists into a wormhole's whirlpool, or hangs behind a black hole, bent into an Einstein ring. Mix brings it "
	"back.",
	"Slipstream FFGL effect"          // About
);

extern "C" const char* SlipstreamEffectBuildStamp()
{
	return "slipstream " SLIPSTREAM_VERSION " effect, built " __DATE__ " " __TIME__;
}
