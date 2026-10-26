#include <astroattitude/astroattitude.h>
#include <stdio.h>
#include <stdlib.h>
struct buffer {
  unsigned char *data;
  uint64_t size;
};
static struct buffer read_file(const char *name) {
  struct buffer b = {0};
  FILE *f = fopen(name, "rb");
  if (!f)
    return b;
  if (fseek(f, 0, SEEK_END)) {
    fclose(f);
    return b;
  }
  long n = ftell(f);
  if (n <= 0 || n > 16777216) {
    fclose(f);
    return b;
  }
  rewind(f);
  b.data = malloc((size_t)n);
  if (b.data && fread(b.data, 1, (size_t)n, f) == (size_t)n)
    b.size = (uint64_t)n;
  fclose(f);
  return b;
}
int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "Usage: aa_c_example catalog.json astrometry.json "
                    "rays-or-pixels.json\n");
    return 2;
  }
  struct buffer c = read_file(argv[1]), d = read_file(argv[2]),
                r = read_file(argv[3]);
  aa_engine *engine = NULL;
  aa_message *message = NULL;
  int status =
      aa_engine_create_v1(c.data, c.size, d.data, d.size, &engine, &message);
  if (status == 0)
    status = aa_solve_v1(engine, r.data, r.size, NULL, &message);
  if (message) {
    uint64_t n;
    const uint8_t *p = aa_message_bytes_v1(message, &n);
    fwrite(p, 1, (size_t)n, stdout);
    putchar('\n');
  }
  aa_message_destroy_v1(message);
  aa_engine_destroy_v1(engine);
  free(c.data);
  free(d.data);
  free(r.data);
  return status;
}
