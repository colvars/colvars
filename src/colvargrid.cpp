// -*- c++ -*-

#include "colvarmodule.h"
#include "colvarvalue.h"
#include "colvarparse.h"
#include "colvar.h"
#include "colvarcomp.h"
#include "colvargrid.h"

#include <ctime>

colvar_grid_count::colvar_grid_count()
  : colvar_grid<size_t>()
{
  mult = 1;
}

colvar_grid_count::colvar_grid_count(std::vector<int> const &nx_i,
                                     size_t const &def_count)
  : colvar_grid<size_t>(nx_i, def_count, 1)
{}

colvar_grid_count::colvar_grid_count(std::vector<colvar *>  &colvars,
                                     size_t const &def_count,
                                     bool margin)
  : colvar_grid<size_t>(colvars, def_count, 1, margin)
{}

colvar_grid_scalar::colvar_grid_scalar()
  : colvar_grid<cvm::real>(), samples(NULL), grad(NULL)
{}

colvar_grid_scalar::colvar_grid_scalar(colvar_grid_scalar const &g)
  : colvar_grid<cvm::real>(g), samples(NULL), grad(NULL)
{
  grad = new cvm::real[nd];
}

colvar_grid_scalar::colvar_grid_scalar(std::vector<int> const &nx_i)
  : colvar_grid<cvm::real>(nx_i, 0.0, 1), samples(NULL), grad(NULL)
{
  grad = new cvm::real[nd];
}

colvar_grid_scalar::colvar_grid_scalar(std::vector<colvar *> &colvars, bool margin)
  : colvar_grid<cvm::real>(colvars, 0.0, 1, margin), samples(NULL), grad(NULL)
{
  grad = new cvm::real[nd];
}

colvar_grid_scalar::~colvar_grid_scalar()
{
  if (grad) {
    delete [] grad;
    grad = NULL;
  }
}

cvm::real colvar_grid_scalar::maximum_value() const
{
  cvm::real max = data[0];
  for (size_t i = 0; i < nt; i++) {
    if (data[i] > max) max = data[i];
  }
  return max;
}


cvm::real colvar_grid_scalar::minimum_value() const
{
  cvm::real min = data[0];
  for (size_t i = 0; i < nt; i++) {
    if (data[i] < min) min = data[i];
  }
  return min;
}

cvm::real colvar_grid_scalar::minimum_pos_value() const
{
  cvm::real minpos = data[0];
  size_t i;
  for (i = 0; i < nt; i++) {
    if(data[i] > 0) {
      minpos = data[i];
      break;
    }
  }
  for (i = 0; i < nt; i++) {
    if (data[i] > 0 && data[i] < minpos) minpos = data[i];
  }
  return minpos;
}

cvm::real colvar_grid_scalar::integral() const
{
  cvm::real sum = 0.0;
  for (size_t i = 0; i < nt; i++) {
    sum += data[i];
  }
  cvm::real bin_volume = 1.0;
  for (size_t id = 0; id < widths.size(); id++) {
    bin_volume *= widths[id];
  }
  return bin_volume * sum;
}


cvm::real colvar_grid_scalar::entropy() const
{
  cvm::real sum = 0.0;
  for (size_t i = 0; i < nt; i++) {
    sum += -1.0 * data[i] * std::log(data[i]);
  }
  cvm::real bin_volume = 1.0;
  for (size_t id = 0; id < widths.size(); id++) {
    bin_volume *= widths[id];
  }
  return bin_volume * sum;
}


colvar_grid_gradient::colvar_grid_gradient()
  : colvar_grid<cvm::real>(), samples(NULL)
{}

colvar_grid_gradient::colvar_grid_gradient(std::vector<int> const &nx_i)
  : colvar_grid<cvm::real>(nx_i, 0.0, nx_i.size()), samples(NULL)
{}

colvar_grid_gradient::colvar_grid_gradient(std::vector<colvar *> &colvars)
  : colvar_grid<cvm::real>(colvars, 0.0, colvars.size()), samples(NULL)
{}

void colvar_grid_gradient::write_1D_integral(std::ostream &os)
{
  cvm::real bin, min, integral;
  std::vector<cvm::real> int_vals;

  os << "#       xi            A(xi)\n";

  if ( cv.size() != 1 ) {
    cvm::error("Cannot write integral for multi-dimensional gradient grids.");
    return;
  }

  integral = 0.0;
  int_vals.push_back( 0.0 );
  min = 0.0;

  // correction for periodic colvars, so that the PMF is periodic
  cvm::real corr;
  if ( periodic[0] ) {
    corr = average();
  } else {
    corr = 0.0;
  }

  for (std::vector<int> ix = new_index(); index_ok(ix); incr(ix)) {

    if (samples) {
      size_t const samples_here = samples->value(ix);
      if (samples_here)
        integral += (value(ix) / cvm::real(samples_here) - corr) * cv[0]->width;
    } else {
      integral += (value(ix) - corr) * cv[0]->width;
    }

    if ( integral < min ) min = integral;
    int_vals.push_back( integral );
  }

  bin = 0.0;
  for ( int i = 0; i < nx[0]; i++, bin += 1.0 ) {
    os << std::setw(10) << cv[0]->lower_boundary.real_value + cv[0]->width * bin << " "
       << std::setw(cvm::cv_width)
       << std::setprecision(cvm::cv_prec)
       << int_vals[i] - min << "\n";
  }

  os << std::setw(10) << cv[0]->lower_boundary.real_value + cv[0]->width * bin << " "
     << std::setw(cvm::cv_width)
     << std::setprecision(cvm::cv_prec)
     << int_vals[nx[0]] - min << "\n";

  return;
}






// Parameters:
// b (divergence + BC): member of class integrate_cg; updated locally every ts
// x (solution PMF): reference to pmf object? or copy of thestd::vector if more efficient
// atimes, asolve: member functions of class integrate_cg, relying on
// laplacian: member data (vector) of integrate_cg; sparse matrix representation of
// finite diff. Laplacian, defined once and for all at construction time.
// NOTE: most of the data needs complete updates if the grid size changes...

integrate_potential::integrate_potential(std::vector<colvar *> &colvars)
  : colvar_grid_scalar(colvars, true)
{
  // parent class colvar_grid_scalar is constructed with margin option set to true
  // hence PMF grid is wider than gradient grid if non-PBC

  divergence.resize(nt);

  // Weighted Poisson
  div_weights = colvar_grid_count(colvars, 0, true); // enable margin
  div_weights_gradx.resize(nt);
  div_weights_grady.resize(nt);

//   // Compute inverse of Laplacian diagonal for Jacobi preconditioning
//   inv_lap_diag.resize(nt);
//   std::vector<cvm::real> id(nt), lap_col(nt);
//   for (int i = 0; i < nt; i++) {
//     id[i] = 1.;
//     atimes(id, lap_col);
//     id[i] = 0.;
//     inv_lap_diag[i] = 1. / lap_col[i];
//   }
}


int integrate_potential::integrate(const int itmax, const cvm::real tol, cvm::real & err)
{
  int iter;

  nr_linbcg_sym(divergence, data, tol, itmax, iter, err);
  cvm::log ("Completed integration in " + cvm::to_str(iter) + " steps with"
     + " error " + cvm::to_str(err));

  // TODO remove this test of laplacian calcs
  std::vector<cvm::real> backup (data);
  std::ofstream p("pmf.dat");
  add_constant(-1.0 * minimum_value());
  write_multicol(p);
  std::vector<cvm::real> lap = std::vector<cvm::real>(data.size());
  atimes(data, lap);
  data = lap;
  std::ofstream l("laplacian.dat");
  write_multicol(l);
  data = divergence;
  std::ofstream d("divergence.dat");
  write_multicol(d);
  data = backup;
  std::ofstream dw("div_weights.dat");
  div_weights.write_multicol(dw);

  if (nx[0]*nx[1] <= 100) {
    // Write explicit Laplacian FIXME debug output
    std::ofstream lap_out("lap_op.dat");
    std::vector<cvm::real> id(nx[0]*nx[1]), lap_col(nx[0]*nx[1]);
    for (int i = 0; i < nx[0] * nx[1]; i++) {
      id[i] = 1.;
      atimes(id, lap_col);
      id[i] = 0.;
      for (int j = 0; j < nx[0] * nx[1]; j++) {
        lap_out << cvm::to_str(i) + " " + cvm::to_str(j) + " " + cvm::to_str(lap_col[j]) << std::endl;
      }
      lap_out << std::endl;
    }
  }
  // TODO TODO TODO
  return iter;
}


void integrate_potential::set_div(const colvar_grid_gradient &gradient)
{
  for (std::vector<int> ix = new_index(); index_ok(ix); incr(ix)) {
    update_div_local(gradient, ix);
  }
  // Weighted Poisson
  size_t il = 0;
  for (std::vector<int> ix = new_index(); index_ok(ix); incr(ix), il++) {
    div_weights_gradx[il] = div_weights.gradient_finite_diff(ix, 0);
    div_weights_grady[il] = div_weights.gradient_finite_diff(ix, 1);
  }
}


void integrate_potential::update_div(const colvar_grid_gradient &gradient, const std::vector<int> &ix0)
{
  std::vector<int> ix(ix0);
  size_t il;

  // If not periodic, expanded grid ensures that neighbors of ix0 are valid grid points
  update_div_local(gradient, ix);
  ix[0]++; wrap(ix);
  update_div_local(gradient, ix);
  ix[1]++; wrap(ix);
  update_div_local(gradient, ix);
  ix[0]--; wrap(ix);
  update_div_local(gradient, ix);

  // Weighted Poisson
  ix = ix0;
  il = address(ix); // linear index
  div_weights_gradx[il] = div_weights.gradient_finite_diff(ix, 0);
  div_weights_grady[il] = div_weights.gradient_finite_diff(ix, 1);
  ix[0]++; wrap(ix);
  il = address(ix);
  div_weights_gradx[il] = div_weights.gradient_finite_diff(ix, 0);
  div_weights_grady[il] = div_weights.gradient_finite_diff(ix, 1);
  ix[1]++; wrap(ix);
  il = address(ix);
  div_weights_gradx[il] = div_weights.gradient_finite_diff(ix, 0);
  div_weights_grady[il] = div_weights.gradient_finite_diff(ix, 1);
  ix[0]--; wrap(ix);
  il = address(ix);
  div_weights_gradx[il] = div_weights.gradient_finite_diff(ix, 0);
  div_weights_grady[il] = div_weights.gradient_finite_diff(ix, 1);
}


void integrate_potential::update_div_local(const colvar_grid_gradient &gradient, const std::vector<int> &ix0)
{
  std::vector<int> ix(2);
  const int linear_index = address(ix0);

  get_local_grads(gradient, ix0);
  // Special case of corners: there is only one value of the gradient to average
  cvm::real fact_corner = 0.5;
  if (!periodic[0] && !periodic[1] && (ix0[0] == 0 || ix0[0] == nx[0]-1) && (ix0[1] == 0 || ix0[1] == nx[1]-1)) {
    fact_corner = 1.0;
  }
  divergence[linear_index] = (g10[0]-g00[0] + g11[0]-g01[0]) * fact_corner / widths[0]
                           + (g01[1]-g00[1] + g11[1]-g10[1]) * fact_corner / widths[1];
  // Weight is just the combined number of samples in given bins
  div_weights.set_value(linear_index, n00 + n01 + n10 + n11);
}


void integrate_potential::get_local_grads(const colvar_grid_gradient &gradient, const std::vector<int> & ix0)
{
  cvm::real   fact;
  const cvm::real   * g;
  std::vector<int> ix = ix0;
  bool edge;
  n00 = n01 = n10 = n11 = 0;

  edge = gradient.wrap_edge(ix);
  if (!edge && (n11 = gradient.samples->value(ix))) {
    g = &(gradient.value(ix));
    fact = 1.0; // / count; // weighted Poisson
    g11[0] = fact * g[0];
    g11[1] = fact * g[1];
  } else
    g11[0] = g11[1] = 0.0;

  ix[0] = ix0[0] - 1;
  edge = gradient.wrap_edge(ix);
  if (!edge && (n01 = gradient.samples->value(ix))) {
    g = & (gradient.value(ix));
    fact = 1.0; // / count; // weighted Poisson
    g01[0] = fact * g[0];
    g01[1] = fact * g[1];
  } else
    g01[0] = g01[1] = 0.0;

  ix[1] = ix0[1] - 1;
  edge = gradient.wrap_edge(ix);
  if (!edge && (n00 = gradient.samples->value(ix))) {
    g = & (gradient.value(ix));
    fact = 1.0; //  / count; // weighted Poisson
    g00[0] = fact * g[0];
    g00[1] = fact * g[1];
  } else
    g00[0] = g00[1] = 0.0;

  ix[0] = ix0[0];
  edge = gradient.wrap_edge(ix);
  if (!edge && (n10 = gradient.samples->value(ix))) {
    g = & (gradient.value(ix));
    fact = 1.0; //  / count; // weighted Poisson
    g10[0] = fact * g[0];
    g10[1] = fact * g[1];
  } else
    g10[0] = g10[1] = 0.0;
}


/// Multiplication by sparse matrix representing Laplacian
void integrate_potential::atimes(const std::vector<cvm::real> &A, std::vector<cvm::real> &LA)
{
  size_t index, index2;
  const cvm::real fx = 1.0 / widths[0];
  const cvm::real fy = 1.0 / widths[1];
  const cvm::real ffx = 1.0 / (widths[0] * widths[0]);
  const cvm::real ffy = 1.0 / (widths[1] * widths[1]);
  const int h = nx[1];
  const int w = nx[0];
  // offsets for 4 reference points of the Laplacian stencil
  int xm = -h;
  int xp =  h;
  int ym = -1;
  int yp =  1;

  index = h + 1;
  for (int i=1; i<w-1; i++) {
    for (int j=1; j<h-1; j++) {
      LA[index] = ffx * (A[index + xm] + A[index + xp] - 2.0 * A[index])
                + ffy * (A[index + ym] + A[index + yp] - 2.0 * A[index]);
      LA[index] *= div_weights.value(index); // Divergence of weighted gradient
      LA[index] += .5 * fx * (A[index + xp] - A[index + xm]) * div_weights_gradx[index]
                 + .5 * fy * (A[index + yp] - A[index + ym]) * div_weights_grady[index];
      index++;
    }
    index += 2; // skip the edges and move to next column
  }

  // then, edges depending on BC
  if (periodic[0]) {
    // i = 0 and i = w are periodic images
    xm =  h * (w - 1);
    xp =  h;
    ym = -1;
    yp =  1;
    index = 1; // Follows left edge
    index2 = h * (w - 1) + 1; // Follows right edge
    for (int j=1; j<h-1; j++) {
      LA[index] = ffx * (A[index + xm] + A[index + xp] - 2.0 * A[index])
                + ffy * (A[index + ym] + A[index + yp] - 2.0 * A[index]);
      LA[index] *= div_weights.value(index); // Divergence of weighted gradient
      LA[index] += .5 * fx * (A[index + xp] - A[index + xm]) * div_weights_gradx[index]
                 + .5 * fy * (A[index + yp] - A[index + ym]) * div_weights_grady[index];

      LA[index2] = ffx * (A[index2 - xp] + A[index2 - xm] - 2.0 * A[index2])
                 + ffy * (A[index2 + ym] + A[index2 + yp] - 2.0 * A[index2]);
      LA[index2] *= div_weights.value(index2); // Divergence of weighted gradient
      LA[index2] += .5 * fx * (A[index2 - xm] - A[index2 - xp]) * div_weights_gradx[index2]
                  + .5 * fy * (A[index2 + yp] - A[index2 + ym]) * div_weights_grady[index2];
      index++;
      index2++;
    }
  } else {
    xm = -h;
    xp =  h;
    ym = -1;
    yp =  1;
    index = 1; // Follows left edge
    index2 = h * (w - 1) + 1; // Follows right edge
    for (int j=1; j<h-1; j++) {
      // x gradient beyond the edge is taken to be zero
      // alternate: x gradient + y term of laplacian
      LA[index] = ffx * (A[index + xp] - A[index])
                + ffy * (A[index + ym] + A[index + yp] - 2.0 * A[index]);
      LA[index] *= div_weights.value(index2); // Divergence of weighted gradient
      LA[index] += fx * (A[index + xp] - A[index]) * div_weights_gradx[index]
                 + .5 * fy * (A[index + yp] - A[index + ym]) * div_weights_grady[index];

      LA[index2] = ffx * (A[index2 + xm] - A[index2])
                 + ffy * (A[index2 + ym] + A[index2 + yp] - 2.0 * A[index2]);
      LA[index2] *= div_weights.value(index2); // Divergence of weighted gradient
      LA[index2] += fx * (A[index2] - A[index2 + xm]) * div_weights_gradx[index2]
                 + .5 * fy * (A[index2 + yp] - A[index2 + ym]) * div_weights_grady[index2];
      index++;
      index2++;
    }
  }

  if (periodic[1]) {
    // j = 0 and j = h are periodic images
    xm = -h;
    xp =  h;
    ym =  h - 1;
    yp =  1;
    index = h; // Follows bottom edge
    index2 = 2 * h - 1; // Follows top edge
    for (int i=1; i<w-1; i++) {
      LA[index] = ffx * (A[index + xm] + A[index + xp] - 2.0 * A[index])
                + ffy * (A[index + ym] + A[index + yp] - 2.0 * A[index]);
      LA[index] *= div_weights.value(index); // Divergence of weighted gradient
      LA[index] += .5 * fx * (A[index + xp] - A[index + xm]) * div_weights_gradx[index]
                 + .5 * fy * (A[index + yp] - A[index + ym]) * div_weights_grady[index];

      LA[index2] = ffx * (A[index2 + xm] + A[index2 + xp] - 2.0 * A[index2])
                 + ffy * (A[index2 - yp] + A[index2 - ym] - 2.0 * A[index2]);
      LA[index2] *= div_weights.value(index2); // Divergence of weighted gradient
      LA[index2] += .5 * fx * (A[index2 + xp] - A[index2 + xm]) * div_weights_gradx[index2]
                  + .5 * fy * (A[index2 - ym] - A[index2 - yp]) * div_weights_grady[index2];
      index  += h;
      index2 += h;
    }
  } else {
    xm = -h;
    xp =  h;
    ym =  -1;
    yp =  1;
    index = h; // Follows bottom edge
    index2 = 2 * h - 1; // Follows top edge
    for (int i=1; i<w-1; i++) {
      // alternate: y gradient + x term of laplacian
      LA[index] = ffx * (A[index + xm] + A[index + xp] - 2.0 * A[index])
                + ffy * (A[index + yp] - A[index]);
      LA[index] *= div_weights.value(index); // Divergence of weighted gradient
      LA[index] += .5 * fx * (A[index + xp] - A[index + xm]) * div_weights_gradx[index]
                 + fy * (A[index + yp] - A[index]) * div_weights_grady[index];

      LA[index2] = ffx * (A[index2 + xm] + A[index2 + xp] - 2.0 * A[index2])
                 + ffy * (A[index2 + ym] - A[index2]);
      LA[index2] *= div_weights.value(index2); // Divergence of weighted gradient
      LA[index2] += .5 * fx * (A[index2 + xp] - A[index2 + xm]) * div_weights_gradx[index2]
                  + fy * (A[index2] - A[index2 + ym]) * div_weights_grady[index2];
      index  += h;
      index2 += h;
    }
  }

  // 4 corners
  xm = h;
  xp = h * (w - 1);
  ym = 1;
  yp = h - 1;
  cvm::real lx, ly;

  index = 0;
  lx = periodic[0] ? (A[xp] + A[xm] - 2.0 * A[0]) : (A[h] - A[0]);
  ly = periodic[1] ? (A[yp] + A[ym] - 2.0 * A[0]) : (A[1] - A[0]);
  LA[index] = ffx * lx + ffy * ly;

  index = h-1;
  lx = periodic[0] ? (A[index + xp] + A[index + xm] - 2.0 * A[index]) : (A[index + h] - A[index]);
  ly = periodic[1] ? (A[index - ym] + A[index - yp] - 2.0 * A[index]) : (A[index - 1] - A[index]);
  LA[index] = ffx * lx + ffy * ly;

  index = h * (w-1);
  lx = periodic[0] ? (A[index - xm] + A[index - xp] - 2.0 * A[index]) : (A[index - h] - A[index]);
  ly = periodic[1] ? (A[index + yp] + A[index + ym] - 2.0 * A[index]) : (A[index + 1] - A[index]);
  LA[index] = ffx * lx + ffy * ly;

  index = h * w - 1;
  lx = periodic[0] ? (A[index - xm] + A[index - xp] - 2.0 * A[index]) : (A[index - h] - A[index]);
  ly = periodic[1] ? (A[index - ym] + A[index - yp] - 2.0 * A[index]) : (A[index - 1] - A[index]);
  LA[index] = ffx * lx + ffy * ly;
}


/// Inversion of preconditioner matrix (e.g. diagonal of the Laplacian)
void integrate_potential::asolve(const std::vector<cvm::real> &b, std::vector<cvm::real> &x, const int itrnsp)
{
  for (size_t i=0; i<nt; i++) {
    // x[i] = b[i] * inv_lap_diag[i]; // Jacobi preconditioner - no benefit in tests
    x[i] = b[i];
  }
  return;
}


// b : RHS of equation
// x : initial guess for the solution; output is solution
// itol : convergence criterion
void integrate_potential::nr_linbcg_sym(const std::vector<cvm::real> &b, std::vector<cvm::real> &x, const cvm::real tol,
  const int itmax, int &iter, cvm::real &err)
{
  cvm::real ak,akden,bk,bkden=1.0,bknum,bnrm,dxnrm,xnrm,zm1nrm,znrm;
  const cvm::real EPS=1.0e-14;
  int j;
  const int itol = 1; // Use L2 norm as target
  std::vector<cvm::real> p(nt), r(nt), z(nt);

  iter=0;
  atimes(x,r);
  for (j=0;j<nt;j++) {
    r[j]=b[j]-r[j];
  }
  bnrm=nr_snrm(b,itol);
  if (bnrm < EPS) {
    return; // Target is zero
  }
  asolve(r,z,0);
  while (iter < itmax) {
    ++iter;
    for (bknum=0.0,j=0;j<nt;j++) {
      bknum += z[j]*r[j];
    }
    if (iter == 1) {
      for (j=0;j<nt;j++) {
        p[j]  = z[j];
      }
    } else {
      bk=bknum/bkden;
      for (j=0;j<nt;j++) {
        p[j]  = bk*p[j] + z[j];
      }
    }
    bkden = bknum;
    atimes(p,z);
    for (akden=0.0,j=0;j<nt;j++) {
      akden += z[j]*p[j];
    }
    ak = bknum/akden;
    for (j=0;j<nt;j++) {
      x[j] += ak*p[j];
      r[j] -= ak*z[j];
    }
    asolve(r,z,0);
    err = nr_snrm(r,itol)/bnrm;
//  std::cout << "iter=" << std::setw(4) << iter+1 << std::setw(12) << err << std::endl;
    if (err <= tol)
      break;
  }
}

cvm::real integrate_potential::nr_snrm(const std::vector<cvm::real> &sx, const int itol)
{
  int i,isamax;
  cvm::real ans;

  int n=sx.size();
  if (itol <= 3) {
    ans = 0.0;
    for (i=0;i<n;i++) ans += sx[i]*sx[i];
    return ::sqrt(ans);
  } else {
    isamax=0;
    for (i=0;i<n;i++) {
      if (::fabs(sx[i]) > ::fabs(sx[isamax])) isamax=i;
    }
    return ::fabs(sx[isamax]);
  }
}
