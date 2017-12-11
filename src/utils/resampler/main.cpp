/*****************************************************************************

        main.cpp
        Copyright (c) 2003 Laurent de Soras

Contact: laurent@ohmforce.com

Test program for the resampler C++ classes. It is intended to be an example
of the method described in the article "The Quest For The Perfect Resampler",
by Laurent de Soras, June 2003. http://ldesoras.free.fr/prod.html

For now:
- Does various performance tests on resampler components.
- Outputs a 15 kHz sine in raw 16 bit/mono/44.1 kHz format and its
resampled version, sweeping from -10 to +2 octaves.
- Outputs a resampled saw waveform sweeping from -2 to +10 octaves. Same
format as above.

--- Legal stuff ---

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*Tab=3***********************************************************************/



#if defined (_MSC_VER)
	#pragma warning (4 : 4786) // "identifier was truncated to '255' characters in the debug information"
	#pragma warning (4 : 4800) // "forcing value to bool 'true' or 'false' (performance warning)"
#endif



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/


#include	"def.h"
#include	"Downsampler2Flt.h"
#include	"fnc.h"
#include	"Fixed3232.h"
#include	"Int16.h"
#include	"Int64.h"
#include	"InterpFlt.h"
#include	"InterpPack.h"
#include	"MipMapFlt.h"
#include	"ResamplerFlt.h"
#include	"StopWatch.h"

#if defined (_MSC_VER)
#include	<crtdbg.h>
#include	<new.h>
#endif	// _MSC_VER

#include	<fstream>
#include	<iostream>
#include	<new>
#include	<stdexcept>
#include	<streambuf>
#include	<vector>

#include	<cassert>
#include	<climits>
#include	<cstdlib>



/*\\\ CLASS DEFINITIONS \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



/*\\\ FUNCTIONS \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



void	basic_checking ();
void	test_speed_InterpFlt ();
void	test_speed_Downsampler2Flt ();
void	test_sine_15k ();
void	test_saw ();

template <class T>
void	generate_random_vector (std::vector <T> &v, long len, T amp = 1);
void	generate_steady_sine (std::vector <float> &v, long len, double freq);
void	generate_steady_saw (std::vector <float> &v, long len, long wavelength);
void	save_raw_sample_16 (const std::vector <float> &v, const char *filename_0);
void	save_raw_sample_16 (const std::vector <rspl::Int16> &v, const char *filename_0);
void	convert_flt_to_16 (std::vector <rspl::Int16> &v_16, const std::vector <float> &v_flt);
void	scale_vector (std::vector <float> &v, float scale);



// -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
// DEBUG CRAP FOR MS VISUAL C++ STARTS HERE

#if defined (_MSC_VER)
static int __cdecl	main_new_handler_cb (size_t dummy)
{
	throw std::bad_alloc ();
	return (0);
}
#endif	// _MSC_VER



#if defined (_MSC_VER) && ! defined (NDEBUG)
static int	__cdecl	main_debug_alloc_hook_cb (int alloc_type, void *user_data_ptr, size_t size, int block_type, long request_nbr, const unsigned char *filename_0, int line_nbr)
{
	if (block_type != _CRT_BLOCK)	// Ignore CRT blocks to prevent infinite recursion
	{
		switch (alloc_type)
		{
		case	_HOOK_ALLOC:
		case	_HOOK_REALLOC:
		case	_HOOK_FREE:

			// Put some debug code here

			break;

		default:
			assert (false);	// Undefined allocation type
			break;
		}
	}

	return (1);
}
#endif



#if defined (_MSC_VER) && ! defined (NDEBUG)
static int	__cdecl	main_debug_report_hook_cb (int report_type, char *user_msg_0, int *ret_val_ptr)
{
	*ret_val_ptr = 0;	// 1 to override the CRT default reporting mode

	switch (report_type)
	{
	case	_CRT_WARN:
	case	_CRT_ERROR:
	case	_CRT_ASSERT:

// Put some debug code here

		break;
	}

	return (*ret_val_ptr);
}
#endif



static void	main_prog_init ()
{
#if defined (_MSC_VER)
	::_set_new_handler (::main_new_handler_cb);
#endif	// _MSC_VER

#if defined (_MSC_VER) && ! defined (NDEBUG)
	{
		const int	mode =   (1 * _CRTDBG_MODE_DEBUG)
						       | (1 * _CRTDBG_MODE_WNDW);
		::_CrtSetReportMode (_CRT_WARN, mode);
		::_CrtSetReportMode (_CRT_ERROR, mode);
		::_CrtSetReportMode (_CRT_ASSERT, mode);

		const int	old_flags = ::_CrtSetDbgFlag (_CRTDBG_REPORT_FLAG);
		::_CrtSetDbgFlag (  old_flags
		                  | (1 * _CRTDBG_LEAK_CHECK_DF)
		                  | (1 * _CRTDBG_CHECK_ALWAYS_DF));
		::_CrtSetBreakAlloc (-1);	// Specify here a memory bloc number
		::_CrtSetAllocHook (main_debug_alloc_hook_cb);
		::_CrtSetReportHook (main_debug_report_hook_cb);

		// Speed up I/O but breaks C stdio compatibility
//		std::cout.sync_with_stdio (false);
//		std::cin.sync_with_stdio (false);
//		std::cerr.sync_with_stdio (false);
//		std::clog.sync_with_stdio (false);
	}
#endif	// _MSC_VER, NDEBUG
}



static void	main_prog_end ()
{
#if defined (_MSC_VER) && ! defined (NDEBUG)
	{
		const int	mode =   (1 * _CRTDBG_MODE_DEBUG)
						       | (0 * _CRTDBG_MODE_WNDW);
		::_CrtSetReportMode (_CRT_WARN, mode);
		::_CrtSetReportMode (_CRT_ERROR, mode);
		::_CrtSetReportMode (_CRT_ASSERT, mode);

		::_CrtMemState	mem_state;
		::_CrtMemCheckpoint (&mem_state);
		::_CrtMemDumpStatistics (&mem_state);
	}
#endif	// _MSC_VER, NDEBUG
}

// -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
// END OF DEBUG CRAP



int main (int argc, char *argv [])
{
	main_prog_init ();

	try
	{
		basic_checking ();
		test_speed_InterpFlt ();
		test_speed_Downsampler2Flt ();
		test_sine_15k ();
		test_saw ();
	}

	catch (std::exception &e)
	{
		std::cout << "*** main() : Exception (std::exception) : ";
		std::cout << e.what () << std::endl;
		throw;
	}

	catch (...)
	{
		std::cout << "*** main() : Undefined exception" << std::endl;
		throw;
	}

	main_prog_end ();

	return (0);
}



// -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
// Test functions



/*
==============================================================================
Name: basic_checking
Description:
	Checks the source code part depending on the processor architecture.
Throws: std::runtime_error
==============================================================================
*/

void	basic_checking ()
{
	// rspl::Int64
	if (sizeof (rspl::Int64) * CHAR_BIT != 64)
	{
		std::cerr << "*** DATA CAPACITY PROBLEM ***\n";
		std::cerr << "Please fix rspl::Int64 declaration.\n";
		throw std::runtime_error ("Fatal error (rspl::Int64 definition).");
	}

	// rspl::Int32 and rspl::UInt32
	if (   sizeof (rspl::Int32)  * CHAR_BIT != 32
	    || sizeof (rspl::UInt32) * CHAR_BIT != 32)
	{
		std::cerr << "*** DATA CAPACITY PROBLEM ***\n";
		std::cerr << "Please fix rspl::Int32 or rspl::UInt32 declaration.\n";
		throw std::runtime_error ("Fatal error (rspl::Int32 definition).");
	}

	// rspl::Int16
	if (sizeof (rspl::Int16) * CHAR_BIT != 16)
	{
		std::cerr << "*** DATA CAPACITY PROBLEM ***\n";
		std::cerr << "Please fix rspl::Int16 declaration.\n";
		throw std::runtime_error ("Fatal error (rspl::Int16 definition).");
	}

	// rspl::Fixed3232
	const int		magic_gluar = 123;
	rspl::Fixed3232	extra_cow;

	extra_cow._all = magic_gluar;
	if (extra_cow._part._lsw != static_cast <rspl::UInt32> (magic_gluar))
	{
		std::cerr << "*** ENDIAN PROBLEM ***\n";
		std::cerr << "Invert _lsw and _msw members in Fixed3232.h !!!\n";
		throw std::runtime_error ("Fatal error (rspl::Fixed3232 definition).");
	}

	extra_cow._all = static_cast <rspl::Int64> (magic_gluar) << 32;
	if (extra_cow._part._msw !=  static_cast <rspl::Int32> (magic_gluar))
	{
		std::cerr << "*** ALIGNMENT PROBLEM (most likely) ***\n";
		std::cerr << "Check union definition in Fixed3232.h.\n";
		throw std::runtime_error ("Fatal error (rspl::Fixed3232 definition).");
	}
}



/*
==============================================================================
Name: test_speed_InterpFlt
Description:
	Test raw performances of the interpolator.
Throws: ?
==============================================================================
*/

void	test_speed_InterpFlt ()
{
	const long		nbr_it = 1000000;

	std::cout << "Testing InterpFlt raw performance...\n";

	// Build a test impulse with non-null components
	std::vector <double>	imp;
	generate_random_vector (imp, rspl::InterpFlt <1>::IMPULSE_LEN);

	// Input sample: vector full of random crap
	std::vector <float>	sample;
	generate_random_vector (sample, rspl::InterpFlt <1>::FIR_LEN * 2);
	const float *	sample_ptr = &sample [rspl::InterpFlt <1>::FIR_LEN];

	rspl::InterpFlt <1>	interp;
	interp.set_impulse (&imp [0]);

	const rspl::UInt32	step = 0xC3752149UL;
	rspl::UInt32	interp_pos = 0;
	float				dummy_sum = 0;

	rspl::StopWatch	sw;
	sw.start ();

	// Raw performance test only. Does not care about cache or accuracy
	for (long it_cnt = 0; it_cnt < nbr_it; ++it_cnt)
	{
		dummy_sum += interp.interpolate (sample_ptr, interp_pos);
		interp_pos += step;
	}

	sw.stop ();

	const double	unsignificant = 1e-40;
	const double	t = sw.get_clk_per_op (nbr_it) + dummy_sum * unsignificant;
	std::cout << t << " clocks/sample\n\n";
}



/*
==============================================================================
Name: test_speed_Downsampler2Flt
Description:
	Test raw performance of the downsampler
Throws: ?
==============================================================================
*/

void	test_speed_Downsampler2Flt ()
{
	const long		nbr_it = 1000000;
	const long		block_len = 256;

	std::cout << "Testing Downsampler2Flt raw performance...\n";

	std::vector <double>	coef_arr;
	for (int coef_cnt = 0
	;	coef_cnt < rspl::Downsampler2Flt::NBR_COEFS
	;	++coef_cnt)
	{
		const double	num = static_cast <double> (coef_cnt + 1);
		const double	den =
			static_cast <double> (rspl::Downsampler2Flt::NBR_COEFS + 1);
		coef_arr.push_back (num / den);
	}

	// Input sample: vector full of random crap
	std::vector <float>	sample;
	generate_random_vector (sample, block_len * 2);

	// Where we put the result
	std::vector <float>	trash (block_len);

	rspl::Downsampler2Flt	ds;
	ds.set_coefs (&coef_arr [0]);

	rspl::StopWatch	sw;
	sw.start ();

	for (long block_pos = 0; block_pos < nbr_it; block_pos += block_len)
	{
		const long		nbr_spl = rspl::min (block_len, nbr_it - block_pos);
		ds.downsample_block (&trash [0], &sample [0], nbr_spl);
	}

	sw.stop ();

	const double	t = sw.get_clk_per_op (nbr_it);
	std::cout << t << " clocks/output sample.\n\n";
}



/*
==============================================================================
Name: test_sine_15k
Description:
	We test here :
	- Full resampling process (MIP-mapping, interpolation, downsampling)
	- Single frequency component interpolation
	- Pitch change between processed blocks
	- Extreme pitch range (low ones)
	- Performance test in quite realistic conditions
Throws: ?
==============================================================================
*/

void	test_sine_15k ()
{
	const double	fs = 44100;				// Sampling rate, Hz
	const double	fc = 15000;				// Sine frequency, Hz
	const double	data_duration = 20;	// Seconds, must be long enough
	const double	test_duration = 30;	// Seconds
	const long		block_len = 256;

	std::cout << "Testing 15 kHz sine resampling...\n";

	// The sine wave
	const long		sine_len = rspl::round_long (data_duration * fs);
	std::vector <float>	sine;
	generate_steady_sine (sine, sine_len, fc / fs);
	save_raw_sample_16 (sine, "sine_15k.raw");

	// Init resampler components
	rspl::InterpPack	interp_pack;
	rspl::MipMapFlt	mip_map;
	mip_map.init_sample (
		sine_len,
		rspl::InterpPack::get_len_pre (),
		rspl::InterpPack::get_len_post (),
		6,
		rspl::ResamplerFlt::_fir_mip_map_coef_arr,
		rspl::ResamplerFlt::MIP_MAP_FIR_LEN
	);
	mip_map.fill_sample (&sine [0], sine_len);

	rspl::ResamplerFlt	rspl;
	rspl.set_sample (mip_map);
	rspl.set_interp (interp_pack);
	rspl.clear_buffers ();

	// Output data
	const long		test_len = rspl::round_long (test_duration * fs);
	std::vector <float>	out_sig (test_len, 0);
	
	using namespace rspl;
	
	// Processing
	StopWatch		sw;
	sw.start ();

	for (long block_pos = 0; block_pos < test_len; block_pos += block_len)
	{
		const long		depth = 12L << rspl::ResamplerFlt::NBR_BITS_PER_OCT;
		const long		offset = -10L << rspl::ResamplerFlt::NBR_BITS_PER_OCT;
		const double	ratio =
			  static_cast <double> (block_pos)
			/ static_cast <double> (test_len);
		const long		pitch = rspl::round_long (depth * ratio) + offset;
		rspl.set_pitch (pitch);

		const long		nbr_spl = rspl::min (block_len, test_len - block_pos);
		rspl.interpolate_block (&out_sig [block_pos], nbr_spl);
	}

	sw.stop ();

	const double	t = sw.get_clk_per_op (test_len);
	std::cout
		<< t
		<< " clocks/output sample (taking cache issues into account).\n\n";

	scale_vector (out_sig, 0.5f);
	save_raw_sample_16 (out_sig, "sine_15k_interp.raw");
}



/*
==============================================================================
Name: test_saw
Description:
	We test here :
	- Full resampling process (MIP-mapping, interpolation, downsampling)
	- Broadband waveform interpolation
	- Odd block length
	- Pitch change between processed blocks
	- Playback position change
	- Extreme pitch range (high ones)
	- Performance test in quite realistic conditions
Throws: ?
==============================================================================
*/

void	test_saw ()
{
	const double	fs = 44100;					// Sampling rate, Hz
	const long		wavelength = 1L << 10;	// Must be a power of two
	const double	test_duration = 60;		// Seconds
	const long		block_len = 57;			// Let's try an odd length...

	std::cout << "Testing saw wave resampling...\n";

	// The saw wave
	const long		saw_len = wavelength * block_len * 4;
	std::vector <float>	saw;
	generate_steady_saw (saw, saw_len, wavelength);
	save_raw_sample_16 (saw, "saw.raw");

	// Init resampler components
	rspl::InterpPack	interp_pack;
	rspl::MipMapFlt	mip_map;
	mip_map.init_sample (
		saw_len,
		rspl::InterpPack::get_len_pre (),
		rspl::InterpPack::get_len_post (),
		12,	// We're testing up to 10 octaves above the original rate
		rspl::ResamplerFlt::_fir_mip_map_coef_arr,
		rspl::ResamplerFlt::MIP_MAP_FIR_LEN
	);
	mip_map.fill_sample (&saw [0], saw_len);

	rspl::ResamplerFlt	rspl;
	rspl.set_sample (mip_map);
	rspl.set_interp (interp_pack);
	rspl.clear_buffers ();

	// Output data
	const long		test_len = rspl::round_long (test_duration * fs);
	std::vector <float>	out_sig (test_len, 0);

	using namespace rspl;
	
	// Processing
	StopWatch		sw;
	sw.start ();

	for (long block_pos = 0; block_pos < test_len; block_pos += block_len)
	{
		const long		depth = 12L << rspl::ResamplerFlt::NBR_BITS_PER_OCT;
		const long		offset = -2L << rspl::ResamplerFlt::NBR_BITS_PER_OCT;
		const double	ratio =
			  static_cast <double> (block_pos)
			/ static_cast <double> (test_len);
		const long		pitch = rspl::round_long (depth * ratio) + offset;
		rspl.set_pitch (pitch);

		// Check wether we're not going too far, out of the sample
		Int64				pos = rspl.get_playback_pos ();
		if ((pos >> 32) > (saw_len >> 1))
		{
			// Simulate "loop" by going back to the begining
			pos &= (static_cast <rspl::Int64> (wavelength) << 32) - 1;

			// But skip a few periods in order to ensure that we get the
			// periodic waveform part on highest MIP-map levels. Indeed, first
			// periods are interpolated with silent signal located before the
			// actual waveform.
			pos += static_cast <rspl::Int64> (wavelength * 16) << 32;

			rspl.set_playback_pos (pos);
		}

		const long		nbr_spl = rspl::min (block_len, test_len - block_pos);
		rspl.interpolate_block (&out_sig [block_pos], nbr_spl);
	}

	sw.stop ();

	const double	t = sw.get_clk_per_op (test_len);
	std::cout
		<< t
		<< " clocks/output sample (taking cache issues into account).\n\n";

	scale_vector (out_sig, 0.5f);
	save_raw_sample_16 (out_sig, "saw_interp.raw");
}



// -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
// Misc tool functions and helpers



template <class T>
void	generate_random_vector (std::vector <T> &v, long len, T amp)
{
	using namespace std;

	assert (len > 0);

	v.clear ();
	for (long pos = 0; pos < len; ++pos)
	{
		const double	x = static_cast <double> (rand ()) / RAND_MAX - 0.5;
		const T			val = static_cast <T> (x * amp);
		v.push_back (val);
	}
}



void	generate_steady_sine (std::vector <float> &v, long len, double freq)
{
	assert (&v != 0);
	assert (len > 0);
	assert (freq <= 0.5);
	assert (freq > 0);

	v.resize (len);
	for (long pos = 0; pos < len; ++pos)
	{
		using namespace std;

		v [pos] = static_cast <float> (cos (pos * freq * (2 * rspl::PI)));
	}
}



void	generate_steady_saw (std::vector <float> &v, long len, long wavelength)
{
	assert (&v != 0);
	assert (len > 0);
	assert (wavelength >= 2);

	v.resize (len);
	double			val = 0;
	const double	step = 2.0 / static_cast <double> (wavelength - 1);
	for (long pos = 0; pos < len; ++pos)
	{
		using namespace std;

		if ((pos % wavelength) == 0)
		{
			val = -1;
		}
		v [pos] = static_cast <float> (val);
		val += step;
	}
}



void	save_raw_sample_16 (const std::vector <float> &v, const char *filename_0)
{
	assert (&v != 0);
	assert (v.size () > 0);
	assert (filename_0 != 0);
	assert (filename_0 [0] != '\0');

	std::vector <rspl::Int16>	v_16;
	convert_flt_to_16 (v_16, v);
	save_raw_sample_16 (v_16, filename_0);
}



void	save_raw_sample_16 (const std::vector <rspl::Int16> &v, const char *filename_0)
{
	assert (&v != 0);
	assert (v.size () > 0);
	assert (filename_0 != 0);
	assert (filename_0 [0] != '\0');

	std::cout << "Saving " << filename_0 << "... " << std::flush;
	std::ofstream	ofs (
		filename_0,
		std::ios::binary | std::ios::out | std::ios::trunc
	);
	std::filebuf *	osb_ptr = ofs.rdbuf ();
	if (ofs && osb_ptr != 0)
	{
		osb_ptr->sputn (
			reinterpret_cast <const std::filebuf::char_type *> (&v [0]),
			v.size () * sizeof (v [0]) / sizeof (const std::filebuf::char_type)
		);
	}
	else
	{
		std::cerr << "UH-UH PROBLEM... " << std::flush;
	}
	ofs.close ();
	std::cout << "done.\n";
}



void	convert_flt_to_16 (std::vector <rspl::Int16> &v_16, const std::vector <float> &v_flt)
{
	const long		len = v_flt.size ();
	v_16.resize (len);

	for (long pos = 0; pos < len; ++pos)
	{
		const float		scaled_val = v_flt [pos] * 32768.0f;
		const float		val =
			rspl::max (rspl::min (scaled_val, 32767.0f), -32768.0f);
		v_16 [pos] = static_cast <rspl::Int16>	(rspl::round_int (val));
	}
}



void	scale_vector (std::vector <float> &v, float scale)
{
	const long		len = v.size ();
	for (long pos = 0; pos < len; ++pos)
	{
		v [pos] *= scale;
	}
}



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
