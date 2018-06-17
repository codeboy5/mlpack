/**
 * @file cf_test.cpp
 * @author Mudit Raj Gupta
 * @author Haritha Nair
 *
 * Test file for CF class.
 *
 * mlpack is free software; you may redistribute it and/or modify it under the
 * terms of the 3-clause BSD license.  You should have received a copy of the
 * 3-clause BSD license along with mlpack.  If not, see
 * http://www.opensource.org/licenses/BSD-3-Clause for more information.
 */

#include <mlpack/core.hpp>
#include <mlpack/methods/cf/cf.hpp>
#include <mlpack/methods/cf/decomposition_policies/batch_svd_method.hpp>
#include <mlpack/methods/cf/decomposition_policies/randomized_svd_method.hpp>
#include <mlpack/methods/cf/decomposition_policies/regularized_svd_method.hpp>
#include <mlpack/methods/cf/decomposition_policies/svd_complete_method.hpp>
#include <mlpack/methods/cf/decomposition_policies/svd_incomplete_method.hpp>
#include <mlpack/methods/cf/normalization/no_normalization.hpp>
#include <mlpack/methods/cf/normalization/overall_mean_normalization.hpp>
#include <mlpack/methods/cf/normalization/user_mean_normalization.hpp>
#include <mlpack/methods/cf/normalization/item_mean_normalization.hpp>
#include <mlpack/methods/cf/normalization/z_score_normalization.hpp>
#include <mlpack/methods/cf/normalization/combined_normalization.hpp>
#include <mlpack/methods/cf/neighbor_search_policies/lmetric_search.hpp>
#include <mlpack/methods/cf/neighbor_search_policies/cosine_search.hpp>
#include <mlpack/methods/cf/neighbor_search_policies/pearson_search.hpp>
#include <mlpack/methods/cf/interpolation_policies/average_interpolation.hpp>
#include <mlpack/methods/cf/interpolation_policies/similarity_interpolation.hpp>
#include <mlpack/methods/cf/interpolation_policies/regression_interpolation.hpp>

#include <iostream>

#include <boost/test/unit_test.hpp>
#include "test_tools.hpp"
#include "serialization.hpp"

BOOST_AUTO_TEST_SUITE(CFTest);

using namespace mlpack;
using namespace mlpack::cf;
using namespace std;

// Get train and test dataset. We save datasets as static variables
// to avoid reloading and regenerating datasets.
static void GetDatasets(arma::mat& dataset, arma::mat& savedCols)
{
  // Whether datasets have been initialized.
  static bool datasetsInitialized = false;

  static arma::mat initDataset;
  static arma::mat initSavedCols(3, 50);

  // If datasets have been initialized, just pass them to parameters.
  if (datasetsInitialized)
  {
    dataset = initDataset;
    savedCols = initSavedCols;
    return;
  }

  // If datasets haven't been initialized, initialize them.
  data::Load("GroupLensSmall.csv", initDataset);

  // Save the columns we've removed.
  initSavedCols.fill(/* random very large value */ 10000000);
  size_t currentCol = 0;
  for (size_t i = 0; i < initDataset.n_cols; ++i)
  {
    if (currentCol == 50)
      break;

    if (initDataset(2, i) > 4.5) // 5-star rating.
    {
      // Make sure we don't have this user yet.  This is a slow way to do this
      // but I don't particularly care here because it's in the tests.
      bool found = false;
      for (size_t j = 0; j < currentCol; ++j)
      {
        if (initSavedCols(0, j) == initDataset(0, i))
        {
          found = true;
          break;
        }
      }

      // If this user doesn't already exist in initSavedCols, add them.
      // Otherwise ignore this point.
      if (!found)
      {
        initSavedCols.col(currentCol) = initDataset.col(i);
        initDataset.shed_col(i);
        ++currentCol;
      }
    }
  }

  // Pass datasets to parameters and set datasetsInitialized to true.
  dataset = initDataset;
  savedCols = initSavedCols;
  datasetsInitialized = true;
}

/**
 * Make sure that correct number of recommendations are generated when query
 * set. Default case.
 */
template<typename DecompositionPolicy>
void GetRecommendationsAllUsers()
{
  DecompositionPolicy decomposition;
  // Dummy number of recommendations.
  size_t numRecs = 3;
  // GroupLensSmall.csv dataset has 200 users.
  size_t numUsers = 200;

  // Matrix to save recommendations into.
  arma::Mat<size_t> recommendations;

  // Load GroupLens data.
  arma::mat dataset;
  data::Load("GroupLensSmall.csv", dataset);

  CFType<> c(dataset, decomposition, 5, 5, 70);

  // Generate recommendations when query set is not specified.
  c.GetRecommendations(numRecs, recommendations);

  // Check if correct number of recommendations are generated.
  BOOST_REQUIRE_EQUAL(recommendations.n_rows, numRecs);

  // Check if recommendations are generated for all users.
  BOOST_REQUIRE_EQUAL(recommendations.n_cols, numUsers);
}

/**
 * Make sure that the recommendations are generated for queried users only.
 */
template<typename DecompositionPolicy>
void GetRecommendationsQueriedUser()
{
  DecompositionPolicy decomposition;
  // Number of users that we will search for recommendations for.
  size_t numUsers = 10;

  // Default number of recommendations.
  size_t numRecsDefault = 5;

  // Create dummy query set.
  arma::Col<size_t> users = arma::zeros<arma::Col<size_t> >(numUsers, 1);
  for (size_t i = 0; i < numUsers; i++)
    users(i) = i;

  // Matrix to save recommendations into.
  arma::Mat<size_t> recommendations;

  // Load GroupLens data.
  arma::mat dataset;
  data::Load("GroupLensSmall.csv", dataset);

  CFType<> c(dataset, decomposition, 5, 5, 70);

  // Generate recommendations when query set is specified.
  c.GetRecommendations(numRecsDefault, recommendations, users);

  // Check if correct number of recommendations are generated.
  BOOST_REQUIRE_EQUAL(recommendations.n_rows, numRecsDefault);

  // Check if recommendations are generated for the right number of users.
  BOOST_REQUIRE_EQUAL(recommendations.n_cols, numUsers);
}

/**
 * Make sure recommendations that are generated are reasonably accurate.
 */
template<typename DecompositionPolicy,
         typename NormalizationType = NoNormalization>
void RecommendationAccuracy()
{
  DecompositionPolicy decomposition;

  // Small GroupLens dataset.
  arma::mat dataset;

  // Save the columns we've removed.
  arma::mat savedCols;

  GetDatasets(dataset, savedCols);

  CFType<NormalizationType> c(dataset, decomposition, 5, 5, 70);

  // Obtain 150 recommendations for the users in savedCols, and make sure the
  // missing item shows up in most of them.  First, create the list of users,
  // which requires casting from doubles...
  arma::Col<size_t> users(50);
  for (size_t i = 0; i < 50; ++i)
    users(i) = (size_t) savedCols(0, i);
  arma::Mat<size_t> recommendations;
  size_t numRecs = 150;
  c.GetRecommendations(numRecs, recommendations, users);

  BOOST_REQUIRE_EQUAL(recommendations.n_rows, numRecs);
  BOOST_REQUIRE_EQUAL(recommendations.n_cols, 50);

  size_t failures = 0;
  for (size_t i = 0; i < 50; ++i)
  {
    size_t targetItem = (size_t) savedCols(1, i);
    bool found = false;
    // Make sure the target item shows up in the recommendations.
    for (size_t j = 0; j < numRecs; ++j)
    {
      const size_t user = users(i);
      const size_t item = recommendations(j, i);
      if (item == targetItem)
      {
        found = true;
      }
      else
      {
        // Make sure we aren't being recommended an item that the user already
        // rated.
        BOOST_REQUIRE_EQUAL((double) c.CleanedData()(item, user), 0.0);
      }
    }

    if (!found)
      ++failures;
  }

  // Make sure the right item showed up in at least 1/3 of the recommendations.
  // GroupLens dataset would give somewhere around a 10% success rate (failures
  // would be closer to 270).  The failure rate is allowed to be so high because
  // the dataset used here is pretty small and it is hard to generalize.
  BOOST_REQUIRE_LT(failures, 17);
}

// Make sure that Predict() is returning reasonable results.
template<typename DecompositionPolicy,
         typename NormalizationType = NoNormalization,
         typename NeighborSearchPolicy = EuclideanSearch,
         typename InterpolationPolicy = AverageInterpolation>
void CFPredict(const double rmseBound = 2.0)
{
  DecompositionPolicy decomposition;

  // Small GroupLens dataset.
  arma::mat dataset;

  // Save the columns we've removed.
  arma::mat savedCols;

  GetDatasets(dataset, savedCols);

  CFType<NormalizationType> c(dataset, decomposition, 5, 5, 70);

  // Now, for each removed rating, make sure the prediction is... reasonably
  // accurate.
  double totalError = 0.0;
  for (size_t i = 0; i < savedCols.n_cols; ++i)
  {
    const double prediction = c.template Predict<NeighborSearchPolicy,
        InterpolationPolicy>(savedCols(0, i), savedCols(1, i));

    const double error = std::pow(prediction - savedCols(2, i), 2.0);
    totalError += error;
  }

  const double rmse = std::sqrt(totalError / savedCols.n_cols);

  // The root mean square error should be less than ?.
  BOOST_REQUIRE_LT(rmse, rmseBound);
}

// Do the same thing as the previous test, but ensure that the ratings we
// predict with the batch Predict() are the same as the individual Predict()
// calls.
template<typename DecompositionPolicy>
void BatchPredict()
{
  DecompositionPolicy decomposition;

  // Small GroupLens dataset.
  arma::mat dataset;

  // Save the columns we've removed.
  arma::mat savedCols;

  GetDatasets(dataset, savedCols);

  CFType<> c(dataset, decomposition, 5, 5, 70);

  // Get predictions for all user/item pairs we held back.
  arma::Mat<size_t> combinations(2, savedCols.n_cols);
  for (size_t i = 0; i < savedCols.n_cols; ++i)
  {
    combinations(0, i) = size_t(savedCols(0, i));
    combinations(1, i) = size_t(savedCols(1, i));
  }

  arma::vec predictions;
  c.Predict(combinations, predictions);

  for (size_t i = 0; i < combinations.n_cols; ++i)
  {
    const double prediction = c.Predict(combinations(0, i), combinations(1, i));
    BOOST_REQUIRE_CLOSE(prediction, predictions[i], 1e-8);
  }
}

/**
 * Make sure we can train an already-trained model and it works okay.
 */
template<typename DecompositionPolicy>
void Train(DecompositionPolicy& decomposition)
{
  // Generate random data.
  arma::sp_mat randomData;
  randomData.sprandu(100, 100, 0.3);
  CFType<> c(randomData, decomposition, 5, 5, 70);

  // Small GroupLens dataset.
  arma::mat dataset;

  // Save the columns we've removed.
  arma::mat savedCols;

  GetDatasets(dataset, savedCols);

  // Make data into sparse matrix.
  arma::sp_mat cleanedData;
  CFType<>::CleanData(dataset, cleanedData);

  // Now retrain.
  c.Train(dataset, decomposition, 70);

  // Get predictions for all user/item pairs we held back.
  arma::Mat<size_t> combinations(2, savedCols.n_cols);
  for (size_t i = 0; i < savedCols.n_cols; ++i)
  {
    combinations(0, i) = size_t(savedCols(0, i));
    combinations(1, i) = size_t(savedCols(1, i));
  }

  arma::vec predictions;
  c.Predict(combinations, predictions);

  for (size_t i = 0; i < combinations.n_cols; ++i)
  {
    const double prediction = c.Predict(combinations(0, i), combinations(1, i));
    BOOST_REQUIRE_CLOSE(prediction, predictions[i], 1e-8);
  }
}

/**
 * Make sure we can train an already-trained model and it works okay
 * for policies that use coordinate lists.
 */
template<>
void Train<>(RegSVDPolicy& decomposition)
{
  arma::mat randomData = arma::zeros(100, 100);
  randomData.diag().ones();
  CFType<> c(randomData, decomposition, 5, 5, 70);

  // Now retrain with data we know about.
  // Small GroupLens dataset.
  arma::mat dataset;

  // Save the columns we've removed.
  arma::mat savedCols;

  GetDatasets(dataset, savedCols);

  // Now retrain.
  c.Train(dataset, decomposition, 70);

  // Get predictions for all user/item pairs we held back.
  arma::Mat<size_t> combinations(2, savedCols.n_cols);
  for (size_t i = 0; i < savedCols.n_cols; ++i)
  {
    combinations(0, i) = size_t(savedCols(0, i));
    combinations(1, i) = size_t(savedCols(1, i));
  }

  arma::vec predictions;
  c.Predict(combinations, predictions);

  for (size_t i = 0; i < combinations.n_cols; ++i)
  {
    const double prediction = c.Predict(combinations(0, i), combinations(1, i));
    BOOST_REQUIRE_CLOSE(prediction, predictions[i], 1e-8);
  }
}

/**
 * Make sure we can train a model after using the empty constructor.
 */
template<typename DecompositionPolicy>
void EmptyConstructorTrain()
{
  DecompositionPolicy decomposition;
  // Use default constructor.
  CFType<> c;

  // Now retrain with data we know about.
  // Small GroupLens dataset.
  arma::mat dataset;

  // Save the columns we've removed.
  arma::mat savedCols;

  GetDatasets(dataset, savedCols);

  c.Train(dataset, decomposition, 70);

  // Get predictions for all user/item pairs we held back.
  arma::Mat<size_t> combinations(2, savedCols.n_cols);
  for (size_t i = 0; i < savedCols.n_cols; ++i)
  {
    combinations(0, i) = size_t(savedCols(0, i));
    combinations(1, i) = size_t(savedCols(1, i));
  }

  arma::vec predictions;
  c.Predict(combinations, predictions);

  for (size_t i = 0; i < combinations.n_cols; ++i)
  {
    const double prediction = c.Predict(combinations(0, i),
        combinations(1, i));
    BOOST_REQUIRE_CLOSE(prediction, predictions[i], 1e-8);
  }
}

/**
 * Ensure we can load and save the CF model.
 */
template<typename DecompositionPolicy,
         typename NormalizationType = NoNormalization>
void Serialization()
{
  DecompositionPolicy decomposition;
  // Load a dataset to train on.
  arma::mat dataset;
  data::Load("GroupLensSmall.csv", dataset);

  arma::sp_mat cleanedData;
  CFType<NormalizationType>::CleanData(dataset, cleanedData);

  CFType<NormalizationType> c(cleanedData, decomposition, 5, 5, 70);

  arma::sp_mat randomData;
  randomData.sprandu(100, 100, 0.3);

  CFType<NormalizationType> cXml(randomData, decomposition, 5, 5, 70);
  CFType<NormalizationType> cBinary;
  CFType<NormalizationType> cText(cleanedData, decomposition, 5, 5, 70);

  SerializeObjectAll(c, cXml, cText, cBinary);

  // Check the internals.
  BOOST_REQUIRE_EQUAL(c.NumUsersForSimilarity(), cXml.NumUsersForSimilarity());
  BOOST_REQUIRE_EQUAL(c.NumUsersForSimilarity(),
      cBinary.NumUsersForSimilarity());
  BOOST_REQUIRE_EQUAL(c.NumUsersForSimilarity(), cText.NumUsersForSimilarity());

  BOOST_REQUIRE_EQUAL(c.Rank(), cXml.Rank());
  BOOST_REQUIRE_EQUAL(c.Rank(), cBinary.Rank());
  BOOST_REQUIRE_EQUAL(c.Rank(), cText.Rank());

  CheckMatrices(c.W(), cXml.W(), cBinary.W(), cText.W());
  CheckMatrices(c.H(), cXml.H(), cBinary.H(), cText.H());

  BOOST_REQUIRE_EQUAL(c.CleanedData().n_rows, cXml.CleanedData().n_rows);
  BOOST_REQUIRE_EQUAL(c.CleanedData().n_rows, cBinary.CleanedData().n_rows);
  BOOST_REQUIRE_EQUAL(c.CleanedData().n_rows, cText.CleanedData().n_rows);

  BOOST_REQUIRE_EQUAL(c.CleanedData().n_cols, cXml.CleanedData().n_cols);
  BOOST_REQUIRE_EQUAL(c.CleanedData().n_cols, cBinary.CleanedData().n_cols);
  BOOST_REQUIRE_EQUAL(c.CleanedData().n_cols, cText.CleanedData().n_cols);

  BOOST_REQUIRE_EQUAL(c.CleanedData().n_nonzero, cXml.CleanedData().n_nonzero);
  BOOST_REQUIRE_EQUAL(c.CleanedData().n_nonzero,
      cBinary.CleanedData().n_nonzero);
  BOOST_REQUIRE_EQUAL(c.CleanedData().n_nonzero, cText.CleanedData().n_nonzero);

#if ARMA_VERSION_MAJOR >= 8
  c.CleanedData().sync();
#endif

  for (size_t i = 0; i <= c.CleanedData().n_cols; ++i)
  {
    BOOST_REQUIRE_EQUAL(c.CleanedData().col_ptrs[i],
        cXml.CleanedData().col_ptrs[i]);
    BOOST_REQUIRE_EQUAL(c.CleanedData().col_ptrs[i],
        cBinary.CleanedData().col_ptrs[i]);
    BOOST_REQUIRE_EQUAL(c.CleanedData().col_ptrs[i],
        cText.CleanedData().col_ptrs[i]);
  }

  for (size_t i = 0; i <= c.CleanedData().n_nonzero; ++i)
  {
    BOOST_REQUIRE_EQUAL(c.CleanedData().row_indices[i],
        cXml.CleanedData().row_indices[i]);
    BOOST_REQUIRE_EQUAL(c.CleanedData().row_indices[i],
        cBinary.CleanedData().row_indices[i]);
    BOOST_REQUIRE_EQUAL(c.CleanedData().row_indices[i],
        cText.CleanedData().row_indices[i]);

    BOOST_REQUIRE_CLOSE(c.CleanedData().values[i], cXml.CleanedData().values[i],
        1e-5);
    BOOST_REQUIRE_CLOSE(c.CleanedData().values[i],
        cBinary.CleanedData().values[i], 1e-5);
    BOOST_REQUIRE_CLOSE(c.CleanedData().values[i],
        cText.CleanedData().values[i], 1e-5);
  }
}

/**
 * Make sure that correct number of recommendations are generated when query
 * set for randomized SVD.
 */
BOOST_AUTO_TEST_CASE(CFGetRecommendationsAllUsersRandSVDTest)
{
  GetRecommendationsAllUsers<RandomizedSVDPolicy>();
}

/**
 * Make sure that correct number of recommendations are generated when query
 * set for regularized SVD.
 */
BOOST_AUTO_TEST_CASE(CFGetRecommendationsAllUsersRegSVDTest)
{
  GetRecommendationsAllUsers<RegSVDPolicy>();
}

/**
 * Make sure that correct number of recommendations are generated when query
 * set for Batch SVD.
 */

BOOST_AUTO_TEST_CASE(CFGetRecommendationsAllUsersBatchSVDTest)
{
  GetRecommendationsAllUsers<BatchSVDPolicy>();
}

/**
 * Make sure that correct number of recommendations are generated when query
 * set for NMF.
 */
BOOST_AUTO_TEST_CASE(CFGetRecommendationsAllUsersNMFTest)
{
  GetRecommendationsAllUsers<NMFPolicy>();
}

/**
 * Make sure that correct number of recommendations are generated when query
 * set for SVD Complete Incremental method.
 */
BOOST_AUTO_TEST_CASE(CFGetRecommendationsAllUsersSVDCompleteTest)
{
  GetRecommendationsAllUsers<SVDCompletePolicy>();
}

/**
 * Make sure that correct number of recommendations are generated when query
 * set for SVD Incomplete Incremental method.
 */
BOOST_AUTO_TEST_CASE(CFGetRecommendationsAllUsersSVDIncompleteTest)
{
  GetRecommendationsAllUsers<SVDIncompletePolicy>();
}

/**
 * Make sure that the recommendations are generated for queried users only
 * for randomized SVD.
 */
BOOST_AUTO_TEST_CASE(CFGetRecommendationsQueriedUserRandSVDTest)
{
  GetRecommendationsQueriedUser<RandomizedSVDPolicy>();
}

/**
 * Make sure that the recommendations are generated for queried users only
 * for regularized SVD.
 */
BOOST_AUTO_TEST_CASE(CFGetRecommendationsQueriedUserRegSVDTest)
{
  GetRecommendationsQueriedUser<RegSVDPolicy>();
}

/**
 * Make sure that the recommendations are generated for queried users only
 * for batch SVD.
 */
BOOST_AUTO_TEST_CASE(CFGetRecommendationsQueriedUserBatchSVDTest)
{
  GetRecommendationsQueriedUser<BatchSVDPolicy>();
}

/**
 * Make sure that the recommendations are generated for queried users only
 * for NMF.
 */
BOOST_AUTO_TEST_CASE(CFGetRecommendationsQueriedUserNMFTest)
{
  GetRecommendationsQueriedUser<NMFPolicy>();
}

/**
 * Make sure that the recommendations are generated for queried users only
 * for SVD Complete Incremental method.
 */
BOOST_AUTO_TEST_CASE(CFGetRecommendationsQueriedUserSVDCompleteTest)
{
  GetRecommendationsQueriedUser<SVDCompletePolicy>();
}

/**
 * Make sure that the recommendations are generated for queried users only
 * for SVD Incomplete Incremental method.
 */
BOOST_AUTO_TEST_CASE(CFGetRecommendationsQueriedUserSVDIncompleteTest)
{
  GetRecommendationsQueriedUser<SVDIncompletePolicy>();
}

/**
 * Make sure recommendations that are generated are reasonably accurate
 * for randomized SVD.
 */
BOOST_AUTO_TEST_CASE(RecommendationAccuracyRandSVDTest)
{
  RecommendationAccuracy<RandomizedSVDPolicy>();
}

/**
 * Make sure recommendations that are generated are reasonably accurate
 * for regularized SVD.
 */
BOOST_AUTO_TEST_CASE(RecommendationAccuracyRegSVDTest)
{
  RecommendationAccuracy<RegSVDPolicy>();
}

/**
 * Make sure recommendations that are generated are reasonably accurate
 * for batch SVD.
 */
BOOST_AUTO_TEST_CASE(RecommendationAccuracyBatchSVDTest)
{
  RecommendationAccuracy<BatchSVDPolicy>();
}

/**
 * Make sure recommendations that are generated are reasonably accurate
 * for NMF.
 */
BOOST_AUTO_TEST_CASE(RecommendationAccuracyNMFTest)
{
  RecommendationAccuracy<NMFPolicy>();
}

/**
 * Make sure recommendations that are generated are reasonably accurate
 * for SVD Complete Incremental method.
 */
BOOST_AUTO_TEST_CASE(RecommendationAccuracySVDCompleteTest)
{
  RecommendationAccuracy<SVDCompletePolicy>();
}

/**
 * Make sure recommendations that are generated are reasonably accurate
 * for SVD Incomplete Incremental method.
 */ 
BOOST_AUTO_TEST_CASE(RecommendationAccuracySVDIncompleteTest)
{
  RecommendationAccuracy<SVDIncompletePolicy>();
}

// Make sure that Predict() is returning reasonable results for randomized SVD.
BOOST_AUTO_TEST_CASE(CFPredictRandSVDTest)
{
  CFPredict<RandomizedSVDPolicy>(4.5);
}

// Make sure that Predict() is returning reasonable results for regularized SVD.
BOOST_AUTO_TEST_CASE(CFPredictRegSVDTest)
{
  CFPredict<RegSVDPolicy>();
}

// Make sure that Predict() is returning reasonable results for batch SVD.
BOOST_AUTO_TEST_CASE(CFPredictBatchSVDTest)
{
  CFPredict<BatchSVDPolicy>();
}

// Make sure that Predict() is returning reasonable results for NMF.
BOOST_AUTO_TEST_CASE(CFPredictNMFTest)
{
  CFPredict<NMFPolicy>(3.5);
}

/**
 * Make sure that Predict() is returning reasonable results for SVD Complete
 * Incremental method.
 */
BOOST_AUTO_TEST_CASE(CFPredictSVDCompleteTest)
{
  CFPredict<SVDCompletePolicy>(3.5);
}

/**
 * Make sure that Predict() is returning reasonable results for SVD Incomplete
 * Incremental method.
 */
BOOST_AUTO_TEST_CASE(CFPredictSVDIncompleteTest)
{
  CFPredict<SVDIncompletePolicy>(3.5);
}

// Compare batch Predict() and individual Predict() for randomized SVD.
BOOST_AUTO_TEST_CASE(CFBatchPredictRandSVDTest)
{
  BatchPredict<RandomizedSVDPolicy>();
}

// Compare batch Predict() and individual Predict() for regularized SVD.
BOOST_AUTO_TEST_CASE(CFBatchPredictRegSVDTest)
{
  BatchPredict<RegSVDPolicy>();
}

// Compare batch Predict() and individual Predict() for batch SVD.
BOOST_AUTO_TEST_CASE(CFBatchPredictBatchSVDTest)
{
  BatchPredict<BatchSVDPolicy>();
}

// Compare batch Predict() and individual Predict() for NMF.
BOOST_AUTO_TEST_CASE(CFBatchPredictNMFTest)
{
  BatchPredict<NMFPolicy>();
}

// Compare batch Predict() and individual Predict() for
// SVD Complete Incremental method.
BOOST_AUTO_TEST_CASE(CFBatchPredictSVDCompleteTest)
{
  BatchPredict<SVDCompletePolicy>();
}

// Compare batch Predict() and individual Predict() for
// SVD Incomplete Incremental method.
BOOST_AUTO_TEST_CASE(CFBatchPredictSVDIncompleteTest)
{
  BatchPredict<SVDIncompletePolicy>();
}

/**
 * Make sure we can train an already-trained model and it works okay for
 * randomized SVD.
 */
BOOST_AUTO_TEST_CASE(TrainRandSVDTest)
{
  RandomizedSVDPolicy decomposition;
  Train(decomposition);
}

/**
 * Make sure we can train an already-trained model and it works okay for
 * regularized SVD.
 */
BOOST_AUTO_TEST_CASE(TrainRegSVDTest)
{
  RegSVDPolicy decomposition;
  Train(decomposition);
}

/**
 * Make sure we can train an already-trained model and it works okay for
 * batch SVD.
 */
BOOST_AUTO_TEST_CASE(TrainBatchSVDTest)
{
  BatchSVDPolicy decomposition;
  Train(decomposition);
}

/**
 * Make sure we can train an already-trained model and it works okay for
 * NMF.
 */
BOOST_AUTO_TEST_CASE(TrainNMFTest)
{
  NMFPolicy decomposition;
  Train(decomposition);
}

/**
 * Make sure we can train an already-trained model and it works okay for
 * SVD Complete Incremental method.
 */
BOOST_AUTO_TEST_CASE(TrainSVDCompleteTest)
{
  SVDCompletePolicy decomposition;
  Train(decomposition);
}

/**
 * Make sure we can train an already-trained model and it works okay for
 * SVD Incomplete Incremental method.
 */
BOOST_AUTO_TEST_CASE(TrainSVDIncompleteTest)
{
  SVDIncompletePolicy decomposition;
  Train(decomposition);
}

/**
 * Make sure we can train a model after using the empty constructor when
 * using randomized SVD.
 */
BOOST_AUTO_TEST_CASE(EmptyConstructorTrainRandSVDTest)
{
  EmptyConstructorTrain<RandomizedSVDPolicy>();
}

/**
 * Make sure we can train a model after using the empty constructor when
 * using regularized SVD.
 */
BOOST_AUTO_TEST_CASE(EmptyConstructorTrainRegSVDTest)
{
  EmptyConstructorTrain<RegSVDPolicy>();
}

/**
 * Make sure we can train a model after using the empty constructor when
 * using batch SVD.
 */
BOOST_AUTO_TEST_CASE(EmptyConstructorTrainBatchSVDTest)
{
  EmptyConstructorTrain<BatchSVDPolicy>();
}

/**
 * Make sure we can train a model after using the empty constructor when
 * using NMF.
 */
BOOST_AUTO_TEST_CASE(EmptyConstructorTrainNMFTest)
{
  EmptyConstructorTrain<NMFPolicy>();
}

/**
 * Make sure we can train a model after using the empty constructor when
 * using SVD Complete Incremental method.
 */
BOOST_AUTO_TEST_CASE(EmptyConstructorTrainSVDCompleteTest)
{
  EmptyConstructorTrain<SVDCompletePolicy>();
}

/**
 * Make sure we can train a model after using the empty constructor when
 * using SVD Incomplete Incremental method.
 */
BOOST_AUTO_TEST_CASE(EmptyConstructorTrainSVDIncompleteTest)
{
  EmptyConstructorTrain<SVDIncompletePolicy>();
}

/**
 * Ensure we can load and save the CF model using randomized SVD policy.
 */
BOOST_AUTO_TEST_CASE(SerializationRandSVDTest)
{
  Serialization<RandomizedSVDPolicy>();
}

/**
 * Ensure we can load and save the CF model using batch SVD policy.
 */
BOOST_AUTO_TEST_CASE(SerializationBatchSVDTest)
{
  Serialization<BatchSVDPolicy>();
}

/**
 * Ensure we can load and save the CF model using NMF policy.
 */
BOOST_AUTO_TEST_CASE(SerializationNMFTest)
{
  Serialization<NMFPolicy>();
}

/**
 * Ensure we can load and save the CF model using SVD Complete Incremental.
 */
BOOST_AUTO_TEST_CASE(SerializationSVDCompleteTest)
{
  Serialization<SVDCompletePolicy>();
}

/**
 * Ensure we can load and save the CF model using SVD Incomplete Incremental.
 */
BOOST_AUTO_TEST_CASE(SerializationSVDIncompleteTest)
{
  Serialization<SVDIncompletePolicy>();
}

/**
 * Make sure that Predict() is returning reasonable results for NMF and
 * OverallMeanNormalization.
 */
BOOST_AUTO_TEST_CASE(CFPredictOverallMeanNormalization)
{
  CFPredict<NMFPolicy, OverallMeanNormalization>();
}

/**
 * Make sure that Predict() is returning reasonable results for NMF and
 * UserMeanNormalization.
 */
BOOST_AUTO_TEST_CASE(CFPredictUserMeanNormalization)
{
  CFPredict<NMFPolicy, UserMeanNormalization>();
}

/**
 * Make sure that Predict() is returning reasonable results for NMF and
 * ItemMeanNormalization.
 */
BOOST_AUTO_TEST_CASE(CFPredictItemMeanNormalization)
{
  CFPredict<NMFPolicy, ItemMeanNormalization>();
}

/**
 * Make sure that Predict() is returning reasonable results for NMF and
 * ZScoreNormalization.
 */
BOOST_AUTO_TEST_CASE(CFPredictZScoreNormalization)
{
  CFPredict<NMFPolicy, ZScoreNormalization>();
}

/**
 * Make sure that Predict() is returning reasonable results for NMF and
 * CombinedNormalization<OverallMeanNormalization, UserMeanNormalization,
 * ItemMeanNormalization>.
 */
BOOST_AUTO_TEST_CASE(CFPredictCombinedNormalization)
{
  CFPredict<NMFPolicy,
            CombinedNormalization<
                OverallMeanNormalization,
                UserMeanNormalization,
                ItemMeanNormalization>>();
}

/**
 * Make sure recommendations that are generated are reasonably accurate
 * for OverallMeanNormalization.
 */
BOOST_AUTO_TEST_CASE(RecommendationAccuracyOverallMeanNormalizationTest)
{
  RecommendationAccuracy<NMFPolicy, OverallMeanNormalization>();
}

/**
 * Make sure recommendations that are generated are reasonably accurate
 * for UserMeanNormalization.
 */
BOOST_AUTO_TEST_CASE(RecommendationAccuracyUserMeanNormalizationTest)
{
  RecommendationAccuracy<NMFPolicy, UserMeanNormalization>();
}

/**
 * Make sure recommendations that are generated are reasonably accurate
 * for ItemMeanNormalization.
 */
BOOST_AUTO_TEST_CASE(RecommendationAccuracyItemMeanNormalizationTest)
{
  RecommendationAccuracy<NMFPolicy, ItemMeanNormalization>();
}

/**
 * Make sure recommendations that are generated are reasonably accurate
 * for ZScoreNormalization.
 */
BOOST_AUTO_TEST_CASE(RecommendationAccuracyZScoreNormalizationTest)
{
  RecommendationAccuracy<NMFPolicy, ZScoreNormalization>();
}

/**
 * Make sure recommendations that are generated are reasonably accurate
 * for CombinedNormalization.
 */
BOOST_AUTO_TEST_CASE(RecommendationAccuracyCombinedNormalizationTest)
{
  RecommendationAccuracy<NMFPolicy,
                         CombinedNormalization<
                           OverallMeanNormalization,
                           UserMeanNormalization,
                           ItemMeanNormalization>>();
}

/**
 * Ensure we can load and save the CF model using OverallMeanNormalization.
 */
BOOST_AUTO_TEST_CASE(SerializationOverallMeanNormalizationTest)
{
  Serialization<NMFPolicy, OverallMeanNormalization>();
}

/**
 * Ensure we can load and save the CF model using UserMeanNormalization.
 */
BOOST_AUTO_TEST_CASE(SerializationUserMeanNormalizationTest)
{
  Serialization<NMFPolicy, UserMeanNormalization>();
}

/**
 * Ensure we can load and save the CF model using ItemMeanNormalization.
 */
BOOST_AUTO_TEST_CASE(SerializationItemMeanNormalizationTest)
{
  Serialization<NMFPolicy, ItemMeanNormalization>();
}

/**
 * Ensure we can load and save the CF model using ZScoreMeanNormalization.
 */
BOOST_AUTO_TEST_CASE(SerializationZScoreNormalizationTest)
{
  Serialization<NMFPolicy, ZScoreNormalization>();
}

/**
 * Ensure we can load and save the CF model using CombinedNormalization.
 */
BOOST_AUTO_TEST_CASE(SerializationCombinedNormalizationTest)
{
  Serialization<NMFPolicy,
                CombinedNormalization<
                    OverallMeanNormalization,
                    UserMeanNormalization,
                    ItemMeanNormalization>>();
}

/**
 * Make sure that Predict() is returning reasonable results for
 * EuclideanSearch.
 */
BOOST_AUTO_TEST_CASE(CFPredictEuclideanSearch)
{
  CFPredict<NMFPolicy, OverallMeanNormalization, EuclideanSearch>();
}

/**
 * Make sure that Predict() is returning reasonable results for
 * CosineSearch.
 */
BOOST_AUTO_TEST_CASE(CFPredictCosineSearch)
{
  CFPredict<NMFPolicy, OverallMeanNormalization, CosineSearch>();
}

/**
 * Make sure that Predict() is returning reasonable results for
 * PearsonSearch.
 */
BOOST_AUTO_TEST_CASE(CFPredictPearsonSearch)
{
  CFPredict<NMFPolicy, OverallMeanNormalization, PearsonSearch>();
}

/**
 * Make sure that Predict() is returning reasonable results for
 * AverageInterpolation.
 */
BOOST_AUTO_TEST_CASE(CFPredictAverageInterpolation)
{
  CFPredict<NMFPolicy,
            OverallMeanNormalization,
            EuclideanSearch,
            AverageInterpolation>();
}

/**
 * Make sure that Predict() is returning reasonable results for
 * SimilarityInterpolation.
 */
BOOST_AUTO_TEST_CASE(CFPredictSimilarityInterpolation)
{
  CFPredict<NMFPolicy,
            OverallMeanNormalization,
            EuclideanSearch,
            SimilarityInterpolation>();
}

/**
 * Make sure that Predict() is returning reasonable results for
 * RegressionInterpolation.
 */
BOOST_AUTO_TEST_CASE(CFPredictRegressionInterpolation)
{
  // The small Grouplens dataset we use is very sparse.
  // RegressionInterpolation doesn't generalize well when the data
  // is very sparse. Thus we set a very high rmse bound here.
  CFPredict<NMFPolicy,
            OverallMeanNormalization,
            EuclideanSearch,
            RegressionInterpolation>(12);
}

BOOST_AUTO_TEST_SUITE_END();
