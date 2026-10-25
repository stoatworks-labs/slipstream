#pragma once

#include "Controls.h"
#include "Flight.h"
#include "PassBuffer.h"

#include <FFGLSDK.h>

// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <vector>

namespace slipstream
{
/**
    The plugin: the source (space, with nothing in it but stars and whatever
    the look brings) and, with `isEffect`, the Over (the clip is the view
    ahead). One class, two registrations; see SourcePlugin.cpp and
    EffectPlugin.cpp.

    The flight is CPU state in double (Flight.h): the phase J, the camera's
    distance, the flash. Each frame hands the GPU only what the shutter saw:
    the distance mod the field's period, the distance flown while it was
    open, and each look's script at its opening and its closing.

    Host indices are NOT ParamIds: each plugin declares its own dense list,
    `HostOrder( isEffect )`. The host-facing calls translate; everything
    inside works in ParamIds.
*/
class SlipstreamPlugin : public CFFGLPlugin
{
public:
	explicit SlipstreamPlugin( bool isEffect );
	~SlipstreamPlugin() override;

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	/// The base class's stub fails, and a failed default deletes the instance
	/// -- so without this no real host can load the plugin.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;

	bool IsEffect() const
	{
		return isEffect;
	}
	unsigned int ParamCount() const
	{
		return static_cast< unsigned int >( hostOrder.size() );
	}
	int HostIndexOf( unsigned int id ) const
	{
		return id < PT_COUNT ? idToHost[ id ] : -1;
	}
	void SetById( unsigned int id, float value );
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	/// The journey's phase, now (Hold, Trip and Loop then carry on from it).
	void SetPhaseForTest( double j )
	{
		journey.SetPhaseForTest( j );
	}
	/// The camera's distance mod the field's period, from the next frame.
	void SetCameraForTest( double z )
	{
		cameraForTest = z;
	}
	/// --streak's negative control: a star's flux held at its shutter-close
	/// value along the whole streak.
	void SetFluxLawForTest( bool inverseSquare )
	{
		fluxLaw = inverseSquare;
	}
	/// --copies's negative control: the field's periodic copies not counted.
	void SetCopyLawForTest( bool copies )
	{
		copyLaw = copies;
	}
	/// --energy's negative control: a streak's light not spread over its length.
	void SetLengthLawForTest( bool spread )
	{
		lengthLaw = spread;
	}
	/// --identity's negative control: the clip's way into exposure skipped.
	void SetRoundTripForTest( bool on )
	{
		roundTrip = on;
	}
	/// --ring's negative control: straight rays.
	void SetBendLawForTest( bool bend )
	{
		bendLaw = bend;
	}
	/// --shadow's negative control: flat-space capture, b < 2.
	void SetCaptureLawForTest( bool critical )
	{
		captureLaw = critical;
	}
	/// --disc's negative control: the disc's spin flipped in its shift only.
	void SetSpinSignForTest( float sign )
	{
		spinSign = sign;
	}
	/// --clock's negative control: elapsed time from the host clock in float.
	void SetFloatClockForTest( bool on )
	{
		floatClockForTest = on;
	}
	/// One star, white, at (x, y) across the line of flight (field units),
	/// z0 along the box and luminosity L, in place of the field. L <= 0 puts
	/// the field back.
	void SetTestStarForTest( double x, double y, double z0, double luminosity )
	{
		testStar[ 0 ] = static_cast< float >( x );
		testStar[ 1 ] = static_cast< float >( y );
		testStar[ 2 ] = static_cast< float >( z0 );
		testStar[ 3 ] = static_cast< float >( luminosity );
	}
	/// The disc's light without its turbulence: --disc reads the shift alone.
	void SetDiscTextureForTest( bool on )
	{
		discTexture = on;
	}
	/// --seam's negative control: the clouds' lattice one cell short of
	/// wrapping round the vanishing point.
	void SetSeamForTest( bool on )
	{
		seamForTest = on;
	}

	const Journey& JourneyState() const
	{
		return journey;
	}
	const Camera& CameraState() const
	{
		return camera;
	}
	GLuint StarBufferID() const
	{
		return starBuffer.TextureID();
	}
	GLuint SceneBufferID() const
	{
		return sceneBuffer.TextureID();
	}
	double LastDt() const
	{
		return frameDt;
	}
	double LastShutterZ() const
	{
		return shutterZ;
	}
	double LastFocal() const
	{
		return focal;
	}
	double LastFlash() const
	{
		return flashLight;
	}
	double HoleDistance() const
	{
		return holeD;
	}
	double LastDiscLight() const
	{
		return discLight;
	}
	double LastCoronaLight() const
	{
		return coronaLight;
	}

private:
	void UpdateClock();
	bool ensureClipBuffer( int width, int height );
	void uploadTables();

	const bool isEffect;
	const std::vector< unsigned int >& hostOrder;
	int idToHost[ PT_COUNT ];
	float params[ PT_COUNT ] = {};

	ffglex::FFGLShader intakeShader, starShader, sceneShader, compositeShader;
	ffglex::FFGLScreenQuad quad;
	GLuint starArray = 0;
	PassBuffer starBuffer, sceneBuffer;
	GLuint clipTexture = 0, clipFramebuffer = 0;
	int clipWidth = 0, clipHeight = 0, clipLevels = 1;
	GLuint blackbodyTexture = 0, orbitTexture = 0, endTexture = 0, blankClip = 0;

	Journey journey;
	Camera camera;
	double cameraForTest = -1.0;
	bool jumpHeld        = false;
	double flashAge      = 1.0e9;///< seconds since the last flash, at the end of the frame
	int lastLook         = -1;
	double spinClock = 0.0, discClock = 0.0;

	//Time. Resolume has sent both seconds and milliseconds (see millpond).
	double hostTime = -1.0, lastRawTime = -1.0, lastWallTime = -1.0, wallStart = -1.0;
	double clockScale = 0.0;
	int secondsVotes = 0, millisVotes = 0;
	double now = 0.0, lastNow = -1.0;
	bool settledJump = false;
	double frameDt   = 0.0;

	//What the last frame used, for the harness.
	double shutterZ = 0.0, focal = 0.0, flashLight = 0.0, holeD = 0.0, discLight = 0.0, coronaLight = 0.0;

	bool fluxLaw = true, copyLaw = true, lengthLaw = true, roundTrip = true, bendLaw = true, captureLaw = true;
	bool discTexture = true, seamForTest = false;
	float testStar[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float spinSign         = 1.0f;
	bool floatClockForTest = false;
};

} // namespace slipstream
