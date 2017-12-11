/*****************************************************************************

        InterpPack.h
        Copyright (c) 2003 Laurent de Soras

Instance of this class should be constructed by client code and submitted to
ResamplerFlt upon initialisation. Because the class is stateless, a single
instance of it can be shared by multiple ResamplerFlt (voices). Actually it
is recommended to share it in order to save memory and cache.

Client code has just to construct an instance and attach it to one or more
ResamplerFlt object. Functions get_len_pre() and get_len_post() help to
calculate the required sample headroom before and after actual data. Pass
these results to the MIP-mapper object initialisation function.

Other public functions are for the sole ResamplerFlt's use

Technically, it groups two flavours of interpolators, one for r >= 1
(oversampled) and the other one for r < 1 (single rate).

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



#if ! defined (rspl_InterpPack_HEADER_INCLUDED)
#define	rspl_InterpPack_HEADER_INCLUDED

#if defined (_MSC_VER)
	#pragma once
	#pragma warning (4 : 4250) // "Inherits via dominance."
#endif



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

#include	"InterpFlt.h"



namespace rspl
{



class BaseVoiceState;

class InterpPack
{

/*\\\ PUBLIC \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

public:

						InterpPack ();
	virtual			~InterpPack () {}

	void				interp_ovrspl (float dest_ptr [], long nbr_spl, BaseVoiceState &voice) const;
	void				interp_norm (float dest_ptr [], long nbr_spl, BaseVoiceState &voice) const;
	void				interp_ovrspl_ramp_add (float dest_ptr [], long nbr_spl, BaseVoiceState &voice, float vol, float vol_step) const;
	void				interp_norm_ramp_add (float dest_ptr [], long nbr_spl, BaseVoiceState &voice, float vol, float vol_step) const;

	static long		get_len_pre ();
	static long		get_len_post ();



/*\\\ PROTECTED \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

protected:



/*\\\ PRIVATE \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

private:

	typedef	InterpFlt <2>	InterpRate1x;
	typedef	InterpFlt <1>	InterpRate2x;

	InterpRate1x	_interp_1x;		// Single-rate interpolation (larger imp.)
	InterpRate2x	_interp_2x;		// For double-sampled interpolation

	static const double
						_fir_1x_coef_arr [InterpRate1x::IMPULSE_LEN];
	static const double
						_fir_2x_coef_arr [InterpRate2x::IMPULSE_LEN];



/*\\\ FORBIDDEN MEMBER FUNCTIONS \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

private:

						InterpPack (const InterpPack &other);
	InterpPack &	operator = (const InterpPack &other);
	bool				operator == (const InterpPack &other);
	bool				operator != (const InterpPack &other);

};	// class InterpPack



}	// namespace rspl



//#include	"rspl/InterpPack.hpp"



#endif	// rspl_InterpPack_HEADER_INCLUDED



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
