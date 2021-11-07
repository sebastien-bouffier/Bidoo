/*****************************************************************************

        ResamplerFlt.cpp
        Copyright (c) 2003 Laurent de Soras

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
	#pragma warning (1 : 4130) // "'operator' : logical operation on address of string constant"
	#pragma warning (1 : 4223) // "nonstandard extension used : non-lvalue array converted to pointer"
	#pragma warning (1 : 4705) // "statement has no effect"
	#pragma warning (1 : 4706) // "assignment within conditional expression"
	#pragma warning (4 : 4786) // "identifier was truncated to '255' characters in the debug information"
	#pragma warning (4 : 4800) // "forcing value to bool 'true' or 'false' (performance warning)"
	#pragma warning (4 : 4355) // "'this' : used in base member initializer list"
#endif



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

#include	"fnc.h"
#include	"InterpPack.h"
#include	"MipMapFlt.h"
#include	"ResamplerFlt.h"
#include	"UInt32.h"

#include	<cassert>
#include	<cstring>



namespace rspl
{



/*\\\ PUBLIC \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



/*
==============================================================================
Name: ctor
Description:
	Not very exciting.
Throws: std::vector related exceptions.
==============================================================================
*/

ResamplerFlt::ResamplerFlt ()
:	_buf ()
,	_mip_map_ptr (0)
,	_interp_ptr (0)
,	_dwnspl ()
,	_voice_arr ()
,	_pitch (0)
,	_buf_len (128)
,	_fade_pos (0)
,	_fade_flag (false)
,	_fade_needed_flag (false)
,	_can_use_flag (false)
{
	_dwnspl.set_coefs (_dwnspl_coef_arr);
	_buf.resize (_buf_len * 2);
}



/*
==============================================================================
Name: set_interp
Description:
	Set the resampler interpolator.
	This function has to be called at least once before using the resampler.
Input parameters:
	- interp:
Throws: ?
==============================================================================
*/

void	ResamplerFlt::set_interp (const InterpPack &interp)
{
	assert (&interp != 0);

	_interp_ptr = &interp;
}



/*
==============================================================================
Name: set_sample
Description:
	Specifies the sample to use. Playback positions and pitch are automatically
	reset to 0.
	This function has to be called at least once before using the resampler.
	Although the sample may not be initialised during this call, it must be
	initialised before actual resampler use.
Input parameters:
	- spl: reference on the sample, it must have been fully initialised before.
Throws: std::vector related exceptions.
==============================================================================
*/

void	ResamplerFlt::set_sample (const MipMapFlt &spl)
{
	assert (&spl != 0);
	assert (spl.is_ready ());

	_mip_map_ptr = &spl;
	_pitch = 0;
	_voice_arr [VoiceInfo_CURRENT]._pos._all = 0;
	reset_pitch_cur_voice ();
}



/*
==============================================================================
Name: remove_sample
Description:
	Remove the sample (but leave the MipMapFlt object intact). Another sample
	must be set to use the resampler again.
Throws: Nothing
==============================================================================
*/

void	ResamplerFlt::remove_sample ()
{
	_mip_map_ptr = 0;
}



/*
==============================================================================
Name: set_pitch
Description:
	Set the new pitch. Pitch is logarithmic (linear in octaves) and relative to
	the original sample rate. 0x10000 is one octave up (0x20000 two octaves up
	and -0x1555 one semi-tone down). Of course, 0 is the original sample pitch.
Input parameters:
	- pitch: New pitch to set. Virtually no lower limit, but must be lower than
		number of table * 0x10000.
Throws: Nothing.
==============================================================================
*/

void	ResamplerFlt::set_pitch (long pitch)
{
	assert (_mip_map_ptr != 0);
	assert (_interp_ptr != 0);
	assert (pitch < _mip_map_ptr->get_nbr_tables () * (1L << NBR_BITS_PER_OCT));

	BaseVoiceState &	old_voc = _voice_arr [VoiceInfo_FADEOUT];
	BaseVoiceState &	cur_voc = _voice_arr [VoiceInfo_CURRENT];

	_pitch = pitch;
	const int		new_table = compute_table (pitch);
	const bool		new_ovrspl_flag = (_pitch >= 0);
	_fade_needed_flag = (   new_table != cur_voc._table
	                     || new_ovrspl_flag != cur_voc._ovrspl_flag);

	cur_voc.compute_step (_pitch);
	if (_fade_flag)
	{
		old_voc.compute_step (_pitch);
	}
}



/*
==============================================================================
Name: get_pitch
Description:
	Returns the current pitch.
Returns: The current pitch, log scale, 0x10000 is one octave up.
Throws: Nothing
==============================================================================
*/

long	ResamplerFlt::get_pitch () const
{
	assert (_mip_map_ptr != 0);
	assert (_interp_ptr != 0);

	return (_pitch);
}



/*
==============================================================================
Name: set_playback_pos
Description:
	Set a new fractional playback position within the sample. Change is
	immediate, without any crossfading. Lower 32 bits are the fractional part,
	higher 32 bits are the integer part.
Input parameters:
	- pos: New playback position, 32:32-fixed-point. Must be bounded by 0
		(inclusive) and the length of the sample (exclusive).
Throws: Nothing
==============================================================================
*/

void	ResamplerFlt::set_playback_pos (Int64 pos)
{
	assert (_mip_map_ptr != 0);
	assert (_interp_ptr != 0);
	assert (pos >= 0);
	assert ((pos >> 32) < _mip_map_ptr->get_sample_len ());

	_voice_arr [VoiceInfo_CURRENT]._pos._all =
		pos >> _voice_arr [VoiceInfo_CURRENT]._table;
	if (_fade_flag)
	{
		_voice_arr [VoiceInfo_FADEOUT]._pos._all =
			pos >> _voice_arr [VoiceInfo_FADEOUT]._table;
	}
}



/*
==============================================================================
Name: get_playback_pos
Description:
	Returns the current playback position, as a 32:32-fixed point integer.
Returns: The position, 32:32-fixed, range is between 0 and the sample length.
Throws: Nothing.
==============================================================================
*/

Int64	ResamplerFlt::get_playback_pos () const
{
	assert (_mip_map_ptr != 0);
	assert (_interp_ptr != 0);

	const BaseVoiceState &	cur_voc = _voice_arr [VoiceInfo_CURRENT];
	const Int64		pos = cur_voc._pos._all;
	const int		table = cur_voc._table;

	return (pos << table);
}



/*
==============================================================================
Name: interpolate_block
Description:
	Generates a block of resampled data. Care must be taken in order no to let
	the playback position overtake the sample length. Except during MIP-map
	crossfading, CPU load per output sample is roughly constant and not
	dependent on the resampling ratio.
Input parameters:
	- dest_ptr: Pointer on the location where the data must be written.
	- nbr_spl: Number of samples to generate. > 0.
Throws: Nothing.
==============================================================================
*/

void	ResamplerFlt::interpolate_block (float dest_ptr [], long nbr_spl)
{
	assert (_mip_map_ptr != 0);
	assert (_interp_ptr != 0);
	assert (dest_ptr != 0);
	assert (nbr_spl > 0);

	if (_fade_needed_flag && ! _fade_flag)
	{
		begin_mip_map_fading ();
	}

	long				block_pos = 0;
	while (block_pos < nbr_spl)
	{
		long				work_len = nbr_spl - block_pos;

		// Fading
		if (_fade_flag)
		{
			work_len = min (work_len, _buf_len);
			work_len = min (work_len, BaseVoiceState::FADE_LEN - _fade_pos);
			fade_block (&dest_ptr [block_pos], work_len);
		}

		// Oversampling required
		else if (_voice_arr [VoiceInfo_CURRENT]._ovrspl_flag)
		{
			work_len = min (work_len, _buf_len);
			_interp_ptr->interp_ovrspl (
				&_buf [0],
				work_len * 2,
				_voice_arr [VoiceInfo_CURRENT]
			);
			_dwnspl.downsample_block (
				&dest_ptr [block_pos],
				&_buf [0],
				work_len
			);
		}

		// No oversampling
		else
		{
			_interp_ptr->interp_norm (
				&dest_ptr [block_pos],
				work_len,
				_voice_arr [VoiceInfo_CURRENT]
			);
			_dwnspl.phase_block (
				&dest_ptr [block_pos],
				&dest_ptr [block_pos],
				work_len
			);
		}

		block_pos += work_len;
	}
}



/*
==============================================================================
Name: clear_buffers
Description:
	Clears interpolator miscellaneous buffers, cancels the crossfadings.
	Therefore the interpolator is in a state equivalent to the one caused by
	the processing of silent input during an infinite duration.
	Does not reset the playback position.
Throws: Nothing
==============================================================================
*/

void	ResamplerFlt::clear_buffers ()
{
	_dwnspl.clear_buffers ();

	if (_mip_map_ptr != 0)
	{
		reset_pitch_cur_voice ();
	}

	_fade_needed_flag = false;
	_fade_flag = false;
}



// Specs:
// Half-band FIR LPF
// 81 coefficients
// Passband frequency range: 0 - 9*Fs/40
// Passband level: 0 dB
// Passband ripple: 0.04 dB
// Stopband frequency range: 11*Fs/40 - Fs/2
// Stopband level: -oo dB
// Stopband attenuation: -92.2 dB
const double	ResamplerFlt::_fir_mip_map_coef_arr [MIP_MAP_FIR_LEN] =
{
	0.000199161, 0.000652681, 0.000732184, -5.20771e-005,
	-0.0007641618462, -5.638448426e-005, 0.001043007134, 0.0002618629784,
	-0.001427331739, -0.0005903598292, 0.001886374043, 0.001076466716,
	-0.00241732264, -0.001750193532, 0.003009892088, 0.002655276516,
	-0.003657747155, -0.003838234309, 0.004347045423, 0.005358361364,
	-0.005067457982, -0.007294029088, 0.00579846115, 0.009746961422,
	-0.006524811739, -0.01286996135, 0.007226610031, 0.01690351872,
	-0.007884756779, -0.02225965965, 0.008479591244, 0.02971806374,
	-0.008993687303, -0.04095867041, 0.009411374397, 0.06037160312,
	-0.009719550593, -0.1041018935, 0.009908930771, 0.3176382757,
	0.4900273874, 0.3176382757, 0.009908930771, -0.1041018935,
	-0.009719550593, 0.06037160312, 0.009411374397, -0.04095867041,
	-0.008993687303, 0.02971806374, 0.008479591244, -0.02225965965,
	-0.007884756779, 0.01690351872, 0.007226610031, -0.01286996135,
	-0.006524811739, 0.009746961422, 0.00579846115, -0.007294029088,
	-0.005067457982, 0.005358361364, 0.004347045423, -0.003838234309,
	-0.003657747155, 0.002655276516, 0.003009892088, -0.001750193532,
	-0.00241732264, 0.001076466716, 0.001886374043, -0.0005903598292,
	-0.001427331739, 0.0002618629784, 0.001043007134, -5.638448426e-005,
	-0.0007641618462, -5.20770713e-005, 0.0007321838627, 0.000652681315,
	0.0001991612514
};



/*\\\ PROTECTED \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



/*\\\ PRIVATE \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



void	ResamplerFlt::reset_pitch_cur_voice ()
{
	assert (_mip_map_ptr != 0);

	BaseVoiceState &	cur_voc = _voice_arr [VoiceInfo_CURRENT];

	cur_voc._table = compute_table (_pitch);
	cur_voc._table_len = _mip_map_ptr->get_lev_len (cur_voc._table);
	cur_voc._table_ptr = _mip_map_ptr->use_table (cur_voc._table);
	cur_voc._ovrspl_flag = (_pitch >= 0);
	cur_voc.compute_step (_pitch);
}



void	ResamplerFlt::fade_block (float dest_ptr [], long nbr_spl)
{
	assert (dest_ptr != 0);
	assert (nbr_spl <= BaseVoiceState::FADE_LEN - _fade_pos);
	assert (nbr_spl <= _buf_len);

	const long		nbr_spl_ovr = nbr_spl * 2;
	const float		vol_step = 1.0f / (BaseVoiceState::FADE_LEN * 2);
	const float		vol = _fade_pos * (vol_step * 2);
	BaseVoiceState &	old_voc = _voice_arr [VoiceInfo_FADEOUT];
	BaseVoiceState &	cur_voc = _voice_arr [VoiceInfo_CURRENT];

	using namespace std;
	memset (&_buf [0], 0, sizeof (_buf [0]) * nbr_spl_ovr);

	assert (old_voc._ovrspl_flag || cur_voc._ovrspl_flag);

	if (   cur_voc._ovrspl_flag
	    && old_voc._ovrspl_flag)
	{
		_interp_ptr->interp_ovrspl_ramp_add (
			&_buf [0],
			nbr_spl_ovr,
			cur_voc,
			vol,
			vol_step
		);
		_interp_ptr->interp_ovrspl_ramp_add (
			&_buf [0],
			nbr_spl_ovr,
			old_voc,
			1.0f - vol,
			-vol_step
		);
	}

	else if ( ! cur_voc._ovrspl_flag
	         && old_voc._ovrspl_flag)
	{
		_interp_ptr->interp_norm_ramp_add (
			&_buf [0],
			nbr_spl_ovr,
			cur_voc,
			vol,
			vol_step
		);
		_interp_ptr->interp_ovrspl_ramp_add (
			&_buf [0],
			nbr_spl_ovr,
			old_voc,
			1.0f - vol,
			-vol_step
		);
	}

	else
	{
		_interp_ptr->interp_ovrspl_ramp_add (
			&_buf [0],
			nbr_spl_ovr,
			cur_voc,
			vol,
			vol_step
		);
		_interp_ptr->interp_norm_ramp_add (
			&_buf [0],
			nbr_spl_ovr,
			old_voc,
			1.0f - vol,
			-vol_step
		);
	}

	_dwnspl.downsample_block (&dest_ptr [0], &_buf [0], nbr_spl);

	_fade_pos += nbr_spl;
	_fade_flag = (_fade_pos < BaseVoiceState::FADE_LEN);
}



int	ResamplerFlt::compute_table (long pitch)
{
	int				table = 0;

	if (pitch >= 0)
	{
		table = pitch >> NBR_BITS_PER_OCT;
	}

	return (table);
}



void	ResamplerFlt::begin_mip_map_fading ()
{
	BaseVoiceState &	old_voc = _voice_arr [VoiceInfo_FADEOUT];
	BaseVoiceState &	cur_voc = _voice_arr [VoiceInfo_CURRENT];

	old_voc = cur_voc;

	reset_pitch_cur_voice ();
	const int		table_dif = old_voc._table - cur_voc._table;
	cur_voc._pos._all = shift_bidi (old_voc._pos._all, table_dif);

	_fade_needed_flag = false;
	_fade_flag = true;
	_fade_pos = 0;
}



// Specs:
// Half-band IIR LPF (all-pass pair)
// Transtion band: 0.05*Fs
// Stopband attenuation: -90 dB
const double	ResamplerFlt::_dwnspl_coef_arr [Downsampler2Flt::NBR_COEFS] =
{
	0.0457281, 0.168088, 0.332501, 0.504486, 0.663202, 0.803781, 0.933856
};



}	// namespace rspl



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
