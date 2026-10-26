/* Versioned C workflow guide; resources and recorded journal are caller-owned.
 */
#include <astroattitude/astroattitude.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *read_file(const char *name, uint64_t *size) {
  FILE *f = fopen(name, "rb");
  if (!f)
    return NULL;
  if (fseek(f, 0, SEEK_END)) {
    fclose(f);
    return NULL;
  }
  long n = ftell(f);
  if (n < 0 || n > 134217728) {
    fclose(f);
    return NULL;
  }
  rewind(f);
  unsigned char *b = malloc((size_t)n + 1);
  if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) {
    free(b);
    fclose(f);
    return NULL;
  }
  fclose(f);
  *size = (uint64_t)n;
  return b;
}
static void show(aa_message *m) {
  uint64_t n = 0;
  const uint8_t *b = aa_message_bytes_v1(m, &n);
  if (b)
    fwrite(b, 1, (size_t)n, stdout);
  putchar('\n');
  aa_message_destroy_v1(m);
}
int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr,
            "Usage: c_calibration catalog.json astrometry.json journal.json\n");
    return 2;
  }
  uint64_t cn = 0, an = 0, jn = 0;
  unsigned char *c = read_file(argv[1], &cn), *a = read_file(argv[2], &an),
                *j = read_file(argv[3], &jn);
  if (!c || !a || !j) {
    free(c);
    free(a);
    free(j);
    return 2;
  }
  aa_calibration_session *session = NULL;
  aa_message *out = NULL;
  int status =
      aa_calibration_restore_v1(c, cn, a, an, j, jn, NULL, &session, &out);
  free(c);
  free(a);
  free(j);
  if (status) {
    show(out);
    return status;
  }
  const char *request = "{\"schema\":\"astroattitude.calibration-call/"
                        "1\",\"operation\":\"next_action\",\"value\":null}";
  aa_calibration_artifact *artifact = NULL;
  status = aa_calibration_call_v1(session, (const uint8_t *)request,
                                  strlen(request), NULL, NULL, &artifact, &out);
  show(out);
  aa_calibration_artifact_destroy_v1(artifact);
  aa_calibration_destroy_v1(session);
  puts(
      "Recorded-data guide only. Frozen validation requires future exposures.");
  return status;
}
