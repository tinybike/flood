/*
 * compiled with:
 * g++ -std=c++11 main.c -Wall -lrocksdb -lbz2 -lpthread -lsnappy -lz
 */

#include <rocksdb/c.h>

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    rocksdb_t *db;
    rocksdb_options_t *opts;
    char *err = NULL;

    opts = rocksdb_options_create();

    // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
    rocksdb_options_increase_parallelism(opts, 0);
    rocksdb_options_optimize_level_style_compaction(opts, 0);

    // create the DB if it's not already present
    rocksdb_options_set_create_if_missing(opts, 1);

    // open the db
    db = rocksdb_open(opts, "test.db", &err);
    if (err != NULL) {
        fprintf(stderr, "open database %s\n", err);
        exit(1);
    }

    free(err);
    err = NULL;

    rocksdb_writeoptions_t *writeopts = rocksdb_writeoptions_create();

    // put a new or existing key
    rocksdb_put(db, writeopts, "name", 4, "Koye", 4, &err);
    if (err != NULL) {
        fprintf(stderr, "put key %s\n", err);
        exit(1);
    }

    free(err);
    err = NULL;

    rocksdb_readoptions_t *readopts = rocksdb_readoptions_create();

    size_t rlen;
    // read the key back out
    char *value = rocksdb_get(db, readopts, "name", 4, &rlen, &err);
    if (err != NULL) {
        fprintf(stderr, "get key %s\n", err);
        exit(1);
    }

    free(err);
    err = NULL;

    printf("get key len: %lu value: \"%s\"\n", rlen, value);

    rocksdb_close(db);

    return 0;
}
