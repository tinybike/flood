#include <stdio.h>
#include <libxml/xmlreader.h>
#include <leveldb/c.h>

#ifdef LIBXML_READER_ENABLED

/**
 * processNode:
 * @reader: the xmlReader
 *
 * Dump information about the current node
 */
static void
processNode(xmlTextReaderPtr reader) {
    const xmlChar *name, *value;

    name = xmlTextReaderConstName(reader);
    if (name == NULL)
    name = BAD_CAST "--";

    value = xmlTextReaderConstValue(reader);

    printf("%d %d %s %d %d", 
        xmlTextReaderDepth(reader),
        xmlTextReaderNodeType(reader),
        name,
        xmlTextReaderIsEmptyElement(reader),
        xmlTextReaderHasValue(reader));
    if (value == NULL)
    printf("\n");
    else {
        if (xmlStrlen(value) > 40)
            printf(" %.40s...\n", value);
        else
        printf(" %s\n", value);
    }
}

/**
 * streamFile:
 * @filename: the file name to parse
 *
 * Parse and print information about an XML file.
 */
static void
streamFile(const char *filename, leveldb_t *db) {
    xmlTextReaderPtr reader;
    int ret;
    char *err = NULL;

    reader = xmlReaderForFile(filename, NULL, 0);
    if (reader != NULL) {
        ret = xmlTextReaderRead(reader);
        while (ret == 1) {
            processNode(reader);
            ret = xmlTextReaderRead(reader);
        }
        xmlFreeTextReader(reader);
        if (ret != 0) {
            fprintf(stderr, "%s : failed to parse\n", filename);
        }
    } else {
        fprintf(stderr, "Unable to open %s\n", filename);
    }

    leveldb_writeoptions_t woptions = leveldb_writeoptions_create();
    leveldb_put(db, woptions, "zzz", 3, "lolul", 5, &err);
    if (err != NULL) {
        fprintf(stderr, "Write fail.\n");
        exit(1);
    }
    leveldb_free(err); err = NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) return(1);

    leveldb_t *db;
    leveldb_options_t *options;
    leveldb_readoptions_t *roptions;
    char *err = NULL;
    char *read;
    size_t read_len;

    options = leveldb_options_create();
    leveldb_options_set_create_if_missing(options, 1);
    db = leveldb_open(options, "links", &err);
    if (err != NULL) {
      fprintf(stderr, "Open fail.\n");
      return(1);
    }
    leveldb_free(err); err = NULL;

    /*
     * this initialize the library and check potential ABI mismatches
     * between the version it was compiled for and the actual shared
     * library used.
     */
    LIBXML_TEST_VERSION

    streamFile(argv[1], db);

    leveldb_close(db);
    leveldb_free(err); err = NULL;

    /*
     * Cleanup function for the XML library.
     */
    xmlCleanupParser();
    /*
     * this is to debug memory for regression tests
     */
    xmlMemoryDump();
    return(0);
}

#else
int main(void) {
    fprintf(stderr, "XInclude support not compiled in\n");
    exit(1);
}
#endif
