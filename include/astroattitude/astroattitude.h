#pragma once
#include <stdint.h>
#if defined(_WIN32)
#define AA_PUBLIC __declspec(dllexport)
#else
#define AA_PUBLIC __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct aa_engine aa_engine;
typedef struct aa_message aa_message;
typedef struct aa_cancel aa_cancel;
typedef struct aa_calibration_session aa_calibration_session;
typedef struct aa_calibration_artifact aa_calibration_artifact;

AA_PUBLIC uint32_t aa_abi_version(void); /* Returns 1. */

AA_PUBLIC int32_t aa_engine_create_v1(const uint8_t *catalog, uint64_t catalog_len,
                            const uint8_t *astrometry, uint64_t astrometry_len,
                            aa_engine **out_engine, aa_message **out_error);
AA_PUBLIC void aa_engine_destroy_v1(aa_engine *engine);

AA_PUBLIC int32_t aa_solve_v1(const aa_engine *engine, const uint8_t *request,
                    uint64_t request_len, const aa_cancel *cancel,
                    aa_message **out_response);

AA_PUBLIC const uint8_t *aa_message_bytes_v1(const aa_message *message,
                                   uint64_t *out_len);
AA_PUBLIC void aa_message_destroy_v1(aa_message *message);

AA_PUBLIC int32_t aa_cancel_create_v1(aa_cancel **out_cancel);
AA_PUBLIC void aa_cancel_request_v1(aa_cancel *cancel);
AA_PUBLIC void aa_cancel_destroy_v1(aa_cancel *cancel);

/* Additive experimental workflow API. Buffers are borrowed for the call only.
 * Session calls must be serialized; destruction requires no active calls.
 * Artifacts are immutable. Existing solve_v1 behavior and ABI remain unchanged. */
AA_PUBLIC int32_t aa_extract_v1(const float *pixels, uint64_t sample_count,
    const uint8_t *mask, uint64_t mask_count, const uint8_t *metadata,
    uint64_t metadata_len, const aa_cancel *cancel, aa_message **out_response);
AA_PUBLIC int32_t aa_calibration_create_v1(const uint8_t *catalog, uint64_t catalog_len,
    const uint8_t *astrometry, uint64_t astrometry_len, const uint8_t *configuration,
    uint64_t configuration_len, aa_calibration_session **out_session, aa_message **out_error);
AA_PUBLIC int32_t aa_calibration_restore_v1(const uint8_t *catalog, uint64_t catalog_len,
    const uint8_t *astrometry, uint64_t astrometry_len, const uint8_t *journal,
    uint64_t journal_len, const aa_cancel *cancel, aa_calibration_session **out_session,
    aa_message **out_error);
AA_PUBLIC int32_t aa_calibration_call_v1(aa_calibration_session *session,
    const uint8_t *request, uint64_t request_len, const aa_calibration_artifact *candidate,
    const aa_cancel *cancel, aa_calibration_artifact **out_artifact, aa_message **out_response);
AA_PUBLIC void aa_calibration_destroy_v1(aa_calibration_session *session);
AA_PUBLIC void aa_calibration_artifact_destroy_v1(aa_calibration_artifact *artifact);

#ifdef __cplusplus
}
#endif
