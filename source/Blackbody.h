#pragma once

#include <array>
#include <vector>

/**
    The colour of a blackbody, for the stars, the disc and the corona.

    Planck's law integrated against the CIE 1931 2-degree colour matching
    functions, in the multi-lobe Gaussian fit of Wyman, Sloan and Shirley
    (JCGT 2013), then to linear sRGB (IEC 61966-2-1's matrix). A Doppler or
    gravitational shift by g turns a blackbody at T into exactly a blackbody
    at g T (the spectrum's shape is invariant, I_nu / nu^3), so the disc's
    light is this table read at g T: the shift is a lookup, not a hue twist.

    The table is 256 temperatures, geometric from kMinKelvin to kMaxKelvin.
    Each entry is the chromaticity scaled to unit luminance (Y = 1) in linear
    sRGB, out-of-gamut negatives lifted by adding white, and log2 of the
    luminance relative to the luminance at 6500 K.
*/
namespace slipstream::blackbody
{
constexpr int kEntries       = 256;
constexpr double kMinKelvin  = 800.0;
constexpr double kMaxKelvin  = 120000.0;

struct Entry
{
	float rgb[ 3 ];   ///< linear sRGB, Y = 1
	float log2Y;      ///< log2( Y( T ) / Y( 6500 K ) )
};

/// The table, built once.
const std::array< Entry, kEntries >& Table();

/// CIE 1931 xy chromaticity of a blackbody at T, from the same integral.
void Chromaticity( double kelvin, double& x, double& y );

/// Relative luminance Y( T ) / Y( 6500 K ), from the same integral.
double RelativeLuminance( double kelvin );

/// The table's coordinate for a temperature, 0..1 (centre of the first and
/// last texels at 0.5 / kEntries and 1 - 0.5 / kEntries).
double Coordinate( double kelvin );

} // namespace slipstream::blackbody
