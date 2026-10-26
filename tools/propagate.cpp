#include <erfa.h>
#include <iomanip>
#include <iostream>
int main() {
  double ra, dec, pmr, pmd, px, rv, epoch;
  std::cout << std::setprecision(17);
  while (std::cin >> ra >> dec >> pmr >> pmd >> px >> rv >> epoch) {
    double a, b, c, d, e, f;
    int status =
        eraPmsafe(ra, dec, pmr, pmd, px, rv, 2451545., epoch - 2451545.,
                  2451545., 0, &a, &b, &c, &d, &e, &f);
    std::cout << status << ' ' << a << ' ' << b << ' ' << c << ' ' << d << ' '
              << e << ' ' << f << '\n';
  }
  return std::cin.eof() ? 0 : 1;
}
