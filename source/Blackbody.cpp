#include "Blackbody.h"

#include <algorithm>
#include <cmath>

namespace slipstream::blackbody
{
namespace
{
/// Wyman, Sloan and Shirley's piecewise Gaussian: a different width on
/// each side of the peak.
double lobe( double lambda, double mu, double below, double above )
{
	const double t = ( lambda - mu ) / ( lambda < mu ? below : above );
	return std::exp( -0.5 * t * t );
}
void cie( double lambda, double& x, double& y, double& z )
{
	x = 1.056 * lobe( lambda, 599.8, 37.9, 31.0 ) + 0.362 * lobe( lambda, 442.0, 16.0, 26.7 )
	    - 0.065 * lobe( lambda, 501.1, 20.4, 26.2 );
	y = 0.821 * lobe( lambda, 568.8, 46.9, 40.5 ) + 0.286 * lobe( lambda, 530.9, 16.3, 31.1 );
	z = 1.217 * lobe( lambda, 437.0, 11.8, 36.0 ) + 0.681 * lobe( lambda, 459.0, 26.0, 13.8 );
}
/// Planck's spectral radiance, up to a constant (2hc^2), lambda in nm.
double planck( double lambdaNm, double kelvin )
{
	constexpr double c2 = 1.438776877e7;///< hc / k, in nm K
	const double l      = lambdaNm;
	return 1.0 / ( l * l * l * l * l * std::expm1( c2 / ( l * kelvin ) ) );
}
/// XYZ of a blackbody, 360 to 830 nm in 1 nm steps (Simpson).
void xyzOf( double kelvin, double& X, double& Y, double& Z )
{
	X = Y = Z = 0.0;
	for( int i = 0; i <= 470; ++i )
	{
		const double lambda = 360.0 + i;
		const double w      = ( i == 0 || i == 470 ) ? 1.0 : ( i % 2 ? 4.0 : 2.0 );
		double x, y, z;
		cie( lambda, x, y, z );
		const double p = planck( lambda, kelvin ) * w;
		X += p * x;
		Y += p * y;
		Z += p * z;
	}
}
double kelvinAt( int i )
{
	return kMinKelvin * std::pow( kMaxKelvin / kMinKelvin, ( i + 0.5 ) / kEntries );
}
} // namespace

void Chromaticity( double kelvin, double& x, double& y )
{
	double X, Y, Z;
	xyzOf( kelvin, X, Y, Z );
	const double sum = X + Y + Z;
	x                = X / sum;
	y                = Y / sum;
}

double RelativeLuminance( double kelvin )
{
	double X, Y, Z, X0, Y0, Z0;
	xyzOf( kelvin, X, Y, Z );
	xyzOf( 6500.0, X0, Y0, Z0 );
	return Y / Y0;
}

double Coordinate( double kelvin )
{
	const double t = std::log( std::clamp( kelvin, kMinKelvin, kMaxKelvin ) / kMinKelvin ) / std::log( kMaxKelvin / kMinKelvin );
	return t;
}

const std::array< Entry, kEntries >& Table()
{
	static const std::array< Entry, kEntries > table = [] {
		std::array< Entry, kEntries > out {};
		double X0, Y0, Z0;
		xyzOf( 6500.0, X0, Y0, Z0 );
		for( int i = 0; i < kEntries; ++i )
		{
			double X, Y, Z;
			xyzOf( kelvinAt( i ), X, Y, Z );
			double rgb[ 3 ] = { 3.2406 * X - 1.5372 * Y - 0.4986 * Z, -0.9689 * X + 1.8758 * Y + 0.0415 * Z,
				                0.0557 * X - 0.2040 * Y + 1.0570 * Z };
			for( double& c : rgb )
				c /= Y;
			//Out of gamut (the reddest embers): lift by white until nothing
			//is negative, then put the luminance back to 1.
			const double low = std::min( { rgb[ 0 ], rgb[ 1 ], rgb[ 2 ] } );
			if( low < 0.0 )
			{
				for( double& c : rgb )
					c -= low;
				const double lum = 0.2126 * rgb[ 0 ] + 0.7152 * rgb[ 1 ] + 0.0722 * rgb[ 2 ];
				for( double& c : rgb )
					c /= lum;
			}
			for( int c = 0; c < 3; ++c )
				out[ i ].rgb[ c ] = static_cast< float >( rgb[ c ] );
			out[ i ].log2Y = static_cast< float >( std::log2( Y / Y0 ) );
		}
		return out;
	}();
	return table;
}

} // namespace slipstream::blackbody
