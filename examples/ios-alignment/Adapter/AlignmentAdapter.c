#include "AlignmentAdapter.h"
#include "apriltag.h"
#include "erfa.h"
#include "tag36h11.h"
#include <math.h>
int aa_demo_tags(const uint8_t *pixels, int width, int height, int stride,
                 int *ids, double *corners, int capacity) {
  if (!pixels || !ids || !corners || width <= 0 || height <= 0 ||
      width > 1920 || height > 1920 || width * height > 1300000 ||
      stride < width || capacity < 1 || capacity > 32)
    return -1;
  apriltag_family_t *family = tag36h11_create();
  apriltag_detector_t *detector = apriltag_detector_create();
  detector->nthreads = 1;
  detector->quad_decimate = 1.5;
  detector->refine_edges = 1;
  apriltag_detector_add_family_bits(detector, family, 0);
  image_u8_t image = {width, height, stride, (uint8_t *)pixels};
  zarray_t *found = apriltag_detector_detect(detector, &image);
  int count = 0;
  for (int id = 0; id < 9 && count < capacity; ++id) {
    for (int i = 0; i < zarray_size(found) && count < capacity; ++i) {
      apriltag_detection_t *d;
      zarray_get(found, i, &d);
      if (d->id != id || d->hamming != 0 || d->decision_margin < 30)
        continue;
      ids[count] = id;
      for (int k = 0; k < 4; ++k) {
        corners[8 * count + 2 * k] = d->p[k][0];
        corners[8 * count + 2 * k + 1] = d->p[k][1];
      }
      ++count;
    }
  }
  apriltag_detections_destroy(found);
  apriltag_detector_destroy(detector);
  tag36h11_destroy(family);
  return count;
}
int aa_demo_propagate(const double *in, double *out) {
  if (!in || !out)
    return -1;
  for (int i = 0; i < 7; ++i)
    if (!isfinite(in[i]))
      return -1;
  return eraPmsafe(in[0], in[1], in[2], in[3], in[4], in[5], in[6], 0, 2451545,
                   0, out, out + 1, out + 2, out + 3, out + 4, out + 5);
}
