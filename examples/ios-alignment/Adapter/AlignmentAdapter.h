#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Bounded example-only detector. Input is unrotated 8-bit luma, <= 1.3 MP.
// Output: eight doubles per tag: ordered x,y corner coordinates; IDs 0..8.
int aa_demo_tags(const uint8_t *pixels, int width, int height, int stride,
                 int *ids, double *corners, int capacity);
// Convert one Hipparcos astrometric record from its original epoch to J2000.
int aa_demo_propagate(const double *input7, double *output6);
#ifdef __cplusplus
}
#endif
