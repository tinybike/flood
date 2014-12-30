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

#define DB "flood.dat"
#define MAX_DATA 10000
#define MAX_ROWS 500

struct Torrent {
    char *type;
    int files;
    char *size;
    char *hash;
    int seeders;
    int leechers;
    char *magnet;
};

struct Link {
    int id;
    int set;
    char name[MAX_DATA];
};

struct LinkDB {
    struct Link rows[MAX_ROWS];
};

struct Connection {
    FILE *file;
    struct LinkDB *db;
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

static size_t write_memory(void* data, size_t size, size_t n, void* userp)
{
    char **response =  (char **)userp;
    *response = strndup(data, (size_t)(size *n));
    return (size_t)(size *n);
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

void printlink(struct Link *addr)
{
    printf("%d: %s\n", addr->id, addr->name);
}

void LinkDB_load(struct Connection *conn)
{
    int rc = fread(conn->db, sizeof(struct LinkDB), 1, conn->file);
    if (rc != 1) die("Failed to load database.");
}

struct Connection *LinkDB_open(const char *filename, char mode)
{
    struct Connection *conn = malloc(sizeof(struct Connection));
    if (!conn) die("Memory error");

    conn->db = malloc(sizeof(struct LinkDB));
    if (!conn->db) die("Memory error");

    if (mode == 'c') {
        conn->file = fopen(filename, "w");
    } else {
        conn->file = fopen(filename, "r+");
        if (conn->file) {
            LinkDB_load(conn);
        }
    }
    if (!conn->file) die("Failed to open the file");

    return conn;
}

void LinkDB_close(struct Connection *conn)
{
    if (conn) {
        if (conn->file) fclose(conn->file);
        if (conn->db) free(conn->db);
        free(conn);
    }
}

void LinkDB_write(struct Connection *conn)
{
    rewind(conn->file);

    int rc = fwrite(conn->db, sizeof(struct LinkDB), 1, conn->file);
    if (rc != 1) die("Failed to write database.");

    rc = fflush(conn->file);
    if (rc == -1) die("Cannot flush database.");
}

void LinkDB_create(struct Connection *conn)
{
    int i = 0;
    for (i = 0; i < MAX_ROWS; i++) {
        // make a prototype to initialize it
        struct Link addr = {.id = i, .set = 0};
        conn->db->rows[i] = addr;
    }
}

void LinkDB_set(struct Connection *conn, int id, const char *name)
{
    struct Link *addr = &conn->db->rows[id];
    
    if (addr->set) die("Already set, delete it first");
    addr->set = 1;

    size_t rc = strlcpy(addr->name, name, MAX_DATA);
    if (!rc) die("Name copy failed");
}

void LinkDB_get(struct Connection *conn, int id)
{
    struct Link *addr = &conn->db->rows[id];
    if (addr->set) {
        printlink(addr);
    } else {
        die("ID is not set");
    }
}

void LinkDB_delete(struct Connection *conn, int id)
{
    struct Link addr = {.id = id, .set = 0};
    conn->db->rows[id] = addr;
}

void LinkDB_list(struct Connection *conn)
{
    int i = 0;
    struct LinkDB *db = conn->db;

    for(i = 0; i < MAX_ROWS; i++) {
        struct Link *cur = &db->rows[i];
        if (cur->set) {
            printlink(cur);
        }
    }
}

static void _strrev(char* begin, char* end)
{
    char aux;
    while (end > begin) {
        aux = *end, *end-- = *begin, *begin++ = aux;
    }
}

static void _itoa(int value, char* str, int base)
{
    static char num[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char* wstr = str;
    int sign;
    div_t res;

    if (base < 2 || base > 35) {
        *wstr = '\0';
        return;
    }
    if ( (sign = value) < 0) {
        value = -value;
    }

    do {
        res = div(value, base);
        *wstr++ = num[res.rem];
    } while( (value = res.quot));
    
    if (sign < 0) {
        *wstr++ = '-';
    }
    *wstr = '\0';
    
    _strrev(str, wstr - 1);    
}

static size_t intsize(int x)
{
    size_t size = (x == 0 ? 1 : (int)(log10(x) + 1));
    return size;
}

static char *jsonify(const struct Torrent *torr)
{
    size_t fsz = intsize(torr->files);
    size_t ssz = intsize(torr->seeders);
    size_t lsz = intsize(torr->leechers);
    char files[fsz];
    char seeders[ssz];
    char leechers[lsz];
    size_t tsz = fsz + ssz + lsz + strlen(torr->type) + strlen(torr->size) + strlen(torr->hash) + strlen(torr->magnet) + 112; // 112 bytes for json format
    char *torrjson = malloc(tsz);
    
    _itoa(torr->files, files, 10);
    _itoa(torr->seeders, seeders, 10);
    _itoa(torr->leechers, leechers, 10);

    strlcpy(torrjson, "{\n   \"type\": \"", tsz);
    strlcat(torrjson, torr->type, tsz);
    strlcat(torrjson, "\",", tsz);
    strlcat(torrjson, "\n   \"files\": ", tsz);
    strlcat(torrjson, files, tsz);
    strlcat(torrjson, ",", tsz);
    strlcat(torrjson, "\n   \"size\": \"", tsz);
    strlcat(torrjson, torr->size, tsz);
    strlcat(torrjson, "\",", tsz);
    strlcat(torrjson, "\n   \"hash\": \"", tsz);
    strlcat(torrjson, torr->hash, tsz);
    strlcat(torrjson, "\",", tsz);
    strlcat(torrjson, "\n   \"seeders\": ", tsz);
    strlcat(torrjson, seeders, tsz);
    strlcat(torrjson, ",", tsz);
    strlcat(torrjson, "\n   \"leechers\": ", tsz);
    strlcat(torrjson, leechers, tsz);
    strlcat(torrjson, ",", tsz);
    strlcat(torrjson, "\n   \"magnet\": \"", tsz);
    strlcat(torrjson, torr->magnet, tsz);
    strlcat(torrjson, "\"", tsz);
    strlcat(torrjson, "\n}", tsz);

    return torrjson;
}

void runserver(void)
{
    int sockfd, n, client, backlog, rc, reuse;
    char recvline[MAXLINE + 1];
    char *external_ip, *local_ip, *lq, *torrjson;
    // unsigned nodes = { 0x45a4c4ef }; // 69.164.196.239
    struct Connection *conn;
    struct sockaddr_in ip;
    struct sockaddr_in servaddr;
    struct sockaddr_in cliaddr;
    struct Torrent torr = {
        .type = "Books",
        .files = 3,
        .size = "15.11",
        .hash = "a89be40ce171a21442003090b5de9d177a474951",
        .seeders = 113,
        .leechers = 0,
        .magnet = "magnet:?xt=urn:btih:a89be40ce171a21442003090b5de9d177a474951&dn=Death+by+Food+Pyramid+-+How+Shoddy+Science%2C+Sketchy+Politics+And+Shady+Special+Interests+Have+Ruined+Our+Health+%28epub%2Cmobi%2Cazw3%29+Gooner&xl=15844183&dl=15844183&tr=udp://tracker.openbittorrent.com:80/announce&tr=udp://tracker.istole.it:80/announce&tr=udp://tracker.publicbt.com:80/announce&tr=udp://12.rarbg.me:80/announce"
    };

    torrjson = jsonify(&torr);

    // zero and set server socket struct fields
    bzero(&servaddr, sizeof(servaddr));
    bzero(&cliaddr, sizeof(cliaddr));
    bzero(&ip, sizeof(ip));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(9876);

    external_ip = get_external_ip();
    local_ip = get_local_ip();

    // convert external IP to network IP
    rc = inet_pton(AF_INET, external_ip, &ip.sin_addr.s_addr);
    if (rc <= 0) die("Cannot convert network IP");

    debug("External IP: %s (%x)\n", external_ip, ntohl(ip.sin_addr.s_addr));
    debug("Local IP:    %s\n", local_ip);

    // create TCP socket
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

    // set socket timeouts
    // struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
    // rc = setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    // if (rc < 0) die("Cannot set recv timeout");
    // rc = setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
    // if (rc < 0) die("Cannot set send timeout");

    // create the db if it doesn't exist already
    int dbfd = open(DB, O_CREAT | O_EXCL, 0664);
    if (dbfd >= 0) {
        struct Connection *conn = LinkDB_open(DB, 'c');
        LinkDB_create(conn);
        LinkDB_write(conn);
        LinkDB_close(conn);
        debug("Created %s\n", DB);
    } else {
        debug("Found %s\n", DB);
    }
    
    conn = LinkDB_open(DB, 's');

    loop {
        do {
            socklen_t addrsz = sizeof(cliaddr);
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

        while ( (n = recv(client, recvline, MAXLINE, 0)) > 0) {
            recvline[n] = 0;
            // check(fputs(recvline, stdout) != EOF);
        }
        check(n >= 0);
        // fflush(stdout);

        // write to file db
        int i = 0;
        struct LinkDB *db = conn->db;
        for (i = 0; i < MAX_ROWS; i++) {
            struct Link *cur = &db->rows[i];
            if (cur->set) {
                if (!strncmp(recvline, cur->name, strlen(cur->name))) {
                    debug("Ignore duplicate entry\n");
                    break;
                }
            } else {
                debug("Writing to row %d\n", i);
                LinkDB_set(conn, i, recvline);
                LinkDB_write(conn);
                break;
            }
        }

        check(close(client) != -1);
    }
    
    LinkDB_close(conn);

    free(external_ip);
    free(local_ip);
    free(torrjson);
    if (conn) free(conn);
    exit(0);

error:
    free(external_ip);
    free(local_ip);
    free(torrjson);
    if (conn) free(conn);
    exit(1);
}

void ping(char *input_ip)
{
    int sockfd, rc, remain;
    struct sockaddr_in servaddr;
    struct Torrent torr = {
        .type = "Books",
        .files = 3,
        .size = "15.11",
        .hash = "a89be40ce171a21442003090b5de9d177a474951",
        .seeders = 113,
        .leechers = 0,
        .magnet = "magnet:?xt=urn:btih:a89be40ce171a21442003090b5de9d177a474951&dn=Death+by+Food+Pyramid+-+How+Shoddy+Science%2C+Sketchy+Politics+And+Shady+Special+Interests+Have+Ruined+Our+Health+%28epub%2Cmobi%2Cazw3%29+Gooner&xl=15844183&dl=15844183&tr=udp://tracker.openbittorrent.com:80/announce&tr=udp://tracker.istole.it:80/announce&tr=udp://tracker.publicbt.com:80/announce&tr=udp://12.rarbg.me:80/announce"
    };

    // zero and populate sockaddr_in fields
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(9876);
    
    // convert input_ip to network address
    rc = inet_pton(AF_INET, input_ip, &servaddr.sin_addr);
    if (rc <= 0) die("Cannot convert network IP");

    // create TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) die("Unable to create socket");

    // connect socket to address of input_ip
    connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
    if (rc < 0) die("Socket connection failed");

    // send IP address
    // remain = MAXLINE;
    // while (remain > 0) {
    //     rc = send(sockfd, input_ip, remain, 0);
    //     if (rc == -1) break;
    //     remain -= rc;
    //     input_ip += rc;
    // }

    // send magnet link
    remain = MAXLINE;
    while (remain > 0) {
        rc = send(sockfd, torr.magnet, remain, 0);
        if (rc == -1) break;
        remain -= rc;
        torr.magnet += rc;
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
            int id = 0;
            char *filename = DB;
            char action = argv[2][0];
            if (argc > 3) id = atoi(argv[3]);
            struct Connection *conn = LinkDB_open(filename, action);
            switch (action) {
                case 'c':
                    LinkDB_create(conn);
                    LinkDB_write(conn);
                    break;
                case 'g':
                    LinkDB_get(conn, id);
                    break;
                case 's':
                    LinkDB_set(conn, id, argv[4]);
                    LinkDB_write(conn);
                    break;
                case 'd':
                    LinkDB_delete(conn, id);
                    LinkDB_write(conn);
                    break;
                case 'l':
                    LinkDB_list(conn);
                    break;
                default:
                    die("Invalid action, only: c=create, g=get, s=set, d=del, l=list");
            }
            LinkDB_close(conn);
        }
    }
    return 0;
}

#ifdef __cplusplus
}
#endif
