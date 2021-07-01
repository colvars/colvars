// -*- c++ -*-

// This file is part of the Collective Variables module (Colvars).
// The original version of Colvars and its updates are located at:
// https://github.com/Colvars/colvars
// Please update all Colvars source files before making any changes.
// If you wish to distribute your changes, please submit them to the
// Colvars repository at GitHub.


#include <algorithm>

#include "colvarmodule.h"
#include "colvarvalue.h"
#include "colvarparse.h"
#include "colvar.h"
#include "colvarcomp.h"


colvar::alch_lambda::alch_lambda(std::string const &conf)
  : cvc(conf)
{
  function_type = "alch_lambda";

  disable(f_cvc_explicit_gradient);
  disable(f_cvc_gradient);

  x.type(colvarvalue::type_scalar);
  // Query initial value from back-end
  cvm::proxy->get_alch_lambda(&x.real_value);
}


void colvar::alch_lambda::calc_value()
{
  // Special workflow:
  // at the beginning of the timestep we get a force instead of calculating the value

  cvm::proxy->get_dE_dlambda(&ft.real_value);
  ft.real_value *= -1.0; // Energy derivative to force
}


void colvar::alch_lambda::calc_gradients()
{
}


void colvar::alch_lambda::apply_force(colvarvalue const &force)
{
  // Special workflow:
  // at the end of the time step we send a new value
  cvm::proxy->set_alch_lambda(&x.real_value);
}

simple_scalar_dist_functions(alch_lambda)



colvar::alch_Flambda::alch_Flambda(std::string const &conf)
  : cvc(conf)
{
  function_type = "alch_Flambda";

  disable(f_cvc_explicit_gradient);
  disable(f_cvc_gradient);

  x.type(colvarvalue::type_scalar);
}


void colvar::alch_Flambda::calc_value()
{
  // Special workflow:
  // at the beginning of the timestep we get a force instead of calculating the value

  // Query initial value from back-end
  cvm::proxy->get_dE_dlambda(&x.real_value);
  x.real_value *= -1.0; // Energy derivative to force
}


void colvar::alch_Flambda::calc_gradients()
{
}


void colvar::alch_Flambda::apply_force(colvarvalue const &force)
{
  // Convert force on Flambda to force on dE/dlambda
  cvm::real f = -1.0 * force.real_value;
  // Send scalar force to back-end, which will distribute it onto atoms
  cvm::proxy->apply_force_dE_dlambda(&f);
}

simple_scalar_dist_functions(alch_Flambda)
