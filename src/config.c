/*
 * config.c — Read GROQ_API_KEY from .env next to the .exe
 */

#include "../include/app.h"

char *ReadApiKey(void) {
    /* Find the folder the exe lives in */
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);

    char *slash = strrchr(path, '\\');
    if (slash) *(slash + 1) = '\0';
    strcat(path, ".env");

    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    char  line[512];
    char *key = NULL;

    while (fgets(line, sizeof(line), f)) {
        /* Strip CR / LF */
        char *end = line + strlen(line) - 1;
        while (end >= line && (*end == '\r' || *end == '\n')) *end-- = '\0';

        if (strncmp(line, "GROQ_API_KEY=", 13) == 0) {
            key = strdup(line + 13);
            break;
        }
    }

    fclose(f);
    return key;   /* caller must free() */
}
