/*****************************************************************************

        Downsampler2Flt.cpp
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

#include	"def.h"
#include	"Downsampler2Flt.h"

#include	<cassert>



namespace rspl
{



/*\\\ PUBLIC \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



/*
==============================================================================
Name: ctor
Throws: Nothing
==============================================================================
*/

Downsampler2Flt::Downsampler2Flt ()
:	_coef_arr ()
,	_x_arr ()
,	_y_arr ()
{
	_coef_arr [0] = static_cast <float> (CHK_COEFS_NOT_SET);
	clear_buffers ();
}



/*
==============================================================================
Name: set_coefs
Description:
	Set the filter coefficients.
Input parameters:
	- coef_ptr: pointer on the array containing the coefficients.
Throws: Nothing
==============================================================================
*/

void	Downsampler2Flt::set_coefs (const double coef_ptr [NBR_COEFS])
{
	assert (coef_ptr != 0);

	for (int mem = 0; mem < NBR_COEFS; ++mem)
	{
		const float		coef = static_cast <float> (coef_ptr [mem]);
		assert (coef > 0);
		assert (coef < 1);
		_coef_arr [mem] = coef;
	}
}



/*
==============================================================================
Name: clear_buffers
Description:
	Clear the state buffer of the filter, as if input sample were 0 for an
	infinite time.
Throws: Nothing.
==============================================================================
*/

void	Downsampler2Flt::clear_buffers ()
{
	_x_arr [0] = 0;
	_x_arr [1] = 0;
	for (int mem = 0; mem < NBR_COEFS; ++mem)
	{
		_y_arr [mem] = 0;
	}
}



/*
==============================================================================
Name: downsample_block
Description:
	Downsample by 2 a block of sample data. Output rate is half the input.
	set_coefs() must have been called at least once before processing anything.
	Warning: the volume is doubled! The right formula includes a 0.5 gain,
	averaging both paths instead of just summing them. We removed this gain to
	ensure maximum performance. You can compensate it later in the processing
	path.
Input parameters:
	- src_ptr: pointer on input sample data.
	- nbr_spl: Number of output samples to generate. Number of provided samples
		has to be twice this value.
Output parameters:
	- dest_ptr: pointer on downsampled sample data to generate. Can be the
		same address as src_ptr, because the algorithm can process in-place.
Throws: Nothing
==============================================================================
*/

void	Downsampler2Flt::downsample_block (float dest_ptr [], const float src_ptr [], long nbr_spl)
{
	assert (_coef_arr [0] != static_cast <float> (CHK_COEFS_NOT_SET));
	assert (dest_ptr != 0);
	assert (src_ptr != 0);
	assert (nbr_spl > 0);

	long				pos = 0;
	do
	{
		const float		path_0 = src_ptr [pos * 2 + 1];
		const float		path_1 = src_ptr [pos * 2    ];
		dest_ptr [pos] = process_sample (path_0, path_1);
		++ pos;
	}
	while (pos < nbr_spl);
}



/*
==============================================================================
Name: phase_block
Description:
	This function is used to adjust the phase of a signal which rate is not
	change, in order to match phase of downsampled signals. Basically works
	by inserting 0 between samples and downsampling by a factor two.
	Unlike downsample_block(), the gain does not need to be fixed.
Input parameters:
	- src_ptr: Pointer on the data whose phase has to be shifted.
	- nbr_spl: Number of samples to process. > 0.
Output parameters:
	- dest_ptr: Pointer on data to generate. Can be the same address as
		src_ptr, because the algorithm can process in-place.
Throws: Nothing
==============================================================================
*/

void	Downsampler2Flt::phase_block (float dest_ptr [], const float src_ptr [], long nbr_spl)
{
	assert (_coef_arr [0] != static_cast <float> (CHK_COEFS_NOT_SET));
	assert (dest_ptr != 0);
	assert (src_ptr != 0);
	assert (nbr_spl > 0);

	long				pos = 0;
	do
	{
		float				path_1 = src_ptr [pos];
		dest_ptr [pos] = process_sample (0.0f, path_1);
		++ pos;
	}
	while (pos < nbr_spl);

	// Kills denormals on path 0, if any. Theoretically we just need to do it
	// on results of multiplications with coefficients < 0.5.
	_y_arr [0] += ANTI_DENORMAL_FLT;
	_y_arr [2] += ANTI_DENORMAL_FLT;
	_y_arr [4] += ANTI_DENORMAL_FLT;
	_y_arr [6] += ANTI_DENORMAL_FLT;

	_y_arr [0] -= ANTI_DENORMAL_FLT;
	_y_arr [2] -= ANTI_DENORMAL_FLT;
	_y_arr [4] -= ANTI_DENORMAL_FLT;
	_y_arr [6] -= ANTI_DENORMAL_FLT;
}



/*\\\ PROTECTED \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



/*\\\ PRIVATE \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



}	// namespace rspl



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
