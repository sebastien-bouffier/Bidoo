/*****************************************************************************

        BaseVoiceState.h
        Copyright (c) 2003 Laurent de Soras

This is a private class used by ResamplerFlt to pass voice data to InterpPack
for the rendering.

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



#if ! defined (rspl_BaseVoiceState_HEADER_INCLUDED)
#define	rspl_BaseVoiceState_HEADER_INCLUDED

#if defined (_MSC_VER)
	#pragma once
	#pragma warning (4 : 4250) // "Inherits via dominance."
#endif



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

#include	"Fixed3232.h"



namespace rspl
{



class BaseVoiceState
{

/*\\\ PUBLIC \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

public:

	enum {			NBR_BITS_PER_OCT	= 16	};
	enum {			FADE_LEN				= 64	};

						BaseVoiceState ();
	BaseVoiceState &
						operator = (const BaseVoiceState &other);

	void				compute_step (long pitch);

	Fixed3232		_pos;			// Position in the current MIP-map level
	Fixed3232		_step;		// Step in the current MIP-map level
	const float *	_table_ptr;
	long				_table_len;
	int				_table;
	bool				_ovrspl_flag;



/*\\\ PROTECTED \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

protected:



/*\\\ PRIVATE \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

private:



/*\\\ FORBIDDEN MEMBER FUNCTIONS \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

private:

						BaseVoiceState (const BaseVoiceState &other);
	bool				operator == (const BaseVoiceState &other);
	bool				operator != (const BaseVoiceState &other);

};	// class BaseVoiceState



}	// namespace rspl



//#include	"rspl/BaseVoiceState.hpp"



#endif	// rspl_BaseVoiceState_HEADER_INCLUDED



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
