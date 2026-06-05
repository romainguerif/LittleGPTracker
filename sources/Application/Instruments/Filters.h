/****************************
some wanabe filter code by Beausoleil S Guillemette
hope it will work :D
[2006-09-25 .. 2006-09-30]
****************************/

/*-------------------------------------------
some useful abstract info:
 there is one filter per voice (SONG_CHANNEL_COUNT of them -- the array is sized
 by that, not the literal 8 this comment used to claim). each filter is stereo,
 so there are 2 height and speed vars. The filters are static/globals, so the
 state resumes between buffer renders (removing clics).
-------------------------------------------*/

#include "Application/Utils/fixed.h"

typedef enum 
{
	FLT_LOWPASS,		//only lowpass is implemented so far
	FLT_HIGHPASS,
	FLT_BANDPASS,
	FLT_NOTCH,
}filterType_t;


typedef struct
{
	fixed height[2];
	fixed speed[2];
	fixed hipdelay[2] ;
	filterType_t type;
	fixed parm1,parm2;
  fixed freq,reso;
	fixed dirt ;
	fixed mix ;
} filter_t;

void set_filter(int channel, filterType_t type, fixed parm1, fixed parm2,int mix,bool bassyMapping);

void init_filters(void) ;


filter_t *get_filter(int channel) ;