// Copyright 2014, Max Planck Institute for Intelligent Systems.
// Distributed under the BSD 3-Clause license.
// (See accompanying file LICENSE.txt or copy at
// http://opensource.org/licenses/BSD-3-Clause)

/*!@file
 * This file includes the tests for the trimmed version of the robust pca
 */

#include <boost/test/unit_test.hpp>
// data stored into a matrix
#include <boost/numeric/ublas/matrix.hpp>
#include <boost/numeric/ublas/io.hpp>


// boost chrono
#include <boost/chrono/include.hpp>

#include <fstream>
#include <sstream>

#include <include/robust_pca_trimming.hpp>
#include <include/private/boost_ublas_matrix_helper.hpp>



std::string filename_data;
std::string filename_eigen_vectors;
std::string filename_expected_result;


struct MyConfig
{

  MyConfig()
  {
    int argc  = boost::unit_test::framework::master_test_suite().argc;
    char**argv= boost::unit_test::framework::master_test_suite().argv;

    // from the command line, we expect the file that should be read.
    for(int i = 0; i < argc - 1; i++)
    {
      if(std::string(argv[i]) == "--data")
      {
        if(!filename_data.empty())
        {
          std::cerr << "Test initialisation error: data given several times" << std::endl;
        }
        filename_data = argv[i+1];
      }
      if(std::string(argv[i]) == "--eigen_vectors")
      {
        if(!filename_eigen_vectors.empty())
        {
          std::cerr << "Test initialisation error: eigen_vectors given several times" << std::endl;
        }
        filename_eigen_vectors = argv[i+1];
      }
      if(std::string(argv[i]) == "--expected_result")
      {
        if(!filename_expected_result.empty())
        {
          std::cerr << "Test initialisation error: expected_result given several times" << std::endl;
        }
        filename_expected_result = argv[i+1];
      }
    }
  }
  ~MyConfig()
  {
    std::cout << "global teardown\n";
  }
};

BOOST_GLOBAL_FIXTURE( MyConfig );







template <class T>
std::vector<T>* readLines(std::istream& str)
{
  std::vector<T>* result = new std::vector<T>();
  std::string line;
  std::getline(str,line);

  std::stringstream lineStream(line);
  std::string cell;

  
  while(std::getline(lineStream, cell, '\t'))
  {
    std::stringstream current_stream(cell);
    T current_element;
    current_stream >> current_element;
    assert(!current_stream.fail());
    result->push_back(current_element);
  }
  return result;
}


BOOST_AUTO_TEST_CASE(convergence_rate_tests_several_workers)
{

  using namespace robust_pca;
  using namespace robust_pca::ublas_adaptor;
  namespace ub = boost::numeric::ublas;
  typedef boost::chrono::steady_clock clock_type;
  typedef boost::numeric::ublas::matrix<double> matrix_t;


  BOOST_REQUIRE(!filename_data.empty());


  typedef robust_pca_with_trimming_impl< ub::vector<double> > robust_pca_t;
  robust_pca_t instance(.1);
  typedef row_iter<const matrix_t> const_row_iter_t;

  typedef ub::vector<double> data_t;

  std::string const filename_to_read = filename_data;
  std::ifstream ff(filename_to_read.c_str());

  std::vector< std::vector<double>* > read_vectors;

  std::cout << "Reading data" << std::endl;
  matrix_t mat_data;
  while(!ff.eof())
  {
    std::vector<double>* v = readLines<double>(ff);
    if(v->size())
    {
      read_vectors.push_back(v);
      if((read_vectors.size() % 1000) == 0)
      {
        std::cout << ".";
        std::cout.flush();
      }
    }
  }
  std::cout << std::endl << "copying" << std::endl;

  mat_data.resize(read_vectors.size(), read_vectors[0]->size());
  for(size_t i = 0; i < read_vectors.size(); i++)
  {
    std::copy(read_vectors[i]->begin(), read_vectors[i]->end(), ub::row(mat_data, i).begin());
    delete read_vectors[i];
  }
  read_vectors.clear();

  const size_t nb_elements = mat_data.size1();
  const size_t dimensions = mat_data.size2();
  std::cout << "Data ok : dimensions = " << dimensions << " #elements = " << nb_elements << std::endl;


  const size_t max_dimensions = 5;

  std::vector<data_t> temporary_data(nb_elements);
  std::vector<data_t> eigen_vectors(max_dimensions);
  const int max_iterations = 1000;


  BOOST_CHECK(instance.set_nb_processors(7));

  clock_type::duration elapsed;

  clock_type::time_point start = clock_type::now(); 
  BOOST_CHECK(instance.batch_process(
    max_iterations,
    max_dimensions,
    const_row_iter_t(mat_data, 0),
    const_row_iter_t(mat_data, mat_data.size1()),
    temporary_data.begin(),
    eigen_vectors.begin()));
  elapsed = clock_type::now() - start;
  
  std::cout << "processing " << nb_elements << " elements "
    << "in " << boost::chrono::duration_cast<boost::chrono::microseconds>(elapsed) << "microseconds" << std::endl;


  // testing the output sizes
  BOOST_REQUIRE_EQUAL(eigen_vectors.size(), max_dimensions);
  for(int i = 0; i < max_dimensions; i++)
  {
    BOOST_CHECKPOINT("testing eigenvector size for vector " << i);
    BOOST_REQUIRE_EQUAL(eigen_vectors[i].size(), dimensions);
  }

  // testing orthogonality of all eigenvectors
  for(int i = 0; i < max_dimensions - 1; i++)
  {
    for(int j = i + 1; j < max_dimensions; j++)
    {
      BOOST_CHECK_LE(ub::inner_prod(eigen_vectors[i], eigen_vectors[j]), 1E-6);
    }
  }


  // testing unitarity
  for(int i = 0; i < max_dimensions; i++)
  {
    BOOST_CHECK_CLOSE(ub::inner_prod(eigen_vectors[i], eigen_vectors[i]), 1, 1E-6);
  }


}
