#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Input must be a local regular file; Inno's standard sibling .bin files must
 * already reside in the same private input directory. Never executes input.
 * Output must be an existing empty private directory, separate from input/live
 * data. On failure discard the entire staging directory. Caller owns commit. */
enum storm_archive_status {
 STORM_ARCHIVE_OK=0, STORM_ARCHIVE_IO=1, STORM_ARCHIVE_UNSUPPORTED=2,
 STORM_ARCHIVE_UNSUPPORTED_EXECUTABLE=3, STORM_ARCHIVE_ENCRYPTED=4,
 STORM_ARCHIVE_INVALID=5, STORM_ARCHIVE_LIMIT=6, STORM_ARCHIVE_CANCELLED=7,
 STORM_ARCHIVE_MISSING_VOLUME=8, STORM_ARCHIVE_NO_GAME_DATA=9,
 STORM_ARCHIVE_NO_SPACE=10
};
struct storm_archive_limits {
 uint64_t max_total_bytes; /* Default 32 GiB. Includes discarded solid data. */
 uint64_t max_file_bytes;  /* Default 4 GiB. */
 uint64_t max_entries;     /* Default 200000. */
 uint64_t max_seconds;     /* Default 3600. */
};
/* Return nonzero to cancel. Called on the importing thread. */
typedef int (*storm_archive_progress)(void*, const char*, uint64_t, uint64_t);
/* Inspect never extracts. Returns OK with format "inno", "zip", "7z", "tar",
 * "rar", "cab", or "iso9660". Classification does not imply a complete,
 * unencrypted, supported installer; extraction validates full metadata/data. */
int storm_archive_inspect(const char *input, char *format, size_t capacity,
                          char *error, size_t error_capacity);
int storm_archive_extract(const char *input, const char *empty_staging,
 const struct storm_archive_limits *limits, storm_archive_progress progress,
 void *userdata, char *error, size_t error_capacity);
/* Metadata only: no extraction and no .bin reads. Caller frees *json with
 * storm_archive_free. A structurally incompatible game returns NO_GAME_DATA
 * while still returning the parsed manifest for an explanatory preview. */
int storm_archive_manifest(const char *input, char **json,
                           char *error, size_t error_capacity);

/* ---- Source sets: discovery of game roots and nested containers ----------
 * A source is a private directory holding the player's files (copied picker
 * documents or a downloaded set), or one file in such a directory (its Inno
 * volumes or archive volumes are then taken from that directory and nothing
 * else is read). Discovery finds every complete game root
 * (engine.ini + PROGRAM/ + RESOURCE/) in loose folders, archives, Inno
 * installers with their volumes, multi-volume archives and containers nested
 * up to max_depth levels. Nested containers are written to scratch_dir, which
 * must be an existing empty private directory outside the source; the caller
 * keeps it until extraction finished and then deletes it. */
enum storm_archive_stage {
 STORM_ARCHIVE_STAGE_READING=1,  /* listing sources and containers */
 STORM_ARCHIVE_STAGE_NESTED=2,   /* writing nested containers to scratch */
 STORM_ARCHIVE_STAGE_INSTALL=3   /* writing the selected game into staging */
};
/* bytes/total are for the current stage (total 0 when unknown); files counts
 * created game files. Return nonzero to cancel. Called on the calling thread. */
typedef int (*storm_archive_stage_progress)(void*, int stage, const char *path,
 uint64_t bytes, uint64_t total, uint64_t files);
struct storm_archive_source_limits {
 uint64_t max_total_bytes;   /* Decoded bytes per call. Default 64 GiB. */
 uint64_t max_file_bytes;    /* One game file. Default 4 GiB. */
 uint64_t max_entries;       /* Entries seen per call. Default 400000. */
 uint64_t max_seconds;       /* Per call. Default 3600. */
 uint64_t max_scratch_bytes; /* Nested containers in scratch. Default 32 GiB. */
 uint64_t max_depth;         /* Nested container levels, 1..3. Default 3. */
 uint64_t max_ratio;         /* Decoded:compressed bound per container. Default 100. */
 uint64_t reserve_bytes;     /* Free space kept on the device. Default 64 MiB. */
};
/* *json (free with storm_archive_free) is the discovery document on success.
 * On ENCRYPTED, NO_SPACE or LIMIT it describes the failure; see README.
 * password may be NULL; it is applied to every container that needs one. */
int storm_archive_discover(const char *source, const char *scratch_dir,
 const char *password, const struct storm_archive_source_limits *limits,
 storm_archive_stage_progress progress, void *userdata,
 char **json, char *error, size_t error_capacity);
/* Extracts exactly one discovered root (its opaque id) into empty_staging.
 * Source and scratch must be unchanged since discovery. */
int storm_archive_extract_root(const char *source, const char *scratch_dir,
 const char *password, const char *root_id, const char *empty_staging,
 const struct storm_archive_source_limits *limits,
 storm_archive_stage_progress progress, void *userdata,
 char **json, char *error, size_t error_capacity);
void storm_archive_free(char *json);
#ifdef __cplusplus
}
#endif
