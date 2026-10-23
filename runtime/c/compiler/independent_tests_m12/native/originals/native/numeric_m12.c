/* SPDX-License-Identifier: MIT
 * M12 native publication and caller-environment integration.
 * Expected admission comes only from the sealed evaluator case. Reference
 * bytes below test host-state invariance, not independent shader semantics. */
#define _GNU_SOURCE
#include "environment_m12.h"
#include <pthread.h>

static const unsigned modes[] = {1u, 2u, 3u, 5u, 6u, 7u};
static const DoeCompilerJobCapacities job_caps = {
    65536u, 8192u, 1024u, 4096u, 65536u, 32u, 16u
};

typedef struct {
    const char *source;
    size_t length;
    DoeCompilerStatus expected;
    DoeCompilerStatus legacy_expected;
    const char *projection_name;
    DoeCompilerTypedOutputCounts counts;
    Bulk reference;
} Case;

static int save_words(const char *directory, unsigned api,
                      const uint32_t *words, size_t count)
{
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s/api%u.spv", directory, api);
    CHECK(length > 0 && (size_t)length < sizeof(path), "artifact path bound");
    FILE *file = fopen(path, "wbx");
    CHECK(file != NULL, "new per-adapter output path");
    /* Explicit little-endian encoding does not inherit host endianness. */
    for (size_t i = 0u; i < count; ++i) {
        unsigned char bytes[4] = {
            (unsigned char)words[i], (unsigned char)(words[i] >> 8),
            (unsigned char)(words[i] >> 16), (unsigned char)(words[i] >> 24)
        };
        CHECK(fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes),
              "complete SPIR-V word write");
    }
    CHECK(fclose(file) == 0, "artifact close");
    printf("ARTIFACT api=%u words=%zu path=%s\n", api, count, path);
    return 0;
}

static int prepare_case(Case *test)
{
    Client client;
    CHECK(client_new(&client, &generous) == 0, "baseline client");
    DoeCompilerTypedResult result;
    const char *source = test->expected == DOE_COMPILER_OK ?
        test->source : old_source;
    size_t length = test->expected == DOE_COMPILER_OK ?
        test->length : strlen(old_source);
    CHECK(invoke(&client, source, length, 7u, NULL, &result) == DOE_COMPILER_OK,
          "independent admitted source fits unchanged profile");
    test->counts = result.required;
    CHECK(bulk_new(&test->reference, &test->counts) == 0, "reference outputs");
    CHECK(invoke(&client, source, length, 3u, &test->reference.o, &result) ==
          DOE_COMPILER_OK, "host-invariance reference compilation");
    client_free(&client);
    return 0;
}

static int typed_call(const Case *test, Client *client, Bulk *outputs,
                      unsigned flags, int short_output)
{
    DoeCompilerTypedOutputs selected_outputs = selected(outputs, flags);
    DoeCompilerStatus expected = test->expected;
    if (short_output) {
        CHECK(expected == DOE_COMPILER_OK && !(flags & 4u),
              "only successful nonmeasure output shortage");
        if (flags & 1u) selected_outputs.word_capacity = 0u;
        else selected_outputs.name_capacity = 0u;
        expected = DOE_COMPILER_OUTPUT_TOO_SMALL;
    }
    bulk_mark(outputs);
    NativeEnvironment before, after;
    CHECK(environment_snapshot(&before) == 0, "typed host before");
    errno = 27319;
    DoeCompilerTypedResult result;
    DoeCompilerStatus status = invoke(
        client, test->source, test->length, flags,
        (flags & 4u) ? NULL : &selected_outputs, &result);
    int saved_errno = errno;
    CHECK(environment_snapshot(&after) == 0, "typed host after");
    CHECK(saved_errno == 27319 && environment_equal(&before, &after),
          "typed success/error preserves complete caller state");
    CHECK(status == expected, "sealed typed admission/error category");
    CHECK(guard_ok(&client->workspace) && bulk_guards(outputs),
          "typed host output and workspace redzones");
    if (status == DOE_COMPILER_OK) {
        CHECK(result.required_is_exact == 1u, "typed success exact counts");
        DoeCompilerTypedOutputCounts wanted = test->counts;
        if (!(flags & 1u)) wanted.words = 0u;
        if (!(flags & 2u)) {
            wanted.types = wanted.members = wanted.resources = 0u;
            wanted.name_bytes = 0u;
        }
        CHECK(counts_equal(&result.required, &wanted),
              "selected required counts invariant across host states");
        if (flags & 4u) {
            CHECK(bytes_are(&result.published, sizeof(result.published), 0u),
                  "measure no published counts");
        } else CHECK(counts_equal(&result.published, &wanted),
                     "normal output publishes full selected counts");
        for (size_t i = 0u; i < COUNT(outputs->span); ++i) {
            int active = !(flags & 4u) &&
                (i == 0u ? (int)(flags & 1u) : (int)(flags & 2u));
            CHECK(active ?
                  memcmp(test->reference.span[i].data,
                         outputs->span[i].data, outputs->span[i].bytes) == 0 :
                  bytes_are(outputs->span[i].data, outputs->span[i].bytes,
                            MARK), "byte determinism and unselected canaries");
        }
    } else {
        CHECK(failure_result(&result, status, short_output) == 0 &&
              bulk_unchanged(outputs), "atomic all-span failure publication");
        CHECK(result.diagnostic.offset <= test->length,
              "diagnostic is within original source or its EOF boundary");
    }
    return 0;
}

static DoeCompilerStatus legacy_invoke(unsigned api, Guard *workspace,
                                       const Case *test, Guard *words,
                                       size_t word_capacity,
                                       DoeCompilerResult *first,
                                       DoeCompilerResultV2 *other)
{
    ++native_inside;
    DoeCompilerStatus status;
    if (api == 1u) {
        status = doe_compiler_compile(1u, test->source, test->length,
            (uint32_t *)words->data, word_capacity, first, sizeof(*first));
    } else if (api == 2u) {
        status = doe_compiler_compile_v2(2u, workspace->data, workspace->bytes,
            test->source, test->length, (uint32_t *)words->data,
            word_capacity, other, sizeof(*other));
    } else {
        status = doe_compiler_compile_job(3u, &job_caps, sizeof(job_caps),
            workspace->data, workspace->bytes, test->source, test->length,
            (uint32_t *)words->data, word_capacity, other, sizeof(*other));
    }
    --native_inside;
    return status;
}

static int legacy_matrix(const Case *test, const char *directory)
{
    for (unsigned api = 1u; api <= 3u; ++api) {
        Guard workspace, words;
        memset(&workspace, 0, sizeof(workspace));
        if (api > 1u) {
            DoeCompilerWorkspaceRequirements requirements;
            ++native_inside;
            DoeCompilerStatus status = api == 2u ?
                doe_compiler_workspace_requirements(
                    2u, &requirements, sizeof(requirements)) :
                doe_compiler_job_requirements(3u, &job_caps, sizeof(job_caps),
                    &requirements, sizeof(requirements));
            --native_inside;
            CHECK(status == DOE_COMPILER_OK, "legacy workspace query");
            CHECK(guard_new(&workspace, requirements.size_bytes) == 0,
                  "separate legacy workspace");
        }
        CHECK(guard_new(&words, 65536u * sizeof(uint32_t)) == 0,
              "legacy caller output");
        unsigned char *reference = NULL;
        size_t reference_count = 0u;
        for (unsigned profile = 0u; profile < 32u; ++profile) {
            CHECK(environment_configure(profile) == 0, "legacy FP profile");
            for (unsigned short_output = 0u; short_output < 2u;
                 ++short_output) {
                if (short_output && test->legacy_expected != DOE_COMPILER_OK) break;
                guard_mark(&words);
                NativeEnvironment before, after;
                CHECK(environment_snapshot(&before) == 0, "legacy before");
                errno = 18377;
                DoeCompilerResult first;
                DoeCompilerResultV2 other;
                DoeCompilerStatus status = legacy_invoke(
                    api, &workspace, test, &words,
                    short_output ? 0u : 65536u, &first, &other);
                int saved_errno = errno;
                CHECK(environment_snapshot(&after) == 0, "legacy after");
                CHECK(saved_errno == 18377 && environment_equal(&before, &after),
                      "all legacy adapters preserve FP/MXCSR/traps/errno");
                DoeCompilerStatus expected = short_output ?
                    DOE_COMPILER_OUTPUT_TOO_SMALL : test->legacy_expected;
                CHECK(status == expected, "legacy source semantics and named capability");
                if (status == DOE_COMPILER_UNSUPPORTED) {
                    const DoeCompilerDiagnostic *diagnostic = api == 1u ?
                        &first.diagnostic : &other.diagnostic;
                    const char prefix[] =
                        "legacy reflection projection cannot represent ";
                    CHECK(test->expected == DOE_COMPILER_OK &&
                          test->projection_name[0] != '\0',
                          "only explicitly mapped legacy projection rejection");
                    CHECK(strncmp(diagnostic->message, prefix,
                                  sizeof(prefix) - 1u) == 0 &&
                          strcmp(diagnostic->message + sizeof(prefix) - 1u,
                                 test->projection_name) == 0,
                          "exact source-derived unrepresentable resource");
                    CHECK(diagnostic->offset < test->length,
                          "legacy projection original declaration offset");
                }
                size_t count = api == 1u ? first.word_count : other.word_count;
                if (status == DOE_COMPILER_OK) {
                    CHECK(count > 5u && count <= 65536u, "legacy word extent");
                    CHECK(bytes_are(words.data + count * sizeof(uint32_t),
                                    words.bytes - count * sizeof(uint32_t),
                                    MARK), "legacy output suffix untouched");
                    if (profile == 0u) {
                        reference_count = count;
                        reference = malloc(count * sizeof(uint32_t));
                        CHECK(reference != NULL, "caller reference storage");
                        memcpy(reference, words.data, count * sizeof(uint32_t));
                        CHECK(save_words(directory, api,
                            (uint32_t *)words.data, count) == 0,
                            "raw output for independent target execution");
                    } else CHECK(count == reference_count &&
                        memcmp(reference, words.data,
                               count * sizeof(uint32_t)) == 0,
                        "legacy host-state target invariance");
                } else {
                    CHECK(count == 0u && bytes_are(words.data, words.bytes, MARK),
                          "legacy failure words unpublished");
                    if (api == 1u) CHECK(bytes_are(&first.reflection,
                        sizeof(first.reflection), 0u), "ABI1 no reflection");
                    else CHECK(bytes_are(&other.reflection,
                        sizeof(other.reflection), 0u), "legacy no reflection");
                }
                CHECK(guard_ok(&words) &&
                      (api == 1u || guard_ok(&workspace)), "legacy canaries");
            }
            printf("PROGRESS legacy api=%u profile=%u/32 expected=%d\n",
                   api, profile + 1u, (int)test->legacy_expected);
        }
        CHECK(environment_configure(0u) == 0, "reset before caller cleanup");
        free(reference);
        guard_free(&words);
        if (api > 1u) guard_free(&workspace);
    }
    return 0;
}

typedef struct {
    const Case *test;
    unsigned profile;
    int failed;
} Thread;

static void *thread_main(void *opaque)
{
    Thread *thread = opaque;
    NativeEnvironment original;
    thread->failed = environment_snapshot(&original);
    if (thread->failed) return NULL;
    Client client;
    thread->failed = client_new(&client, &generous);
    if (thread->failed) return NULL;
    Bulk outputs;
    thread->failed = bulk_new(&outputs, &thread->test->counts);
    if (thread->failed) {
        client_free(&client);
        return NULL;
    }
    thread->failed = environment_configure(thread->profile);
    for (unsigned i = 0u; i < 24u && !thread->failed; ++i) {
        thread->failed = typed_call(thread->test, &client, &outputs,
                                    modes[i % COUNT(modes)], 0);
    }
    if (fesetenv(&original.raw) != 0) thread->failed = 1;
    bulk_free(&outputs);
    client_free(&client);
    return NULL;
}

int main(int argc, char **argv)
{
    CHECK(setvbuf(stdout, NULL, _IOLBF, 0u) == 0, "line-buffered progress");
    CHECK(argc == 6 || argc == 7,
          "usage: numeric_m12 source.wgsl typed_status legacy_status projection_name new_out [threads]");
    CHECK(strcmp(argv[2], "0") == 0 || strcmp(argv[2], "4") == 0,
          "frozen selected statuses success or semantic error");
    CHECK(strcmp(argv[3], "0") == 0 || strcmp(argv[3], "3") == 0 ||
          strcmp(argv[3], "4") == 0, "explicit frozen legacy status");
    CHECK(native_allocator_control() == 0, "allocator positive control");
    NativeEnvironment original;
    CHECK(environment_snapshot(&original) == 0, "save caller state");
    CHECK(environment_configure(0u) == 0, "reference neutral state");
    Case test;
    memset(&test, 0, sizeof(test));
    char *source = NULL;
    CHECK(load_source(argv[1], &source, &test.length) == 0, "sealed source load");
    test.source = source;
    test.expected = strcmp(argv[2], "0") == 0 ?
        DOE_COMPILER_OK : DOE_COMPILER_SEMANTIC_ERROR;
    test.legacy_expected = strcmp(argv[3], "0") == 0 ? DOE_COMPILER_OK :
        (strcmp(argv[3], "3") == 0 ? DOE_COMPILER_UNSUPPORTED :
                                   DOE_COMPILER_SEMANTIC_ERROR);
    test.projection_name = argv[4];
    CHECK(prepare_case(&test) == 0, "prepare invariant reference");
    if (test.expected == DOE_COMPILER_OK) {
        CHECK(save_words(argv[5], 4u, test.reference.o.words,
                         test.counts.words) == 0, "typed target artifact");
    }
    Client client;
    Bulk outputs;
    CHECK(client_new(&client, &generous) == 0, "typed independent workspace");
    CHECK(bulk_new(&outputs, &test.counts) == 0, "typed exact output spans");
    for (unsigned profile = 0u; profile < 32u; ++profile) {
        CHECK(environment_configure(profile) == 0, "typed FP profile");
        for (size_t i = 0u; i < COUNT(modes); ++i) {
            CHECK(typed_call(&test, &client, &outputs, modes[i], 0) == 0,
                  "typed environment/mode matrix");
            if (test.expected == DOE_COMPILER_OK && !(modes[i] & 4u)) {
                CHECK(typed_call(&test, &client, &outputs, modes[i], 1) == 0,
                      "typed output failure restores caller state");
            }
        }
        printf("PROGRESS typed profile=%u/32 expected=%d modes=6\n",
               profile + 1u, (int)test.expected);
    }
    CHECK(environment_configure(0u) == 0, "reset harness environment");
    bulk_free(&outputs);
    client_free(&client);
    CHECK(legacy_matrix(&test, argv[5]) == 0, "all adapter matrix");
    if (argc == 7) {
        pthread_t ids[8];
        Thread threads[8];
        for (unsigned i = 0u; i < 8u; ++i) {
            threads[i] = (Thread){&test, i * 3u, 0};
            CHECK(pthread_create(&ids[i], NULL, thread_main, &threads[i]) == 0,
                  "concurrent independent workspace creation");
        }
        for (unsigned i = 0u; i < 8u; ++i) {
            CHECK(pthread_join(ids[i], NULL) == 0 && !threads[i].failed,
                  "concurrent distinct workspace and caller FP state");
        }
    }
    bulk_free(&test.reference);
    free(source);
    CHECK(atomic_load(&native_allocations) == 0u, "no library allocation/free");
    CHECK(fesetenv(&original.raw) == 0, "restore original caller state");
    puts("PASS 32 FP profiles, all selected output masks, four adapters; target semantics require independent consumption");
    return 0;
}
