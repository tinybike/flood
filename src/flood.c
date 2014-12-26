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

#ifdef __cplusplus
extern "C" {
#endif

#include "flood.h"

// static const unsigned node[] = { 0x45a4c4ef };
static const char *node[] = { "69.164.196.239" };

void die(const char *message)
{
    if (errno) {
        perror(message);
    } else {
        printf("ERROR: %s\n", message);
    }
    exit(1);
}

static size_t curl_memwrite(void *data, size_t size, size_t n, void *userp)
{
    char **response =  (char **)userp;
    *response = strndup(data, (size_t)(size * n));
    return (size_t)(size * n);
}

char *get_external_ip(void)
{
    CURL *curl_handle;
    CURLcode res;
    char *external_ip = NULL;

    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();

    // ipecho.net/plain or ipinfo.io/ip (also IPv6)
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
    char *local_ip = malloc(NI_MAXHOST); // 1025

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
    if ((substring = malloc(sizeof(*substring)*(len+1))) == NULL)
        return NULL;
    len += pos;
    for (i = 0; pos != len; i++, pos++)
        substring[i] = string[pos];
    substring[i] = '\0';
 
    return substring;
}

void runserver(void)
{
    int sockfd, rc, reuse;
    char buf[BUFLEN + 1];
    char *external_ip, *local_ip, *read, *btih, *hash, *err = NULL;
    size_t read_len;
    struct sockaddr_in servaddr, cliaddr, ip;
    socklen_t slen = sizeof servaddr;
    leveldb_t *db;
    leveldb_options_t *options;
    leveldb_readoptions_t *roptions;
    leveldb_writeoptions_t *woptions;

    // zero and set server socket struct fields
    bzero(&servaddr, slen);
    bzero(&cliaddr, slen);
    bzero(&ip, sizeof(ip));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    external_ip = get_external_ip();
    local_ip = get_local_ip();

    // convert external IP to network IP
    rc = inet_pton(AF_INET, external_ip, &ip.sin_addr.s_addr);
    if (rc <= 0) die("Cannot convert network IP");

    debug("Seed:        %s\n", node[0]);
    debug("External IP: %s (%x)\n", external_ip, ntohl(ip.sin_addr.s_addr));
    debug("Local IP:    %s\n", local_ip);

    // create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) die("Unable to create socket");

    // set socket to reusable
    reuse = 1;
    rc = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    if (rc < 0) die("Cannot set socket to reuse");

    // bind socket to address
    rc = bind(sockfd, (struct sockaddr *)&servaddr, slen);
    if (rc < 0) die("Failed to bind socket");

    // create the db if it doesn't exist already
    options = leveldb_options_create();
    roptions = leveldb_readoptions_create();
    woptions = leveldb_writeoptions_create();
    leveldb_options_set_create_if_missing(options, 1);
    
    loop {
        if (recvfrom(sockfd, buf, BUFLEN, 0, (struct sockaddr *)&cliaddr, &slen) == -1)
            die("recvfrom failed");
        
        debug("Receive packet from %s:%d\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));
        // debug("Data: %s\n\n", buf);

        // parse the magnet link and extract parameters
        if (!(btih = strstr(buf, "btih:"))) {
            debug(" - Skip: infohash not found\n");
            continue;
        }
        hash = _substr(btih, 5, HASHLEN - 1);

        // check if the hash exists already
        debug(" - BT infohash: %s\n", hash);
        db = leveldb_open(options, DB, &err);
        if (err != NULL) {
            leveldb_free(err);
            die("Failed to open database");
        }
        read = leveldb_get(db, roptions, hash, HASHLEN, &read_len, &err);
        if (err != NULL) {
            leveldb_free(err);
            leveldb_close(db);
            die("Database read failed");
        }

        // write the hash to the database, unless the hash is already in the
        // database, and the stored link is identical to the new link
        if (read && !strncmp(buf, read, BUFLEN)) {
            debug(" - Skip: link already in database\n");
        } else {
            debug(" - Save link to database\n");
            leveldb_put(db, woptions, hash, HASHLEN, buf, BUFLEN, &err);
            if (err != NULL) {
                leveldb_free(err);
                leveldb_close(db);
                die("Database write failed");
            }
        }

        leveldb_close(db);
    }

    check(close(sockfd) != -1);

    free(external_ip);
    free(local_ip);
    exit(0);

error:
    free(external_ip);
    free(local_ip);
    exit(1);
}

void ping(const char *input_ip)
{
    int sockfd, rc, remain = BUFLEN;
    char buf[BUFLEN], *bufptr;
    struct sockaddr_in servaddr;
    const socklen_t slen = sizeof servaddr;

    // example magnet link for testing
    const char *magnet = "magnet:?xt=urn:btih:a89be40ce171a21442003090b5de9d177a474951&dn=Death+by+Food+Pyramid+-+How+Shoddy+Science%2C+Sketchy+Politics+And+Shady+Special+Interests+Have+Ruined+Our+Health+%28epub%2Cmobi%2Cazw3%29+Gooner&xl=15844183&dl=15844183&tr=udp://tracker.openbittorrent.com:80/announce&tr=udp://tracker.istole.it:80/announce&tr=udp://tracker.publicbt.com:80/announce&tr=udp://12.rarbg.me:80/announce";

    // zero and populate sockaddr_in fields
    bzero(&servaddr, slen);
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    
    // convert input_ip to network address
    // rc = inet_pton(AF_INET, node[0], &servaddr.sin_addr);
    rc = inet_pton(AF_INET, input_ip, &servaddr.sin_addr);
    if (rc <= 0) die("Cannot convert network IP");

    // create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) die("Unable to create socket");

    // send magnet link
    strlcpy(buf, magnet, BUFLEN);
    bufptr = (char *)&buf;
    while (remain > 0) {
        rc = sendto(sockfd, bufptr, remain, 0, (struct sockaddr *)&servaddr, slen);
        if (rc == -1) die("Failed to send link");
        remain -= rc;
        bufptr += rc;
    }

    if (close(sockfd) == -1) die("Unable to close socket");
}

int main(int argc, char *argv[])
{
    switch (argc) {
        case 1:
            runserver();
            break;
        case 2:
            ping(argv[1]);
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

#ifdef __cplusplus
}
#endif
