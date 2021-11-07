/*****************************************************************************

        InterpFlt.h
        Copyright (c) 2003 Laurent de Soras

FIR interpolator. This class is stateless and therefore can be used in "random
access" on the source sample.

Template parameters:

- SC: Scale of the FIR interpolator. Its length is 64 * 12 * SC.

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



#if ! defined (rspl_InterpFlt_HEADER_INCLUDED)
#define	rspl_InterpFlt_HEADER_INCLUDED

#if defined (_MSC_VER)
	#pragma once
	#pragma warning (4 : 4250) // "Inherits via dominance."
#endif



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

#include	"InterpFltPhase.h"
#include	"UInt32.h"



namespace rspl
{



template <int SC = 1>
class InterpFlt
{

/*\\\ PUBLIC \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

public:

	typedef	InterpFltPhase <SC>	Phase;

	enum {			SCALE				= Phase::SCALE	};
	enum {			FIR_LEN			= Phase::FIR_LEN	};
	enum {			NBR_PHASES_L2	= 6	};

	enum {			NBR_PHASES		= 1 << NBR_PHASES_L2	};
	enum {			IMPULSE_LEN		= FIR_LEN * NBR_PHASES	};

						InterpFlt ();
	virtual			~InterpFlt () {}

	void				set_impulse (const double imp_ptr [IMPULSE_LEN]);
	rspl_FORCEINLINE float
						interpolate (const float data_ptr [], UInt32 frac_pos) const;



/*\\\ PROTECTED \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

protected:



/*\\\ PRIVATE \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

private:

	Phase				_phase_arr [NBR_PHASES];



/*\\\ FORBIDDEN MEMBER FUNCTIONS \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

private:

						InterpFlt (const InterpFlt &other);
	InterpFlt &		operator = (const InterpFlt &other);
	bool				operator == (const InterpFlt &other);
	bool				operator != (const InterpFlt &other);

};	// class InterpFlt



}	// namespace rspl



#include	"InterpFlt.hpp"



#endif	// rspl_InterpFlt_HEADER_INCLUDED



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
