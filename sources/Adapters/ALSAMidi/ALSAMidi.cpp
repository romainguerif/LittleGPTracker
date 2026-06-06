#include "ALSAMidi.h"
#include "MidiOutScheduler.h"
#include "System/Console/Trace.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>

// ---- rawmidi out device ----------------------------------------------------

ALSARawMidiOutDevice::ALSARawMidiOutDevice(const char *devPath, const char *name)
 : MidiOutDevice(name), devPath_(devPath), fd_(-1), scheduler_(0) {}

ALSARawMidiOutDevice::~ALSARawMidiOutDevice() { Close(); }

bool ALSARawMidiOutDevice::Init() {
	if (fd_<0) {
		// Blocking write is fine: the writes happen on the dedicated scheduler
		// thread, never on the audio thread, so a momentarily-full kernel buffer
		// stalls only MIDI output (slow rate -> never in practice), not the audio.
		fd_=open(devPath_.c_str(), O_WRONLY);
		if (fd_<0) {
			Trace::Error("ALSAMidi: open(%s) failed: %s",devPath_.c_str(),strerror(errno));
			return false;
		}
		Trace::Log("ALSAMidi","opened %s (fd=%d)",devPath_.c_str(),fd_);
	}
	if (!scheduler_) {
		scheduler_=new MidiOutScheduler(this);
		scheduler_->StartScheduler();
	}
	return true;
}

void ALSARawMidiOutDevice::Close() {
	if (scheduler_) { scheduler_->StopScheduler(); delete scheduler_; scheduler_=0; }
	if (fd_>=0) { close(fd_); fd_=-1; }
}

bool ALSARawMidiOutDevice::Start() { return Init(); }
void ALSARawMidiOutDevice::Stop() { Close(); }

// MIDI message length from the status byte.
static int midiMsgLen(unsigned char status) {
	if (status>=0xF8) return 1;             // system realtime (clock/start/stop/...)
	switch (status&0xF0) {
		case 0xC0: case 0xD0: return 2;     // program change, channel aftertouch
		case 0xF0:                          // system common
			if (status==0xF1 || status==0xF3) return 2;
			if (status==0xF2) return 3;
			return 1;                       // F6 etc. (F0 sysex not produced here)
		default: return 3;                  // note on/off, poly AT, CC, pitch bend
	}
}

// Defer to the precise scheduler: it emits the message the instant its frame
// stamp is played (audio-locked, jitter-free). Untimed messages (stamp 0) go out
// immediately.
void ALSARawMidiOutDevice::SendMessage(MidiMessage &m) {
	if (scheduler_)
		scheduler_->Push(m.stamp_, m.status_, m.data1_, m.data2_);
	else
		WriteRaw(m.status_, m.data1_, m.data2_); // fallback (scheduler not up)
}

void ALSARawMidiOutDevice::SetTimingOffset(int frames) {
	if (scheduler_) scheduler_->SetOffsetFrames(frames);
}

// The actual byte write (scheduler thread only). Blocking; handles EINTR.
void ALSARawMidiOutDevice::WriteRaw(unsigned char status, unsigned char d1, unsigned char d2) {
	if (fd_<0) return;
	unsigned char buf[3] = { status, d1, d2 };
	int len=midiMsgLen(status);
	int off=0;
	while (off<len) {
		ssize_t n=write(fd_, buf+off, (size_t)(len-off));
		if (n>0) { off+=(int)n; continue; }
		if (n<0 && errno==EINTR) continue;
		break; // EAGAIN (shouldn't happen on a blocking fd) or other error: give up
	}
}

// ---- service: enumerate rawmidi ports + probe ------------------------------

ALSARawMidiService::ALSARawMidiService() {}
ALSARawMidiService::~ALSARawMidiService() {}

// Copy the first line of a small /proc file into out (for a human-friendly name).
static bool readProcLine(const char *path, char *out, int outSize) {
	FILE *f=fopen(path,"r");
	if (!f) return false;
	bool ok = (fgets(out,outSize,f)!=0);
	fclose(f);
	if (ok) { size_t l=strlen(out); while (l>0 && (out[l-1]=='\n'||out[l-1]=='\r')) out[--l]=0; }
	return ok && out[0];
}

void ALSARawMidiService::buildDriverList() {
	// Probe file at the SD-card root (readable over the dufs HTTP server) so we can
	// confirm, off-device, exactly what MIDI hardware the kernel exposes. Harmless
	// no-op on platforms where /mnt/SDCARD doesn't exist (fopen returns NULL).
	FILE *pf=fopen("/mnt/SDCARD/midi_probe.txt","w");
	if (pf) {
		// dump the sound-card list first (tells us if USB-MIDI was enumerated at all)
		FILE *cf=fopen("/proc/asound/cards","r");
		fprintf(pf,"=== /proc/asound/cards ===\n");
		if (cf) { char line[256]; while (fgets(line,sizeof(line),cf)) fputs(line,pf); fclose(cf); }
		else fprintf(pf,"(could not open /proc/asound/cards)\n");
		fprintf(pf,"\n=== /dev/snd rawmidi nodes (midiCxDy) ===\n");
	}

	int found=0;
	DIR *d=opendir("/dev/snd");
	if (d) {
		struct dirent *e;
		while ((e=readdir(d))!=0) {
			int card=-1, dev=-1;
			if (sscanf(e->d_name,"midiC%dD%d",&card,&dev)==2) {
				char devPath[64]; snprintf(devPath,sizeof(devPath),"/dev/snd/%s",e->d_name);
				// human name: try the card's id from /proc, else the node name
				char cardId[64]={0};
				char idPath[64]; snprintf(idPath,sizeof(idPath),"/proc/asound/card%d/id",card);
				char name[96];
				if (readProcLine(idPath,cardId,sizeof(cardId)))
					snprintf(name,sizeof(name),"%s (%s)",cardId,e->d_name);
				else
					snprintf(name,sizeof(name),"%s",e->d_name);
				Insert(new ALSARawMidiOutDevice(devPath,name));
				found++;
				Trace::Log("ALSAMidi","found MIDI out: %s -> %s",name,devPath);
				if (pf) fprintf(pf,"  %s  ->  %s\n",devPath,name);
			}
		}
		closedir(d);
	} else if (pf) {
		fprintf(pf,"(could not open /dev/snd: %s)\n",strerror(errno));
	}

	if (pf) {
		fprintf(pf,"\n%d MIDI out port(s) found.\n",found);
		fprintf(pf,"Plug the USB-MIDI cable BEFORE launching, then pick the port in\n");
		fprintf(pf,"the project 'midi' field. If 0 ports: the kernel didn't enumerate\n");
		fprintf(pf,"USB-MIDI (check snd-usb-audio / USB host mode).\n");
		fclose(pf);
	}
	Trace::Log("ALSAMidi","%d MIDI out port(s)",found);
}
