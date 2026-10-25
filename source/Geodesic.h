#pragma once

#include <vector>

/**
    Light round a Schwarzschild black hole, tabulated once in double.

    Units G = M = c = 1: the event horizon is at r = 2, the photon sphere at
    r = 3, and a ray from infinity with impact parameter b < sqrt( 27 ) is
    captured. With u = 1/r, a ray's orbit obeys

        ( du/dphi )^2 = 1/b^2 - u^2 + 2u^3,  so  u'' = -u + 3u^2,

    and it is the same curve whoever sits on it: the camera, wherever it is,
    is a point on an orbit that came in from infinity. So one table per
    branch, indexed by b, answers everything:

      - escape (b > sqrt 27): the incoming half, from infinity (u = 0) to
        periapsis u_end; the outgoing half is its mirror image about phi_end.
      - capture (b < sqrt 27): from infinity to the horizon, u_end = 1/2.

    For each b the table holds phi_in( t ), the angle swept from infinity to
    u = u_end ( 1 - t^2 ) (t = 1 at infinity, 0 at the end: phi has a square
    root singularity at a periapsis, linear in t), and u( s ) / u_end at
    phi = s phi_end. Near sqrt 27 phi_end grows like -ln( b - sqrt 27 ) (the
    photon ring), so the b axis is ln| b - sqrt 27 |.

    The orbits are integrated with RK4 in phi (no singularity anywhere on
    that form), the periapsis and the horizon found by interpolation on the
    trajectory. `--geodesic` holds the table to the weak-field deflection
    4/b, to b_c, to the photon ring's logarithmic winding, and to a second
    integration (quadrature of dphi = du / sqrt( F ) on the first-integral
    form).
*/
namespace slipstream::geodesic
{
constexpr double kCritical = 5.196152422706632;///< sqrt( 27 )

constexpr int kB = 512;   ///< b samples per branch
constexpr int kT = 512;   ///< phi_in samples over t
constexpr int kS = 512;   ///< u samples over s
constexpr double kLogMin = -14.0;///< ln| b - b_c | at the edge of the photon ring
constexpr double kEscapeLogMax = 10.0;///< ln( b - b_c ) at the far end: b ~ 22000

struct Tables
{
	/// Row-major [ b ][ j ]: R = phi_in( t_j ), G = u( s_j ) / u_end, for the
	/// escape branch; B, A the same for the capture branch. t_j = s_j = j / ( kT - 1 ).
	std::vector< float > orbits;///< kB x kT x 4
	/// Per b: R = u_end, G = phi_end (escape); B = u_end (1/2), A = phi_end (capture).
	std::vector< float > ends;  ///< kB x 4

	/// The same in double, for the harness.
	std::vector< double > phiEscape, uEscape, phiCapture, uCapture;
	std::vector< double > uEndEscape, phiEndEscape, phiEndCapture;
};

/// Built on first use.
const Tables& Get();

/// b of row i of each branch.
double EscapeB( int i );
double CaptureB( int i );
/// The row coordinate of a b, 0..kB-1 (fractional), per branch.
double EscapeRow( double b );
double CaptureRow( double b );

/// One orbit from infinity, by RK4 in phi with step h. Returns phi at the
/// end (periapsis for escape, the horizon for capture) and u_end.
/// `newtonian` drops the 3u^2 term: the --geodesic negative control.
void Integrate( double b, double h, double& phiEnd, double& uEnd, bool newtonian = false );

/// The same by quadrature of the first integral: the second method.
double QuadraturePhiEnd( double b );

/// What the table says: the total deflection of a ray from infinity to
/// infinity, 2 phi_end - pi (escape branch only).
double Deflection( double b );

} // namespace slipstream::geodesic
