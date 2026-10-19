/****************************************************************************
 * BS-X GX
 *
 * http.cpp
 *
 * Minimal HTTP/1.0 GET for talking to the station
 ***************************************************************************/

#include <gccore.h>
#include <network.h>
#include <ogc/lwp_watchdog.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <zlib.h>

#include "http.h"

#define IOS_O_NONBLOCK		0x04
#define CONNECT_TIMEOUT_MS	5000
#define IDLE_TIMEOUT_MS		10000
#define HEADER_MAX			2048
#define CHUNK				8192

// IOS writes into this rather than the caller's MEM2 buffer; one worker thread only
static u8 bounce[CHUNK] ATTRIBUTE_ALIGN(32);

static u32 ElapsedMs(u64 since)
{
	return ticks_to_millisecs(diff_ticks(since, gettime()));
}

static bool Retry(s32 res)
{
	return res == -EAGAIN || res == -EWOULDBLOCK || res == -EINPROGRESS || res == -EALREADY;
}

static s32 Connect(const char *host, u16 port)
{
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_len = sizeof(sa);
	sa.sin_port = htons(port);

	if(!inet_aton(host, &sa.sin_addr))
	{
		struct hostent *hp = net_gethostbyname(host);
		if(!hp || hp->h_addrtype != AF_INET || !hp->h_addr_list[0])
			return HTTP_ERR_RESOLVE;
		memcpy(&sa.sin_addr, hp->h_addr_list[0], hp->h_length);
	}

	s32 s = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if(s < 0)
		return HTTP_ERR_CONNECT;
	s32 flags = net_fcntl(s, F_GETFL, 0);
	if(flags >= 0)
		net_fcntl(s, F_SETFL, flags | IOS_O_NONBLOCK);

	u64 start = gettime();
	while(1)
	{
		s32 res = net_connect(s, (struct sockaddr *)&sa, sizeof(sa));
		if(res == 0 || res == -EISCONN)
			return s;
		if(!Retry(res) || ElapsedMs(start) > CONNECT_TIMEOUT_MS)
			break;
		usleep(5000);
	}
	net_close(s);
	return HTTP_ERR_CONNECT;
}

static bool SendAll(s32 s, const char *p, int len)
{
	u64 t = gettime();
	while(len > 0)
	{
		s32 res = net_write(s, p, len);
		if(res == 0 || Retry(res))
		{
			if(ElapsedMs(t) > IDLE_TIMEOUT_MS)
				return false;
			usleep(2000);
			continue;
		}
		if(res < 0)
			return false;
		p += res;
		len -= res;
		t = gettime();
	}
	return true;
}

static long ContentLength(char *header)
{
	for(char *line = strstr(header, "\r\n"); line; line = strstr(line + 2, "\r\n"))
		if(strncasecmp(line + 2, "Content-Length:", 15) == 0)
			return strtol(line + 17, NULL, 10);
	return -1;
}

// the body goes to sink as it arrives; a false from sink stops the download
static int Fetch(const char *host, u16 port, const char *path, u32 max,
                 bool (*sink)(void *ctx, const u8 *data, u32 len), void *ctx)
{
	s32 s = Connect(host, port);
	if(s < 0)
		return s;

	char req[512];
	int n = snprintf(req, sizeof(req),
		"GET %s HTTP/1.0\r\nHost: %s:%u\r\nUser-Agent: BS-X GX\r\nConnection: close\r\n\r\n",
		path, host, port);
	if(!SendAll(s, req, n))
	{
		net_close(s);
		return HTTP_ERR_SEND;
	}

	char header[HEADER_MAX + 1];
	u32 hlen = 0, got = 0;
	long length = -1;
	bool inBody = false;
	int result = 0;
	u64 t = gettime();

	while(1)
	{
		s32 r = net_read(s, bounce, CHUNK);
		if(Retry(r))
		{
			if(ElapsedMs(t) > IDLE_TIMEOUT_MS)
			{
				result = HTTP_ERR_RECEIVE;
				break;
			}
			usleep(2000);
			continue;
		}
		if(r < 0)
		{
			result = HTTP_ERR_RECEIVE;
			break;
		}
		if(r == 0)
			break;
		t = gettime();

		u8 *p = bounce;
		u32 left = r;
		if(!inBody)
		{
			while(left && hlen < HEADER_MAX && !inBody)
			{
				header[hlen++] = *p++;
				left--;
				inBody = hlen >= 4 && memcmp(header + hlen - 4, "\r\n\r\n", 4) == 0;
			}
			if(!inBody)
			{
				if(hlen < HEADER_MAX)
					continue;
				result = HTTP_ERR_HEADER;
				break;
			}

			header[hlen] = 0;
			int status = 0;
			if(sscanf(header, "HTTP/%*d.%*d %d", &status) != 1)
			{
				result = HTTP_ERR_HEADER;
				break;
			}
			if(status != 200)
			{
				result = HTTP_ERR_STATUS;
				break;
			}
			length = ContentLength(header);
			if(length > (long)max)
			{
				result = HTTP_ERR_TOOBIG;
				break;
			}
		}

		if(got + left > max)
		{
			result = HTTP_ERR_TOOBIG;
			break;
		}
		if(left && !sink(ctx, p, left))
		{
			result = HTTP_ERR_WRITE;
			break;
		}
		got += left;
		if(length >= 0 && got >= (u32)length)
			break;
	}
	net_close(s);

	if(result)
		return result;
	if(!inBody || (length >= 0 && got != (u32)length))
		return HTTP_ERR_RECEIVE;
	return got;
}

struct BufferSink { u8 *buf; u32 got; };

static bool ToBuffer(void *ctx, const u8 *data, u32 len)
{
	BufferSink *b = (BufferSink *)ctx;
	memcpy(b->buf + b->got, data, len);
	b->got += len;
	return true;
}

int HttpGet(const char *host, u16 port, const char *path, u8 *buf, u32 max)
{
	BufferSink b = { buf, 0 };
	return Fetch(host, port, path, max, ToBuffer, &b);
}

struct FileSink { FILE *f; u32 crc; };

static bool ToFile(void *ctx, const u8 *data, u32 len)
{
	FileSink *s = (FileSink *)ctx;
	s->crc = crc32(s->crc, data, len);
	return fwrite(data, 1, len, s->f) == len;
}

int HttpGetFile(const char *host, u16 port, const char *path, FILE *f, u32 max, u32 *crc)
{
	FileSink s = { f, crc32(0L, Z_NULL, 0) };
	int n = Fetch(host, port, path, max, ToFile, &s);
	*crc = s.crc;
	return n;
}

const char *HttpErrorString(int err)
{
	switch(err)
	{
		case HTTP_ERR_RESOLVE: return "can't find the station's address";
		case HTTP_ERR_CONNECT: return "can't connect to the station";
		case HTTP_ERR_SEND: return "request failed";
		case HTTP_ERR_HEADER: return "bad reply from the station";
		case HTTP_ERR_STATUS: return "the station refused the request";
		case HTTP_ERR_TOOBIG: return "broadcast too big";
		case HTTP_ERR_RECEIVE: return "download interrupted";
		case HTTP_ERR_WRITE: return "can't write to the SD card";
	}
	return "unknown error";
}
