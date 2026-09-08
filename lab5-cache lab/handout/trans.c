/*
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */
#include "cachelab.h"
#include <stdio.h>

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/*
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded.
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N]) {
  // 8 for storage and 4 for loop temp
  int a, b, c, d, e, f, g, h;
  int i, j, k, l;

  if (M == 32) {
    for (i = 0; i < N; i += 8) {
      for (j = 0; j < M; j += 8) {
        for (k = 0; k < 8; ++k) {
          a = A[i + k][j];
          b = A[i + k][j + 1];
          c = A[i + k][j + 2];
          d = A[i + k][j + 3];
          e = A[i + k][j + 4];
          f = A[i + k][j + 5];
          g = A[i + k][j + 6];
          h = A[i + k][j + 7];
          B[j + k][i] = a;
          B[j + k][i + 1] = b;
          B[j + k][i + 2] = c;
          B[j + k][i + 3] = d;
          B[j + k][i + 4] = e;
          B[j + k][i + 5] = f;
          B[j + k][i + 6] = g;
          B[j + k][i + 7] = h;
        }
        for (k = 0; k < 8; ++k) {
          for (l = 0; l < k; ++l) {
            a = B[j + k][i + l];
            B[j + k][i + l] = B[j + l][i + k];
            B[j + l][i + k] = a;
          }
        }
      }
    }
  } else if (M == 64) {
    for (l = 0; l < N; l += 8) {
      // process diagonal blocks first

      // k: j-index of target block (block-shifting)
      // more specifically, use the upper half of [l, k] to transpose [l, l]
      // block the target block is the one that will be used immediately after
      // the diagonal processing
      if (l == 0)
        k = 8;
      else
        k = 0;

      // move the lower 4x8 blocks from A to B, with block-shifting to the
      // target block
      for (i = l; i < l + 4; ++i) {
        a = A[i + 4][l + 0];
        b = A[i + 4][l + 1];
        c = A[i + 4][l + 2];
        d = A[i + 4][l + 3];
        e = A[i + 4][l + 4];
        f = A[i + 4][l + 5];
        g = A[i + 4][l + 6];
        h = A[i + 4][l + 7];

        B[i][k + 0] = a;
        B[i][k + 1] = b;
        B[i][k + 2] = c;
        B[i][k + 3] = d;
        B[i][k + 4] = e;
        B[i][k + 5] = f;
        B[i][k + 6] = g;
        B[i][k + 7] = h;
      }

      // taking transpose of lower-left and lower-right 4x4 within themselves
      // respectively
      for (i = 0; i < 4; ++i) {
        for (j = i + 1; j < 4; ++j) {
          a = B[l + i][k + j];
          B[l + i][k + j] = B[l + j][k + i];
          B[l + j][k + i] = a;

          a = B[l + i][k + j + 4];
          B[l + i][k + j + 4] = B[l + j][k + i + 4];
          B[l + j][k + i + 4] = a;
        }
      }

      // moving the upper 4x8 blocks from A to B
      for (i = l; i < l + 4; ++i) {
        a = A[i][l + 0];
        b = A[i][l + 1];
        c = A[i][l + 2];
        d = A[i][l + 3];
        e = A[i][l + 4];
        f = A[i][l + 5];
        g = A[i][l + 6];
        h = A[i][l + 7];

        B[i][l + 0] = a;
        B[i][l + 1] = b;
        B[i][l + 2] = c;
        B[i][l + 3] = d;
        B[i][l + 4] = e;
        B[i][l + 5] = f;
        B[i][l + 6] = g;
        B[i][l + 7] = h;
      }

      // taking transpose of upper-left and upper-right 4x4 within themselves
      // respectively
      for (i = l; i < l + 4; ++i) {
        for (j = i + 1; j < l + 4; ++j) {
          a = B[i][j];
          B[i][j] = B[j][i];
          B[j][i] = a;

          a = B[i][j + 4];
          B[i][j + 4] = B[j][i + 4];
          B[j][i + 4] = a;
        }
      }

      // swaping the lower-left and upper-right
      for (i = 0; i < 4; ++i) {
        a = B[l + i][l + 4];
        b = B[l + i][l + 5];
        c = B[l + i][l + 6];
        d = B[l + i][l + 7];

        B[l + i][l + 4] = B[l + i][k + 0];
        B[l + i][l + 5] = B[l + i][k + 1];
        B[l + i][l + 6] = B[l + i][k + 2];
        B[l + i][l + 7] = B[l + i][k + 3];

        B[l + i][k + 0] = a;
        B[l + i][k + 1] = b;
        B[l + i][k + 2] = c;
        B[l + i][k + 3] = d;
      }

      // filling the original lower 4x8 from the block-shifting block
      for (i = 0; i < 4; ++i) {
        B[l + i + 4][l + 0] = B[l + i][k + 0];
        B[l + i + 4][l + 1] = B[l + i][k + 1];
        B[l + i + 4][l + 2] = B[l + i][k + 2];
        B[l + i + 4][l + 3] = B[l + i][k + 3];
        B[l + i + 4][l + 4] = B[l + i][k + 4];
        B[l + i + 4][l + 5] = B[l + i][k + 5];
        B[l + i + 4][l + 6] = B[l + i][k + 6];
        B[l + i + 4][l + 7] = B[l + i][k + 7];
      }

      // processing off-diagonal blocks
      for (k = 0; k < M; k += 8) {
        if (k == l) {
          // skip diagonal blocks
          continue;
        } else {
          // taking transpose of upper-left 4x4 and upper-right 4x4 within
          // themselves respectively
          for (i = k; i < k + 4; ++i) {
            a = A[i][l + 0];
            b = A[i][l + 1];
            c = A[i][l + 2];
            d = A[i][l + 3];
            e = A[i][l + 4];
            f = A[i][l + 5];
            g = A[i][l + 6];
            h = A[i][l + 7];

            B[l + 0][i] = a;
            B[l + 1][i] = b;
            B[l + 2][i] = c;
            B[l + 3][i] = d;
            B[l + 0][i + 4] = e;
            B[l + 1][i + 4] = f;
            B[l + 2][i + 4] = g;
            B[l + 3][i + 4] = h;
          }

          // taking transpose of lower-left 4x4 and store to upper-right 4x4,
          // and move upper-right 4x4 to lower-left 4x4
          for (j = l; j < l + 4; ++j) {
            a = A[k + 4][j];
            b = A[k + 5][j];
            c = A[k + 6][j];
            d = A[k + 7][j];
            e = B[j][k + 4];
            f = B[j][k + 5];
            g = B[j][k + 6];
            h = B[j][k + 7];

            B[j][k + 4] = a;
            B[j][k + 5] = b;
            B[j][k + 6] = c;
            B[j][k + 7] = d;
            B[j + 4][k + 0] = e;
            B[j + 4][k + 1] = f;
            B[j + 4][k + 2] = g;
            B[j + 4][k + 3] = h;
          }

          // taking transpose of lower-right 4x4
          for (i = k + 4; i < k + 8; ++i) {
            a = A[i][l + 4];
            b = A[i][l + 5];
            c = A[i][l + 6];
            d = A[i][l + 7];
            B[l + 4][i] = a;
            B[l + 5][i] = b;
            B[l + 6][i] = c;
            B[l + 7][i] = d;
          }
        }
      }
    }
  } else {
    for (i = 0; i < N; i += 16)
      for (j = 0; j < M; j += 16)
        for (k = i; k < i + 16 && k < N; ++k)
          for (l = j; l < j + 16 && l < M; ++l)
            B[l][k] = A[k][l];
  }
}

char transpose_def_desc[] = "Transpose default";
void transpose_def(int M, int N, int A[N][M], int B[M][N]) {
  int a, b, c, d, e, f, g, h;
  int i, j, k;

  if (M == 32 || M == 64) {
    for (i = 0; i < N; i += 8) {
      for (j = 0; j < M; j += 8) {
        for (k = 0; k < 8; ++k) {
          a = A[i + k][j];
          b = A[i + k][j + 1];
          c = A[i + k][j + 2];
          d = A[i + k][j + 3];
          e = A[i + k][j + 4];
          f = A[i + k][j + 5];
          g = A[i + k][j + 6];
          h = A[i + k][j + 7];
          B[j][i + k] = a;
          B[j + 1][i + k] = b;
          B[j + 2][i + k] = c;
          B[j + 3][i + k] = d;
          B[j + 4][i + k] = e;
          B[j + 5][i + k] = f;
          B[j + 6][i + k] = g;
          B[j + 7][i + k] = h;
        }
      }
    }
  }
}

/*
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started.
 */

/*
 * trans - A simple baseline transpose function, not optimized for the cache.
 */
char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N]) {
  int i, j, tmp;

  for (i = 0; i < N; i++) {
    for (j = 0; j < M; j++) {
      tmp = A[i][j];
      B[j][i] = tmp;
    }
  }
}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions() {
  /* Register your solution function */
  registerTransFunction(transpose_submit, transpose_submit_desc);
  registerTransFunction(transpose_def, transpose_def_desc);

  /* Register any additional transpose functions */
  registerTransFunction(trans, trans_desc);
}

/*
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N]) {
  int i, j;

  for (i = 0; i < N; i++) {
    for (j = 0; j < M; ++j) {
      if (A[i][j] != B[j][i]) {
        return 0;
      }
    }
  }
  return 1;
}
