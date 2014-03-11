#ifndef ROBUST_PCA_TEST_MAIN_HPP__
#define ROBUST_PCA_TEST_MAIN_HPP__


#include <boost/random/mersenne_twister.hpp>
#include <boost/numeric/ublas/matrix.hpp>
// generating data randomly
#include <boost/random/uniform_real_distribution.hpp>

// random number generator
extern boost::random::mt19937 rng;

// Fixture for the tests
struct fixture_simple_matrix_creation
{
  typedef boost::numeric::ublas::matrix<double> matrix_t;


  static const int nb_elements;// = 1000;
  static const int dimensions;// = 5;
  matrix_t mat_data;

  boost::random::uniform_real_distribution<double> dist;


  fixture_simple_matrix_creation() : dist(-1000, 1000)
  {
    // creating some data, 1000 lines of a vector of length 5
    mat_data.resize(nb_elements, dimensions);

    // default seed for reproductible sequences
    rng.seed();

    //std::cout << "current seed : " << ;

    //const std::string filename = "./toto.txt";
    //std::ofstream ff(filename.c_str());

    //BOOST_REQUIRE(ff.is_open());

    for(int i = 0; i < nb_elements; i++)
    {
      for(int j = 0; j < dimensions; j++)
      {
        mat_data(i, j) = dist(rng);
        //ff << mat_data(i, j) << " ";
      }
      //ff << std::endl;
    }




  }
};


#endif /* ROBUST_PCA_TEST_MAIN_HPP__ */
