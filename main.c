#include <archive.h>
#include <archive_entry.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/multi.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct archive_state {
    char* data;
    size_t len;
    CURLM* handle;
};

static ssize_t archive_read(struct archive* a, void* userdata,
    const void** buff)
{
    struct archive_state* state = (struct archive_state*)userdata;

    state->len = 0;
    int still_running = 0;

    do {
        CURLMcode mc = curl_multi_perform(state->handle, &still_running);

        if (mc == CURLM_OK && still_running) {
            mc = curl_multi_poll(state->handle, NULL, 0, 1000, NULL);
        }

        if (mc != CURLM_OK) {
            fprintf(stderr, "curl_multi_poll failed: %d\n", (int)mc);
            return -1;
        }
    } while (still_running && state->len == 0);

    *buff = state->data;

    return state->len;
}

static int extract(struct archive* ar, struct archive* aw)
{
    const void* buff;
    size_t size;
    la_int64_t offset;

    for (;;) {
        int r = archive_read_data_block(ar, &buff, &size, &offset);
        if (r == ARCHIVE_EOF)
            return (ARCHIVE_OK);
        if (r < ARCHIVE_OK)
            return (r);
        r = archive_write_data_block(aw, buff, size, offset);
        if (r < ARCHIVE_OK) {
            fprintf(stderr, "%s\n", archive_error_string(aw));
            return (r);
        }
    }
    return 0;
}

size_t write_callback(char* ptr, size_t size, size_t nmemb,
    struct archive_state* state)
{
    size_t length = state->len;
    size_t chunk_size = size * nmemb;

    if (chunk_size == 0) {
        return 0;
    }

    state->len = length + chunk_size;
    state->data = realloc(state->data, state->len);
    memcpy(state->data + length, ptr, chunk_size);
    return chunk_size;
}

static void* download_archive(void* url)
{
    struct archive_state state = {
        .data = NULL,
        .len = 0,
        .handle = curl_multi_init()
    };

    CURL* http_handle = curl_easy_init();
    curl_easy_setopt(http_handle, CURLOPT_URL, (char*)url);
    curl_easy_setopt(http_handle, CURLOPT_USERAGENT, "curl");
    curl_easy_setopt(http_handle, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(http_handle, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(http_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(http_handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(http_handle, CURLOPT_WRITEDATA, &state);

    // Use multi interface for a non-blocking HTTP request where we can stream
    // body content to libarchive
    curl_multi_add_handle(state.handle, http_handle);

    struct archive* a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    if (archive_read_open(a, &state, NULL, archive_read, NULL) != 0) {
        fprintf(stderr, "Error reading archive: %s\n", archive_error_string(a));
        goto cleanup;
    }

    int archive_ret = -1;
    struct archive* ext = archive_write_disk_new();
    struct archive_entry* entry;
    archive_write_disk_set_standard_lookup(ext);

    do {
        archive_ret = archive_read_next_header(a, &entry);
        if (archive_ret == ARCHIVE_OK) {
            int r = archive_write_header(ext, entry);
            if (r < ARCHIVE_OK)
                fprintf(stderr, "Can not write header: %s\n",
                    archive_error_string(ext));
            else if (archive_entry_size(entry) > 0) {
                r = extract(a, ext);
                if (r < ARCHIVE_OK)
                    fprintf(stderr, "Some error unknown occured: %s\n",
                        archive_error_string(ext));
                if (r < ARCHIVE_WARN)
                    break;
            }

            r = archive_write_finish_entry(ext);
            if (r < ARCHIVE_OK)
                fprintf(stderr, "%s\n", archive_error_string(ext));
            if (r < ARCHIVE_WARN)
                fprintf(stderr, "Can not write entry: %d: \n", r);

            printf("%s\n", archive_entry_pathname(entry));
        }
    } while (archive_ret == ARCHIVE_OK || archive_ret == ARCHIVE_RETRY);

    free(state.data);
    archive_write_close(ext);
    archive_write_free(ext);

cleanup:
    archive_read_free(a);
    curl_easy_cleanup(http_handle);
    curl_multi_remove_handle(state.handle, http_handle);
    curl_multi_cleanup(state.handle);

    return NULL;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "Enter list of direct URLs to the archives you want to download\n");
        return 1;
    }

    curl_global_init(CURL_GLOBAL_ALL);
    pthread_t threads[argc - 1];

    for (int i = 1; i < argc; i++) {
        int ret = pthread_create(&threads[i - 1], NULL, download_archive, (void*)argv[i]);
        if (ret != 0) {
            fprintf(stderr, "Failed to spawn new thread: %d", ret);
        } else {
            fprintf(stdin, "Thread %d gets: %s", i, argv[i]);
        }
    }

    for (int i = 1; i < argc; i++) {
        pthread_join(threads[i - 1], NULL);
        fprintf(stdin, "Thread %d finished", i - 1);
    }

    curl_global_cleanup();
    return 0;
}
