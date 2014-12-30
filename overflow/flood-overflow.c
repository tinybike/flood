#include "Cello.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

struct sockaddr_in ip;

char *local_ip = malloc(NI_MAXHOST); /* 1025 */

bzero(&ip, sizeof(ip));

/* convert external IP to network IP */
rc = inet_pton(AF_INET, external_ip, &ip.sin_addr.s_addr);
if (rc <= 0) die("Cannot convert network IP");

debug("External IP: %s (%x)\n", external_ip, ntohl(ip.sin_addr.s_addr));

debug("Data: %s\n\n", buf);

/* bind socket to address */
rc = bind(sockfd, (struct sockaddr *)&servaddr, slen);
if (rc < 0) die("[share] Failed to bind socket");

/* example magnet link for testing */
const char *magnet = "magnet:?xt=urn:btih:a89be40ce171a21442003090b5de9d177a474951&dn=Death+by+Food+Pyramid+-+How+Shoddy+Science%2C+Sketchy+Politics+And+Shady+Special+Interests+Have+Ruined+Our+Health+%28epub%2Cmobi%2Cazw3%29+Gooner&xl=15844183&dl=15844183&tr=udp://tracker.openbittorrent.com:80/announce&tr=udp://tracker.istole.it:80/announce&tr=udp://tracker.publicbt.com:80/announce&tr=udp://12.rarbg.me:80/announce";

if ( (he = gethostbyname(ip)) == NULL) die("[share] gethostbyname");
if ( (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof broadcast) == -1))
    die("[share] setsockopt(SO_BROADCAST)");

their_addr.sin_family = AF_INET;
their_addr.sin_port = htons(PORT);
their_addr.sin_addr = *((struct in_addr *)he->h_addr);

/* send links to node */
debug(" - Send links\n");
leveldb_iter_seek_to_first(iter);
while (leveldb_iter_valid(iter))
{
    remain = BUFLEN;
    hash = leveldb_iter_key(iter, &hashlen);
    link = leveldb_iter_value(iter, &readlen);

    debug(" -> %s\n", hash);

    /* send magnet link */
    strlcpy(buf, link, BUFLEN);
    bufptr = (char *)&buf;
    while (remain > 0) {
        rc = sendto(sockfd, bufptr, remain, 0, (struct sockaddr *)&servaddr, slen);
        if (rc == -1) die("[share] Failed to send link");
        debug(" - Sent %s to %s [%d bytes]\n", hash, inet_ntoa(servaddr.sin_addr), rc);
        remain -= rc;
        bufptr += rc;
    }

    leveldb_iter_next(iter);
}


memset(xtrnaddr.sin_zero, '\0', sizeof xtrnaddr.sin_zero);