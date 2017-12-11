/*****************************************************************************

        StopWatch.h
        Copyright (c) 2003 Laurent de Soras

Instrumentation class, for accurate time interval measurement. You may have
to modify the implementation to adapt it to your system and/or compiler.

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



#if ! defined (rspl_StopWatch_HEADER_INCLUDED)
#define	rspl_StopWatch_HEADER_INCLUDED

#if defined (_MSC_VER)
	#pragma once
	#pragma warning (4 : 4250) // "Inherits via dominance."
#endif



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

#include	"Int64.h"



namespace rspl
{



class StopWatch
{

/*\\\ PUBLIC \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

public:

						StopWatch ();

	inline void		start ();
	inline void		stop ();
	Int64				get_clk () const;
	double			get_clk_per_op (long div_1, long div_2 = 1) const;



/*\\\ PROTECTED \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

protected:



/*\\\ PRIVATE \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

private:

	Int64				_start_time;
	Int64				_stop_time;
#if defined (__MACOS__)
	int				_clk_mul;
#endif



/*\\\ FORBIDDEN MEMBER FUNCTIONS \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

private:

						StopWatch (const StopWatch &other);
	StopWatch &		operator = (const StopWatch &other);
	bool				operator == (const StopWatch &other);
	bool				operator != (const StopWatch &other);

};	// class StopWatch



}	// namespace rspl



#include	"StopWatch.hpp"



#endif	// rspl_StopWatch_HEADER_INCLUDED



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
