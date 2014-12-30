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

#include "flood.h"

static const char *seeds[] = { "69.164.196.239" };

struct node {
    const char *ip;
    struct node *next;
};

struct params {
    int num_tr;
    int xl;
    int dl;
    char *hash;
    char *dn;
    char *tr[MAXTR];
};

void die(const char *message)
{
    if (errno) {
        perror(message);
    } else {
        printf("ERROR: %s\n", message);
    }
    exit(1);
}

static size_t curl_memwrite(void *buf, size_t size, size_t n, void *userp)
{
    char **response =  (char **)userp;
    *response = strndup(buf, (size_t)(size * n));
    return (size_t)(size * n);
}

char *get_external_ip(void)
{
    CURL *curl_handle;
    CURLcode res;
    char *external_ip = NULL;

    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();

    /* ipecho.net/plain or ipinfo.io/ip (also IPv6) */
    curl_easy_setopt(curl_handle, CURLOPT_URL, "ipecho.net/plain");
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, curl_memwrite);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &external_ip);

    if ( (res = curl_easy_perform(curl_handle)) != CURLE_OK)
        debug("curl failed: %s\n", curl_easy_strerror(res));    

    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();

    return external_ip;
}

char *get_local_ip(void)
{
    struct ifaddrs *ifaddr, *ifa;
    int s;
    char *local_ip = malloc(NI_MAXHOST);

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(1);
    }
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL) continue;
        s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                                       local_ip,
                                       NI_MAXHOST,
                                       NULL,
                                       0,
                                       NI_NUMERICHOST);
        if ((strcmp(ifa->ifa_name, "wlan0") == 0) &&
            (ifa->ifa_addr->sa_family == AF_INET)) {
            if (s != 0) {
                debug("getnameinfo() failed: %s\n", gai_strerror(s));
                exit(1);
            }
        }
    }

    freeifaddrs(ifaddr);
    return local_ip;
}

char *_substr(const char *string, int pos, int len)
{
    int i, length;
    char *substring;
 
    if (string == NULL) return NULL;
    length = strlen(string);
    if (pos < 0) {
        pos = length + pos;
        if (pos < 0) pos = 0;
    }
    else if (pos > length) {
        pos = length;
    }
    if (len <= 0) {
        len = length - pos + len;
        if (len < 0) len = length - pos;
    }
    if (pos + len > length) len = length - pos;
    if ( (substring = malloc(sizeof(*substring)*(len+1))) == NULL)
        return NULL;
    len += pos;
    for (i = 0; pos != len; i++, pos++)
        substring[i] = string[pos];
    substring[i] = '\0';
 
    return substring;
}

void runserver(void)
{
    debug("Start server...\n");

    int sockfd, rc, remain, reuse, len;
    char buf[BUFLEN + 1];
    char *external_ip, *local_ip, *walk, *next, *read, *err, *bufptr;
    char *xl, *dl;
    const char *hash, *link;
    size_t readlen;
    size_t hashlen = HASHLEN;
    struct params magnet;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t slen = sizeof servaddr;
    leveldb_t *db;
    leveldb_options_t *options;
    leveldb_readoptions_t *roptions;
    leveldb_writeoptions_t *woptions;

    /* zero and set server socket struct fields */
    bzero(&servaddr, slen);
    bzero(&cliaddr, slen);
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    external_ip = get_external_ip();
    local_ip = get_local_ip();

    debug(" - External IP: %s\n", external_ip);
    debug(" - Local IP:    %s\n", local_ip);

    /* create UDP socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) die("[runserver] Unable to create socket");

    /* set socket to reusable */
    reuse = 1;
    rc = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    if (rc < 0) die("[runserver] Cannot set socket to reuse");

    /* bind socket to address */
    rc = bind(sockfd, (struct sockaddr *)&servaddr, slen);
    if (rc < 0) die("[runserver] Failed to bind socket");

    /* create the db if it doesn't exist already */
    err = NULL;
    options = leveldb_options_create();
    roptions = leveldb_readoptions_create();
    woptions = leveldb_writeoptions_create();
    leveldb_options_set_create_if_missing(options, 1);

    loop
    {
        /* wait for incoming socket data */
        rc = recvfrom(sockfd, buf, BUFLEN, 0, (struct sockaddr *)&cliaddr, &slen);
        if (rc == -1) die("[runserver] recvfrom failed");
        
        debug("Receive packet from %s:%d\n", inet_ntoa(cliaddr.sin_addr),
                                             ntohs(cliaddr.sin_port));

        /* if this is a link request, fetch all links from
           database and send to requestor */
        if (!strcmp(buf, "r")) {
            debug(" - Link request\n");
            leveldb_iterator_t* iter;

            /* open leveldb */
            db = leveldb_open(options, DB, &err);
            if (err != NULL) die("[runserver] Could not open LevelDB");
            leveldb_free(err);
            err = NULL;

            /* leveldb iterator */
            iter = leveldb_create_iterator(db, roptions);

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
                    if (rc == -1) die("[runserver] Failed to send link");
                    remain -= rc;
                    bufptr += rc;
                }

                leveldb_iter_next(iter);
            }

            /* send "transmission complete" signal to requestor */
            rc = sendto(sockfd, "c", 2, 0, (struct sockaddr *)&servaddr, slen);
            if (rc == -1) die("[runserver] transmission complete sendto failed");
            debug(" - Transmission complete\n");

            leveldb_iter_destroy(iter);
            leveldb_close(db);

            continue;
        }

        /* parse the magnet link and get infohash */
        if (!(walk = strstr(buf, "btih:"))) {
            debug(" - Skip: infohash not found\n");
            continue;
        }
        magnet.hash = _substr(walk, 5, HASHLEN - 1);
        debug(" - BT infohash: %s\n", magnet.hash);
        walk += HASHLEN + 5;

        /* extract the remaining link parameters */
        len = (int)strlen(buf);
        magnet.num_tr = 0;
        while ( (walk = strstr(walk, "&")))
        {
            walk++;
            next = strstr(walk, "&");
            len = (next) ? next - walk : (int)strlen(buf) - len;
            if (walk[0] == 'd' && walk[1] == 'n') {
                magnet.dn = _substr(walk, 3, len);
                debug(" - dn: %s\n", magnet.dn);
            } else if (walk[0] == 'x' && walk[1] == 'l') {
                xl = _substr(walk, 3, len);
                magnet.xl = atoi(xl);
                debug(" - xl: %d\n", magnet.xl);
            } else if (walk[0] == 'd' && walk[1] == 'l') {
                dl = _substr(walk, 3, len);
                magnet.dl = atoi(dl);
                debug(" - dl: %d\n", magnet.dl);
            } else if (walk[0] == 't' && walk[1] == 'r') {
                magnet.tr[magnet.num_tr++] = _substr(walk, 3, len);
                debug(" - tr[%d]: %s\n", magnet.num_tr - 1,
                                         magnet.tr[magnet.num_tr - 1]);
            }
        }

        /* check if the hash exists already */
        db = leveldb_open(options, DB, &err);
        if (err != NULL) {
            leveldb_free(err);
            die("[runserver] Failed to open database");
        }
        read = leveldb_get(db, roptions, magnet.hash, HASHLEN, &readlen, &err);
        if (err != NULL) {
            leveldb_free(err);
            leveldb_close(db);
            die("[runserver] Database read failed");
        }

        /* write the hash to the database, unless the hash is already in the
           database, and the stored link is identical to the new link */
        if (read && !strncmp(buf, read, BUFLEN)) {
            debug(" - Skip: link already in database\n");
        } else {
            debug(" - Save link to database\n");
            leveldb_put(db, woptions, magnet.hash, HASHLEN, buf, BUFLEN, &err);
            if (err != NULL) {
                leveldb_free(err);
                leveldb_close(db);
                die("[runserver] Database write failed");
            }
        }

        leveldb_close(db);
    }

    if (close(sockfd) == -1) exit(1);

    free(external_ip);
    free(local_ip);
    exit(0);
}

void share(const char *ip)
{
    int sockfd, rc, remain, reuse, len;
    char buf[BUFLEN], *bufptr, *err, *walk, *next, *read, *xl, *dl;
    const char *hash, *link;
    size_t readlen;
    size_t hashlen = HASHLEN;
    struct params magnet;
    struct node *root;
    struct node *peer;
    struct sockaddr_in servaddr, xtrnaddr, recvaddr;
    socklen_t slen = sizeof servaddr;
    leveldb_t *db;
    leveldb_options_t *options;
    leveldb_readoptions_t *roptions;
    leveldb_writeoptions_t *woptions;
    leveldb_iterator_t* iter;

    /* zero and populate sockaddr_in fields */
    bzero(&servaddr, slen);
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    bzero(&xtrnaddr, slen);
    xtrnaddr.sin_family = AF_INET;
    xtrnaddr.sin_port = htons(PORT);

    bzero(&recvaddr, slen);

    /* convert input ip to network address */
    rc = inet_pton(AF_INET, ip, &xtrnaddr.sin_addr);
    if (rc <= 0) die("[share] Cannot convert network IP");

    /* create UDP socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) die("[share] Unable to create socket");

    /* set socket to reusable */
    reuse = 1;
    rc = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    if (rc < 0) die("[share] Cannot set socket to reuse");

    /* bind socket to address */
    // rc = bind(sockfd, (struct sockaddr *)&servaddr, slen);
    // if (rc < 0) die("[share] Failed to bind socket");

    /* open leveldb */
    err = NULL;
    options = leveldb_options_create();
    roptions = leveldb_readoptions_create();
    leveldb_options_set_create_if_missing(options, 1);
    db = leveldb_open(options, DB, &err);
    if (err != NULL) die("[share] Could not open LevelDB");
    leveldb_free(err);
    err = NULL;

    /* leveldb iterator */
    iter = leveldb_create_iterator(db, roptions);

    /* send links */
    debug("Share links:\n");
    leveldb_iter_seek_to_first(iter);
    while (leveldb_iter_valid(iter))
    {
        remain = BUFLEN;
        hash = leveldb_iter_key(iter, &hashlen);
        link = leveldb_iter_value(iter, &readlen);

        /* send magnet link */
        strlcpy(buf, link, BUFLEN);
        bufptr = (char *)&buf;
        while (remain > 0) {
            rc = sendto(sockfd, bufptr, remain, 0, (struct sockaddr *)&xtrnaddr, sizeof xtrnaddr);
            if (rc == -1) die("[share] sendto");
            debug(" - Sent %s to %s [%d bytes]\n", hash, inet_ntoa(xtrnaddr.sin_addr), rc);
            remain -= rc;
            bufptr += rc;
        }

        leveldb_iter_next(iter);
    }

    /* request links from node */
    debug("Request links:\n");
    rc = sendto(sockfd, "r", 2, 0, (struct sockaddr *)&servaddr, slen);
    if (rc == -1) die("[share] Link request failed");

    /* wait for incoming socket data, block until "transmission complete"
       signal received from node (TODO use TCP for this?) */
    loop
    {
        /* wait for incoming socket data */
        rc = recvfrom(sockfd, buf, BUFLEN, 0, (struct sockaddr *)&recvaddr, &slen);
        if (rc == -1) die("[share] recvfrom failed");

        debug(" - Receive packet from %s:%d\n", inet_ntoa(recvaddr.sin_addr),
                                                ntohs(recvaddr.sin_port));

        /* stop expecting links when transmission complete packet received */
        if (!strcmp(buf, "c")) {
            debug(" - Transmission complete\n");
            break;
        }

        /* parse the magnet link and get infohash */
        if (!(walk = strstr(buf, "btih:"))) {
            debug(" - Skip: infohash not found\n");
            continue;
        }
        magnet.hash = _substr(walk, 5, HASHLEN - 1);
        debug(" - BT infohash: %s\n", magnet.hash);
        walk += HASHLEN + 5;

        /* extract the remaining link parameters */
        len = (int)strlen(buf);
        magnet.num_tr = 0;
        while ( (walk = strstr(walk, "&")))
        {
            walk++;
            next = strstr(walk, "&");
            len = (next) ? next - walk : (int)strlen(buf) - len;
            if (walk[0] == 'd' && walk[1] == 'n') {
                magnet.dn = _substr(walk, 3, len);
                debug(" - dn: %s\n", magnet.dn);
            } else if (walk[0] == 'x' && walk[1] == 'l') {
                xl = _substr(walk, 3, len);
                magnet.xl = atoi(xl);
                debug(" - xl: %d\n", magnet.xl);
            } else if (walk[0] == 'd' && walk[1] == 'l') {
                dl = _substr(walk, 3, len);
                magnet.dl = atoi(dl);
                debug(" - dl: %d\n", magnet.dl);
            } else if (walk[0] == 't' && walk[1] == 'r') {
                magnet.tr[magnet.num_tr++] = _substr(walk, 3, len);
                debug(" - tr[%d]: %s\n", magnet.num_tr - 1,
                                         magnet.tr[magnet.num_tr - 1]);
            }
        }

        /* check if the hash exists already */
        db = leveldb_open(options, DB, &err);
        if (err != NULL) {
            leveldb_free(err);
            debug("[share] Failed to open database\n");
        }
        read = leveldb_get(db, roptions, magnet.hash, HASHLEN, &readlen, &err);
        if (err != NULL) {
            leveldb_free(err);
            leveldb_close(db);
            debug("[share] Database read failed\n");
        }

        /* write the hash to the database, unless the hash is already in the
           database, and the stored link is identical to the new link */
        if (read && !strncmp(buf, read, BUFLEN)) {
            debug(" - Skip: link already in database\n");
        } else {
            debug(" - Save link to database\n");
            leveldb_put(db, woptions, magnet.hash, HASHLEN, buf, BUFLEN, &err);
            if (err != NULL) {
                leveldb_free(err);
                leveldb_close(db);
                debug("[share] Database write failed\n");
            }
        }
    }

    /* receive peers from node */
    root = malloc(sizeof(struct node));
    root->next = 0;
    root->ip = "127.0.0.1";
    peer = root;
    if (peer != 0) {
        while (peer->next != 0)
        {
            peer = peer->next;
        }
    }
    free(peer);

    leveldb_iter_destroy(iter);
    leveldb_close(db);

    if (close(sockfd) == -1) exit(1);
}

void synchronize(void)
{
    debug("Sync with network...\n");

    int num_seeds;
    num_seeds = 1;

    int i;
    for (i = 0; i < num_seeds; i++)
    {
        debug("Seed: %s\n", seeds[0]);
        const char *external_ip;

        external_ip = get_external_ip();
        
        if (strncmp(external_ip, seeds[0], strlen(seeds[0]))) {
            share(seeds[0]);
        }
    }
}

int main(int argc, char *argv[])
{
    switch (argc) {
        case 1:
            /* normal mode: sync with network then broadcast */
            synchronize();
            runserver();
            break;
        case 2:
            /* share links with a specific node (IP address) */
            share(argv[1]);
            break;
        default: {
            /* manual database I/O */
            leveldb_t *db;
            leveldb_options_t *options;
            leveldb_readoptions_t *roptions;
            leveldb_writeoptions_t *woptions;
            char *err = NULL;
            char *read;
            size_t readlen;

            options = leveldb_options_create();
            leveldb_options_set_create_if_missing(options, 1);

            db = leveldb_open(options, DB, &err);
            if (err != NULL) die("Could not open LevelDB");
            leveldb_free(err);
            err = NULL;

            switch (argv[2][0]) {
                case 'g': {
                    roptions = leveldb_readoptions_create();

                    read = leveldb_get(db, roptions, argv[3], HASHLEN, &readlen, &err);
                    if (err != NULL) die("LevelDB read failed");
                    printf("%s\n", read);

                    leveldb_free(err);
                    err = NULL;
                    break;
                }
                case 's': {
                    woptions = leveldb_writeoptions_create();

                    leveldb_put(db, woptions, argv[3], strlen(argv[3]), argv[4], strlen(argv[4]), &err);
                    if (err != NULL) die("LevelDB write failed");

                    leveldb_free(err);
                    err = NULL;
                    break;
                }
                case 'd': {
                    woptions = leveldb_writeoptions_create();

                    leveldb_delete(db, woptions, argv[3], HASHLEN, &err);
                    if (err != NULL) die("Delete from LevelDB failed");
                    
                    leveldb_free(err);
                    err = NULL;
                    break;
                }
                default:
                    die("Invalid action, only: g=get, s=set, d=delete");
            }
            leveldb_close(db);
        }
    }
    return 0;
}
