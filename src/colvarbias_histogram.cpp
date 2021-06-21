// -*- c++ -*-

// This file is part of the Collective Variables module (Colvars).
// The original version of Colvars and its updates are located at:
// https://github.com/Colvars/colvars
// Please update all Colvars source files before making any changes.
// If you wish to distribute your changes, please submit them to the
// Colvars repository at GitHub.

#include "colvarmodule.h"
#include "colvarproxy.h"
#include "colvar.h"
#include "colvarbias_histogram.h"

#include <algorithm>
#include <numeric>

colvarbias_histogram::colvarbias_histogram(char const *key)
  : colvarbias(key),
    grid(NULL), out_name("")
{
  provide(f_cvb_bypass_ext_lagrangian); // Allow histograms of actual cv for extended-Lagrangian
}


int colvarbias_histogram::init(std::string const &conf)
{
  colvarbias::init(conf);

  enable(f_cvb_scalar_variables);
  enable(f_cvb_history_dependent);

  size_t i;

  get_keyval(conf, "outputFile", out_name, "");
  // Write DX file by default only in dimension >= 3
  std::string default_name_dx = this->num_variables() > 2 ? "" : "none";
  get_keyval(conf, "outputFileDX", out_name_dx, default_name_dx);

  /// with VMD, this may not be an error
  // if ( output_freq == 0 ) {
  //   cvm::error("User required histogram with zero output frequency");
  // }

  colvar_array_size = 0;
  {
    bool colvar_array = false;
    get_keyval(conf, "gatherVectorColvars", colvar_array, colvar_array);

    if (colvar_array) {
      for (i = 0; i < num_variables(); i++) { // should be all vector
        if (colvars[i]->value().type() != colvarvalue::type_vector) {
          cvm::error("Error: used gatherVectorColvars with non-vector colvar.\n", INPUT_ERROR);
          return INPUT_ERROR;
        }
        if (i == 0) {
          colvar_array_size = colvars[i]->value().size();
          if (colvar_array_size < 1) {
            cvm::error("Error: vector variable has dimension less than one.\n", INPUT_ERROR);
            return INPUT_ERROR;
          }
        } else {
          if (colvar_array_size != colvars[i]->value().size()) {
            cvm::error("Error: trying to combine vector colvars of different lengths.\n", INPUT_ERROR);
            return INPUT_ERROR;
          }
        }
      }
    } else {
      for (i = 0; i < num_variables(); i++) { // should be all scalar
        if (colvars[i]->value().type() != colvarvalue::type_scalar) {
          cvm::error("Error: only scalar colvars are supported when gatherVectorColvars is off.\n", INPUT_ERROR);
          return INPUT_ERROR;
        }
      }
    }
  }

  if (colvar_array_size > 0) {
    weights.assign(colvar_array_size, 1.0);
    get_keyval(conf, "weights", weights, weights);
  }

  for (i = 0; i < num_variables(); i++) {
    colvars[i]->enable(f_cv_grid); // Could be a child dependency of a f_cvb_use_grids feature
  }

  grid = new colvar_grid_scalar();
  grid->init_from_colvars(colvars);

  if (is_enabled(f_cvb_bypass_ext_lagrangian)) {
    grid->request_actual_value();
  }

  {
    std::string grid_conf;
    if (key_lookup(conf, "histogramGrid", &grid_conf)) {
      grid->parse_params(grid_conf);
      grid->check_keywords(grid_conf, "histogramGrid");
    }
  }

  return COLVARS_OK;
}


colvarbias_histogram::~colvarbias_histogram()
{
  if (grid) {
    delete grid;
    grid = NULL;
  }
}


int colvarbias_histogram::update()
{
  int error_code = COLVARS_OK;
  // update base class
  error_code |= colvarbias::update();

  if (cvm::debug()) {
    cvm::log("Updating histogram bias " + this->name);
  }

  // assign a valid bin size
  bin.assign(num_variables(), 0);

  if (out_name.size() == 0) {
    // At the first timestep, we need to assign out_name since
    // output_prefix is unset during the constructor
    if (cvm::step_relative() == 0) {
      out_name = cvm::output_prefix() + "." + this->name + ".dat";
      cvm::log("Histogram " + this->name + " will be written to file \"" + out_name + "\"\n");
    }
  }

  if (out_name_dx.size() == 0) {
    if (cvm::step_relative() == 0) {
      out_name_dx = cvm::output_prefix() + "." + this->name + ".dx";
      cvm::log("Histogram " + this->name + " will be written to file \"" + out_name_dx + "\"\n");
    }
  }

  if (colvar_array_size == 0) {
    // update indices for scalar values
    size_t i;
    for (i = 0; i < num_variables(); i++) {
      bin[i] = grid->current_bin_scalar(i);
    }

    if (can_accumulate_data()) {
      if (grid->index_ok(bin)) {
        grid->acc_value(bin, 1.0);
      }
    }
  } else {
    // update indices for vector/array values
    size_t iv, i;
    for (iv = 0; iv < colvar_array_size; iv++) {
      for (i = 0; i < num_variables(); i++) {
        bin[i] = grid->current_bin_scalar(i, iv);
      }

      if (grid->index_ok(bin)) {
        grid->acc_value(bin, weights[iv]);
      }
    }
  }

  error_code |= cvm::get_error();
  return error_code;
}


int colvarbias_histogram::write_output_files()
{
  if (!has_data) {
    // nothing to write
    return COLVARS_OK;
  }

  if (out_name.size() && out_name != "none") {
    cvm::log("Writing the histogram file \""+out_name+"\".\n");
    cvm::backup_file(out_name.c_str());
    std::ostream *grid_os = cvm::proxy->output_stream(out_name);
    if (!grid_os) {
      return cvm::error("Error opening histogram file "+out_name+
                        " for writing.\n", FILE_ERROR);
    }
    grid->write_multicol(*grid_os);
    cvm::proxy->close_output_stream(out_name);
  }

  if (out_name_dx.size() && out_name_dx != "none") {
    cvm::log("Writing the histogram file \""+out_name_dx+"\".\n");
    cvm::backup_file(out_name_dx.c_str());
    std::ostream *grid_os = cvm::proxy->output_stream(out_name_dx);
    if (!grid_os) {
      return cvm::error("Error opening histogram file "+out_name_dx+
                        " for writing.\n", FILE_ERROR);
    }
    grid->write_opendx(*grid_os);
    cvm::proxy->close_output_stream(out_name_dx);
  }

  return COLVARS_OK;
}


std::istream & colvarbias_histogram::read_state_data(std::istream& is)
{
  if (! read_state_data_key(is, "grid")) {
    return is;
  }
  if (! grid->read_raw(is)) {
    return is;
  }

  return is;
}


std::ostream & colvarbias_histogram::write_state_data(std::ostream& os)
{
  std::ios::fmtflags flags(os.flags());
  os.setf(std::ios::fmtflags(0), std::ios::floatfield);
  os << "grid\n";
  grid->write_raw(os, 8);
  os.flags(flags);
  return os;
}

colvarbias_reweightaMD::colvarbias_reweightaMD(char const *key)
  : colvarbias_histogram(key), grid_count(NULL), grid_dV(NULL),
    grid_dV_square(NULL), pmf_grid_exp_avg(NULL), pmf_grid_cumulant(NULL),
    grad_grid_exp_avg(NULL), grad_grid_cumulant(NULL)
{
}

colvarbias_reweightaMD::~colvarbias_reweightaMD() {
  if (grid_dV) {
    delete grid_dV;
    grid_dV = NULL;
  }
  if (grid_dV_square) {
    delete grid_dV_square;
    grid_dV_square = NULL;
  }
  if (grid_count) {
    delete grid_count;
    grid_count = NULL;
  }
  if (pmf_grid_exp_avg) {
    delete pmf_grid_exp_avg;
    pmf_grid_exp_avg = NULL;
  }
  if (pmf_grid_cumulant) {
    delete pmf_grid_cumulant;
    pmf_grid_cumulant = NULL;
  }
  if (grad_grid_exp_avg) {
    delete grad_grid_exp_avg;
    grad_grid_exp_avg = NULL;
  }
  if (grad_grid_cumulant) {
    delete grad_grid_cumulant;
    grad_grid_cumulant = NULL;
  }
}

int colvarbias_reweightaMD::init(std::string const &conf) {
  if (cvm::proxy->accelMD_enabled() == false) {
    cvm::error("Error: accelerated MD in your MD engine is not enabled.\n", INPUT_ERROR);
  }
  int baseclass_init_code = colvarbias_histogram::init(conf);
  get_keyval(conf, "CollectAfterSteps", start_after_steps, 0);
  get_keyval(conf, "CumulantExpansion", b_use_cumulant_expansion, true);
  get_keyval(conf, "WriteGradients", b_write_gradients, true);
  get_keyval(conf, "historyFreq", history_freq, 0);
  b_history_files = (history_freq > 0);
  grid_count = new colvar_grid_scalar(colvars);
  grid_count->request_actual_value();
  grid->request_actual_value();
  pmf_grid_exp_avg = new colvar_grid_scalar(colvars);
  if (b_write_gradients) {
    grad_grid_exp_avg = new colvar_grid_gradient(colvars);
  }
  if (b_use_cumulant_expansion) {
    grid_dV = new colvar_grid_scalar(colvars);
    grid_dV_square = new colvar_grid_scalar(colvars);
    pmf_grid_cumulant = new colvar_grid_scalar(colvars);
    grid_dV->request_actual_value();
    grid_dV_square->request_actual_value();
    if (b_write_gradients) {
      grad_grid_cumulant = new colvar_grid_gradient(colvars);
    }
  }
  return baseclass_init_code;
}

int colvarbias_reweightaMD::update() {
  int error_code = COLVARS_OK;
  if (cvm::step_relative() >= start_after_steps) {
    // update base class
    error_code |= colvarbias::update();

    if (cvm::debug()) {
      cvm::log("Updating histogram bias " + this->name);
    }

    previous_bin.assign(num_variables(), 0);
    if (cvm::step_relative() > 0) {
      previous_bin = bin;
    }
    
    // assign a valid bin size
    bin.assign(num_variables(), 0);

    if (out_name.size() == 0) {
      // At the first timestep, we need to assign out_name since
      // output_prefix is unset during the constructor
      out_name = cvm::output_prefix() + "." + this->name + ".dat";
      cvm::log("Histogram " + this->name + " will be written to file \"" + out_name + "\"");
    }

    if (colvar_array_size == 0) {
      // update indices for scalar values
      size_t i;
      for (i = 0; i < num_variables(); i++) {
        bin[i] = grid->current_bin_scalar(i);
      }

      if (grid->index_ok(previous_bin) && cvm::step_relative() > 0) {
        const cvm::real reweighting_factor = cvm::proxy->get_accelMD_factor();
        grid_count->acc_value(previous_bin, 1.0);
        grid->acc_value(previous_bin, reweighting_factor);
        if (b_use_cumulant_expansion) {
          const cvm::real dV = std::log(reweighting_factor) *
                               cvm::temperature() * cvm::boltzmann();
          grid_dV->acc_value(previous_bin, dV);
          grid_dV_square->acc_value(previous_bin, dV * dV);
        }
      }
    } else {
      // update indices for vector/array values
      size_t iv, i;
      for (iv = 0; iv < colvar_array_size; iv++) {
        for (i = 0; i < num_variables(); i++) {
          bin[i] = grid->current_bin_scalar(i, iv);
        }

        if (grid->index_ok(previous_bin) && cvm::step_relative() > 0) {
          const cvm::real reweighting_factor = cvm::proxy->get_accelMD_factor();
          grid_count->acc_value(previous_bin, 1.0);
          grid->acc_value(previous_bin, reweighting_factor);
          if (b_use_cumulant_expansion) {
            const cvm::real dV = std::log(reweighting_factor) *
                                 cvm::temperature() * cvm::boltzmann();
            grid_dV->acc_value(previous_bin, dV);
            grid_dV_square->acc_value(previous_bin, dV * dV);
          }
        }
      }
    }

    if (output_freq && (cvm::step_absolute() % output_freq) == 0) {
      write_output_files();
    }

    error_code |= cvm::get_error();
  }
  return error_code;
}

int colvarbias_reweightaMD::write_output_files() {
  int error_code = COLVARS_OK;
  error_code |= colvarbias_histogram::write_output_files();
  const std::string out_name_pmf = cvm::output_prefix() + "." +
                                   this->name + ".reweight";
  error_code |= write_exponential_reweighted_pmf(out_name_pmf);
  const std::string out_count_name = cvm::output_prefix() + "." +
                                     this->name + ".count";
  error_code |= write_count(out_count_name);
  const bool write_history = b_history_files &&
                             (cvm::step_absolute() % history_freq) == 0;
  if (write_history) {
    error_code |= write_exponential_reweighted_pmf(
      out_name_pmf + ".hist", (cvm::step_relative() > 0));
    error_code |= write_count(out_count_name + ".hist",
                              (cvm::step_relative() > 0));
  }
  if (b_use_cumulant_expansion) {
    const std::string out_name_cumulant_pmf = cvm::output_prefix() + "." +
                                              this->name + ".cumulant";
    error_code |= write_cumulant_expansion_pmf(out_name_cumulant_pmf);
    if (write_history) {
      error_code |= write_cumulant_expansion_pmf(
        out_name_cumulant_pmf + ".hist", (cvm::step_relative() > 0));
    }
  }
  error_code |= cvm::get_error();
  return error_code;
}

int colvarbias_reweightaMD::write_exponential_reweighted_pmf(
  const std::string& output_prefix, bool append) {
  const std::string output_pmf = output_prefix + ".pmf";
  cvm::log("Writing the accelerated MD PMF file \"" + output_pmf + "\".\n");
  if (!append) {
    cvm::backup_file(output_pmf.c_str());
  }
  const std::ios::openmode mode = (append ? std::ios::app : std::ios::out);
  std::ostream *pmf_grid_os = cvm::proxy->output_stream(output_pmf, mode);
  if (!pmf_grid_os) {
    return cvm::error("Error opening PMF file " + output_pmf +
                      " for writing.\n", FILE_ERROR);
  }
  pmf_grid_exp_avg->copy_grid(*grid);
  // compute the average
  for (size_t i = 0; i < pmf_grid_exp_avg->raw_data_num(); ++i) {
    const double count = grid_count->value(i);
    if (count > 0) {
      const double tmp = pmf_grid_exp_avg->value(i);
      pmf_grid_exp_avg->set_value(i, tmp / count);
    }
  }
  hist_to_pmf(pmf_grid_exp_avg, grid_count);
  pmf_grid_exp_avg->write_multicol(*pmf_grid_os);
  cvm::proxy->close_output_stream(output_pmf);
  if (b_write_gradients) {
    const std::string output_grad = output_prefix + ".grad";
    cvm::log("Writing the accelerated MD gradients file \"" + output_grad +
             "\".\n");
    if (!append) {
      cvm::backup_file(output_grad.c_str());
    }
    std::ostream *grad_grid_os = cvm::proxy->output_stream(output_grad, mode);
    if (!grad_grid_os) {
      return cvm::error("Error opening grad file " + output_grad +
                        " for writing.\n", FILE_ERROR);
    }
    for (std::vector<int> ix = grad_grid_exp_avg->new_index();
          grad_grid_exp_avg->index_ok(ix); grad_grid_exp_avg->incr(ix)) {
      for (size_t n = 0; n < grad_grid_exp_avg->multiplicity(); n++) {
        grad_grid_exp_avg->set_value(
          ix, pmf_grid_exp_avg->gradient_finite_diff(ix, n), n);
      }
    }
    grad_grid_exp_avg->write_multicol(*grad_grid_os);
    cvm::proxy->close_output_stream(output_grad);
  }
  return COLVARS_OK;
}

int colvarbias_reweightaMD::write_cumulant_expansion_pmf(
  const std::string& output_prefix, bool append) {
  const std::string output_pmf = output_prefix + ".pmf";
  cvm::log("Writing the accelerated MD PMF file using cumulant expansion: \"" + output_pmf + "\".\n");
  if (!append) cvm::backup_file(output_pmf.c_str());
  const std::ios::openmode mode = (append ? std::ios::app : std::ios::out);
  std::ostream *pmf_grid_cumulant_os = cvm::proxy->output_stream(output_pmf, mode);
  if (!pmf_grid_cumulant_os) {
    return cvm::error("Error opening PMF file " + output_pmf +
                      " for writing.\n", FILE_ERROR);
  }
  compute_cumulant_expansion_factor(grid_dV, grid_dV_square,
                                    grid_count, pmf_grid_cumulant);
  hist_to_pmf(pmf_grid_cumulant, grid_count);
  pmf_grid_cumulant->write_multicol(*pmf_grid_cumulant_os);
  cvm::proxy->close_output_stream(output_pmf);
  if (b_write_gradients) {
    const std::string output_grad = output_prefix + ".grad";
    cvm::log("Writing the accelerated MD gradients file \"" + output_grad + "\".\n");
    if (!append) {
      cvm::backup_file(output_grad.c_str());
    }
    std::ostream *grad_grid_os = cvm::proxy->output_stream(output_grad, mode);
    if (!grad_grid_os) {
      return cvm::error("Error opening grad file " + output_grad +
                        " for writing.\n", FILE_ERROR);
    }
    for (std::vector<int> ix = grad_grid_cumulant->new_index();
          grad_grid_cumulant->index_ok(ix); grad_grid_cumulant->incr(ix)) {
      for (size_t n = 0; n < grad_grid_cumulant->multiplicity(); n++) {
        grad_grid_cumulant->set_value(
          ix, pmf_grid_cumulant->gradient_finite_diff(ix, n), n);
      }
    }
    grad_grid_cumulant->write_multicol(*grad_grid_os);
    cvm::proxy->close_output_stream(output_grad);
  }
  return COLVARS_OK;
}

int colvarbias_reweightaMD::write_count(const std::string& output_name, bool append) {
  cvm::log("Writing the accelerated MD count file \""+output_name+"\".\n");
  if (!append) cvm::backup_file(output_name.c_str());
  const std::ios::openmode mode = (append ? std::ios::app : std::ios::out);
  std::ostream *grid_count_os = cvm::proxy->output_stream(output_name, mode);
  if (!grid_count_os) {
    return cvm::error("Error opening count file "+output_name+
                      " for writing.\n", FILE_ERROR);
  }
  grid_count->write_multicol(*grid_count_os);
  cvm::proxy->close_output_stream(output_name);
  return COLVARS_OK;
}

void colvarbias_reweightaMD::hist_to_pmf(
  colvar_grid_scalar* hist, const colvar_grid_scalar* hist_count) const {
  if (hist->raw_data_num() == 0) return;
  const cvm::real kbt = cvm::boltzmann() * cvm::temperature();
  bool first_min_element = true;
  bool first_max_element = true;
  cvm::real min_element = 0;
  cvm::real max_element = 0;
  // the first loop: using logarithm to compute PMF
  for (size_t i = 0; i < hist->raw_data_num(); ++i) {
    const cvm::real count = hist_count->value(i);
    if (count > 0) {
      const cvm::real x = hist->value(i);
      const cvm::real pmf_value = -1.0 * kbt * std::log(x);
      hist->set_value(i, pmf_value);
      // find the minimum PMF value
      if (first_min_element) {
        // assign current PMF value to min_element at the first time
        min_element = pmf_value;
        first_min_element = false;
      } else {
        // if this is not the first time, then
        min_element = (pmf_value < min_element) ? pmf_value : min_element;
      }
      // do the same to the maximum
      if (first_max_element) {
        max_element = pmf_value;
        first_max_element = false;
      } else {
        max_element = (pmf_value > max_element) ? pmf_value : max_element;
      }
    }
  }
  // the second loop: bringing the minimum PMF value to zero
  for (size_t i = 0; i < hist->raw_data_num(); ++i) {
    const cvm::real count = hist_count->value(i);
    if (count > 0) {
      // bins that have samples
      const cvm::real x = hist->value(i);
      hist->set_value(i, x - min_element);
    } else {
      hist->set_value(i, max_element - min_element);
    }
  }
}

void colvarbias_reweightaMD::compute_cumulant_expansion_factor(
  const colvar_grid_scalar* hist_dV,
  const colvar_grid_scalar* hist_dV_square,
  const colvar_grid_scalar* hist_count,
  colvar_grid_scalar* cumulant_expansion_factor) const {
  const cvm::real beta = 1.0 / (cvm::boltzmann() * cvm::temperature());
  for (size_t i = 0; i < hist_dV->raw_data_num(); ++i) {
    const cvm::real count = hist_count->value(i);
    if (count > 0) {
      const cvm::real dV_avg = hist_dV->value(i) / count;
      const cvm::real dV_square_avg = hist_dV_square->value(i) / count;
      const cvm::real factor = std::exp(beta * dV_avg + 0.5 * beta * beta * (dV_square_avg - dV_avg * dV_avg));
      cumulant_expansion_factor->set_value(i, factor);
    }
  }
}

std::ostream & colvarbias_reweightaMD::write_state_data(std::ostream& os)
{
  std::ios::fmtflags flags(os.flags());
  os.setf(std::ios::fmtflags(0), std::ios::floatfield);
  os << "grid\n";
  grid->write_raw(os, 8);
  os << "grid_count\n";
  grid_count->write_raw(os, 8);
  os << "grid_dV\n";
  grid_dV->write_raw(os, 8);
  os << "grid_dV_square\n";
  grid_dV_square->write_raw(os, 8);
  os.flags(flags);
  return os;
}

std::istream & colvarbias_reweightaMD::read_state_data(std::istream& is)
{
  if (! read_state_data_key(is, "grid")) {
    return is;
  }
  if (! grid->read_raw(is)) {
    return is;
  }
  if (! read_state_data_key(is, "grid_count")) {
    return is;
  }
  if (! grid_count->read_raw(is)) {
    return is;
  }
  if (! read_state_data_key(is, "grid_dV")) {
    return is;
  }
  if (! grid_dV->read_raw(is)) {
    return is;
  }
  if (! read_state_data_key(is, "grid_dV_square")) {
    return is;
  }
  if (! grid_dV_square->read_raw(is)) {
    return is;
  }
  return is;
}
