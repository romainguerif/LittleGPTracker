#ifndef _HELP_LEGEND_H_
#define _HELP_LEGEND_H_

#include <string>
#include <stdlib.h>
#include <string.h>

static inline std::string* getHelpLegend(FourCC command) {
	std::string* result = new std::string[3];
	result[2].assign("bb at speed aa");
	switch (command) {
		case I_CMD_KILL:
			result[0].assign("KILl:--bb");
			result[1].assign("stop playing note");
			result[2].assign("after bb ticks");
			break;
		case I_CMD_LPOF:
			result[0].assign("LooP OFset: Shift both");
			result[1].assign("the loop start & loop ");
			result[2].assign("end values aaaa digits");
			break;
		case I_CMD_ARPG:
			result[0].assign("ARPeGgio:abcd Cycle");
			result[1].assign("through relative pitches");
			result[2].assign("from original pitch");
			break;
		case I_CMD_VOLM:
			result[0].assign("VOLuMe:aabb");
			result[1].assign("approach volume");
			break;
		case I_CMD_PTCH:
			result[0].assign("PiTCH:aabb");
			result[1].assign("approach pitch");
			break;
		case I_CMD_HOP:
			result[0].assign("HOP:aabb");
			result[1].assign("hop to bb");
			result[2].assign("aa times");
			break;
		case I_CMD_LEGA:
			result[0].assign("LEGAto: slide from");
			result[1].assign("previous note to pitch");
			break;
		case I_CMD_RTRG:
			result[0].assign("ReTRiG:aabb retrigger loop");
			result[1].assign("from current position over");
			result[2].assign("bb ticks at speed aa");
			break;
		case I_CMD_TMPO:
			result[0].assign("TeMPO:--bb");
			result[1].assign("sets the tempo to hex");
			result[2].assign("value bb");
			break;
		case I_CMD_MDCC:
			result[0].assign("MiDiCC:aabb");
			result[1].assign("CC message aa");
			result[2].assign("value bb");
			break;
		case I_CMD_MDPG:
			result[0].assign("MiDi ProGram Change");
			result[1].assign("send program change bb");
			result[2].assign("to current channel");
			break;
		case I_CMD_MVEL:
			result[0].assign("MidiVELocity:--bb");
			result[1].assign("Set velocity bb for step");
			result[2].assign("");
	    break;
		case I_CMD_PLOF:
			result[0].assign("PLay OFfset:aabb");
			result[1].assign("jump abs to aa or");
			result[2].assign("move rel bb chunks");
			break;
		case I_CMD_FLTR:
			result[0].assign("FiLTer&Resonance:aabb");
			result[1].assign("cutoff aa");
			result[2].assign("resonance bb");
			break;
		case I_CMD_TABL:
			result[0].assign("TABLe:--bb");
			result[1].assign("trigger table bb");
			result[2].assign("");
			break;
		case I_CMD_CRSH:
			result[0].assign("drive&CRuSH:aa-b");
			result[1].assign("drive aa");
			result[2].assign("crush -b");
			break;
		case I_CMD_FCUT:
			result[0].assign("FilterCUToff:aabb");
			result[1].assign("set cutoff to");
			break;
		case I_CMD_FRES:
			result[0].assign("FilterRESonance:aabb");
			result[1].assign("set resonance to");
			break;
		case I_CMD_PAN_:
			result[0].assign("PAN:aabb");
			result[1].assign("pan to value");
			break;
		case I_CMD_GROV:
			result[0].assign("GROoVe:--bb");
			result[1].assign("trigger groove bb");
			result[2].assign("");
			break;
		case I_CMD_IRTG:
			result[0].assign("InstrumentReTriG:aabb");
			result[1].assign("retrig and transpose to");
			break;
		case I_CMD_PFIN:
			result[0].assign("PitchFINetune:aabb");
			result[1].assign("fine tune to ");
			break;
		case I_CMD_DLAY:
			result[0].assign("DeLAY:--bb");
			result[1].assign("delay bb tics");
			result[2].assign("");
			break;
		case I_CMD_FBMX:
			result[0].assign("FeedBack MiX:aabb");
			result[1].assign("feedback mix to");
			break;
		case I_CMD_FBTN:
			result[0].assign("FeedBack TuNe:aabb");
			result[1].assign("feedback tune to");
			break;
		case I_CMD_STOP:
			result[0].assign("STOP playing song");
			result[1].assign("immediately");
			result[2].assign("");
			break;
		case I_CMD_EQLO:
			result[0].assign("EqLOw:--bb");
			result[1].assign("set low shelf to bb");
			result[2].assign("80=flat 00=cut FF=boost");
			break;
		case I_CMD_EQMD:
			result[0].assign("EqMiD:--bb");
			result[1].assign("set mid band to bb");
			result[2].assign("80=flat 00=cut FF=boost");
			break;
		case I_CMD_EQHI:
			result[0].assign("EqHIgh:--bb");
			result[1].assign("set high shelf to bb");
			result[2].assign("80=flat 00=cut FF=boost");
			break;
		case I_CMD_LFOR:
			result[0].assign("LFORate:--bb");
			result[1].assign("set lfo rate to bb");
			result[2].assign("00=slow FF=fast");
			break;
		case I_CMD_LFOD:
			result[0].assign("LFODepth:--bb");
			result[1].assign("set lfo depth to bb");
			result[2].assign("00=none FF=max");
			break;
		case I_CMD_DSND:
			result[0].assign("DelaySeND:--bb");
			result[1].assign("delay send amount bb");
			result[2].assign("00=dry FF=full send");
			break;
		case I_CMD_PROB:
			result[0].assign("PROBability:aabb");
			result[1].assign("play note aa out of bb");
			result[2].assign("e.g. 0108 = 1 in 8");
			break;
		case I_CMD_RPAN:
			result[0].assign("RandomPAN:--bb");
			result[1].assign("randomize pan, spread bb");
			result[2].assign("00=off FF=full width");
			break;
		case I_CMD_RPIT:
			result[0].assign("RandomPITch:--bb");
			result[1].assign("randomize pitch, range bb");
			result[2].assign("00=off FF=wide");
			break;
		case I_CMD_RVOL:
			result[0].assign("RandomVOLume:--bb");
			result[1].assign("randomize volume, range bb");
			result[2].assign("00=off FF=wide");
			break;
		default:

			result[0].assign("");
			result[1].assign("");
			result[2].assign("");
		break;
	}
	return result;
}

#endif //_HELP_LEGEND_H_
