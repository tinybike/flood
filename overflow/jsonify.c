static char *jsonify(const struct Torrent *torr)
{
    size_t tsz = strlen(torr->hash) + strlen(torr->magnet) + 112; // 112 bytes for json format
    char *torrjson = malloc(tsz);
    
    strlcat(torrjson, "{\n   \"hash\": \"", tsz);
    strlcat(torrjson, torr->hash, tsz);
    strlcat(torrjson, "\",", tsz);
    strlcat(torrjson, "\n   \"magnet\": \"", tsz);
    strlcat(torrjson, torr->magnet, tsz);
    strlcat(torrjson, "\"", tsz);
    strlcat(torrjson, "\n}", tsz);

    return torrjson;
}
