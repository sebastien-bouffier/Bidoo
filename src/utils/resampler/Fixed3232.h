/*****************************************************************************

        Fixed3232.h
        Copyright (c) 2003 Laurent de Soras

Depending on your computer architecture, you may have to change this file!
Invert the order of the _lsw and _msw members if your machine is Big Endian.

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



#if ! defined (rspl_Fixed3232_HEADER_INCLUDED)
#define	rspl_Fixed3232_HEADER_INCLUDED

#if defined (_MSC_VER)
	#pragma once
	#pragma warning (4 : 4250) // "Inherits via dominance."
#endif



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

#include	"Int32.h"
#include	"Int64.h"
#include	"UInt32.h"



namespace rspl
{



union Fixed3232
{
	Int64				_all;

	class
	{
	public:

#if defined (__POWERPC__)	// Big endian
		Int32				_msw;
		UInt32			_lsw;
#else	// Little endian
		UInt32			_lsw;
		Int32				_msw;
#endif

	}					_part;

};	// union Fixed3232



}	// namespace rspl



#endif	// rspl_Fixed3232_HEADER_INCLUDED



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
