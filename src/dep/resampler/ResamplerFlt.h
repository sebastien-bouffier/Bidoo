/*****************************************************************************

        ResamplerFlt.h
        Copyright (c) 2003 Laurent de Soras

This is the "ready to use" class for sample interpolation. How to proceed:

1. Instanciate an InterpPack. It is just a helper class for ResamplerFlt, and
   can be shared between many instances of ResamplerFlt.

2. Instanciate a MipMapFlt. It will contain your sample data.

3. Initialise MipMapFlt and fill it with the sample. Similarly to InterpPack,
   it can be shared between many instances of ResamplerFlt.

4. Connect the InterpPack instance to the ResamplerFlt

5. Connect a MipMapFlt instance to the ResamplerFlt

6. Set pitch. You cannot do it before this point.

7. Optionally specify a playback position.

8. Generate a block of interpolated data.

9. You can go back to either 5, 6, 7 or 8.

Instance of this class can be assimilated to a "voice", a single unit of
monophonic sound generation. You can change the sample each time it is
needed, making it handy for polyponic synthesiser implementation.

In any case, NEVER EVER let the playback position exceed the sample length.
Check the current position and pitch before generating a new block.

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



#if ! defined (rspl_ResamplerFlt_HEADER_INCLUDED)
#define	rspl_ResamplerFlt_HEADER_INCLUDED

#if defined (_MSC_VER)
	#pragma once
	#pragma warning (4 : 4250) // "Inherits via dominance."
#endif



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

#include	"BaseVoiceState.h"
#include	"Downsampler2Flt.h"
#include	"Fixed3232.h"
#include	"Int64.h"
#include	"UInt32.h"



namespace rspl
{



class InterpPack;
class MipMapFlt;

class ResamplerFlt
{

/*\\\ PUBLIC \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

public:

	enum {			MIP_MAP_FIR_LEN	= 81	};
	enum {			NBR_BITS_PER_OCT	= BaseVoiceState::NBR_BITS_PER_OCT	};

						ResamplerFlt ();
	virtual			~ResamplerFlt () {}

	// Setup
	void				set_interp (const InterpPack &interp);
	void				set_sample (const MipMapFlt &spl);
	void				remove_sample ();

	// Use
	void				set_pitch (long pitch);
	long				get_pitch () const;

	void				set_playback_pos (Int64 pos);
	Int64				get_playback_pos () const;

	void				interpolate_block (float dest_ptr [], long nbr_spl);
	void				clear_buffers ();

	static const double
						_fir_mip_map_coef_arr [MIP_MAP_FIR_LEN];



/*\\\ PROTECTED \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

protected:


/*\\\ PRIVATE \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

private:

	enum VoiceInfo
	{
		VoiceInfo_CURRENT = 0,	// Current/fade-in
		VoiceInfo_FADEOUT,		// Fade-out

		VoiceInfo_NBR_ELT
	};

	typedef	std::vector <float>	SplData;

	void				reset_pitch_cur_voice ();
	void				fade_block (float dest_ptr [], long nbr_spl);
	inline int		compute_table (long pitch);
	void				begin_mip_map_fading ();

	SplData			_buf;
	const MipMapFlt *
						_mip_map_ptr;
	const InterpPack *
						_interp_ptr;
	Downsampler2Flt
						_dwnspl;
	BaseVoiceState	_voice_arr [VoiceInfo_NBR_ELT];
	long				_pitch;		// Current pitch value. 0x10000 = 1 octave. 0 = natural sample rate
	long				_buf_len;	// Downsampled size (real buf is 2x larger)
	long				_fade_pos;
	bool				_fade_flag;
	bool				_fade_needed_flag;
	bool				_can_use_flag;

	static const double
						_dwnspl_coef_arr [Downsampler2Flt::NBR_COEFS];



/*\\\ FORBIDDEN MEMBER FUNCTIONS \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

private:

						ResamplerFlt (const ResamplerFlt &other);
	ResamplerFlt &	operator = (const ResamplerFlt &other);
	bool				operator == (const ResamplerFlt &other);
	bool				operator != (const ResamplerFlt &other);

};	// class ResamplerFlt



}	// namespace rspl



//#include	"ResamplerFlt.hpp"



#endif	// rspl_ResamplerFlt_HEADER_INCLUDED



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
