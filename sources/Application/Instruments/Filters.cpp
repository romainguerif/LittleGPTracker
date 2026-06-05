/****************************
some wanabe filter code by Beausoleil S Guillemette
hope it will work :D
[2006-09-25 .. 2006-09-30]
****************************/

#include "Filters.h"
#include <math.h>
#include "System/Console/Trace.h"
#include "Application/Model/Song.h"

static filter_t filter[SONG_CHANNEL_COUNT];

bool filters_inited=false;

fixed fp_inv_255 ;

void init_filters(void)
{

	for(int i=0;i<SONG_CHANNEL_COUNT;i++)
	{	//set sensible default values
		//lowpass filter where everything passes with no resonance
		set_filter(i,FLT_LOWPASS,i2fp(1),i2fp(0),i2fp(0),false);
	}
	fp_inv_255=fl2fp(1.0f/255) ;
	filters_inited=true;
}

void set_filter(int channel, filterType_t type, fixed param1,fixed param2,int mix,bool bassyMapping)
{
	filter_t* flt=&filter[channel];

	if(flt->type!=type)		//reset only on filter type change   (maybe no reset would make interresting bugs)
	{
		flt->height[0]=0;		//idle state
		flt->height[1]=0;		//idle state
		flt->speed[0]=0;		//paused	(to avoid having it goind crazy)
		flt->speed[1]=0;		//paused	(to avoid having it goind crazy)
		flt->hipdelay[0]=0 ;
		flt->hipdelay[1]=0 ;
		flt->type=type;
	}
  
  
	flt->dirt=fp_mul(i2fp(100),i2fp(1)-param1)+fp_mul(i2fp(5000),param1) ;
	flt->mix =fp_mul(i2fp(mix),fp_inv_255) ;

  if (param1 != flt->parm1)
  {
  	flt->parm1=param1;
    //adjust parm to get the most of the parameters, as the fx are more useful with near-limit parameters.
    if (bassyMapping)
    {
      static const fixed fpFreqDivider = fl2fp(1/22050.0f);
      static const fixed fpZeroSix = fl2fp(0.6f);
      static const fixed fpThreeOne = fl2fp(3.1f);

      fixed power = fp_add(fpZeroSix,fp_mul(param1,fpThreeOne));
      fixed frequency = fl2fp(pow(10.0f,fp2fl(power))) ;
      frequency=fp_mul(frequency,fpFreqDivider);
      flt->freq=frequency;
    }
    else
    {
      flt->freq=fp_mul(param1,param1);				//0 - .5 - 1   =>   0 - .25 - 1
    }    
  }
  
  if (param2 != flt->parm2)
  {
    flt->parm2=param2;
    flt->reso=i2fp(1)-param2 ;
    flt->reso=fp_sub(fl2fp(1.f),fp_mul(flt->reso,fp_mul(flt->reso,flt->reso)));	//0 - .5 - 1   =>   0 - .93 - 1
  }
}


filter_t *get_filter(int channel) {
	return &filter[channel];
} ;
// (Removed two long-dead commented-out filterize() prototypes -- the active
// filtering lives inline in SampleInstrument::Render. git history keeps them.)
