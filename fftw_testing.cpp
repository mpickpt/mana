#include <complex>
#include <cstdlib>
#include <vector>
#include <fftw3.h>
#include <unistd.h>

void print_all(fftw_complex* in, int len) {
  for (int i = 0; i < 20; i++) {
    printf("%2.d. %#.12f + %#.12fi\n", i + 1, in[i][0], in[i][1]);
  }
}

int main(int argc, char *argv) {

  const int len = 200000000;
  fftw_complex in[len];
  fftw_plan p;

  // Seeded random generator yields a consistent vector of doubles across runs.
  std::srand(1);
  for (int i = 0; i < len; i++) {
    in[i][0] = (double) std::rand() / RAND_MAX;
    in[i][1] = (double) std::rand() / RAND_MAX;
  }

  printf("Initial vector:\n");
  print_all(in, len);

  // We need a plan that is executed in a later routine. Here, we set-up an
  // in-place forward transformation. The last argument determines how
  // optimized the transformations are. Since we aim to checkpoint in the
  // middle of a computation, we choose the slower of the two.
  p = fftw_plan_dft_1d(len, in, in, FFTW_FORWARD, FFTW_ESTIMATE);

  for (int i = 0; i < 30; ++i) {
    // We do 30 runs, with a 1s sleep between each of them.

    // One iteration of FFT.
    printf("Starting iteration %d:\n", i + 1);
    fftw_execute(p);
    printf("Finished iteration %d:\n", i + 1);
    print_all(in, len);

    sleep(1);
  }

  fftw_destroy_plan(p);
  return 0;

}
