#include "PersistencyService.h"
#include "Persistent.h"
#include "Externals/Compression/lz.h"
#include "System/Console/Trace.h"
#include "Foundation/Types/Types.h"
#include <stdio.h>   // remove() / rename() for the atomic save-replace
#include <string>
#include <fcntl.h>   // open() — fsync the temp before rename (durability)
#include <unistd.h>  // fsync()

PersistencyService::PersistencyService():Service(MAKE_FOURCC('S','V','P','S')) {
} ;

void PersistencyService::Save(const char *name) {

    Path filename(name);
    std::string finalPath = filename.GetPath() ;
    std::string tmpPath   = finalPath + ".tmp" ;

    // IMPORTANT: write to a brand-new temp file, then rename it over the target.
    // fopen("w") does NOT reliably truncate on the device's SD-card filesystem,
    // so saving shorter content over a longer previous file left the old file's
    // tail bytes appended after the new document ("junk after document element").
    // That corrupt XML then crashed the NEXT project load (black screen on
    // launch). Writing a fresh temp + renaming sidesteps truncation entirely and
    // is crash-safe: the real file is only swapped in once fully written.
    TiXmlDocument doc(tmpPath);
    TiXmlElement first("LITTLEGPTRACKER") ;
	TiXmlNode *node=doc.InsertEndChild(first) ;

	// Loop on all registered service
	// accumulating XML flow

	IteratorPtr<SubService> it(GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		Persistent *currentItem=(Persistent *)&it->CurrentItem() ;
		currentItem->Save(node) ;
	} ;

	::remove(tmpPath.c_str()) ;            // make sure the temp is brand-new
	if (doc.SaveFile()) {                  // write the fresh, clean document
		// Force the temp's bytes to disk BEFORE it becomes the real file. This
		// build defines _64BIT, which compiles out the fsync in UnixFile::Close,
		// so without this a power cut right after the rename could leave a 0-byte
		// / partially-written project -> black screen on the next load.
		int fd=::open(tmpPath.c_str(),O_RDONLY) ;
		if (fd>=0) { ::fsync(fd) ; ::close(fd) ; }
		::remove(finalPath.c_str()) ;      // drop the old (non-truncatable) file
		::rename(tmpPath.c_str(), finalPath.c_str()) ; // atomically swap it in
	}
};

bool PersistencyService::Load() {

	Path filename("project:lgptsav.dat") ;
	PersistencyDocument doc( filename.GetPath() );

	// Try opening the file
	
	FileSystem *fs=FileSystem::GetInstance() ;
	I_File *file=fs->Open(filename.GetPath().c_str(),"r") ;
	if (!file) return false ;
	
	// get file size and read all buffer
	
	file->Seek(0,SEEK_END) ;
	int length=file->Tell() ;

	// +1 and a trailing NUL: TiXmlDocument::Parse() treats the buffer as a
	// C string and runs until it sees '\0'. Without the terminator the parser
	// reads past the end of the allocation (heap-buffer-overflow) -- which, with
	// the wrong file size / heap layout, corrupts the heap and crashes ("malloc:
	// unsorted double linked list corrupted") on a later load.
	unsigned char *compBuffer=(unsigned char *)SYS_MALLOC(length+1) ;

  file->Seek(0,SEEK_SET) ;
	file->Read(compBuffer,1,length) ;
	compBuffer[length]=0 ;
	file->Close();
	delete file ;
	
	if (!doc.Parse((char *)compBuffer)) {
        
		// Get uncompressed buffer size from first byte
		
		int offset=sizeof(int) ;
		int fullLength ;
		memcpy(&fullLength,compBuffer,offset) ;
		
		// Allocate a buffer to decompress data
		
		unsigned char *xmlSource=(unsigned char *)SYS_MALLOC(fullLength+1) ;
		if (!xmlSource) {
			Trace::Error("could not allocate space for %d bytes") ;
			return false ;
		}

    LZ_Uncompress(compBuffer+offset,xmlSource,length-offset);
		xmlSource[fullLength]=0 ; // NUL-terminate for TiXml::Parse (same overflow)

		// Initialize XML document on decompressed buffer
		doc.Parse((char *)xmlSource) ;

		SYS_FREE(xmlSource) ;

		
	} ; 
	SYS_FREE(compBuffer) ;

	TiXmlNode* node = 0;
	node = doc.FirstChild( "LITTLEGPTRACKER" );
	if (!node) {
		Trace::Error("could not find master node") ;
		return false ;
	};

	TiXmlElement* element =node->ToElement();
	node = element->FirstChildElement() ;
	if (node) {
		element = node->ToElement();
		while (element) {
			IteratorPtr<SubService> it(GetIterator()) ;
			for (it->Begin();!it->IsDone();it->Next()) {
				Persistent *currentItem=(Persistent *)&it->CurrentItem() ;
				if (currentItem->Restore(element)) {
					break ;
				} ;
			}
			element = element->NextSiblingElement();
		} ;
	}
	return true ;
} ;
