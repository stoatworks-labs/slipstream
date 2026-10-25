#include "Slipstream.h"

/**
    The source: space, a camera, a shutter, and four ways out of here.

    Listed directly in the SlipstreamSource target, not in slipstream_core:
    both plugins share the class and not the `CFFGLPluginInfo` below, and
    putting either registration in the shared library would register both
    plugins into both bundles. The core is an OBJECT library because this
    registers itself from a file-scope constructor nothing references (see
    CMakeLists.txt).

    `SW Slipstream` is thirteen characters; the FFGL name field is char[ 16 ]
    and not null-terminated. `oxbow probe` reads it back the way a host does.
*/
namespace
{
class SlipstreamSource : public slipstream::SlipstreamPlugin
{
public:
	SlipstreamSource() :
		SlipstreamPlugin( false )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< SlipstreamSource >,// Create method
	"SR01",                           // Plugin unique ID of maximum length 4
	"SW Slipstream",                  // Plugin name
	2,                                // API major version number
	1,                                // API minor version number
	0,                                // Plugin major version number
	1,                                // Plugin minor version number
	FF_SOURCE,                        // Plugin type
	"A camera flying through space with its shutter open: stars stretch into streaks as it accelerates, which is "
	"all the films' hyperspace ever was. Four looks: Hyperspace, Warp (the rubber band and the flash), Wormhole (a "
	"whirlpool of cloud) and Black Hole (Schwarzschild's: the shadow, a lensed sky, the disc and its corona).",
	"Slipstream FFGL source"          // About
);

extern "C" const char* SlipstreamSourceBuildStamp()
{
	return "slipstream " SLIPSTREAM_VERSION " source, built " __DATE__ " " __TIME__;
}
