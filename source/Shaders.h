#pragma once

#include <string>

/**
    The GLSL, as text.

    `kCommon` is a LIBRARY, not a shader: no #version, no main. Every pass is
    assembled as kVersion + kCommon + its piece, so the harness's checks run
    the text the plugin runs. tools/glslc.sh reassembles them for glslc.

    Light is carried as EXPOSURE, not as pixel values: everything adds in
    exposure, and the composite develops it as film does, 1 - exp( -E ) per
    channel. The clip (Over) enters through the inverse of that curve, so at
    rest it comes back to the bit.

    Passes, in order:

      intake     (Over) the clip, sRGB to linear to exposure, into a
                 mipmapped buffer the scene samples at any scale
      stars      instanced: one quad per star, its streak over the shutter,
                 the periodic field's copies counted exactly, additive
      scene      per look: the clip smeared, pulled, swirled or lensed; the
                 tunnel; the clouds; the black hole's sky, disc and corona;
                 with the stars laid in
      composite  the flash, the plunge's blackout, development, sRGB, Mix
*/
namespace slipstream::shaders
{
extern const char* const kVersion;
extern const char* const kCommon;

extern const char* const kQuadVertex;
extern const char* const kIntakeFragment;
extern const char* const kStarVertex;
extern const char* const kStarFragment;
extern const char* const kSceneFragment;
extern const char* const kCompositeFragment;

/// kVersion + kCommon + the piece.
std::string Assemble( const char* piece );

} // namespace slipstream::shaders
