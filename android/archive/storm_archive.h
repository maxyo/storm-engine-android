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
 STORM_ARCHIVE_MISSING_VOLUME=8, STORM_ARCHIVE_NO_GAME_DATA=9
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
void storm_archive_free(char *json);
#ifdef __cplusplus
}
#endif
