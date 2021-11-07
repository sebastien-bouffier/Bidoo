/*****************************************************************************

        UInt32.h
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



#if ! defined (rspl_UInt32_HEADER_INCLUDED)
#define	rspl_UInt32_HEADER_INCLUDED

#if defined (_MSC_VER)
	#pragma once
	#pragma warning (4 : 4250) // "Inherits via dominance."
#endif



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

#include	<climits>



namespace rspl
{



#if defined (_MSC_VER)

	typedef	unsigned __int32	UInt32;

#elif (defined (__MWERKS__) || defined (__GNUC__) || defined (__BEOS__)) && UINT_MAX == 0xFFFFFFFFUL

	typedef	unsigned int	UInt32;

#else

	#error No unsigned 32-bit integer type defined for this compiler !

#endif




}	// namespace rspl



#endif	// rspl_UInt32_HEADER_INCLUDED



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
