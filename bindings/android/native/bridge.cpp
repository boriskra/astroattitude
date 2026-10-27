#include <astroattitude/astroattitude.h>
#include <jni.h>
#include <memory>
#include <stdexcept>
#include <vector>
namespace {
using Message = std::unique_ptr<aa_message, decltype(&aa_message_destroy_v1)>;
jbyteArray message(JNIEnv *env, aa_message *p) {
  uint64_t n = 0;
  auto bytes = aa_message_bytes_v1(p, &n);
  if (!bytes || n > 134217728)
    throw std::runtime_error("Invalid response");
  auto a = env->NewByteArray(static_cast<jsize>(n));
  if (a)
    env->SetByteArrayRegion(a, 0, static_cast<jsize>(n),
                            reinterpret_cast<const jbyte *>(bytes));
  return a;
}
void failure(JNIEnv *env, int status, aa_message *p = nullptr) {
  if (env->ExceptionCheck())
    return;
  auto klass = env->FindClass("org/astroattitude/NativeFailure");
  if (!klass)
    return;
  auto ctor = env->GetMethodID(klass, "<init>", "(I[B)V");
  if (!ctor)
    return;
  auto payload = p ? message(env, p) : nullptr;
  if (env->ExceptionCheck())
    return;
  auto ex = env->NewObject(klass, ctor, status, payload);
  if (ex)
    env->Throw(static_cast<jthrowable>(ex));
}
std::vector<uint8_t> bytes(JNIEnv *env, jbyteArray a, size_t max) {
  if (!a)
    throw std::invalid_argument("Null bytes");
  auto n = env->GetArrayLength(a);
  if (size_t(n) > max)
    throw std::invalid_argument("Byte limit");
  std::vector<uint8_t> v(n);
  env->GetByteArrayRegion(a, 0, n, reinterpret_cast<jbyte *>(v.data()));
  return v;
}
template <class F> jlong guarded(JNIEnv *env, F f) {
  try {
    return f();
  } catch (const std::bad_alloc &) {
    failure(env, 3);
  } catch (const std::invalid_argument &) {
    failure(env, 2);
  } catch (...) {
    failure(env, 4);
  }
  return 0;
}
} // namespace
extern "C" {
JNIEXPORT jlong JNICALL Java_org_astroattitude_Native_create(JNIEnv *env,
                                                             jobject,
                                                             jbyteArray c,
                                                             jbyteArray d) {
  return guarded(env, [&]() -> jlong {
    auto catalog = bytes(env, c, 16777216), data = bytes(env, d, 4194304);
    if (env->ExceptionCheck())
      return 0;
    aa_engine *engine = nullptr;
    aa_message *error = nullptr;
    int status = aa_engine_create_v1(catalog.data(), catalog.size(),
                                     data.data(), data.size(), &engine, &error);
    Message holder(error, aa_message_destroy_v1);
    if (status) {
      failure(env, status, error);
      return 0;
    }
    return reinterpret_cast<jlong>(engine);
  });
}
JNIEXPORT jbyteArray JNICALL Java_org_astroattitude_Native_solve(
    JNIEnv *env, jobject, jlong handle, jbyteArray request, jlong token) {
  jbyteArray result = nullptr;
  guarded(env, [&]() -> jlong {
    auto input = bytes(env, request, 262144);
    if (env->ExceptionCheck())
      return 0;
    aa_message *out = nullptr;
    auto status =
        aa_solve_v1(reinterpret_cast<aa_engine *>(handle), input.data(),
                    input.size(), reinterpret_cast<aa_cancel *>(token), &out);
    Message holder(out, aa_message_destroy_v1);
    if (status > 1) {
      failure(env, status);
      return 0;
    }
    result = message(env, out);
    return 0;
  });
  return result;
}
JNIEXPORT void JNICALL Java_org_astroattitude_Native_destroy(JNIEnv *, jobject,
                                                             jlong h) {
  aa_engine_destroy_v1(reinterpret_cast<aa_engine *>(h));
}
JNIEXPORT jlong JNICALL Java_org_astroattitude_Native_tokenCreate(JNIEnv *env,
                                                                  jobject) {
  aa_cancel *t = nullptr;
  auto status = aa_cancel_create_v1(&t);
  if (status) {
    failure(env, status);
    return 0;
  }
  return reinterpret_cast<jlong>(t);
}
JNIEXPORT void JNICALL Java_org_astroattitude_Native_tokenRequest(JNIEnv *,
                                                                  jobject,
                                                                  jlong t) {
  aa_cancel_request_v1(reinterpret_cast<aa_cancel *>(t));
}
JNIEXPORT void JNICALL Java_org_astroattitude_Native_tokenDestroy(JNIEnv *,
                                                                  jobject,
                                                                  jlong t) {
  aa_cancel_destroy_v1(reinterpret_cast<aa_cancel *>(t));
}
}

extern "C" {
JNIEXPORT jbyteArray JNICALL Java_org_astroattitude_Native_extract(
    JNIEnv *env, jobject, jfloatArray pixels, jbyteArray mask,
    jbyteArray metadata, jlong token) {
  jbyteArray result = nullptr;
  guarded(env, [&]() -> jlong {
    if (!pixels)
      throw std::invalid_argument("Null pixels");
    auto n = env->GetArrayLength(pixels);
    if (n > 24000000)
      throw std::invalid_argument("Sample budget");
    std::vector<float> data(n);
    env->GetFloatArrayRegion(pixels, 0, n, data.data());
    auto m = bytes(env, mask, 24000000), j = bytes(env, metadata, 16384);
    if (env->ExceptionCheck())
      return 0;
    aa_message *out = nullptr;
    auto status =
        aa_extract_v1(data.data(), data.size(), m.data(), m.size(), j.data(),
                      j.size(), reinterpret_cast<aa_cancel *>(token), &out);
    Message holder(out, aa_message_destroy_v1);
    if (status) {
      failure(env, status, out);
      return 0;
    }
    result = message(env, out);
    return 0;
  });
  return result;
}
JNIEXPORT jlong JNICALL Java_org_astroattitude_Native_calibrationCreate(
    JNIEnv *env, jobject, jbyteArray catalog, jbyteArray astro,
    jbyteArray request, jboolean restore, jlong token) {
  return guarded(env, [&]() -> jlong {
    auto c = bytes(env, catalog, 16777216), a = bytes(env, astro, 4194304),
         j = bytes(env, request, restore ? 134217728 : 16384);
    if (env->ExceptionCheck())
      return 0;
    aa_calibration_session *session = nullptr;
    aa_message *out = nullptr;
    auto status =
        restore
            ? aa_calibration_restore_v1(
                  c.data(), c.size(), a.data(), a.size(), j.data(), j.size(),
                  reinterpret_cast<aa_cancel *>(token), &session, &out)
            : aa_calibration_create_v1(c.data(), c.size(), a.data(), a.size(),
                                       j.data(), j.size(), &session, &out);
    Message holder(out, aa_message_destroy_v1);
    if (status) {
      failure(env, status, out);
      return 0;
    }
    return reinterpret_cast<jlong>(session);
  });
}
JNIEXPORT jbyteArray JNICALL Java_org_astroattitude_Native_calibrationCall(
    JNIEnv *env, jobject, jlong session, jbyteArray request, jlong candidate,
    jlong token, jlongArray artifact) {
  jbyteArray result = nullptr;
  guarded(env, [&]() -> jlong {
    if (!artifact || env->GetArrayLength(artifact) != 1)
      throw std::invalid_argument("Artifact output");
    auto j = bytes(env, request, 4194304);
    if (env->ExceptionCheck())
      return 0;
    aa_message *out = nullptr;
    aa_calibration_artifact *object = nullptr;
    auto status = aa_calibration_call_v1(
        reinterpret_cast<aa_calibration_session *>(session), j.data(), j.size(),
        reinterpret_cast<aa_calibration_artifact *>(candidate),
        reinterpret_cast<aa_cancel *>(token), &object, &out);
    Message holder(out, aa_message_destroy_v1);
    std::unique_ptr<aa_calibration_artifact,
                    decltype(&aa_calibration_artifact_destroy_v1)>
        owner(object, aa_calibration_artifact_destroy_v1);
    if (status) {
      failure(env, status, out);
      return 0;
    }
    result = message(env, out);
    if (env->ExceptionCheck() || !result)
      return 0;
    jlong pointer = reinterpret_cast<jlong>(object);
    env->SetLongArrayRegion(artifact, 0, 1, &pointer);
    if (!env->ExceptionCheck())
      owner.release();
    return 0;
  });
  return result;
}
JNIEXPORT void JNICALL
Java_org_astroattitude_Native_calibrationDestroy(JNIEnv *, jobject, jlong p) {
  aa_calibration_destroy_v1(reinterpret_cast<aa_calibration_session *>(p));
}
JNIEXPORT void JNICALL Java_org_astroattitude_Native_artifactDestroy(JNIEnv *,
                                                                     jobject,
                                                                     jlong p) {
  aa_calibration_artifact_destroy_v1(
      reinterpret_cast<aa_calibration_artifact *>(p));
}
JNIEXPORT jbyteArray JNICALL Java_org_astroattitude_Native_alignment(
    JNIEnv *env, jobject, jbyteArray request, jint target, jlong token) {
  jbyteArray result = nullptr;
  guarded(env, [&]() -> jlong {
    auto j = bytes(env, request, target == 1 ? 65536 : 6 * 1048576);
    if (env->ExceptionCheck())
      return 0;
    aa_message *out = nullptr;
    auto status =
        target == 2
            ? aa_rig_alignment_v1(j.data(), j.size(),
                                  reinterpret_cast<aa_cancel *>(token), &out)
        : target == 1
            ? aa_planar_pose_v1(j.data(), j.size(),
                                reinterpret_cast<aa_cancel *>(token), &out)
            : aa_alignment_measure_v1(j.data(), j.size(),
                                      reinterpret_cast<aa_cancel *>(token),
                                      &out);
    Message holder(out, aa_message_destroy_v1);
    if (status) {
      failure(env, status, out);
      return 0;
    }
    result = message(env, out);
    return 0;
  });
  return result;
}
}
