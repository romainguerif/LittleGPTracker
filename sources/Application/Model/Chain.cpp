#include "Chain.h"
#include "System/System/System.h"
#include <stdlib.h>
#include <string.h>

Chain::Chain() {

	int size=CHAIN_COUNT*16 ;
	data_=(unsigned char *)SYS_MALLOC(size) ;
	memset(data_,0xFF,size) ;
	transpose_=(unsigned char *)SYS_MALLOC(size) ;
	memset(transpose_,0x00,size) ;
	for (int i=0;i<CHAIN_COUNT;i++) {
		isUsed_[i]=false ;
	}
} ;

Chain::~Chain() {
	if (data_) SYS_FREE(data_) ;
	if (transpose_) SYS_FREE(transpose_) ;
};

unsigned short Chain::GetNext(int start) {
	// Scan from 'start' (wrapping) so a clone lands on the NEXT free id after the
	// source instead of the first free id anywhere. start=0 = original behaviour.
	if (start < 0) start = 0 ;
	for (int n=0;n<CHAIN_COUNT;n++) {
		int i=(start+n)%CHAIN_COUNT ;
		if (!isUsed_[i]) {
			isUsed_[i]=true ;
			return i ;
		}
	}
	return NO_MORE_CHAIN ;
} ;

void Chain::SetUsed(unsigned char c) {
	isUsed_[c]=true ;
}

void Chain::ClearAllocation() {

	for (int i=0;i<CHAIN_COUNT;i++) {
		isUsed_[i]=false ;
	}
}
