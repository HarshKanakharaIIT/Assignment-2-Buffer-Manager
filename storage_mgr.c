#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "storage_mgr.h"
#include "dberror.h"

/*
 * Storage Manager Implementation
 * ------------------------------
 * This module manages page-based files where each page has a fixed
 * size defined by PAGE_SIZE (4096 bytes).
 *
 * Key responsibilities:
 *  - Create new files initialized with one empty page.
 *  - Open and close page files with tracking information.
 *  - Read and write data at the granularity of pages.
 *  - Extend files by appending new empty pages.
 *  - Ensure that a file contains at least a specified number of pages.
 *
 * Notes:
 *  - Each SM_FileHandle has mgmtInfo, which holds a FileCtx
 *    containing the FILE* pointer and a private copy of the filename.
 *  - An internal registry keeps track of open files so that
 *    destroyPageFile can close them safely (important on Windows).
 */

/* ------------ Internal structures ------------ */

/* Wraps the FILE pointer and a copy of the file name */
typedef struct FileCtx {
    FILE *fp;
    char *fname;
} FileCtx;

/* Local strdup replacement (some environments lack it) */
static char *sm_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *copy = malloc(n + 1);
    if (copy) memcpy(copy, s, n + 1);
    return copy;
}

/* Linked list of currently open files for deletion safety */
typedef struct OpenReg {
    char *name;
    FILE *fp;
    struct OpenReg *next;
} OpenReg;

static OpenReg *openList = NULL;

/* ------------ Registry helpers ------------ */

/* Add a file entry to the open file list */
static void register_open(const char *name, FILE *fp) {
    OpenReg *r = calloc(1, sizeof(OpenReg));
    if (!r) return;
    r->name = sm_strdup(name);
    r->fp = fp;
    r->next = openList;
    openList = r;
}

/* Remove a file entry (matched by FILE* or filename) */
static void unregister_open(const char *name, FILE *fp) {
    OpenReg **pp = &openList;
    while (*pp) {
        OpenReg *cur = *pp;
        if ((fp && cur->fp == fp) || (name && cur->name && strcmp(cur->name, name) == 0)) {
            *pp = cur->next;
            free(cur->name);
            free(cur);
            return;
        }
        pp = &cur->next;
    }
}

/* Find a FILE* by filename */
static FILE *lookup_open(const char *name) {
    for (OpenReg *p = openList; p; p = p->next) {
        if (p->name && strcmp(p->name, name) == 0)
            return p->fp;
    }
    return NULL;
}

/* ------------ Utility functions ------------ */

/* Quick check if a file handle is initialized */
static int validHandle(const SM_FileHandle *h) {
    return (h && h->mgmtInfo);
}

/* Cast mgmtInfo into FileCtx pointer */
static FileCtx *ctx(SM_FileHandle *h) {
    return (FileCtx *)h->mgmtInfo;
}

/* Compute byte offset for a given page number */
static long pageOffset(int pageNum) {
    return (long)pageNum * (long)PAGE_SIZE;
}

/* Read one full page from file */
static RC fread_page(FILE *fp, void *buf) {
    return (fread(buf, 1, PAGE_SIZE, fp) == PAGE_SIZE) ? RC_OK : RC_READ_NON_EXISTING_PAGE;
}

/* Write one full page to file */
static RC fwrite_page(FILE *fp, const void *buf) {
    return (fwrite(buf, 1, PAGE_SIZE, fp) == PAGE_SIZE) ? RC_OK : RC_WRITE_FAILED;
}

/* ------------ Public API implementation ------------ */

/* Initialize global storage manager state (currently nothing needed) */
void initStorageManager(void) { }

/* Create a new file with exactly one empty page */
RC createPageFile(char *fileName) {
    if (!fileName) return RC_WRITE_FAILED;

    FILE *fp = fopen(fileName, "wb+");
    if (!fp) return RC_WRITE_FAILED;

    char *blank = calloc(PAGE_SIZE, 1);   // allocate a zeroed page
    if (!blank) { fclose(fp); remove(fileName); return RC_WRITE_FAILED; }

    RC rc = fwrite_page(fp, blank);
    free(blank);

    if (rc != RC_OK) { fclose(fp); remove(fileName); return rc; }

    fflush(fp);
    fclose(fp);
    return RC_OK;
}

/* Open an existing page file and initialize its handle */
RC openPageFile(char *fileName, SM_FileHandle *fHandle) {
    if (!fileName || !fHandle) return RC_FILE_HANDLE_NOT_INIT;

    FILE *fp = fopen(fileName, "rb+");
    if (!fp) return RC_FILE_NOT_FOUND;

    /* Determine number of pages in the file */
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return RC_FILE_NOT_FOUND; }
    long fsize = ftell(fp);
    if (fsize < 0) { fclose(fp); return RC_FILE_NOT_FOUND; }
    int pages = (int)((fsize + PAGE_SIZE - 1) / PAGE_SIZE);

    FileCtx *c = calloc(1, sizeof(FileCtx));
    if (!c) { fclose(fp); return RC_FILE_HANDLE_NOT_INIT; }
    c->fp = fp;
    c->fname = sm_strdup(fileName);

    fHandle->fileName = fileName;
    fHandle->totalNumPages = pages;
    fHandle->curPagePos = (pages > 0 ? 0 : -1);
    fHandle->mgmtInfo = c;

    register_open(fileName, fp);
    return RC_OK;
}

/* Close a page file and clear its handle */
RC closePageFile(SM_FileHandle *fHandle) {
    if (!validHandle(fHandle)) return RC_FILE_HANDLE_NOT_INIT;

    FileCtx *c = ctx(fHandle);
    unregister_open(c->fname, c->fp);
    int status = fclose(c->fp);
    free(c->fname);
    free(c);

    fHandle->mgmtInfo = NULL;
    fHandle->curPagePos = -1;
    fHandle->totalNumPages = 0;

    return (status == 0) ? RC_OK : RC_FILE_HANDLE_NOT_INIT;
}

/* Remove a page file from disk, closing it if still open */
RC destroyPageFile(char *fileName) {
    if (!fileName) return RC_FILE_NOT_FOUND;

    FILE *fp = lookup_open(fileName);
    if (fp) {
        fclose(fp);
        unregister_open(fileName, NULL);
    }
    return (remove(fileName) == 0) ? RC_OK : RC_FILE_NOT_FOUND;
}

/* ------------ Reading operations ------------ */

/* Read a page at a given index into memPage */
RC readBlock(int pageNum, SM_FileHandle *fHandle, SM_PageHandle memPage) {
    if (!validHandle(fHandle) || !memPage) return RC_FILE_HANDLE_NOT_INIT;
    if (pageNum < 0 || pageNum >= fHandle->totalNumPages) return RC_READ_NON_EXISTING_PAGE;

    FileCtx *c = ctx(fHandle);
    if (fseek(c->fp, pageOffset(pageNum), SEEK_SET) != 0) return RC_READ_NON_EXISTING_PAGE;

    RC rc = fread_page(c->fp, memPage);
    if (rc == RC_OK) fHandle->curPagePos = pageNum;
    return rc;
}

/* Return the current page position */
int getBlockPos(SM_FileHandle *fHandle) {
    return validHandle(fHandle) ? fHandle->curPagePos : -1;
}

/* Convenience wrappers for reading relative positions */
RC readFirstBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    return readBlock(0, fHandle, memPage);
}
RC readPreviousBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    return readBlock(fHandle->curPagePos - 1, fHandle, memPage);
}
RC readCurrentBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    return readBlock(fHandle->curPagePos, fHandle, memPage);
}
RC readNextBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    return readBlock(fHandle->curPagePos + 1, fHandle, memPage);
}
RC readLastBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    return readBlock(fHandle->totalNumPages - 1, fHandle, memPage);
}

/* ------------ Writing operations ------------ */

/* Write a full page at the specified index */
RC writeBlock(int pageNum, SM_FileHandle *fHandle, SM_PageHandle memPage) {
    if (!validHandle(fHandle) || !memPage) return RC_FILE_HANDLE_NOT_INIT;
    if (pageNum < 0 || pageNum >= fHandle->totalNumPages) return RC_WRITE_FAILED;

    FileCtx *c = ctx(fHandle);
    if (fseek(c->fp, pageOffset(pageNum), SEEK_SET) != 0) return RC_WRITE_FAILED;

    RC rc = fwrite_page(c->fp, memPage);
    if (rc == RC_OK) {
        fflush(c->fp);
        fHandle->curPagePos = pageNum;
    }
    return rc;
}

/* Write to the current block position */
RC writeCurrentBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    return writeBlock(fHandle->curPagePos, fHandle, memPage);
}

/* Append a new blank page at the end of the file */
RC appendEmptyBlock(SM_FileHandle *fHandle) {
    if (!validHandle(fHandle)) return RC_FILE_HANDLE_NOT_INIT;

    FileCtx *c = ctx(fHandle);
    if (fseek(c->fp, 0, SEEK_END) != 0) return RC_WRITE_FAILED;

    char *blank = calloc(PAGE_SIZE, 1);
    if (!blank) return RC_WRITE_FAILED;
    RC rc = fwrite_page(c->fp, blank);
    free(blank);

    if (rc == RC_OK) {
        fflush(c->fp);
        fHandle->totalNumPages++;
    }
    return rc;
}

/* Grow the file until it contains at least numberOfPages */
RC ensureCapacity(int numberOfPages, SM_FileHandle *fHandle) {
    if (!validHandle(fHandle)) return RC_FILE_HANDLE_NOT_INIT;
    while (fHandle->totalNumPages < numberOfPages) {
        RC rc = appendEmptyBlock(fHandle);
        if (rc != RC_OK) return rc;
    }
    return RC_OK;
}
