/*****************************************************************************

        Downsampler2Flt.h
        Copyright (c) 2003 Laurent de Soras

Class halving the sample rate (2x-downsampler) with the help of a polyphase
IIR low-pass filter. It is used by ResamplerFlt, but can be used alone.

The number of coefficients is hardwired to 7 in this implementation. Check
the Artur Krukowski's webpage (http://www.cmsa.wmin.ac.uk/~artur/Poly.html)
to know more about the filter coefficients to submit.

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



#if ! defined (rspl_Downsampler2_HEADER_INCLUDED)
#define	rspl_Downsampler2_HEADER_INCLUDED

#if defined (_MSC_VER)
	#pragma once
	#pragma warning (4 : 4250) // "Inherits via dominance."
#endif



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

#include	"def.h"



namespace rspl
{



class Downsampler2Flt
{

/*\\\ PUBLIC \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

public:

	enum {			NBR_COEFS = 7	};

						Downsampler2Flt ();
	virtual			~Downsampler2Flt () {}

	void				set_coefs (const double coef_ptr [NBR_COEFS]);

	void				clear_buffers ();
	void				downsample_block (float dest_ptr [], const float src_ptr [], long nbr_spl);
	void				phase_block (float dest_ptr [], const float src_ptr [], long nbr_spl);



/*\\\ PROTECTED \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

protected:



/*\\\ PRIVATE \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

private:

	// Magic code for checking if coefs have been set or not in process_block()
	enum {			CHK_COEFS_NOT_SET	= 12345	};

	rspl_FORCEINLINE float
						process_sample (float path_0, float path_1);

	float				_coef_arr [NBR_COEFS];
	float				_x_arr [2];
	float				_y_arr [NBR_COEFS];



/*\\\ FORBIDDEN MEMBER FUNCTIONS \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

private:

						Downsampler2Flt (const Downsampler2Flt &other);
	Downsampler2Flt &
						operator = (const Downsampler2Flt &other);
	bool				operator == (const Downsampler2Flt &other);
	bool				operator != (const Downsampler2Flt &other);

};	// class Downsampler2Flt



}	// namespace rspl



#include	"Downsampler2Flt.hpp"



#endif	// rspl_Downsampler2_HEADER_INCLUDED



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
