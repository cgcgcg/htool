#ifndef HTOOL_GENERATOR_HPP
#define HTOOL_GENERATOR_HPP

#include "vector.hpp"
#include <cassert>
#include <iterator>

namespace htool {
/**
 * @brief Define the interface for the user to give Htool a function generating dense sub-blocks of the global matrix the user wants to compress. This is done by the user implementing VirtualGenerator::copy_submatrix.
 * 
 * @tparam T Precision of the coefficients (float, double,...).
 */
template <typename T>
class VirtualGenerator {
  protected:
    // Data members
    int nr;
    int nc;
    int dimension;

  public:
    VirtualGenerator(int nr0, int nc0, int dimension0 = 1) : nr(nr0), nc(nc0), dimension(dimension0) {}

    /**
     * @brief Generate a dense sub-block of the global matrix the user wants to compress. Note that sub-blocks queried by Htool are potentially non-contiguous in the user's numbering.
     * 
     * @param[in] M specifies the number of columns of the queried block
     * @param[in] N specifies the number of rows of the queried block
     * @param[in] rows is an integer array of size \f$M\f$. It specifies the queried columns in the user's numbering
     * @param[in] cols is an integer array of size \f$N\f$. It specifies the queried rows in the user's numbering
     * @param[out] ptr is a \p T precision array of size \f$ M\times N\f$. Htool already allocates and desallocates it internally, so it should **not** be allocated by the user. 
     */
    virtual void copy_submatrix(int M, int N, const int *const rows, const int *const cols, T *ptr) const = 0;

    /**
     * @brief Get the global number of rows.
     * 
     * @return int 
     */
    int nb_rows() const { return nr; }

    /**
     * @brief Get the global number of columns.
     * 
     * @return int 
     */
    int nb_cols() const { return nc; }

    /**
     * @brief Get the dimension of the output coefficients.
     * 
     * @return int 
     */
    int get_dimension() const { return dimension; }

    virtual ~VirtualGenerator(){};
};

} // namespace htool

#endif
