/**
 * gcc src/xmlparse.c -lxml2 -lsnappy -lleveldb -I/usr/include/libxml2 -o xmlparse
 * ./xmlparse data/test2.xml-clean
 */

#include <stdio.h>
#include <string.h>
#include <libxml/xmlreader.h>
#include <leveldb/c.h>

static void stream_file(const char *filename, leveldb_t *db) {
    const xmlChar *value, *next_name;
    xmlTextReaderPtr reader;
    int ret;
    char *err = NULL;
    char name[9], magnet[10000], hash[41];
    leveldb_writeoptions_t *woptions;

    woptions = leveldb_writeoptions_create();

    reader = xmlReaderForFile(filename, NULL, 0);
    if (reader != NULL) {
        ret = xmlTextReaderRead(reader);
        while (ret == 1)
        {
            next_name = xmlTextReaderConstName(reader);
            if (next_name != NULL && strcmp("#text", (char *)next_name)) {
                strcpy(name, (char *)next_name);
            }
            value = xmlTextReaderConstValue(reader);
            if (value != NULL && strcmp("\n", (char *)value)) {
                // printf("%s => %s\n", name, value);
                if (!strcmp("title", name)) {
                    if (strcmp("magnet:?", magnet)) {
                        strcat(magnet, "&");
                    }
                    strcat(magnet, "dn=");
                    strcat(magnet, (char *)value);
                } else if (!strcmp("magnet", name)) {
                    if (strcmp("magnet:?", magnet)) {
                        strcat(magnet, "&");
                    }
                    strcat(magnet, "xt=urn:btih:");
                    strcat(magnet, (char *)value);
                    strcpy(hash, (char *)value);
                } else if (!strcmp("id", name)) {
                    if (strcmp("", magnet)) {
                        leveldb_put(db, woptions, hash, 41, magnet, (int)strlen(magnet), &err);
                        if (err != NULL) {
                            fprintf(stderr, "Write fail.\n");
                            exit(1);
                        }
                        leveldb_free(err); err = NULL;
                    }
                    strcpy(magnet, "magnet:?");
                }
            }
            ret = xmlTextReaderRead(reader);
        }
        if (strcmp("", magnet) && strcmp("magnet:?", magnet)) {
            leveldb_put(db, woptions, hash, 41, magnet, (int)strlen(magnet), &err);
            if (err != NULL) {
                fprintf(stderr, "Write fail.\n");
                exit(1);
            }
            leveldb_free(err); err = NULL;
        }
        xmlFreeTextReader(reader);
        if (ret != 0) {
            fprintf(stderr, "%s : failed to parse\n", filename);
        }
    } else {
        fprintf(stderr, "Unable to open %s\n", filename);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) return 1;

    leveldb_t *db;
    leveldb_options_t *options;
    char *err = NULL;
    char *read;
    size_t read_len;

    options = leveldb_options_create();
    leveldb_options_set_create_if_missing(options, 1);

    db = leveldb_open(options, "links", &err);
    if (err != NULL) {
        fprintf(stderr, "Open fail.\n");
        return 1;
    }
    leveldb_free(err); err = NULL;

    LIBXML_TEST_VERSION

    stream_file(argv[1], db);

    leveldb_close(db);
    leveldb_free(err); err = NULL;

    xmlCleanupParser();
    xmlMemoryDump();
    return 0;
}
