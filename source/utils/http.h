/****************************************************************************
 * BS-X GX
 *
 * http.h
 *
 * Minimal HTTP/1.0 GET for talking to the station
 ***************************************************************************/

#ifndef _HTTP_H_
#define _HTTP_H_

#include <gctypes.h>

enum
{
	HTTP_ERR_RESOLVE = -1,
	HTTP_ERR_CONNECT = -2,
	HTTP_ERR_SEND = -3,
	HTTP_ERR_HEADER = -4,
	HTTP_ERR_STATUS = -5,
	HTTP_ERR_TOOBIG = -6,
	HTTP_ERR_RECEIVE = -7
};

// Returns the body length, or an HTTP_ERR_* code. Blocks; call from a worker thread.
int HttpGet(const char *host, u16 port, const char *path, u8 *buf, u32 max);
const char *HttpErrorString(int err);

#endif
