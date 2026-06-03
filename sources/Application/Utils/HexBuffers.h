#ifndef _HEX_BUFFERS_H_
#define _HEX_BUFFERS_H_

#include "Externals/TinyXML/tinyxml.h"

void saveHexBuffer(TiXmlNode *parent,const char *nodeName,unsigned char *src,unsigned len) ;
void saveHexBuffer(TiXmlNode *parent,const char *nodeName,unsigned short *src,unsigned len) ;
void saveHexBuffer(TiXmlNode *parent,const char *nodeName,unsigned int *src,unsigned len) ;
// Returns the number of bytes written into dst (used to detect legacy buffer
// sizes, e.g. for project format migration).
unsigned restoreHexBuffer(TiXmlNode *node,unsigned char *dst) ;

#endif
