    /* broadcast links */
    debug(" - Link broadcast\n");
    struct sockaddr_in their_addr;
    struct hostent *he;
    int numbytes;
    int broadcast = 1;
    
    if ( (he = gethostbyname(ip)) == NULL) die("[share] gethostbyname");
    if ( (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof broadcast) == -1))
        die("[share] setsockopt(SO_BROADCAST)");
    
    their_addr.sin_family = AF_INET;
    their_addr.sin_port = htons(PORT);
    their_addr.sin_addr = *((struct in_addr *)he->h_addr);
    
    memset(their_addr.sin_zero, '\0', sizeof their_addr.sin_zero);

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
            numbytes = sendto(sockfd, bufptr, remain, 0, (struct sockaddr *)&their_addr, sizeof their_addr);
            if (numbytes == -1) die("[share] sendto");
            debug(" - Sent %s to %s [%d bytes]\n", hash, inet_ntoa(their_addr.sin_addr), numbytes);
            remain -= numbytes;
            bufptr += numbytes;
        }

        leveldb_iter_next(iter);
    }
