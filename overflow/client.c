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

#include "flood.h"

int main(int argc, char *argv[])
{
    int sock, n;
    char recvline[MAXLINE + 1];
    char *input_ip;
    struct sockaddr_in servaddr;

    if (argc == 1) {
        input_ip = "127.0.0.1";
    } else if (argc > 2) {
        puts("usage: client [IP]");
        sentinel();
    } else {
        input_ip = argv[1];
    }

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(9876);
    
    check(inet_pton(AF_INET, input_ip, &servaddr.sin_addr) > 0);

    check( (sock = socket(AF_INET, SOCK_STREAM, 0)) >= 0);

    check(connect(sock, (struct sockaddr *)&servaddr, sizeof(servaddr)) >= 0);

    while ( (n = read(sock, recvline, MAXLINE)) > 0) {
        recvline[n] = 0;
        check(fputs(recvline, stdout) != EOF);
    }
    check(n >= 0);

    return 0;

error:
    return 1;
}
