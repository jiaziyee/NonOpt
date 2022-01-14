// Copyright (C) 2022 Frank E. Curtis
//
// This code is published under the MIT License.
//
// Author(s) : Frank E. Curtis

#include <cmath>
#include <ctime>
#include <vector>

#include "NonOptDeclarations.hpp"
#include "NonOptDefinitions.hpp"
#include "NonOptDirectionComputationCuttingPlane.hpp"

namespace NonOpt
{

// Add options
void DirectionComputationCuttingPlane::addOptions(Options* options)
{

  // Add bool options
  options->addBoolOption("DCCP_add_far_points",
                         false,
                         "Determines whether to add points far outside stationarity\n"
                         "              radius to point set during subproblem solve.\n"
                         "Default     : false.");
  options->addBoolOption("DCCP_fail_on_iteration_limit",
                         false,
                         "Determines whether to fail if iteration limit exceeded.\n"
                         "Default     : false.");
  options->addBoolOption("DCCP_fail_on_QP_failure",
                         false,
                         "Determines whether to fail if QP solver ever fails.\n"
                         "Default     : false.");
  options->addBoolOption("DCCP_try_aggregation",
                         false,
                         "Determines whether to consider aggregating subgradients.\n"
                         "Default     : false.");
  options->addBoolOption("DCCP_try_gradient_step",
                         true,
                         "Determines whether to consider gradient step before solving\n"
                         "              cutting plane subproblem.\n"
                         "              Gradient step stepsize set by DCCP_gradient_stepsize parameter.\n"
                         "Default     : true.");
  options->addBoolOption("DCCP_try_shortened_step",
                         true,
                         "Determines whether to consider shortened step if subproblem\n"
                         "              solver does not terminate after considering full QP step.\n"
                         "              Shortened stepsize set by DCCP_shortened_stepsize parameter.\n"
                         "Default     : true.");

  // Add double options
  options->addDoubleOption("DCCP_aggregation_size_threshold",
                           1e+01,
                           0.0,
                           NONOPT_DOUBLE_INFINITY,
                           "Threshold for switching from aggregation to full point set.\n"
                           "Default     : 1e+01.");
  options->addDoubleOption("DCCP_downshift_constant",
                           1e-02,
                           0.0,
                           NONOPT_DOUBLE_INFINITY,
                           "Downshifting constant.  The linear term corresponding to an\n"
                           "              added cut is set as the minimum of the linearization value\n"
                           "              corresponding to the bundle point and the negative of this value\n"
                           "              times the squared norm difference between the bundle point and\n"
                           "              the current iterate.\n"
                           "Default     : 1e-02.");
  options->addDoubleOption("DCCP_gradient_stepsize",
                           1e-04,
                           0.0,
                           NONOPT_DOUBLE_INFINITY,
                           "Gradient stepsize.  If step computed using only the gradient\n"
                           "              at the current iterate and this stepsize is acceptable,\n"
                           "              then full cutting plane subproblem is avoided.  This scheme\n"
                           "              is only considered if DCCP_try_gradient_step == true.\n"
                           "Default     : 1e-04.");
  options->addDoubleOption("DCCP_shortened_stepsize",
                           1e-02,
                           0.0,
                           NONOPT_DOUBLE_INFINITY,
                           "Shortened stepsize.  If full QP step does not offer desired\n"
                           "              objective reduction, then a shortened step corresponding to\n"
                           "              this stepsize is considered if DCCP_try_shortened_step == true.\n"
                           "              In particular, the shortened stepsize that is considered is\n"
                           "              DCCP_shortened_stepsize*min(stat. rad.,||qp_step||_inf)/||qp_step||_inf.\n"
                           "Default     : 1e-02.");
  options->addDoubleOption("DCCP_step_acceptance_tolerance",
                           1e-08,
                           0.0,
                           1.0,
                           "Tolerance for step acceptance.\n"
                           "Default     : 1e-08.");

  // Add integer options
  options->addIntegerOption("DCCP_inner_iteration_limit",
                            20,
                            0,
                            NONOPT_INT_INFINITY,
                            "Limit on the number of inner iterations that will be performed.\n"
                            "Default     : 20.");

} // end addOptions

// Set options
void DirectionComputationCuttingPlane::setOptions(Options* options)
{

  // Read integer options
  options->valueAsBool("DCCP_add_far_points", add_far_points_);
  options->valueAsBool("DCCP_fail_on_iteration_limit", fail_on_iteration_limit_);
  options->valueAsBool("DCCP_fail_on_QP_failure", fail_on_QP_failure_);
  options->valueAsBool("DCCP_try_aggregation", try_aggregation_);
  options->valueAsBool("DCCP_try_gradient_step", try_gradient_step_);
  options->valueAsBool("DCCP_try_shortened_step", try_shortened_step_);

  // Read double options
  options->valueAsDouble("DCCP_aggregation_size_threshold", aggregation_size_threshold_);
  options->valueAsDouble("DCCP_downshift_constant", downshift_constant_);
  options->valueAsDouble("DCCP_gradient_stepsize", gradient_stepsize_);
  options->valueAsDouble("DCCP_shortened_stepsize", shortened_stepsize_);
  options->valueAsDouble("DCCP_step_acceptance_tolerance", step_acceptance_tolerance_);

  // Read integer options
  options->valueAsInteger("DCCP_inner_iteration_limit", inner_iteration_limit_);

} // end setOptions

// Initialize
void DirectionComputationCuttingPlane::initialize(const Options* options,
                                                  Quantities* quantities,
                                                  const Reporter* reporter) {}

// Iteration header
std::string DirectionComputationCuttingPlane::iterationHeader()
{
  return "In. Its.  QP Pts.  QP Its. QP   QP KKT    |Step|   |Step|_H";
}

// Iteration null values string
std::string DirectionComputationCuttingPlane::iterationNullValues()
{
  return "-------- -------- -------- -- --------- --------- ---------";
}

// Compute direction
void DirectionComputationCuttingPlane::computeDirection(const Options* options,
                                                        Quantities* quantities,
                                                        const Reporter* reporter,
                                                        Strategies* strategies)
{

  // Initialize values
  setStatus(DC_UNSET);
  strategies->qpSolver()->setPrimalSolutionToZero();
  quantities->resetInnerIterationCounter();
  quantities->resetQPIterationCounter();
  quantities->setTrialIterateToCurrentIterate();
  clock_t start_time = clock();

  // try direction computation, terminate on any exception
  try {

    // Declare bool for evaluations
    bool evaluation_success;

    // Check whether to evaluate function with gradient
    if (quantities->evaluateFunctionWithGradient()) {

      // Evaluate current objective
      evaluation_success = quantities->currentIterate()->evaluateObjectiveAndGradient(*quantities);

      // Check for successful evaluation
      if (!evaluation_success) {
        THROW_EXCEPTION(DC_EVALUATION_FAILURE_EXCEPTION, "Direction computation unsuccessful. Evaluation failed.");
      }
    }
    else {

      // Evaluate current objective
      evaluation_success = quantities->currentIterate()->evaluateObjective(*quantities);

      // Check for successful evaluation
      if (!evaluation_success) {
        THROW_EXCEPTION(DC_EVALUATION_FAILURE_EXCEPTION, "Direction computation unsuccessful. Evaluation failed.");
      }

      // Evaluate current gradient
      evaluation_success = quantities->currentIterate()->evaluateGradient(*quantities);

      // Check for successful evaluation
      if (!evaluation_success) {
        THROW_EXCEPTION(DC_EVALUATION_FAILURE_EXCEPTION, "Direction computation unsuccessful. Evaluation failed.");
      }

    } // end else

    // Set QP scalars
    strategies->qpSolver()->setScalar(quantities->trustRegionRadius());
    strategies->qpSolver()->setInexactSolutionTolerance(quantities->stationarityRadius());

    // Declare QP quantities
    std::vector<std::shared_ptr<Vector>> QP_gradient_list;
    std::vector<double> QP_vector;

    // Add pointer to current gradient to list
    QP_gradient_list.push_back(quantities->currentIterate()->gradient());

    // Add linear term value
    QP_vector.push_back(quantities->currentIterate()->objective());

    // Set QP data
    strategies->qpSolver()->setVectorList(QP_gradient_list);
    strategies->qpSolver()->setVector(QP_vector);

    // Try gradient step?
    if (try_gradient_step_) {

      // Solve QP
      strategies->qpSolver()->solveQP(options, reporter, quantities);

      // Convert QP solution to step
      convertQPSolutionToStep(quantities, strategies);

      // Compute shortened trial iterate
      quantities->setTrialIterate(quantities->currentIterate()->makeNewLinearCombination(1.0, gradient_stepsize_, *quantities->direction()));

      // Evaluate trial objective
      if (quantities->evaluateFunctionWithGradient()) {
        evaluation_success = quantities->trialIterate()->evaluateObjectiveAndGradient(*quantities);
      }
      else {
        evaluation_success = quantities->trialIterate()->evaluateObjective(*quantities);
      }

      // Check radius update conditions
      strategies->termination()->checkConditionsDirectionComputation(options, quantities, reporter, strategies);

      // Check for sufficient decrease
      if (evaluation_success &&
          (quantities->trialIterate()->objective() - quantities->currentIterate()->objective() < -step_acceptance_tolerance_ * gradient_stepsize_ * fmin(strategies->qpSolver()->dualObjectiveQuadraticValue(), fmax(strategies->qpSolver()->combinationTranslatedNorm2Squared(), strategies->qpSolver()->primalSolutionNorm2Squared())) ||
           strategies->termination()->updateRadiiDirectionComputation())) {
        THROW_EXCEPTION(DC_SUCCESS_EXCEPTION, "Direction computation successful.");
      }

    } // end if (try gradient step)

    // Loop through point set
    for (int point_count = 0; point_count < (int)quantities->pointSet()->size(); point_count++) {

      // Create difference vector
      std::shared_ptr<Vector> difference = quantities->currentIterate()->vector()->makeNewLinearCombination(1.0, -1.0, *(*quantities->pointSet())[point_count]->vector());

      // Check distance between point and current iterate
      if (difference->normInf() <= quantities->stationarityRadius()) {

        // Evaluate objective and gradient
        if (quantities->evaluateFunctionWithGradient()) {
          evaluation_success = (*quantities->pointSet())[point_count]->evaluateObjectiveAndGradient(*quantities);
        }
        else {
          evaluation_success = ((*quantities->pointSet())[point_count]->evaluateObjective(*quantities) && (*quantities->pointSet())[point_count]->evaluateGradient(*quantities));
        }

        // Check for successful evaluation
        if (evaluation_success) {

          // Add pointer to gradient in point set to list
          QP_gradient_list.push_back((*quantities->pointSet())[point_count]->gradient());

          // Evaluate linearization and downshifting values
          double linearization_value = (*quantities->pointSet())[point_count]->objective() + (*quantities->pointSet())[point_count]->gradient()->innerProduct(*quantities->currentIterate()->vector()) - (*quantities->pointSet())[point_count]->gradient()->innerProduct(*((*quantities->pointSet())[point_count])->vector());
          double downshifting_value = quantities->currentIterate()->objective() - downshift_constant_ * pow(difference->norm2(), 2.0);

          // Add linear term value based on cutting plane
          QP_vector.push_back(fmin(linearization_value, downshifting_value));

        } // end if

      } // end if

    } // end for

    // Set QP data
    strategies->qpSolver()->setVectorList(QP_gradient_list);
    strategies->qpSolver()->setVector(QP_vector);

    // Solve QP
    strategies->qpSolver()->solveQP(options, reporter, quantities);

    // Convert QP solution to step
    convertQPSolutionToStep(quantities, strategies);

    // Check for termination on QP failure
    if (strategies->qpSolver()->status() != QP_SUCCESS && fail_on_QP_failure_) {
      THROW_EXCEPTION(DC_QP_FAILURE_EXCEPTION, "Direction computation unsuccessful. QP solver failed.");
    }

    // Check for QP failure
    if (strategies->qpSolver()->status() != QP_SUCCESS) {

      // Clear data
      QP_gradient_list.clear();
      QP_vector.clear();

      // Add pointer to current gradient to list
      QP_gradient_list.push_back(quantities->currentIterate()->gradient());

      // Add linear term value
      QP_vector.push_back(quantities->currentIterate()->objective());

      // Set QP data
      strategies->qpSolver()->setVectorList(QP_gradient_list);
      strategies->qpSolver()->setVector(QP_vector);

      // Solve QP
      strategies->qpSolver()->solveQP(options, reporter, quantities);

      // Convert QP solution to step
      convertQPSolutionToStep(quantities, strategies);

    } // end if

    // Declare and set bool for switch from aggregated to full
    bool switched_to_full = false;

    // Declare aggregated QP quantities
    std::vector<std::shared_ptr<Vector>> QP_gradient_list_aggregated = QP_gradient_list;
    std::vector<double> QP_vector_aggregated = QP_vector;

    // Inner loop
    while (true) {

      // Flush reporter buffer
      reporter->flushBuffer();

      // Evaluate trial iterate objective
      if (quantities->evaluateFunctionWithGradient()) {
        evaluation_success = quantities->trialIterate()->evaluateObjectiveAndGradient(*quantities);
      }
      else {
        evaluation_success = quantities->trialIterate()->evaluateObjective(*quantities);
      }

      // Check radius update conditions
      strategies->termination()->checkConditionsDirectionComputation(options, quantities, reporter, strategies);

      // Check for sufficient decrease
      if (evaluation_success &&
          (quantities->trialIterate()->objective() - quantities->currentIterate()->objective() < -step_acceptance_tolerance_ * fmin(strategies->qpSolver()->dualObjectiveQuadraticValue(), fmax(strategies->qpSolver()->combinationTranslatedNorm2Squared(), strategies->qpSolver()->primalSolutionNorm2Squared())) ||
           strategies->termination()->updateRadiiDirectionComputation())) {
        THROW_EXCEPTION(DC_SUCCESS_EXCEPTION, "Direction computation successful.");
      }

      // Check for inner iteration limit
      if (quantities->innerIterationCounter() > inner_iteration_limit_) {
        if (fail_on_iteration_limit_) {
          THROW_EXCEPTION(DC_ITERATION_LIMIT_EXCEPTION, "Direction computation unsuccessful. Iteration limit exceeded.");
        }
        else {
          THROW_EXCEPTION(DC_SUCCESS_EXCEPTION, "Direction computation successful.");
        }
      } // end if

      // Check for CPU time limit
      if ((clock() - quantities->startTime()) / (double)CLOCKS_PER_SEC >= quantities->cpuTimeLimit()) {
        quantities->incrementDirectionComputationTime(clock() - start_time);
        THROW_EXCEPTION(DC_CPU_TIME_LIMIT_EXCEPTION, "CPU time limit has been reached.");
      }

      // Set aggregated data
      if (try_aggregation_ && !switched_to_full) {

        // Declare omega part of dual solution
        double* omega = new double[strategies->qpSolver()->dualSolutionOmegaLength()];

        // Get omega part of dual solution
        strategies->qpSolver()->dualSolutionOmega(omega);

        // Declare aggregation vector
        std::shared_ptr<Vector> aggregation_vector(new Vector(quantities->numberOfVariables()));

        // Declare aggregation scalar
        double aggregation_scalar = 0.0;

        // Set aggregation vector and scalar values
        for (int i = 0; i < strategies->qpSolver()->dualSolutionOmegaLength(); i++) {
          aggregation_vector->addScaledVector(omega[i], *QP_gradient_list_aggregated[i]);
          aggregation_scalar += omega[i] * (QP_vector_aggregated[i]);
        } // end for

        // Delete omega
        if (omega != nullptr) {
          delete[] omega;
          omega = nullptr;
        } // end if

        // Clear data
        QP_gradient_list_aggregated.clear();
        QP_vector_aggregated.clear();

        // Add current iterate term
        QP_gradient_list_aggregated.push_back(quantities->currentIterate()->gradient());
        QP_vector_aggregated.push_back(quantities->currentIterate()->objective());

        // Add aggregation vector and scalar
        QP_gradient_list_aggregated.push_back(aggregation_vector);
        QP_vector_aggregated.push_back(aggregation_scalar);

      } // end if

      // Declare new QP data
      std::vector<std::shared_ptr<Vector>> QP_gradient_list_new;
      std::vector<double> QP_vector_new;

      // Check if adding far points
      if (add_far_points_ || strategies->qpSolver()->primalSolutionNormInf() <= quantities->stationarityRadius()) {

        // Check for objective successful evaluation
        if (evaluation_success) {

          // Evaluate trial iterate gradient
          if (!quantities->evaluateFunctionWithGradient()) {
            evaluation_success = quantities->trialIterate()->evaluateGradient(*quantities);
          }

          // Check for gradient successful evaluation
          if (evaluation_success) {

            // Add trial iterate to point set
            quantities->pointSet()->push_back(quantities->trialIterate());

            // Add pointer to gradient in point set to list
            QP_gradient_list_new.push_back(quantities->trialIterate()->gradient());
            if (try_aggregation_ && !switched_to_full) {
              QP_gradient_list.push_back(quantities->trialIterate()->gradient());
              QP_gradient_list_aggregated.push_back(quantities->trialIterate()->gradient());
            } // end if

            // Create difference vector
            std::shared_ptr<Vector> difference = quantities->currentIterate()->vector()->makeNewLinearCombination(1.0, -1.0, *quantities->trialIterate()->vector());

            // Evaluate linearization and downshifting values
            double linearization_value = quantities->trialIterate()->objective() + quantities->trialIterate()->gradient()->innerProduct(*quantities->currentIterate()->vector()) - quantities->trialIterate()->gradient()->innerProduct(*quantities->trialIterate()->vector());
            double downshifting_value = quantities->currentIterate()->objective() - downshift_constant_ * pow(difference->norm2(), 2.0);

            // Add linear term value based on cutting plane
            QP_vector_new.push_back(fmin(linearization_value, downshifting_value));
            if (try_aggregation_ && !switched_to_full) {
              QP_vector.push_back(fmin(linearization_value, downshifting_value));
              QP_vector_aggregated.push_back(fmin(linearization_value, downshifting_value));
            } // end if

          } // end if

        } // end if

      } // end if

      // Try shortened step?
      if (try_shortened_step_) {

        // Set shortened stepsize
        double shortened_stepsize = shortened_stepsize_ * fmin(quantities->stationarityRadius(), strategies->qpSolver()->primalSolutionNormInf()) / strategies->qpSolver()->primalSolutionNormInf();

        // Compute shortened trial iterate
        quantities->setTrialIterate(quantities->currentIterate()->makeNewLinearCombination(1.0, shortened_stepsize, *quantities->direction()));

        // Evaluate trial objective
        if (quantities->evaluateFunctionWithGradient()) {
          evaluation_success = quantities->trialIterate()->evaluateObjectiveAndGradient(*quantities);
        }
        else {
          evaluation_success = quantities->trialIterate()->evaluateObjective(*quantities);
        }

        // Check radius update conditions
        strategies->termination()->checkConditionsDirectionComputation(options, quantities, reporter, strategies);

        // Check for sufficient decrease
        if (evaluation_success &&
            (quantities->trialIterate()->objective() - quantities->currentIterate()->objective() < -step_acceptance_tolerance_ * shortened_stepsize * fmin(strategies->qpSolver()->dualObjectiveQuadraticValue(), fmax(strategies->qpSolver()->combinationTranslatedNorm2Squared(), strategies->qpSolver()->primalSolutionNorm2Squared())) ||
             strategies->termination()->updateRadiiDirectionComputation())) {
          THROW_EXCEPTION(DC_SUCCESS_EXCEPTION, "Direction computation successful.");
        }

        // Check for objective successful evaluation
        if (evaluation_success) {

          // Evaluate trial gradient
          if (!quantities->evaluateFunctionWithGradient()) {
            evaluation_success = quantities->trialIterate()->evaluateGradient(*quantities);
          }

          // Check for gradient successful evaluation
          if (evaluation_success) {

            // Add trial iterate to point set
            quantities->pointSet()->push_back(quantities->trialIterate());

            // Add pointer to gradient in point set to list
            QP_gradient_list_new.push_back(quantities->trialIterate()->gradient());
            if (try_aggregation_ && !switched_to_full) {
              QP_gradient_list.push_back(quantities->trialIterate()->gradient());
              QP_gradient_list_aggregated.push_back(quantities->trialIterate()->gradient());
            } // end if

            // Create difference vector
            std::shared_ptr<Vector> difference = quantities->currentIterate()->vector()->makeNewLinearCombination(1.0, -1.0, *quantities->trialIterate()->vector());

            // Evaluate linearization and downshifting values
            double linearization_value = quantities->trialIterate()->objective() + quantities->trialIterate()->gradient()->innerProduct(*quantities->currentIterate()->vector()) - quantities->trialIterate()->gradient()->innerProduct(*quantities->trialIterate()->vector());
            double downshifting_value = quantities->currentIterate()->objective() - downshift_constant_ * pow(difference->norm2(), 2.0);

            // Add linear term value based on cutting plane
            QP_vector_new.push_back(fmin(linearization_value, downshifting_value));
            if (try_aggregation_ && !switched_to_full) {
              QP_vector.push_back(fmin(linearization_value, downshifting_value));
              QP_vector_aggregated.push_back(fmin(linearization_value, downshifting_value));
            } // end if

          } // end if

        } // end if

      } // end if (try_shortened_step_)

      // Print QP solve / step information
      reporter->printf(R_NL, R_PER_INNER_ITERATION, " %8d %8d %8d %2d %+.2e %+.2e %+.2e", quantities->innerIterationCounter(), strategies->qpSolver()->vectorListLength(), quantities->QPIterationCounter(), strategies->qpSolver()->status(), strategies->qpSolver()->KKTErrorDual(), strategies->qpSolver()->primalSolutionNormInf(), strategies->qpSolver()->dualObjectiveQuadraticValue());

      // Set blank solve string
      std::string blank_solve = "";
      if (strategies->termination()->iterationNullValues().length() > 0) {
        blank_solve += " ";
        blank_solve += strategies->termination()->iterationNullValues();
      } // end if
      if (strategies->lineSearch()->iterationNullValues().length() > 0) {
        blank_solve += " ";
        blank_solve += strategies->lineSearch()->iterationNullValues();
      } // end if
      if (strategies->approximateHessianUpdate()->iterationNullValues().length() > 0) {
        blank_solve += " ";
        blank_solve += strategies->approximateHessianUpdate()->iterationNullValues();
      } // end if
      if (strategies->pointSetUpdate()->iterationNullValues().length() > 0) {
        blank_solve += " ";
        blank_solve += strategies->pointSetUpdate()->iterationNullValues();
      } // end if

      // Print blank solve information
      reporter->printf(R_NL, R_PER_INNER_ITERATION, "%s\n%s", blank_solve.c_str(), quantities->iterationNullValues().c_str());

      // Set QP data and solve
      if (try_aggregation_ && !switched_to_full && (int)quantities->pointSet()->size() < (int)(aggregation_size_threshold_ * (double)quantities->numberOfVariables())) {
        strategies->qpSolver()->setVectorList(QP_gradient_list_aggregated);
        strategies->qpSolver()->setVector(QP_vector_aggregated);
        strategies->qpSolver()->solveQP(options, reporter, quantities);
      } // end if
      else if (try_aggregation_ && !switched_to_full) {
        strategies->qpSolver()->setVectorList(QP_gradient_list);
        strategies->qpSolver()->setVector(QP_vector);
        strategies->qpSolver()->solveQP(options, reporter, quantities);
        switched_to_full = true;
      } // end else if
      else {
        strategies->qpSolver()->addData(QP_gradient_list_new, QP_vector_new);
        strategies->qpSolver()->solveQPHot(options, reporter, quantities);
      } // end else

      // Convert QP solution to step
      convertQPSolutionToStep(quantities, strategies);

      // Check for termination on QP failure
      if (strategies->qpSolver()->status() != QP_SUCCESS && fail_on_QP_failure_) {
        THROW_EXCEPTION(DC_QP_FAILURE_EXCEPTION, "Direction computation unsuccessful. QP solver failed.");
      }

      // Check for QP failure
      if (strategies->qpSolver()->status() != QP_SUCCESS) {

        // Clear data
        QP_gradient_list.clear();
        QP_vector.clear();

        // Add pointer to current gradient to list
        QP_gradient_list.push_back(quantities->currentIterate()->gradient());

        // Add linear term value
        QP_vector.push_back(quantities->currentIterate()->objective());

        // Set QP data
        strategies->qpSolver()->setVectorList(QP_gradient_list);
        strategies->qpSolver()->setVector(QP_vector);

        // Solve QP
        strategies->qpSolver()->solveQP(options, reporter, quantities);

        // Convert QP solution to step
        convertQPSolutionToStep(quantities, strategies);

        // Copy to aggregated values
        QP_gradient_list_aggregated = QP_gradient_list;
        QP_vector_aggregated = QP_vector;

      } // end if

    } // end while

  } // end try

  // catch exceptions
  catch (DC_SUCCESS_EXCEPTION& exec) {
    setStatus(DC_SUCCESS);
  } catch (DC_CPU_TIME_LIMIT_EXCEPTION& exec) {
    setStatus(DC_CPU_TIME_LIMIT);
    THROW_EXCEPTION(NONOPT_CPU_TIME_LIMIT_EXCEPTION, "CPU time limit has been reached.");
  } catch (DC_EVALUATION_FAILURE_EXCEPTION& exec) {
    setStatus(DC_EVALUATION_FAILURE);
  } catch (DC_ITERATION_LIMIT_EXCEPTION& exec) {
    setStatus(DC_ITERATION_LIMIT);
  } catch (DC_QP_FAILURE_EXCEPTION& exec) {
    setStatus(DC_QP_FAILURE);
  }

  // Print iteration information
  reporter->printf(R_NL, R_PER_ITERATION, " %8d %8d %8d %2d %+.2e %+.2e %+.2e", quantities->innerIterationCounter(), strategies->qpSolver()->vectorListLength(), quantities->QPIterationCounter(), strategies->qpSolver()->status(), strategies->qpSolver()->KKTErrorDual(), strategies->qpSolver()->primalSolutionNormInf(), strategies->qpSolver()->dualObjectiveQuadraticValue());

  // Increment total inner iteration counter
  quantities->incrementTotalInnerIterationCounter();

  // Increment total QP iteration counter
  quantities->incrementTotalQPIterationCounter();

  // Increment direction computation time
  quantities->incrementDirectionComputationTime(clock() - start_time);

} // end computeDirection

// Convert QP solution to step
void DirectionComputationCuttingPlane::convertQPSolutionToStep(Quantities* quantities,
                                                               Strategies* strategies)
{

  // Increment QP iteration counter
  quantities->incrementQPIterationCounter(strategies->qpSolver()->numberOfIterations());

  // Increment inner iteration counter
  quantities->incrementInnerIterationCounter(1);

  // Get primal solution
  strategies->qpSolver()->primalSolution(quantities->direction()->valuesModifiable());

  // Set trial iterate
  quantities->setTrialIterate(quantities->currentIterate()->makeNewLinearCombination(1.0, 1.0, *quantities->direction()));

} // end convertQPSolutionToStep

} // namespace NonOpt
