/*
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

#define DB "links"
#define PORT 9876

void die(const char *message)
{
    if (errno) {
        perror(message);
    } else {
        printf("ERROR: %s\n", message);
    }
    exit(1);
}

static size_t write_memory(void* data, size_t size, size_t n, void* userp)
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
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_memory);
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

char *substr(const char *string, int pos, int len)
{
    int i, length;
    char* substring;
 
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
    int sockfd, n, client, backlog, rc, reuse;
    char magnet[MAXLINE + 1];
    char *external_ip, *local_ip, *lq, *read, *btih, *hash, *err = NULL;
    size_t read_len;
    struct sockaddr_in ip, servaddr, cliaddr;
    leveldb_t *db;
    leveldb_options_t *options;
    leveldb_readoptions_t *roptions;
    leveldb_writeoptions_t *woptions;
    // unsigned nodes = { 0x45a4c4ef }; // 69.164.196.239

    // zero and set server socket struct fields
    bzero(&servaddr, sizeof(servaddr));
    bzero(&cliaddr, sizeof(cliaddr));
    bzero(&ip, sizeof(ip));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    external_ip = get_external_ip();
    local_ip = get_local_ip();

    // convert external IP to network IP
    rc = inet_pton(AF_INET, external_ip, &ip.sin_addr.s_addr);
    if (rc <= 0) die("Cannot convert network IP");

    debug("External IP: %s (%x)\n", external_ip, ntohl(ip.sin_addr.s_addr));
    debug("Local IP:    %s\n", local_ip);

    // create UDP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) die("Unable to create socket");

    // set socket to reusable
    reuse = 1;
    rc = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (rc < 0) die("Cannot set socket to reuse");

    // bind socket to address
    rc = bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
    if (rc < 0) die("Failed to bind socket");

    // set socket to listen for connections
    backlog = ( (lq = getenv("LISTENQ")) == NULL) ? LISTENQ : atoi(lq);
    rc = listen(sockfd, backlog);
    if (rc < 0) die("Failed to set socket to listen");

    debug("Listening on port %d\n", ntohs(servaddr.sin_port));

    // create the db if it doesn't exist already
    options = leveldb_options_create();
    leveldb_options_set_create_if_missing(options, 1);
    db = leveldb_open(options, DB, &err);
    if (err != NULL) die("Failed to open database");
    leveldb_free(err);
    err = NULL;
    
    loop {
        do {
            socklen_t addrsz = sizeof(cliaddr);
            debug("Socket accepting connections\n");
            client = accept(sockfd, (struct sockaddr *)&cliaddr, &addrsz);
            if (client < 0) {
                switch (RETRY) {
                    case 0:
                        check(errno == EPROTO || errno == ECONNABORTED);
                        break;
                    default:
                        check(errno == ECONNABORTED);
                }
            }
        } while (client < 0);

        debug("Socket recv data\n");
        while ( (n = recv(client, magnet, MAXLINE, 0)) > 0) {
            magnet[n] = 0;
            // check(fputs(magnet, stdout) != EOF);
        }
        check(n >= 0);
        // fflush(stdout);

        debug("received magnet link: %s\n", &magnet);
        char *ptr = &magnet;
        // debug("received magnet link: %s\n", ptr);
        char *lookfor = "btih:";
        debug("%s\n", lookfor);
        // btih = strstr("btih", *ptr);
        // debug("%s\n", btih);

        // hash = substr(btih, 5, HASHLEN + 5);

        // does this hash exist already?
        // roptions = leveldb_readoptions_create();
        // debug("%s\n", hash);
        // read = leveldb_get(db, roptions, hash, strlen(hash), &read_len, &err);
        // if (err != NULL) die("Database read failed");
        // leveldb_free(err);
        // if (read) {
        //     debug("%s: %s entry exists\n", hash, read);
        //     // todo keep the longer entry
        // } else {            
        //     // write to leveldb
        //     woptions = leveldb_writeoptions_create();
        //     leveldb_put(db, woptions, hash, HASHLEN, magnet, strlen(magnet), &err);
        //     if (err != NULL) die("Database write failed");
        //     leveldb_free(err);
        // }

        check(close(client) != -1);
    }
    
    leveldb_close(db);

    free(external_ip);
    free(local_ip);
    exit(0);

error:
    free(external_ip);
    free(local_ip);
    exit(1);
}

void ping(char *input_ip)
{
    int sockfd, rc, remain;
    struct sockaddr_in servaddr;
    char *magnet = "magnet:?xt=urn:btih:a89be40ce171a21442003090b5de9d177a474951&dn=Death+by+Food+Pyramid+-+How+Shoddy+Science%2C+Sketchy+Politics+And+Shady+Special+Interests+Have+Ruined+Our+Health+%28epub%2Cmobi%2Cazw3%29+Gooner&xl=15844183&dl=15844183&tr=udp://tracker.openbittorrent.com:80/announce&tr=udp://tracker.istole.it:80/announce&tr=udp://tracker.publicbt.com:80/announce&tr=udp://12.rarbg.me:80/announce";

    // zero and populate sockaddr_in fields
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    
    // convert input_ip to network address
    debug("Converting IP\n");
    rc = inet_pton(AF_INET, input_ip, &servaddr.sin_addr);
    if (rc <= 0) die("Cannot convert network IP");

    // create UDP socket
    debug("Create socket\n");
    // sockfd = socket(AF_INET, SOCK_STREAM, 0);
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) die("Unable to create socket");

    // connect socket to address of input_ip
    debug("Socket connect to %s\n", input_ip);
    connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
    if (rc < 0) die("Socket connection failed");

    // send magnet link
    debug("Socket send data: %s\n", magnet);
    remain = MAXLINE;
    while (remain > 0) {
        rc = send(sockfd, magnet, remain, 0);
        if (rc == -1) break;
        remain -= rc;
        magnet += rc;
    }
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
            if (err != NULL) die("Open fail");
            leveldb_free(err);
            err = NULL;

            switch (action) {
                case 'g': {
                    roptions = leveldb_readoptions_create();

                    read = leveldb_get(db, roptions, argv[3], HASHLEN, &read_len, &err);
                    if (err != NULL) die("Read fail");
                    printf("%s\n", read);

                    leveldb_free(err);
                    err = NULL;
                    break;
                }
                case 's': {
                    woptions = leveldb_writeoptions_create();

                    leveldb_put(db, woptions, argv[3], strlen(argv[3]), argv[4], strlen(argv[4]), &err);
                    if (err != NULL) die("Write fail");

                    leveldb_free(err);
                    err = NULL;
                    break;
                }
                case 'd': {
                    woptions = leveldb_writeoptions_create();

                    leveldb_delete(db, woptions, argv[3], HASHLEN, &err);
                    if (err != NULL) die("Delete fail");
                    
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
