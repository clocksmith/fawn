/* SPDX-License-Identifier: MIT */
#include "module.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { INPUT_LIMIT = 16 * 1024 * 1024 };

static char *read_input(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    char *bytes = malloc(INPUT_LIMIT + 1);
    if (!bytes) { fclose(file); return NULL; }
    *length = fread(bytes, 1, INPUT_LIMIT + 1, file);
    bool ok = !ferror(file) && *length <= INPUT_LIMIT;
    if (fclose(file)) ok = false;
    if (!ok) { free(bytes); return NULL; }
    return bytes;
}

bool module_write_file(const char *path, const char *bytes, size_t length)
{
    char *parent = strdup(path);
    if (!parent) return false;
    bool ok = true;
    for (char *cursor = parent + (*parent == '/' ? 1 : 0); *cursor; cursor++) {
        if (*cursor != '/') continue;
        *cursor = 0;
        if (mkdir(parent, 0777) && errno != EEXIST) ok = false;
        *cursor = '/';
        if (!ok) break;
    }
    free(parent);
    FILE *file = ok ? fopen(path, "wb") : NULL;
    if (!file) return false;
    ok = fwrite(bytes, 1, length, file) == length && fputc('\n', file) != EOF;
    if (fclose(file)) ok = false;
    return ok;
}

static bool normalize(const doe_json_value *value, const module_shape *shape, doe_json_document *document,
    char **bytes, size_t *length)
{
    FILE *stream = open_memstream(bytes, length);
    if (!stream) return false;
    bool ok = module_normalize(stream, value, shape);
    if (fclose(stream)) ok = false;
    doe_json_error error;
    return ok && doe_json_parse(*bytes, *length, document, &error) == DOE_JSON_OK;
}

static bool policy_hash(const char *text, size_t length, const char *path, char digest[65])
{
    if (!path) { module_hash(text, length, digest); return true; }
    char *bytes = NULL;
    size_t size = 0;
    FILE *stream = open_memstream(&bytes, &size);
    if (!stream) return false;
    bool ok = fputs("{\"policyRegistryPath\":", stream) >= 0 && module_string(stream, path, strlen(path)) &&
        fputs(",\"registry\":", stream) >= 0 && fwrite(text, 1, length, stream) == length && fputc('}', stream) != EOF;
    if (fclose(stream)) ok = false;
    if (ok) module_hash(bytes, size, digest);
    free(bytes);
    return ok;
}

static bool execute(const module_contract *contract, const doe_json_value *request, const doe_json_value *policy,
    const char *policy_text, size_t policy_size, const char *policy_path, const char request_hash[65],
    const char policy_digest[65], char *error, size_t error_capacity)
{
    if (!module_equal(module_get(request, "moduleId"), contract->id) || !module_equal(module_get(request, "artifactKind"), "request") ||
        (contract->kind != MODULE_NUMERIC && (!module_equal(module_get(policy, "moduleId"), contract->id) ||
            !module_equal(module_get(policy, "artifactKind"), "policy")))) {
        snprintf(error, error_capacity, "moduleId/artifactKind mismatch");
        return false;
    }
    char *bytes = NULL;
    size_t length = 0;
    FILE *stream = open_memstream(&bytes, &length);
    if (!stream) return false;
    bool ok = fprintf(stream, "{\"schemaVersion\":1,\"moduleId\":\"%s\",\"artifactKind\":\"result\"", contract->id) >= 0;
    if (ok) ok = contract->kind == MODULE_NUMERIC ?
        module_numeric(stream, request, policy_text, policy_size, policy_path, error, error_capacity) :
        module_model(stream, contract, request, policy, request_hash);
    ok = fputc('}', stream) != EOF && ok;
    if (fclose(stream)) ok = false;
    if (ok) {
        char result_hash[65];
        module_hash(bytes, length, result_hash);
        ok = fwrite(bytes, 1, length - 1, stdout) == length - 1 && fprintf(stdout,
            ",\"traceLink\":{\"moduleIdentity\":\"%s\",\"requestHash\":\"%s\",\"policyHash\":\"%s\",\"resultHash\":\"%s\"}}\n",
            contract->id, request_hash, policy_digest, result_hash) >= 0 && fflush(stdout) == 0;
    }
    free(bytes);
    return ok;
}

int main(int argc, char **argv)
{
    const char *module_id = NULL, *request_path = NULL, *policy_path = NULL;
    for (int i = 1; i < argc; i++) {
        const char **option = !strcmp(argv[i], "--module") ? &module_id : !strcmp(argv[i], "--request") ? &request_path :
            !strcmp(argv[i], "--policy") ? &policy_path : NULL;
        if (!option || *option || ++i >= argc) {
            fputs("doe-c-module-runner: expected --module ID --request FILE --policy FILE\n", stderr);
            return 2;
        }
        *option = argv[i];
    }
    const module_contract *contract = NULL;
    for (size_t i = 0; module_id && i < 6; i++)
        if (!strcmp(module_id, module_contracts[i].id)) contract = &module_contracts[i];
    if (!contract || !request_path || !policy_path) {
        fputs("doe-c-module-runner: missing request/policy or unsupported module\n", stderr);
        return 2;
    }
    size_t request_size = 0, policy_size = 0;
    char *request_text = read_input(request_path, &request_size), *policy_text = read_input(policy_path, &policy_size);
    doe_json_document request = {0}, policy = {0};
    doe_json_error parse_error;
    char error[256] = "input read/parse, arithmetic, allocation or output failure";
    bool ok = request_text && policy_text && doe_json_parse(request_text, request_size, &request, &parse_error) == DOE_JSON_OK &&
        doe_json_parse(policy_text, policy_size, &policy, &parse_error) == DOE_JSON_OK;
    doe_json_document normalized_request = {0}, normalized_policy = {0};
    char *request_json = NULL, *policy_json = NULL;
    size_t request_length = 0, policy_length = 0;
    char request_digest[65], policy_digest[65];
    if (ok) {
        ok = normalize(request.root, contract->request, &normalized_request, &request_json, &request_length) &&
            normalize(policy.root, contract->policy, &normalized_policy, &policy_json, &policy_length);
        if (!ok) snprintf(error, sizeof(error), "request/policy has unknown, missing or incorrectly typed fields");
    }
    if (ok) {
        module_hash(request_json, request_length, request_digest);
        ok = policy_hash(policy_json, policy_length, contract->kind == MODULE_NUMERIC ? policy_path : NULL, policy_digest);
    }
    if (ok) ok = execute(contract, normalized_request.root, normalized_policy.root, policy_json, policy_length,
        policy_path, request_digest, policy_digest, error, sizeof(error));
    if (!ok) fprintf(stderr, "doe-c-module-runner: %s\n", error);
    doe_json_free(&normalized_request);
    doe_json_free(&normalized_policy);
    free(request_json);
    free(policy_json);
    doe_json_free(&request);
    doe_json_free(&policy);
    free(request_text);
    free(policy_text);
    return ok ? 0 : 1;
}
