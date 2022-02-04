#include <complex>
#include <cstdlib>
#include <unistd.h>
#include <vector>
#include <mkl_cdft.h>

void fft_complex(std::vector<std::complex<double>>& in) {
  DFTI_DESCRIPTOR_HANDLE descriptor;
  MKL_LONG status;
  // Computing FFTs with MKL is a 5-step process
  // 1. Allocate a fresh descriptor - if MPI is desired, the DM variant of
  //    these functions should be used (with the appropriate additional
  //    parameters), such as DftiCreateDescriptorDM.
  status = DftiCreateDescriptor(&descriptor, DFTI_DOUBLE, DFTI_COMPLEX,
    1, in.size());
  // 2. Adjust the descriptor as needed. Here, we do an in-place FFT
  //    (which is the default, so this function doesn't actually do anything).
  status = DftiSetValue(descriptor, DFTI_PLACEMENT, DFTI_INPLACE);
  // 3. Commit the descriptor. Once committed, it cannot be changed.
  status = DftiCommitDescriptor(descriptor);
  // 4. Compute either the forward or backward (inverse) FFT.
  status = DftiComputeForward(descriptor, in.data());
  // 5. Deallocate the descriptor.
  status = DftiFreeDescriptor(&descriptor);

  // The status returned after each step should be 0 on success.
}

void print_all(std::vector<std::complex<double>>& in) {
  for (int i = 0; i < in.size(); ++i) {
    printf("%d. %#.12f + %#.12fi\n", i + 1, real(in[i]), imag(in[i]));
  }
}

int main(int argc, char *argv) {

  std::vector<std::complex<double>> in(10);

  // Seeded random generator yields a consistent vector of complex doubles
  // across runs.
  std::srand(1);
  for (auto &z : in) {
    z = std::complex<double>((double) std::rand() / RAND_MAX,
          (double) std::rand() / RAND_MAX);
  }

  printf("Iteration 0:\n");
  print_all(in);

  for (int i = 0; i < 30; ++i) {
    // We do 30 runs, with a 1s sleep between each of them.

    // One iteration of FFT.
    printf("Iteration %d:\n", i + 1);
    fft_complex(in);
    print_all(in);

    sleep(1);
  }

  return 0;

}
