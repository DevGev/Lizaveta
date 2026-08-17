/* mime.h - filename to MIME type, and MIME type to icon names. */

#ifndef LIZ_MIME_H
#define LIZ_MIME_H

#include <stddef.h>

/* Longest MIME type or icon name kept. Nothing in shared-mime-info comes
 * close, so this only bounds the buffers. */
#define LIZ_MIME_MAX 128

/* A type rarely has more than one or two aliases. */
#define LIZ_MIME_ALIASES_MAX 8

/* An extension can be claimed by several types; ".ogg" claims the most. */
#define LIZ_MIME_TYPES_MAX 12

/* Loads the shared-mime-info glob and icon tables. A missing database is
 * not an error: lookups then yield only the generic fallback names. */
void liz_mime_init(void);
void liz_mime_shutdown(void);

/* Writes the MIME type of `filename` into `out`, falling back to
 * application/octet-stream when no glob matches. */
void liz_mime_type(const char* filename, char* out, size_t outsz);

/* Writes every MIME type whose glob matches `filename`, most specific
 * first: a type that is a subclass of another match sorts before it.
 *
 * An extension is often ambiguous. ".ogg" alone cannot say whether a file
 * holds Vorbis, Opus, FLAC or Theora, and the database registers a type for
 * each; the reference implementation tells them apart by sniffing content,
 * which this does not do. Reporting all of them lets a caller find every
 * application that could open the file rather than only those that happened
 * to register the one spelling a tie-break picked. Returns how many were
 * written. */
int liz_mime_types(const char* filename, char out[][LIZ_MIME_MAX], int max);

/* Writes `mime` plus every type that aliases to it. Applications register
 * the spelling that was current when they were written, so a lookup keyed
 * on the canonical name the database returns misses them otherwise:
 * ".mkv" resolves to video/matroska, while players advertise
 * video/x-matroska. Returns how many were written. */
int liz_mime_alias_names(const char* mime, char out[][LIZ_MIME_MAX], int max);

/* Writes the icon names to try for `filename`, most specific first, and
 * returns how many were written (at most `max`). */
int liz_mime_icon_names(const char* filename, char out[][LIZ_MIME_MAX], int max);

#endif /* LIZ_MIME_H */
