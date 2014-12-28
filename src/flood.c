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
/* #include "Cello.h" */

static const char *node[] = { "69.164.196.239" };

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
    char *local_ip = malloc(NI_MAXHOST); /* 1025 */

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(1);
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;

        s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), local_ip, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

        if ((strcmp(ifa->ifa_name, "wlan0") == 0) && (ifa->ifa_addr->sa_family == AF_INET)) {
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
    int sockfd, rc, reuse, len;
    char buf[BUFLEN + 1];
    char *external_ip, *local_ip, *walk, *next, *read, *err = NULL;
    char *xl, *dl;
    size_t read_len;
    struct params magnet;
    struct sockaddr_in servaddr, cliaddr, ip;
    socklen_t slen = sizeof servaddr;
    leveldb_t *db;
    leveldb_options_t *options;
    leveldb_readoptions_t *roptions;
    leveldb_writeoptions_t *woptions;

    /* zero and set server socket struct fields */
    bzero(&servaddr, slen);
    bzero(&cliaddr, slen);
    bzero(&ip, sizeof(ip));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    external_ip = get_external_ip();
    local_ip = get_local_ip();

    /* convert external IP to network IP */
    rc = inet_pton(AF_INET, external_ip, &ip.sin_addr.s_addr);
    if (rc <= 0) die("Cannot convert network IP");

    debug("Seed:        %s\n", node[0]);
    debug("External IP: %s (%x)\n", external_ip, ntohl(ip.sin_addr.s_addr));
    debug("Local IP:    %s\n", local_ip);

    /* create UDP socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) die("Unable to create socket");

    /* set socket to reusable */
    reuse = 1;
    rc = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    if (rc < 0) die("Cannot set socket to reuse");

    /* bind socket to address */
    rc = bind(sockfd, (struct sockaddr *)&servaddr, slen);
    if (rc < 0) die("Failed to bind socket");

    /* create the db if it doesn't exist already */
    options = leveldb_options_create();
    roptions = leveldb_readoptions_create();
    woptions = leveldb_writeoptions_create();
    leveldb_options_set_create_if_missing(options, 1);

    loop {
        if (recvfrom(sockfd, buf, BUFLEN, 0, (struct sockaddr *)&cliaddr, &slen) == -1)
            die("recvfrom failed");
        
        debug("Receive packet from %s:%d\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));
        /* debug("Data: %s\n\n", buf); */

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
        while ( (walk = strstr(walk, "&"))) {
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
                debug(" - tr[%d]: %s\n", magnet.num_tr - 1, magnet.tr[magnet.num_tr - 1]);
            }
        }

        /* check if the hash exists already */
        db = leveldb_open(options, DB, &err);
        if (err != NULL) {
            leveldb_free(err);
            die("Failed to open database");
        }
        read = leveldb_get(db, roptions, magnet.hash, HASHLEN, &read_len, &err);
        if (err != NULL) {
            leveldb_free(err);
            leveldb_close(db);
            die("Database read failed");
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
                die("Database write failed");
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
    int sockfd, rc, remain = BUFLEN;
    char buf[BUFLEN], *bufptr;
    char *err = NULL;
    struct sockaddr_in servaddr;
    const socklen_t slen = sizeof servaddr;
    leveldb_readoptions_t *roptions;
    leveldb_iterator_t* iter;

    leveldb_t *db;
    leveldb_options_t *options;
    leveldb_writeoptions_t *woptions;
    char *read;
    size_t read_len;

    options = leveldb_options_create();
    leveldb_options_set_create_if_missing(options, 1);

    db = leveldb_open(options, DB, &err);
    if (err != NULL) die("Could not open LevelDB");
    leveldb_free(err);
    err = NULL;

    roptions = leveldb_readoptions_create();
    iter = leveldb_create_iterator(db, roptions);

    leveldb_iter_seek_to_first(iter);

    size_t hashlen = HASHLEN;
    size_t valuelen = LINKBUF;

    char *hash;
    char *magnet;

    /* zero and populate sockaddr_in fields */
    bzero(&servaddr, slen);
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    
    /* convert ip to network address */
    rc = inet_pton(AF_INET, ip, &servaddr.sin_addr);
    if (rc <= 0) die("Cannot convert network IP");

    /* create UDP socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) die("Unable to create socket");

    do {
        hash = (char *)leveldb_iter_key(iter, &hashlen);
        magnet = (char *)leveldb_iter_value(iter, &valuelen);

        debug("sharing hash: %s\n", hash);

        /* send magnet link */
        strlcpy(buf, magnet, BUFLEN);
        bufptr = (char *)&buf;
        while (remain > 0) {
            rc = sendto(sockfd, bufptr, remain, 0, (struct sockaddr *)&servaddr, slen);
            if (rc == -1) die("Failed to send link");
            remain -= rc;
            bufptr += rc;
        }

        leveldb_iter_next(iter);

    } while (leveldb_iter_valid(iter));

    leveldb_close(db);
    if (close(sockfd) == -1) exit(1);
}

int netsync(void)
{
    debug("Sync with network...\n");
    share(node[0]);
    return 1;
}

int main(int argc, char *argv[])
{
    switch (argc) {
        case 1:
            if (!netsync()) die("sync error");
            runserver();
            break;
        case 2:
            share(argv[1]);
            break;
        default: {
            leveldb_t *db;
            leveldb_options_t *options;
            leveldb_readoptions_t *roptions;
            leveldb_writeoptions_t *woptions;
            char *err = NULL;
            char *read;
            size_t read_len;
            char action = argv[2][0];

            options = leveldb_options_create();
            leveldb_options_set_create_if_missing(options, 1);

            db = leveldb_open(options, DB, &err);
            if (err != NULL) die("Could not open LevelDB");
            leveldb_free(err);
            err = NULL;

            switch (action) {
                case 'g': {
                    roptions = leveldb_readoptions_create();

                    read = leveldb_get(db, roptions, argv[3], HASHLEN, &read_len, &err);
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
