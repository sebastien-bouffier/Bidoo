/*****************************************************************************

        StopWatch.hpp
        Copyright (c) 2003 Laurent de Soras

Please complete the definitions according to your compiler/architecture.
It's not a big deal if it's not possible to get the clock count...

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



#if defined (rspl_StopWatch_CURRENT_CODEHEADER)
	#error Recursive inclusion of StopWatch code header.
#endif
#define	rspl_StopWatch_CURRENT_CODEHEADER

#if ! defined (rspl_StopWatch_CODEHEADER_INCLUDED)
#define	rspl_StopWatch_CODEHEADER_INCLUDED



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

#include	"Fixed3232.h"



namespace rspl
{



/*\\\ PUBLIC \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

void	StopWatch::start ()
{

#if defined (_MSC_VER)

	Fixed3232		t;

	__asm
	{
		rdtsc
		mov				t._part._msw, edx
		mov				t._part._lsw, eax
	}

	_start_time = t._all;

#elif defined (__GNUC__) && defined (__i386__)

	Int64				t;
	__asm__ __volatile__ ("rdtsc" : "=A" (t));
	_start_time = t;

#elif (__MWERKS__) && defined (__POWERPC__) 
	
	register Int64	t;
	asm
	{
		loop:
		mftbu			t@hiword
		mftb			t@loword
		mftbu r5
		cmpw t@hiword,r5
		bne loop
	}
	_start_time = t;
	
#endif

}



void	StopWatch::stop ()
{

#if defined (_MSC_VER)

	Fixed3232		t;

	__asm
	{
		rdtsc
		mov				t._part._msw, edx
		mov				t._part._lsw, eax
	}

	_stop_time = t._all;

#elif defined (__GNUC__) && defined (__i386__)

	Int64				t;
	__asm__ __volatile__ ("rdtsc" : "=A" (t));
	_stop_time = t;

#elif (__MWERKS__) && defined (__POWERPC__) 
	
	register Int64	t;
	asm
	{
		loop:
		mftbu			t@hiword
		mftb			t@loword
		mftbu r5
		cmpw t@hiword,r5
		bne loop
	}
	_stop_time = t;
	
#endif

}



/*\\\ PROTECTED \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



/*\\\ PRIVATE \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



}	// namespace rspl



#endif	// rspl_StopWatch_CODEHEADER_INCLUDED

#undef rspl_StopWatch_CURRENT_CODEHEADER



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
