/**
 * Copyright (c) 2014 Jack Peterson <jack@tinybike.net>
 *
 * This file is part of Flood.
 *
 * Flood is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * Flood is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Flood. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __FLOOD_H_INCLUDED__
#define __FLOOD_H_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__STDC__)
# define C89
# if defined(__STDC_VERSION__)
#  define C90
#  if (__STDC_VERSION__ >= 199409L)
#   define C94
#  endif
#  if (__STDC_VERSION__ >= 199901L)
#   define C99
#  endif
# endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <fcntl.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <curl/curl.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>

#ifdef __WIN32
#include <winsock.h>
#endif

#include <leveldb/c.h>
/*#include <libtorrent.h>
#include <Cello.h>*/

#define HASHLEN 41
#define BUFLEN 4096
#define MAXTR 100
#define PORT 9876
#define DB "links"

#ifdef EPROTO
#define RETRY 0
#else
#define RETRY 1
#endif

#ifdef NDEBUG
#define debug(format, args...) ((void)0)
#else
#define debug printf
#endif

#ifndef C99
#define __FUNC__ 0
#else
#define __FUNC__ 1
#endif

#define loop for (;;)

#ifndef HAVE_STRLCAT
size_t strlcat(char *dst, const char *src, size_t size)
{
    size_t srclen, dstlen;

    dstlen = strlen(dst);
    size -= dstlen + 1;

    if (!size) return dstlen;

    srclen = strlen(src);
    if (srclen > size) srclen = size;
    
    memcpy(dst + dstlen, src, srclen);
    dst[dstlen + srclen] = '\0';

    return (dstlen + srclen);
}
#endif

#ifndef HAVE_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t srclen;

    size--;
    srclen = strlen(src);

    if (srclen > size) srclen = size;

    memcpy(dst, src, srclen);
    dst[srclen] = '\0';

    return srclen;
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* __FLOOD_H_INCLUDED__ */
