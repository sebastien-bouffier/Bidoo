/*****************************************************************************

        InterpFlt.hpp
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



#if defined (rspl_InterpFlt_CURRENT_CODEHEADER)
	#error Recursive inclusion of InterpFlt code header.
#endif
#define	rspl_InterpFlt_CURRENT_CODEHEADER

#if ! defined (rspl_InterpFlt_CODEHEADER_INCLUDED)
#define	rspl_InterpFlt_CODEHEADER_INCLUDED



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

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

template <int SC>
InterpFlt <SC>::InterpFlt ()
:	_phase_arr ()
{
	// Nothing
}



/*
==============================================================================
Name: set_impulse
Description:
	Set the FIR impulse. Its length should be IMPULSE_LEN, and being centered
	on IMPULSE_LEN/2. Theoretically its length is odd, with a padding 0 at the
	beginning.
	This function must be called at least once before using interpolate().
Input parameters:
	- imp_ptr: pointer on the first coefficient of the impulse.
Throws: Nothing
==============================================================================
*/

template <int SC>
void	InterpFlt <SC>::set_impulse (const double imp_ptr [IMPULSE_LEN])
{
	assert (imp_ptr != 0);

	double			next_coef_dbl = 0;

	for (int fir_pos = FIR_LEN - 1; fir_pos >= 0; --fir_pos)
	{
		for (int phase_cnt = NBR_PHASES - 1; phase_cnt >= 0; --phase_cnt)
		{
			const int		imp_pos = fir_pos * NBR_PHASES + phase_cnt;
			const double	coef_dbl = imp_ptr [imp_pos];

			const float		coef = static_cast <float> (coef_dbl);
			const float		dif = static_cast <float> (next_coef_dbl - coef_dbl);

			const int		table_pos = FIR_LEN - 1 - fir_pos;
			Phase &			phase = _phase_arr [phase_cnt];
			phase._imp [table_pos] = coef;
			phase._dif [table_pos] = dif;

			next_coef_dbl = coef_dbl;
		}
	}
}



/*
==============================================================================
Name: interpolate
Description:
	Interpolate sample data.
Input parameters:
	- data_ptr: pointer on sample data, at the position of interpolation
	- frac_pos: fractional interpolatin position, full 32-bit scale.
Returns: Interpolated sample
Throws: Nothing
==============================================================================
*/

template <int SC>
float	InterpFlt <SC>::interpolate (const float data_ptr [], UInt32 frac_pos) const
{
	assert (data_ptr != 0);

	// q is made of the lower bits of the fractional position, scaled in the
	// range [0 ; 1[.
	const float		q_scl = 1.0f / (65536.0f * 65536.0f);
	const float		q = static_cast <float> (frac_pos << NBR_PHASES_L2) * q_scl;

	// Compute phase index (the high-order bits)
	const int		phase_index = frac_pos >> (32 - NBR_PHASES_L2);
	const Phase &	phase = _phase_arr [phase_index];
	const int		offset = -FIR_LEN/2 + 1;

	return (phase.convolve (data_ptr + offset, q));
}



/*\\\ PROTECTED \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



/*\\\ PRIVATE \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



}	// namespace rspl



#endif	// rspl_InterpFlt_CODEHEADER_INCLUDED

#undef rspl_InterpFlt_CURRENT_CODEHEADER



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
