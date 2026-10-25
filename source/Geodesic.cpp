#include "Geodesic.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <mutex>

namespace slipstream::geodesic
{
namespace
{
constexpr double kStep = 2.0e-3;///< RK4's step in phi, radians
constexpr double kCaptureLogMax = 1.6447;///< ln( b_c - 0.01 ): b down to 0.01

struct Sample
{
	double phi, u, w;///< w = du/dphi
};

double accel( double u, bool newtonian )
{
	return newtonian ? -u : -u + 3.0 * u * u;
}

/// The orbit from infinity (u = 0, du/dphi = 1/b), to periapsis (escape:
/// w falls through 0) or the horizon (capture: u reaches 1/2). The last
/// sample is the end, found by interpolation.
std::vector< Sample > trajectory( double b, double h, bool newtonian )
{
	std::vector< Sample > out;
	out.reserve( 4096 );
	double phi = 0.0, u = 0.0, w = 1.0 / b;
	out.push_back( { phi, u, w } );
	for( int guard = 0; guard < 400000; ++guard )
	{
		const double k1u = w, k1w = accel( u, newtonian );
		const double k2u = w + 0.5 * h * k1w, k2w = accel( u + 0.5 * h * k1u, newtonian );
		const double k3u = w + 0.5 * h * k2w, k3w = accel( u + 0.5 * h * k2u, newtonian );
		const double k4u = w + h * k3w, k4w = accel( u + h * k3u, newtonian );
		const double nu = u + h / 6.0 * ( k1u + 2.0 * k2u + 2.0 * k3u + k4u );
		const double nw = w + h / 6.0 * ( k1w + 2.0 * k2w + 2.0 * k3w + k4w );
		if( nw <= 0.0 )
		{
			//Periapsis inside this step: where w crosses zero, by the
			//straight line between the two, refined by the Hermite cubic's
			//turning point would be overkill at h = 2e-3: w is smooth.
			const double f = w / ( w - nw );
			const double pphi = phi + f * h;
			//u at the turning point from the cubic Hermite through both ends.
			const double s = f, s2 = s * s, s3 = s2 * s;
			const double pu = ( 2 * s3 - 3 * s2 + 1 ) * u + ( s3 - 2 * s2 + s ) * h * w + ( -2 * s3 + 3 * s2 ) * nu
			                  + ( s3 - s2 ) * h * nw;
			out.push_back( { pphi, pu, 0.0 } );
			return out;
		}
		if( nu >= 0.5 )
		{
			const double f = ( 0.5 - u ) / ( nu - u );
			out.push_back( { phi + f * h, 0.5, w + f * ( nw - w ) } );
			return out;
		}
		phi += h;
		u = nu;
		w = nw;
		out.push_back( { phi, u, w } );
	}
	return out;
}

/// phi where u = target on the (monotone) trajectory, by Hermite inversion
/// on the bracketing step: dphi/du = 1/w, which is finite except at the very
/// end of an escape, where the end sample itself is returned.
double phiAtU( const std::vector< Sample >& path, double target )
{
	if( target <= 0.0 )
		return 0.0;
	if( target >= path.back().u )
		return path.back().phi;
	size_t lo = 0, hi = path.size() - 1;
	while( hi - lo > 1 )
	{
		const size_t mid = ( lo + hi ) / 2;
		( path[ mid ].u < target ? lo : hi ) = mid;
	}
	const Sample& a = path[ lo ];
	const Sample& c = path[ hi ];
	const double f = ( target - a.u ) / ( c.u - a.u );
	return a.phi + f * ( c.phi - a.phi );
}

/// u at phi on the trajectory, by the cubic Hermite through (u, w).
double uAtPhi( const std::vector< Sample >& path, double phi )
{
	if( phi <= 0.0 )
		return 0.0;
	if( phi >= path.back().phi )
		return path.back().u;
	size_t lo = 0, hi = path.size() - 1;
	while( hi - lo > 1 )
	{
		const size_t mid = ( lo + hi ) / 2;
		( path[ mid ].phi < phi ? lo : hi ) = mid;
	}
	const Sample& a = path[ lo ];
	const Sample& c = path[ hi ];
	const double h = c.phi - a.phi;
	if( h <= 0.0 )
		return a.u;
	const double s = ( phi - a.phi ) / h, s2 = s * s, s3 = s2 * s;
	return ( 2 * s3 - 3 * s2 + 1 ) * a.u + ( s3 - 2 * s2 + s ) * h * a.w + ( -2 * s3 + 3 * s2 ) * c.u + ( s3 - s2 ) * h * c.w;
}

void fillRow( double b, std::vector< double >& phiRow, std::vector< double >& uRow, double& uEnd, double& phiEnd )
{
	const std::vector< Sample > path = trajectory( b, kStep, false );
	uEnd   = path.back().u;
	phiEnd = path.back().phi;
	for( int j = 0; j < kT; ++j )
	{
		const double t = static_cast< double >( j ) / ( kT - 1 );
		phiRow[ j ]    = phiAtU( path, uEnd * ( 1.0 - t * t ) );
	}
	for( int j = 0; j < kS; ++j )
	{
		const double s = static_cast< double >( j ) / ( kS - 1 );
		uRow[ j ]      = uAtPhi( path, s * phiEnd ) / uEnd;
	}
}
} // namespace

double EscapeB( int i )
{
	return kCritical + std::exp( kLogMin + ( kEscapeLogMax - kLogMin ) * i / ( kB - 1 ) );
}
double CaptureB( int i )
{
	return kCritical - std::exp( kLogMin + ( kCaptureLogMax - kLogMin ) * i / ( kB - 1 ) );
}
double EscapeRow( double b )
{
	const double l = std::log( std::max( b - kCritical, 1.0e-300 ) );
	return std::clamp( ( l - kLogMin ) / ( kEscapeLogMax - kLogMin ) * ( kB - 1 ), 0.0, kB - 1.0 );
}
double CaptureRow( double b )
{
	const double l = std::log( std::max( kCritical - b, 1.0e-300 ) );
	return std::clamp( ( l - kLogMin ) / ( kCaptureLogMax - kLogMin ) * ( kB - 1 ), 0.0, kB - 1.0 );
}

void Integrate( double b, double h, double& phiEnd, double& uEnd, bool newtonian )
{
	const std::vector< Sample > path = trajectory( b, h, newtonian );
	phiEnd = path.back().phi;
	uEnd   = path.back().u;
}

double QuadraturePhiEnd( double b )
{
	//The first integral: phi = int du / sqrt( F ), F = 1/b^2 - u^2 + 2u^3.
	const double ib2 = 1.0 / ( b * b );
	auto F           = [ ib2 ]( double u ) { return ib2 - u * u + 2.0 * u * u * u; };
	double uEnd      = 0.5;
	const bool escape = b > kCritical;
	if( escape )
	{
		//The periapsis: F's smallest positive root, below 1/3.
		double lo = 0.0, hi = 1.0 / 3.0;
		for( int i = 0; i < 200; ++i )
		{
			const double mid = 0.5 * ( lo + hi );
			( F( mid ) > 0.0 ? lo : hi ) = mid;
		}
		uEnd = lo;
	}
	//u = uEnd ( 1 - tau^2 ): the periapsis's square root becomes linear.
	auto integrand = [ & ]( double tau ) {
		const double u = uEnd * ( 1.0 - tau * tau );
		const double f = F( u );
		if( f <= 0.0 )
			return 0.0;
		return 2.0 * uEnd * tau / std::sqrt( f );
	};
	//Adaptive Simpson, to a tolerance far below the table's.
	std::function< double( double, double, double, double, double, double, int ) > adapt;
	adapt = [ & ]( double a, double c, double fa, double fm, double fc, double whole, int depth ) -> double {
		const double m = 0.5 * ( a + c );
		const double lm = 0.5 * ( a + m ), rm = 0.5 * ( m + c );
		const double flm = integrand( lm ), frm = integrand( rm );
		const double left  = ( m - a ) / 6.0 * ( fa + 4.0 * flm + fm );
		const double right = ( c - m ) / 6.0 * ( fm + 4.0 * frm + fc );
		if( depth <= 0 || std::fabs( left + right - whole ) < 1.0e-13 * 15.0 )
			return left + right + ( left + right - whole ) / 15.0;
		return adapt( a, m, fa, flm, fm, left, depth - 1 ) + adapt( m, c, fm, frm, fc, right, depth - 1 );
	};
	//Split at a geometric ladder towards tau = 0, where a near-critical
	//periapsis puts all the weight.
	double total = 0.0, top = 1.0;
	for( int k = 0; k < 40; ++k )
	{
		const double bottom = top * 0.5;
		const double fa = integrand( bottom ), fc = integrand( top ), fm = integrand( 0.5 * ( bottom + top ) );
		total += adapt( bottom, top, fa, fm, fc, ( top - bottom ) / 6.0 * ( fa + 4.0 * fm + fc ), 40 );
		top = bottom;
	}
	return total;
}

double Deflection( double b )
{
	const Tables& t = Get();
	const double row = EscapeRow( b );
	const int i      = std::min( static_cast< int >( row ), kB - 2 );
	const double f   = row - i;
	const double phi = t.phiEndEscape[ i ] + f * ( t.phiEndEscape[ i + 1 ] - t.phiEndEscape[ i ] );
	return 2.0 * phi - 3.14159265358979323846;
}

const Tables& Get()
{
	static Tables tables;
	static std::once_flag once;
	std::call_once( once, [] {
		Tables& t = tables;
		t.orbits.assign( static_cast< size_t >( kB ) * kT * 4, 0.0f );
		t.ends.assign( static_cast< size_t >( kB ) * 4, 0.0f );
		t.phiEscape.assign( static_cast< size_t >( kB ) * kT, 0.0 );
		t.uEscape.assign( static_cast< size_t >( kB ) * kS, 0.0 );
		t.phiCapture.assign( static_cast< size_t >( kB ) * kT, 0.0 );
		t.uCapture.assign( static_cast< size_t >( kB ) * kS, 0.0 );
		t.uEndEscape.assign( kB, 0.0 );
		t.phiEndEscape.assign( kB, 0.0 );
		t.phiEndCapture.assign( kB, 0.0 );
		std::vector< double > phiRow( kT ), uRow( kS );
		for( int i = 0; i < kB; ++i )
		{
			double uEnd = 0.0, phiEnd = 0.0;
			fillRow( EscapeB( i ), phiRow, uRow, uEnd, phiEnd );
			t.uEndEscape[ i ]   = uEnd;
			t.phiEndEscape[ i ] = phiEnd;
			for( int j = 0; j < kT; ++j )
			{
				t.phiEscape[ static_cast< size_t >( i ) * kT + j ]              = phiRow[ j ];
				t.orbits[ ( static_cast< size_t >( i ) * kT + j ) * 4 + 0 ] = static_cast< float >( phiRow[ j ] );
			}
			for( int j = 0; j < kS; ++j )
			{
				t.uEscape[ static_cast< size_t >( i ) * kS + j ]               = uRow[ j ];
				t.orbits[ ( static_cast< size_t >( i ) * kT + j ) * 4 + 1 ] = static_cast< float >( uRow[ j ] );
			}
			t.ends[ i * 4 + 0 ] = static_cast< float >( EscapeB( i ) * uEnd );
			t.ends[ i * 4 + 1 ] = static_cast< float >( phiEnd );

			fillRow( CaptureB( i ), phiRow, uRow, uEnd, phiEnd );
			t.phiEndCapture[ i ] = phiEnd;
			for( int j = 0; j < kT; ++j )
			{
				t.phiCapture[ static_cast< size_t >( i ) * kT + j ]             = phiRow[ j ];
				t.orbits[ ( static_cast< size_t >( i ) * kT + j ) * 4 + 2 ] = static_cast< float >( phiRow[ j ] );
			}
			for( int j = 0; j < kS; ++j )
			{
				t.uCapture[ static_cast< size_t >( i ) * kS + j ]              = uRow[ j ];
				t.orbits[ ( static_cast< size_t >( i ) * kT + j ) * 4 + 3 ] = static_cast< float >( uRow[ j ] );
			}
			t.ends[ i * 4 + 2 ] = static_cast< float >( uEnd );
			t.ends[ i * 4 + 3 ] = static_cast< float >( phiEnd );
		}
	} );
	return tables;
}

} // namespace slipstream::geodesic
