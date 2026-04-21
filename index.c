#include "index.h"
#include "tree.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
// Phase 3: implemented index_save with atomic write
// ─── PROVIDED ────────────────────────────────────────────────

IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

// ─── LOAD INDEX ──────────────────────────────────────────────

int index_load(Index *index) {
    if (!index) return -1;

    index->count = 0;

    FILE *f = fopen(".pes/index", "r");
    if (!f) return 0;

    while (index->count < MAX_INDEX_ENTRIES) {
        IndexEntry *e = &index->entries[index->count];
        char hash_hex[HASH_HEX_SIZE + 1];

        if (fscanf(f, "%o %s %ld %u %s\n",
                   &e->mode,
                   hash_hex,
                   &e->mtime_sec,
                   &e->size,
                   e->path) != 5) {
            break;
        }

        hex_to_hash(hash_hex, &e->hash);
        index->count++;
    }

    fclose(f);
    return 0;
}

// ─── SAVE INDEX ──────────────────────────────────────────────

static int compare_index_entries(const void *a, const void *b) {
    return strcmp(((IndexEntry *)a)->path, ((IndexEntry *)b)->path);
}

int index_save(const Index *index) {
    if (!index) return -1;

    FILE *f = fopen(".pes/index.tmp", "w");
    if (!f) return -1;

    int count = index->count;

    // 🔥 allocate on heap instead of stack
    IndexEntry *temp = malloc(sizeof(IndexEntry) * count);
    if (!temp) {
        fclose(f);
        return -1;
    }

    for (int i = 0; i < count; i++) {
        temp[i] = index->entries[i];
    }

    qsort(temp, count, sizeof(IndexEntry), compare_index_entries);

    for (int i = 0; i < count; i++) {
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&temp[i].hash, hex);

        fprintf(f, "%o %s %ld %u %s\n",
                temp[i].mode,
                hex,
                temp[i].mtime_sec,
                temp[i].size,
                temp[i].path);
    }

    free(temp);  

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    rename(".pes/index.tmp", ".pes/index");
    return 0;
}

// ─── ADD FILE ────────────────────────────────────────────────

int index_add(Index *index, const char *path) {
    if (!index || !path) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    rewind(f);

    if (size == 0) {
        fclose(f);
        return -1;
    }

    void *data = malloc(size);
    if (!data) {
        fclose(f);
        return -1;
    }

    if (fread(data, 1, size, f) != size) {
        free(data);
        fclose(f);
        return -1;
    }

    fclose(f);

    ObjectID id;
    if (object_write(OBJ_BLOB, data, size, &id) != 0) {
        free(data);
        return -1;
    }

    free(data);

    struct stat st;
    if (stat(path, &st) != 0) return -1;

    IndexEntry *existing = index_find(index, path);

    if (!existing) {
        if (index->count >= MAX_INDEX_ENTRIES) return -1;
        existing = &index->entries[index->count++];
    }

    existing->mode = get_file_mode(path);
    existing->hash = id;
    existing->mtime_sec = st.st_mtime;
    existing->size = st.st_size;

    strncpy(existing->path, path, sizeof(existing->path) - 1);
    existing->path[sizeof(existing->path) - 1] = '\0';

    return index_save(index);
}

// ─── STATUS ──────────────────────────────────────────────────

int index_status(const Index *index) {
    printf("Staged changes:\n");
    if (index->count == 0) {
        printf("  (nothing to show)\n\n");
    } else {
        for (int i = 0; i < index->count; i++) {
            printf("  staged:     %s\n", index->entries[i].path);
        }
        printf("\n");
    }

    printf("Unstaged changes:\n");
    printf("  (nothing to show)\n\n");

    printf("Untracked files:\n");

    DIR *dir = opendir(".");
    int found = 0;

    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            if (strcmp(ent->d_name, ".pes") == 0) continue;

            int tracked = 0;
            for (int i = 0; i < index->count; i++) {
                if (strcmp(index->entries[i].path, ent->d_name) == 0) {
                    tracked = 1;
                    break;
                }
            }

            if (!tracked) {
                printf("  untracked:  %s\n", ent->d_name);
                found = 1;
            }
        }
        closedir(dir);
    }

    if (!found) printf("  (nothing to show)\n");

    printf("\n");
    return 0;
}
