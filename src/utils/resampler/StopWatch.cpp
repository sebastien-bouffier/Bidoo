/*****************************************************************************

        StopWatch.cpp
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

#include	"StopWatch.h"

#if defined (__MACOS__)
#include	"def.h"
#include <Gestalt.h>
#endif	// __MACOS__

#include	<cassert>



namespace rspl
{



/*\\\ PUBLIC \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



StopWatch::StopWatch ()
:	_start_time (0)
,	_stop_time (0)
{
#if defined (__MACOS__)
	long				clk_speed;
	const ::OSErr	err = ::Gestalt (gestaltProcClkSpeed, &clk_speed);

	const long		nbr_loops = 100 * 1000L;
	int				a = 0;

	const Nanoseconds	nano_seconds_1 = AbsoluteToNanoseconds (UpTime ());
	const double	start_time_s =
		nano_seconds_1.hi * 4294967296e-9 + nano_seconds_1.lo * 1e-9;
	start ();

	for (long i = 0; i < nbr_loops; ++i)
	{
		a = a % 56 + 34;
	}

	stop ();
	const Nanoseconds	nano_seconds_2 = AbsoluteToNanoseconds (UpTime ());
	const double	stop_time_s =
		nano_seconds_2.hi * 4294967296e-9 + nano_seconds_2.lo * 1e-9;

	const double	diff_time_s = stop_time_s - start_time_s;
	const double	nbr_cycles = diff_time_s * clk_speed;

	const Int64		diff_time = _stop_time - _start_time;
	const double	clk_mul = nbr_cycles / diff_time;

	// + 1e-300 * a: prevents compiler to remove loop code.
	_clk_mul = round_int (clk_mul + 1e-300 * a);

	_start_time = 0;
	_stop_time = 0;
#endif	// __MACOS__
}



Int64	StopWatch::get_clk () const
{
#if defined (__MACOS__)
	return ((_stop_time - _start_time) * _clk_mul);
#else		// __MACOS__
	return (_stop_time - _start_time);
#endif	// __MACOS__
}



double	StopWatch::get_clk_per_op (long div_1, long div_2) const
{
	assert (div_1 > 0);
	assert (div_2 > 0);

	const double	nbr_clocks = static_cast <double> (get_clk ());
	const double	glob_div =
		static_cast <double> (div_1) * static_cast <double> (div_2);

	return (nbr_clocks / glob_div);
}



/*\\\ PROTECTED \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



/*\\\ PRIVATE \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



}	// namespace rspl



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
