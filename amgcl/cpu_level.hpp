#ifndef AMGCL_EIGEN_LEVEL_HPP
#define AMGCL_EIGEN_LEVEL_HPP

#include <vector>
#include <memory>
#include <type_traits>
#include <amgcl/spmat.hpp>

namespace amg {
namespace level {

// CPU-based AMG hierarchy.
template <typename value_t, typename index_t>
class cpu {
    public:
        typedef sparse::matrix<value_t, index_t> matrix;

        // Construct complete multigrid level from system matrix (a),
        // prolongation (p) and restriction (r) operators.
        // The matrices are moved into the local members.
        cpu(matrix &&a, matrix &&p, matrix &&r, bool has_parent = true)
            : A(std::move(a)), P(std::move(p)), R(std::move(r))
        {
            if (has_parent) {
                u.resize(A.rows);
                f.resize(A.rows);
            }

            t.resize(A.rows);
        }

        // Construct the coarsest hierarchy level from system matrix (a) and
        // its inverse (ai).
        cpu(matrix &&a, matrix &&ai)
            : A(std::move(a)), Ai(std::move(ai)), u(A.rows), f(A.rows), t(A.rows)
        { }

        // Perform one relaxation (smoothing) step.
        template <class vector1, class vector2>
        void relax(const vector1 &rhs, vector2 &x) {
            const index_t n = A.rows;

#pragma omp parallel for schedule(dynamic, 1024)
            for(index_t i = 0; i < n; ++i) {
                value_t temp = rhs[i];
                value_t diag = 1;

                for(index_t j = A.row[i], e = A.row[i + 1]; j < e; ++j) {
                    index_t c = A.col[j];
                    value_t v = A.val[j];

                    temp -= v * x[c];

                    if (c == i) diag = v;
                }

                t[i] = x[i] + 0.72 * (temp / diag);
            }

            vector_copy(t, x);
        }

        // Compute residual value.
        template <class vector1, class vector2>
        value_t resid(const vector1 &rhs, vector2 &x) const {
            const index_t n = A.rows;
            value_t norm = 0;

#pragma omp parallel for reduction(+:norm) schedule(dynamic, 1024)
            for(index_t i = 0; i < n; ++i) {
                value_t temp = rhs[i];

                for(index_t j = A.row[i], e = A.row[i + 1]; j < e; ++j)
                    temp -= A.val[j] * x[A.col[j]];

                norm += temp * temp;
            }

            return sqrt(norm);
        }

        // Perform one V-cycle. Coarser levels are cycled recursively. The
        // coarsest level is solved directly.
        template <class Iterator, class vector1, class vector2>
        static void cycle(Iterator lvl, Iterator end, const amg::params &prm,
                const vector1 &rhs, vector2 &x)
        {
            const index_t n = lvl->A.rows;
            Iterator nxt = lvl; ++nxt;

            if (nxt != end) {
                const index_t nc = nxt->A.rows;

                for(unsigned j = 0; j < prm.ncycle; ++j) {
                    for(unsigned i = 0; i < prm.npre; ++i) lvl->relax(rhs, x);

                    //lvl->t = rhs - lvl->A * x;
#pragma omp parallel for schedule(dynamic, 1024)
                    for(index_t i = 0; i < n; ++i) {
                        value_t temp = rhs[i];

                        for(index_t j = lvl->A.row[i], e = lvl->A.row[i + 1]; j < e; ++j)
                            temp -= lvl->A.val[j] * x[lvl->A.col[j]];

                        lvl->t[i] = temp;
                    }

                    //nxt->f = lvl->R * lvl->t;
#pragma omp parallel for schedule(dynamic, 1024)
                    for(index_t i = 0; i < nc; ++i) {
                        value_t temp = 0;

                        for(index_t j = lvl->R.row[i], e = lvl->R.row[i + 1]; j < e; ++j)
                            temp += lvl->R.val[j] * lvl->t[lvl->R.col[j]];

                        nxt->f[i] = temp;
                    }

                    std::fill(nxt->u.begin(), nxt->u.end(), static_cast<value_t>(0));

                    cycle(nxt, end, prm, nxt->f, nxt->u);

                    //x += lvl->P * nxt->u;
#pragma omp parallel for schedule(dynamic, 1024)
                    for(index_t i = 0; i < n; ++i) {
                        value_t temp = 0;

                        for(index_t j = lvl->P.row[i], e = lvl->P.row[i + 1]; j < e; ++j)
                            temp += lvl->P.val[j] * nxt->u[lvl->P.col[j]];

                        x[i] += temp;
                    }

                    for(unsigned i = 0; i < prm.npost; ++i) lvl->relax(rhs, x);
                }
            } else {
                for(index_t i = 0; i < n; ++i) {
                    value_t temp = 0;
                    for(index_t j = lvl->Ai.row[i], e = lvl->Ai.row[i + 1]; j < e; ++j)
                        temp += lvl->Ai.val[j] * rhs[lvl->Ai.col[j]];
                    x[i] = temp;
                }
            }
        }
    private:
        matrix A;
        matrix P;
        matrix R;

        matrix Ai;

        std::vector<value_t> u;
        std::vector<value_t> f;
        std::vector<value_t> t;

        template <class U, class V>
        typename std::enable_if<std::is_same<U, V>::value, void>::type
        vector_copy(U &u, V &v) {
            std::swap(u, v);
        }

        template <class U, class V>
        typename std::enable_if<!std::is_same<U, V>::value, void>::type
        vector_copy(U &u, V &v) {
            std::copy(u.begin(), u.end(), &v[0]);
        }
};

} // namespace level
} // namespace amg

#endif
