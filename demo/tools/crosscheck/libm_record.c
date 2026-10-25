/*
 * Records every libm call the plugin's C++ makes, for the cross-check's replay.
 *
 *     DYLD_INSERT_LIBRARIES=librecord.dylib LIBM_RECORD=calls.bin ./printer
 *
 * Why this exists: JavaScript's Math.exp, Math.log, Math.pow and friends are
 * V8's own (fdlibm's algorithms), the plugin's std::exp and friends are Apple's
 * libm, and the two disagree in the last bit of a few results in a hundred.
 * Neither is wrong -- neither promises correct rounding -- but a port compared
 * through them can only be compared to within a tolerance, and a tolerance
 * hides the small port errors as well as the libm's. So the cross-check runs
 * the C++ with this interposed (dyld's __interpose section: every call from the
 * printer's code to libm comes here first), writes down each call's argument
 * and result bits, and run.mjs answers the port's Math calls from that record.
 * With the libm taken out, the port's arithmetic must match the plugin's to the
 * bit; crosscheck.sh reports the libm's own differences separately.
 *
 * Record format, little-endian: one byte the function, then its argument(s)
 * and result(s) as raw IEEE bits (8 bytes a double, 4 a float).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* math.h declares __sincos_stret( double ) returning struct __double2 { __sinval, __cosval }:
   clang on Darwin turns a std::sin and a std::cos of the same argument into one call of it. */

static FILE* out;
static int opened;

static void put( uint8_t fn, const void* data, size_t size )
{
	if( !opened )
	{
		opened         = 1;
		const char* to = getenv( "LIBM_RECORD" );
		out            = to ? fopen( to, "wb" ) : NULL;
	}
	if( !out )
		return;
	fputc( fn, out );
	fwrite( data, 1, size, out );
}

#define ONE( id, name )                                \
	static double record_##name( double x )            \
	{                                                  \
		double r = name( x );                          \
		double buf[ 2 ] = { x, r };                    \
		put( id, buf, sizeof buf );                    \
		return r;                                      \
	}

ONE( 1, exp )
ONE( 2, expm1 )
ONE( 3, log )
ONE( 4, log2 )
ONE( 5, exp2 )
ONE( 6, sin )
ONE( 7, cos )
ONE( 8, tan )
ONE( 9, asin )
ONE( 10, atan )

static double record_pow( double x, double y )
{
	double r = pow( x, y );
	double buf[ 3 ] = { x, y, r };
	put( 11, buf, sizeof buf );
	return r;
}
static float record_hypotf( float x, float y )
{
	float r = hypotf( x, y );
	float buf[ 3 ] = { x, y, r };
	put( 12, buf, sizeof buf );
	return r;
}
static struct __double2 record_sincos( double x )
{
	struct __double2 r = __sincos_stret( x );
	double buf[ 3 ] = { x, r.__sinval, r.__cosval };
	put( 13, buf, sizeof buf );
	return r;
}

__attribute__( ( destructor ) ) static void finish( void )
{
	if( out )
		fclose( out );
	out = NULL;
}

#define INTERPOSE( replacement, replacee ) \
	{ (const void*)(unsigned long)&replacement, (const void*)(unsigned long)&replacee }

__attribute__( ( used ) ) static const struct
{
	const void* replacement;
	const void* replacee;
} interposers[] __attribute__( ( section( "__DATA,__interpose" ) ) ) = {
	INTERPOSE( record_exp, exp ),       INTERPOSE( record_expm1, expm1 ), INTERPOSE( record_log, log ),
	INTERPOSE( record_log2, log2 ),     INTERPOSE( record_exp2, exp2 ),   INTERPOSE( record_sin, sin ),
	INTERPOSE( record_cos, cos ),       INTERPOSE( record_tan, tan ),     INTERPOSE( record_asin, asin ),
	INTERPOSE( record_atan, atan ),     INTERPOSE( record_pow, pow ),     INTERPOSE( record_hypotf, hypotf ),
	INTERPOSE( record_sincos, __sincos_stret ),
};
