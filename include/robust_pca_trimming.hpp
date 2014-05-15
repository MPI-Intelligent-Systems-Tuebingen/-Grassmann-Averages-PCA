// Copyright 2014, Max Planck Institute for Intelligent Systems.
// Distributed under the BSD 3-Clause license.
// (See accompanying file LICENSE.txt or copy at
// http://opensource.org/licenses/BSD-3-Clause)

#ifndef ROBUST_PCA_TRIMMED_HPP__
#define ROBUST_PCA_TRIMMED_HPP__



/*!@file
 * Robust PCA functions, following the paper of Soren Hauberg.
 *
 * This file contains the implementation of the trimmed version. 
 */

// boost heaps
#include <boost/heap/pairing_heap.hpp>

// for the thread pools
#include <boost/asio/io_service.hpp>
#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>
#include <boost/signals2.hpp>



// utilities
#include <include/private/utilities.hpp>


namespace robust_pca
{


  namespace details
  {
    
    /*!@internal
     * @brief Helper structure for managing the double trimming. 
     * It is supposed that the trimming is symmetrical: the first K are kept in the 
     * upper and lower part of the distribution. However the K is not managed by this structure
     * directly, but rather by the caller. This structure provides two functions for performing 
     * that:
     * - push that pushes the data unconditionnally into the heap. Each heap size grows by 1 after the call.
     * - push_or_ignore that pushes the data if it is under the maximum and then pops the maximum. It ignores
     *   the data otherwise. The size of each heap remains constant.
     */ 
    template <class scalar_t>
    struct s_double_heap
    {  
      //!@name Heap types
      //!@{
      // apparently fibonacci heaps have a problem in the copy construction on Visual C++ 2013,
      // so I am using pairing_heap here.
      typedef boost::heap::pairing_heap<scalar_t, boost::heap::compare< std::less<scalar_t> > > low_heap_t;
      typedef boost::heap::pairing_heap<scalar_t, boost::heap::compare< std::greater<scalar_t> > > high_heap_t;
      //!@}
      
      
      low_heap_t lowh;
      high_heap_t highh;
      
      //! Uncontrolled push for populating the first K elements.
      void push(scalar_t const& current)
      {
        lowh.push(current);
        highh.push(current);
      }
      
      //! Controlled push.
      //! If the element given in parameter is under the top of the heap, given the
      //! order relationship of the heap, then the element is added to the heap and 
      //! one element is popped out. Otherwise the element is simply ignored.
      void push_or_ignore(scalar_t const& current)
      {
        if(lowh.value_comp()(current, lowh.top()))
        {
          lowh.push(current);
          lowh.pop();
        }
        
        if(highh.value_comp()(current, highh.top()))
        {
          highh.push(current);
          highh.pop();
        }    
      }
      
      //! Merges two double heaps together
      void merge(s_double_heap const& right)
      {
        const typename low_heap_t::size_type max_elements = std::max(lowh.size(), right.lowh.size());
        assert(std::max(highh.size(), right.highh.size()) == max_elements);
        
        for(typename low_heap_t::const_iterator it(right.lowh.begin()), ite(right.lowh.end());
            it != ite;
            ++it)
        {
          typename low_heap_t::const_iterator::reference v(*it);
          if(lowh.size() < max_elements)
          {
            lowh.push(v);
          }
          else if(lowh.value_comp()(v, lowh.top()))
          {
            lowh.push(v);
            lowh.pop();
          }
        }
        
        
        for(typename high_heap_t::const_iterator it(right.highh.begin()), ite(right.highh.end());
            it != ite;
            ++it)
        {
          typename high_heap_t::const_iterator::reference v(*it);
          if(highh.size() < max_elements)
          {
            highh.push(v);
          }
          else if(highh.value_comp()(v, highh.top()))
          {
            highh.push(v);
            highh.pop();
          }
        }
      }
      
      void extract_bounds(scalar_t &min_bound, scalar_t &max_bound) const
      {
        min_bound = lowh.size() == 0 ? boost::numeric::bounds<scalar_t>::lowest() : lowh.top();
        max_bound = highh.size() == 0 ? boost::numeric::bounds<scalar_t>::highest() : highh.top();
      }
      
      //! Clear the content of the heaps when these are not needed anymore
      void clear()
      {
        lowh.clear();
        highh.clear();
      }
    };
  
    
      
    //!@internal
    //! Helper structure for having s_double_heap on vectors of data. 
    template <class data_t>
    struct s_double_heap_vector
    {
      typedef std::vector< s_double_heap<typename data_t::value_type> > v_bounds_t;
      v_bounds_t v_bounds;
      
      s_double_heap_vector()
      {}
      
      /*! Sets the dimension of the bounds to be computed.
       *  The dimension corresponds to the number of elements of each data vector.
       *  @pre dimension >= 0
       */
      void set_dimension(size_t dimension)
      {
        assert(dimension >= 0);
        v_bounds.resize(dimension);
      }
      
      //! Updates the quantile for each dimension of the vector
      void push(const data_t& current_data, bool sign)
      {
        typename v_bounds_t::iterator it_bounds(v_bounds.begin());
        for(typename data_t::const_iterator it(current_data.begin()), ite(current_data.end()); it < ite; ++it, ++it_bounds)
        {
          typename data_t::value_type v(sign ? *it : -(*it));
          it_bounds->push(v);
        }
      }
      
      //! Updates the quantile for each dimension of the vector
      void push_or_ignore(const data_t& current_data, bool sign)
      {
        typename v_bounds_t::iterator it_bounds(v_bounds.begin());
        for(typename data_t::const_iterator it(current_data.begin()), ite(current_data.end()); it < ite; ++it, ++it_bounds)
        {
          typename data_t::value_type v(sign ? *it : -(*it));
          it_bounds->push_or_ignore(v);
        }
      }
      
      //! Merges two instances together. 
      //!
      //! The result goes into the current instance.
      void merge(s_double_heap_vector const& right)
      {
        assert(right.v_bounds.size() == v_bounds.size());
        typename v_bounds_t::iterator it(v_bounds.begin()), ite(v_bounds.end());
        typename v_bounds_t::const_iterator it_input(right.v_bounds.begin());
        
        for(; it < ite; ++it, ++it_input)
        {
          it->merge(*it_input);
        }
        
      }
      
      //! Extracts the computed bounds
      //!
      //! Forwards the extraction to the s_double_heap object.
      void extract_bounds(
        std::vector<double> &v_min_bound,
        std::vector<double> &v_max_bound) const
      {
        v_min_bound.resize(v_bounds.size());
        v_max_bound.resize(v_bounds.size());
        
        typename v_bounds_t::const_iterator it(v_bounds.begin()), ite(v_bounds.end());
        
        std::vector<double>::iterator it_min(v_min_bound.begin()), it_max(v_max_bound.begin());
        
        for(; it < ite; ++it, ++it_min, ++it_max)
        {
          it->extract_bounds(*it_min, *it_max);
        }
        
      }
      
      //! Clear the content of the heaps when these are not needed anymore
      //! @note the dimension of the vector is left unchanged. 
      void clear()
      {
        typename v_bounds_t::iterator it(v_bounds.begin()), ite(v_bounds.end());
        for(; it < ite; ++it)
        {
          it->clear();
        }
      }
      
      //! Empties the internal state and frees the associated memory.
      void clear_all()
      {
        v_bounds_t empty_v;
        v_bounds.swap(empty_v); // clear does not unallocate but resize to 0, hence the swap
      }
      
    };



    //! Adaptation of merger_addition concept for accumulator and count at the same time.
    //! 
    //! The trimmed version of the robust pca algorithm may strip some element along each dimension. 
    //! In order to compute the @f$\mu@f$ properly, the count should also be transfered.
    template <class data_t, class count_t>
    struct merger_addition_with_count
    {
      typedef std::pair<data_t, count_t> input_t;
      bool operator()(input_t &current_state, input_t const& update_value) const
      {
        current_state.first += update_value.first;
        current_state.second += update_value.second;
        return true;
      }
    };


    //! Adaptation of initialisation_vector_specific_dimension concept for accumulation and count.
    //! 
    //! See merger_addition_with_count.
    template <class data_t, class count_t>
    struct initialisation_vector_specific_dimension_with_count
    {
      size_t data_dimension;
      typedef typename data_t::value_type scalar_t;
      typedef typename count_t::value_type count_scalar_t;
      typedef std::pair<data_t, count_t> input_t;

      initialisation_vector_specific_dimension_with_count(size_t dimension) : data_dimension(dimension)
      {}

      bool operator()(input_t & current_state) const
      {
        current_state.first = boost::numeric::ublas::scalar_vector<scalar_t>(data_dimension, 0);
        current_state.second = boost::numeric::ublas::scalar_vector<count_scalar_t>(data_dimension, 0);
        return true;
      }
    };

    
  }


  /*!@brief Robust PCA subspace algorithm, with trimming
   *
   * This class implements the robust PCA using the Grassmanian averaging with trimming.
   *
   * @author Soren Hauberg, Raffi Enficiaud
   */
  template <class data_t, class norm_mu_t = details::norm2>
  struct robust_pca_with_trimming_impl
  {
  private:
    //! Random generator for initialising @f$\mu@f$ at each dimension. 
    details::random_data_generator<data_t> random_init_op;

    //! Norm used for normalizing @f$\mu@f$.
    norm_mu_t norm_op;
    

    //! The percentage of the data that should be trimmed.
    //! The trimming is performed symmetrically in the upper and lower distribution of the data, hence
    //! each side is trimmed by trimming_percentage/2.
    double trimming_percentage;

    //! Number of parallel tasks that will be used for computing.
    size_t nb_processors;

    //! Maximal size of a chunk (infinity by default).
    size_t max_chunk_size;    

    //! Type of the element of data_t. 
    typedef typename data_t::value_type scalar_t;

    //! Type of the vector used for counting the element falling into the non-trimmed range.
    typedef boost::numeric::ublas::vector<size_t> count_vector_t;









    //! Performs a selective update of the accumulator, given the bounds computed previously
    template <class vector_bounds_t, class vector_number_elements_t>
    static void selective_acc_to_vector(
      const vector_bounds_t& lower_bounds, const vector_bounds_t& upper_bounds,
      const data_t &initial_data,
      bool sign,
      data_t &v_selective_accumulator,
      vector_number_elements_t& v_selective_acc_count)
    {
      for(size_t i = 0, j = initial_data.size(); i < j; i++)
      {
        typename data_t::value_type const v(sign ? initial_data[i] : -initial_data[i]);
        if(v < lower_bounds[i])
          continue;
        if(v > upper_bounds[i])
          continue;
        

        v_selective_accumulator[i] += v;
        v_selective_acc_count[i]++;
      }
    }






    //!@internal
    //!@brief Contains the logic for processing part of the accumulator
    template <class container_iterator_t>
    struct s_robust_pca_trimmed_processor
    {
    private:
      //! Iterators on the beginning and end of the current dataset
      container_iterator_t begin, end;
      
      typedef std::pair<data_t, count_vector_t> accumulator_t;
      details::initialisation_vector_specific_dimension_with_count<data_t, count_vector_t> initialisation_object;

      typedef details::s_double_heap_vector<data_t> bounds_accumulator_t;
      
      size_t nb_elements;                 //!< The size of the current dataset
      size_t data_dimension;              //!< The dimension of the data
      int nb_elements_to_keep;            //!< The number of elements to keep.
      
      //! The inner product is computed during the computation of the bounds.
      std::vector<bool> inner_products_results;
      
      //! Upper and lower bounds on data computed by the main thread. These vectors
      //! will be read only and should not be modified during the time they are used.
      std::vector<double> const *v_min_threshold, *v_max_threshold;


      // this is to send an update of the value of the accumulator to all listeners
      // the connexion should be managed externally
      typedef boost::signals2::signal<void (accumulator_t const&)> 
        connector_accumulator_t;
      
      typedef boost::signals2::signal<void (bounds_accumulator_t const&)> 
        connector_bounds_t;
      
      typedef boost::signals2::signal<void ()>
        connector_counter_t;

      connector_accumulator_t signal_acc;
      connector_bounds_t      signal_bounds;
      connector_counter_t     signal_counter;
      


    public:
      s_robust_pca_trimmed_processor() : 
        initialisation_object(0),
        nb_elements(0), 
        data_dimension(0), 
        nb_elements_to_keep(0),
        v_min_threshold(0), 
        v_max_threshold(0)
      {
      }

      //! Sets the data range
      bool set_data_range(container_iterator_t const &b, container_iterator_t const& e)
      {
        begin = b;
        end = e;
        nb_elements = std::distance(b, e);
        assert(nb_elements > 0);
        inner_products_results.resize(nb_elements);
        return true;
      }

      //! Sets the dimension of the data vectors
      //! @pre data_dimensions_ is strictly positive
      void set_data_dimensions(size_t data_dimensions_)
      {
        assert(data_dimensions_ > 0);
        initialisation_object.data_dimension = data_dimensions_;
        data_dimension = data_dimensions_;
      }

      //! Sets the number of element to keep in the upper and lower distributions.
      void set_nb_elements_to_keep(int nb_elements_to_keep_)
      {
        assert(nb_elements_to_keep_ >= 0);
        nb_elements_to_keep = nb_elements_to_keep_;
      }

      //! Sets the bounds vectors.
      void set_bounds(
        std::vector<double> const *min_bounds, 
        std::vector<double> const *max_bounds)
      {
        v_min_threshold = min_bounds;
        v_max_threshold = max_bounds;
      }


      //! Returns the connected object that will receive the notification of the updated accumulator.
      connector_accumulator_t& connector_accumulator()
      {
        return signal_acc;
      }

      //! Returns the connected object that will receive the notification of the end of the current process.
      connector_counter_t& connector_counter()
      {
        return signal_counter;
      }

      //! Returns the connected object that will receive the notification of the updated bounds.
      connector_bounds_t& connector_bounds()
      {
        return signal_bounds;
      }
      

      //! Computes the bounds of the current subset
      void compute_bounds(data_t const &mu)
      {
        if(nb_elements_to_keep == 0)
        {
          // in this special case, we do nothing
          // the bounds should be able to cope with this special case also.
        }
        else
        {

          container_iterator_t it_data(begin);

          // The structure in charge of computing the bounds. The instance is local to
          // this call.
          bounds_accumulator_t bounds_op;

          // we allocate the heaps only when needed
          bounds_op.set_dimension(data_dimension);

          std::vector<bool>::iterator itb(inner_products_results.begin());

          for(size_t s = 0; s < nb_elements; ++it_data, s++, ++itb)
          {
            typename container_iterator_t::reference current_data = *it_data;
            bool sign = boost::numeric::ublas::inner_prod(current_data, mu) >= 0;
            *itb = sign;
            if(s < nb_elements_to_keep)
            { 
              bounds_op.push(current_data, sign);
            }
            else
            {
              bounds_op.push_or_ignore(current_data, sign);
            }
          }


          // posts the new value to the listeners
          signal_bounds(bounds_op);
        
          // we free the heaps right now, will be done in the destructor anyway.
          bounds_op.clear_all();
        }

        // in any case, we signal that we went through this function
        signal_counter();
      }



      //! Performs the accumulation, given the boundaries.
      void accumulation(data_t const &mu)
      {
        accumulator_t acc;
        initialisation_object(acc);

        if(nb_elements_to_keep == 0)
        {
          // in this case, we recompute the inner products
          container_iterator_t it_data(begin);
          
          for(size_t s = 0; s < nb_elements; ++it_data, s++)
          {
            typename container_iterator_t::reference current_data = *it_data;
            bool sign = boost::numeric::ublas::inner_prod(current_data, mu) >= 0;
            
            selective_acc_to_vector(
              *v_min_threshold, 
              *v_max_threshold, 
              current_data, 
              sign, 
              acc.first, 
              acc.second);            
          }

        }
        else
        {
          // in this case, the inner products were already computed.
          std::vector<bool>::const_iterator itb(inner_products_results.begin());

          container_iterator_t it_data(begin);

          for(size_t s = 0; s < nb_elements; ++it_data, s++, ++itb)
          {
            typename container_iterator_t::reference current_data = *it_data;
            
            selective_acc_to_vector(
              *v_min_threshold, 
              *v_max_threshold, 
              current_data, 
              *itb, 
              acc.first, 
              acc.second);            

          }
        }

        // posts the new value to the listeners
        signal_acc(acc);
        signal_counter();
      }

      //! Project the data onto the orthogonal subspace of the provided vector
      void project_onto_orthogonal_subspace(data_t const &mu)
      {
        container_iterator_t it_data(begin);

        // update of vectors in the orthogonal space, and update of the norms at the same time. 
        for(size_t s = 0; s < nb_elements; ++it_data, s++)
        {
          typename container_iterator_t::reference current_vector = *it_data;
          current_vector -= boost::numeric::ublas::inner_prod(mu, current_vector) * mu;
        }

        signal_counter();
      }

    };


    /*!@internal
     * @brief Merges the result of all workers and signals the results to the main thread.
     *
     * The purpose of this class is to add the computed accumulator of each thread to the final result
     * which contains the sum of all accumulators. 
     *
     */
    struct asynchronous_results_merger : 
      details::threading::asynchronous_results_merger<
        std::pair<data_t, count_vector_t>,
        details::merger_addition_with_count<data_t, count_vector_t>,
        details::initialisation_vector_specific_dimension_with_count<data_t, count_vector_t>
      >
    {
    public:
      typedef details::s_double_heap_vector<data_t> bounds_accumulator_t;
      typedef std::pair<data_t, count_vector_t> accumulator_t;

    private:
      typedef details::initialisation_vector_specific_dimension_with_count<data_t, count_vector_t> data_init_type;
      typedef details::merger_addition_with_count<data_t, count_vector_t> merger_type;


      bounds_accumulator_t bounds;
      const size_t data_dimension;

    public:
      typedef details::threading::asynchronous_results_merger<
        accumulator_t,
        merger_type, 
        data_init_type> parent_type;

      /*!Constructor
       *
       * @param dimension_ the number of dimensions of the vector to accumulate
       */
      asynchronous_results_merger(size_t data_dimension_) : 
        parent_type(data_init_type(data_dimension_)),
        data_dimension(data_dimension_)
      {}

      //! Initializes the internal states
      void init()
      {
        parent_type::init();
        init_bounds();
      }

      //! Initialises the internal state of the bounds
      void init_bounds()
      {
        bounds.set_dimension(data_dimension);
        bounds.clear();
      }

      //! Empties the structures related to the computation of the bounds.
      void clear_bounds()
      {
        bounds.clear_all();
      }



      /*! Receives the updated value of the bounds from each worker.
       * 
       *  @note The call is thread safe.
       */
      void update_bounds(bounds_accumulator_t const& new_bounds)
      {
        boost::lock_guard<boost::mutex> guard(parent_type::internal_mutex);
        bounds.merge(new_bounds);
      }

      //! Returns the bound merged from all workers.
      bounds_accumulator_t const& get_computed_bounds() const
      {
        return bounds;
      }
    };





  public:
    robust_pca_with_trimming_impl(double trimming_percentage_ = 0) :
      random_init_op(details::fVerySmallButStillComputable, details::fVeryBigButStillComputable),
      trimming_percentage(trimming_percentage_),
      nb_processors(1),
      max_chunk_size(std::numeric_limits<size_t>::max())
    {
      assert(trimming_percentage_ >= 0 && trimming_percentage_ <= 1);
    }

    //! Sets the number of parallel tasks used for computing.
    bool set_nb_processors(size_t nb_processors_)
    {
      assert(nb_processors_ >= 1);
      nb_processors = nb_processors_;
      return true;
    }
    
    //! Sets the maximum chunk size. 
    //!
    //! By default, the chunk size is the size of the data divided by the number of processing threads.
    //! Lowering the chunk size should provid better granularity in the overall processing time at the end 
    //! of the processing.
    bool set_max_chunk_size(size_t chunk_size)
    {
      if(chunk_size == 0)
      {
        return false;
      }
      max_chunk_size = chunk_size;
      return true;
    }
    
    


    /*! Performs the computation of the current subspace on the elements given by the two iterators.
     *  @tparam it_t an input forward iterator to input vectors points. Each element pointed by the underlying iterator should be iterable and
     *   should provide a vector point.
     *  @tparam it_o_projected_vectors output forward iterator pointing on a container of vector points.
     *  @tparam it_norm_t an output iterator on weights/norms of the vectors. The output elements should be numerical (norm output)
     *
     * @param[in] max_iterations the maximum number of iterations at each dimension.
     * @param[in] max_dimension_to_compute the maximum number of data_dimension to compute in the PCA (only the first max_dimension_to_compute will be
     *            computed).
     * @param[in] it input iterator at the beginning of the data
     * @param[in] ite input iterator at the end of the data
     * @param[in, out] it_projected
     * @param[in] initial_guess if provided, the initial vector will be initialized to this value.
     * @param[out] it_eigenvectors an iterator on the beginning of the area where the detected eigenvectors will be stored. The space should be at least max_dimension_to_compute.
     *
     * @returns true on success, false otherwise
     * @pre
     * - @c !(it >= ite)
     * - all the vectors given by the iterators pair should be of the same size (no check is performed).
     */
    template <class it_t, class it_o_projected_vectors, class it_o_eigenvalues_t>
    bool batch_process(
      const size_t max_iterations,
      size_t max_dimension_to_compute,
      it_t const it,
      it_t const ite,
      it_o_projected_vectors const it_projected,
      it_o_eigenvalues_t it_eigenvectors,
      std::vector<data_t> const * initial_guess = 0)
    {
      // add some log information
      if(it >= ite)
      {
        return false;
      }


      // preparing the thread pool, to avoid individual thread creation/deletion at each step.
      // we perform the init here because it might take some time for the thread to really start.
      boost::asio::io_service ioService;
      boost::thread_group threadpool;


      // in case of non clean exit (or even in case of clean one).
      details::threading::safe_stop worker_lock_guard(ioService, threadpool);


      // this is exactly the number of processors
      boost::asio::io_service::work work(ioService);
      for(int i = 0; i < nb_processors; i++)
      {
        threadpool.create_thread(boost::bind(&boost::asio::io_service::run, &ioService));
      }


      // contains the number of elements. In case the iterator is random access, could be deduced simply 
      // by a call to distance.
      size_t size_data(0);

      // copy of the vectors. This should be avoided if the containers are of the same type and for the first iteration. 
      // The vectors are copied into the temporary container. During the copy, the percentiles are computed.
      it_o_projected_vectors it_tmp_projected(it_projected);
      for(it_t it_copy(it); it_copy != ite; ++it_copy , ++it_tmp_projected, size_data++)
      {
        *it_tmp_projected = *it_copy;
      }

      assert(size_data == std::distance(it, ite)); // the iterators have to be random access anyway

      // size of the chunks.
      const size_t chunks_size = std::min(max_chunk_size, static_cast<size_t>(size_data/nb_processors));
      const size_t nb_chunks = (size_data + chunks_size - 1) / chunks_size;


      // number of dimensions of the data vectors
      const size_t number_of_dimensions = it->size();
      max_dimension_to_compute = std::min(max_dimension_to_compute, number_of_dimensions);


      // initial iterator on the output eigenvectors
      it_o_eigenvalues_t const it_output_eigen_vector_beginning(it_eigenvectors);
      it_o_eigenvalues_t it_output_eigen_vector_end(it_output_eigen_vector_beginning);
      std::advance(it_output_eigen_vector_end, max_dimension_to_compute);

      // the initialisation of mus
      {
        it_o_eigenvalues_t it_eigen(it_output_eigen_vector_beginning);
        for(int i = 0; it_eigen != it_output_eigen_vector_end; ++it_eigen, ++i)
        {
          *it_eigen = initial_guess != 0 ? (*initial_guess)[i] : random_init_op(*it);
        }
      }
      if(!details::gram_schmidt_orthonormalisation(it_output_eigen_vector_beginning, it_output_eigen_vector_end, it_output_eigen_vector_beginning, norm_op))
      {
        return false;
      }


      // preparing mu
      data_t mu(*it_eigenvectors);
      assert(mu.size() == number_of_dimensions);
      

      // vector of valid bounds. These bounds will be shared among every workers. 
      std::vector<double> v_min_threshold(number_of_dimensions);
      std::vector<double> v_max_threshold(number_of_dimensions);


      // number of elements to keep
      const int K_elements = static_cast<int>(std::ceil(trimming_percentage*size_data/2));


      // preparing the ranges on which each processing thread will run.
      // the number of objects can be much more than the current number of processors, in order to
      // avoid waiting too long for a thread (better granularity) but involving a slight overhead in memory and
      // processing at the synchronization point.
      typedef s_robust_pca_trimmed_processor<it_o_projected_vectors> async_processor_t;
      std::vector<async_processor_t> v_individual_accumulators(nb_chunks);

      asynchronous_results_merger async_merger(number_of_dimensions);

      {
        bool b_result;
        it_o_projected_vectors it_current_begin(it_projected);
        for(int i = 0; i < nb_chunks; i++)
        {
          // setting the range
          it_o_projected_vectors it_current_end;
          if(i == nb_chunks - 1)
          {
            // just in case the division giving the chunk has some rounding (the parenthesis are important
            // otherwise it is a + followed by a -, which can be out of range after the first +)
            it_current_end = it_current_begin + (size_data - chunks_size*(nb_chunks - 1));
          }
          else
          {
            it_current_end = it_current_begin + chunks_size;
          }

          // the processor object for this new range
          async_processor_t &current_acc_object = v_individual_accumulators[i];

          b_result = current_acc_object.set_data_range(it_current_begin, it_current_end);
          if(!b_result)
          {
            return b_result;
          }

          // updating the dimension of the problem
          current_acc_object.set_data_dimensions(number_of_dimensions);

          // setting the number of elements to keep
          current_acc_object.set_nb_elements_to_keep(K_elements);

          // setting the bounds to the globally shared objects
          current_acc_object.set_bounds(&v_min_threshold, &v_max_threshold);

          // attaching the update object callbacks
          current_acc_object.connector_accumulator().connect(
            boost::bind(
              /*(void (asynchronous_results_merger::*)(asynchronous_results_merger::result_type const&))*/&asynchronous_results_merger::update, 
              &async_merger, 
              _1));
          current_acc_object.connector_bounds().connect(
            boost::bind(
              /*(void (asynchronous_results_merger::*)(asynchronous_results_merger::bounds_accumulator_t const&))*/&asynchronous_results_merger::update_bounds, 
              &async_merger, 
              _1));
          current_acc_object.connector_counter().connect(
            boost::bind(&asynchronous_results_merger::notify, &async_merger));

          // updating the next 
          it_current_begin = it_current_end;
        }
      }











      // for each dimension
      for(int current_dimension = 0; current_dimension < max_dimension_to_compute; current_dimension++, ++it_eigenvectors)
      {

        details::convergence_check<data_t> convergence_op(mu);

        for(int iterations = 0; (!convergence_op(mu) && iterations < max_iterations) || iterations == 0; iterations++)
        {

          // reseting the merger object
          async_merger.init();

          // pushing the computation of the bounds
          for(int i = 0; i < v_individual_accumulators.size(); i++)
          {
            ioService.post(
              boost::bind(
                &async_processor_t::compute_bounds, 
                boost::ref(v_individual_accumulators[i]), 
                boost::cref(mu)));
          }


          // waiting for completion (barrier)
          async_merger.wait_notifications(v_individual_accumulators.size());


          // gathering the new bounds
          typename asynchronous_results_merger::bounds_accumulator_t const& bounds_acc = async_merger.get_computed_bounds();
          bounds_acc.extract_bounds(v_min_threshold, v_max_threshold);

          // clearing the bounds
          async_merger.clear_bounds();
          async_merger.init_notifications();

          // pushing the computation of the updated mu
          for(int i = 0; i < v_individual_accumulators.size(); i++)
          {
            ioService.post(
              boost::bind(
                &async_processor_t::accumulation, 
                boost::ref(v_individual_accumulators[i]), 
                boost::cref(mu)));
          }


          // waiting for completion (barrier)
          async_merger.wait_notifications(v_individual_accumulators.size());
          

          // gathering the mus
          mu = async_merger.get_merged_result().first;
          count_vector_t const& count_vector = async_merger.get_merged_result().second;
          // divide each of the acc by acc_counts, and then take the norm
          for(int i = 0; i < number_of_dimensions; i++)
          {
            assert(count_vector[i]);
            mu[i] /= count_vector[i];
          }

          // normalize mu on the sphere
          mu /= norm_op(mu);

        }


        // orthogonalisation against previous eigenvectors
        for(it_o_eigenvalues_t it_mus(it_output_eigen_vector_beginning); it_mus < it_eigenvectors; ++it_mus)
        {
          mu -= boost::numeric::ublas::inner_prod(mu, *it_mus) * (*it_mus);
        }
        mu /= norm_op(mu);

        // mu is the eigenvector of the current dimension, we store it in the output vector
        *it_eigenvectors = mu;

        // projection onto the orthogonal subspace
        if(current_dimension < max_dimension_to_compute - 1)
        {
          async_merger.init_notifications();

          // pushing the update of the mu (and signs)
          for(int i = 0; i < v_individual_accumulators.size(); i++)
          {
            ioService.post(
              boost::bind(
                &async_processor_t::project_onto_orthogonal_subspace, 
                boost::ref(v_individual_accumulators[i]), 
                boost::cref(*it_eigenvectors))); // this is not mu, since we are changing it before the process ends here
          }

          // this is to follow the matlab implementation, but the idea is the following:
          // each time we pick a new candidate vector, we project it to the orthogonal subspace of the previously computed 
          // eigenvectors. This can be done in two ways:
          // 1. compute the projection on the orthogonal subspace of the current (or next) candidate
          // 2. compute the projection on the orthogonal subspace of the remainder elements
          //
          // in order to be consistent with the matlab implementation, the second choice is implemented here
          if(current_dimension+1 < max_dimension_to_compute)
          {
            it_o_eigenvalues_t remainder(it_eigenvectors);
            ++remainder;

            if(!details::gram_schmidt_orthonormalisation(it_output_eigen_vector_beginning, it_output_eigen_vector_end, remainder, norm_op))
            {
              return false;
            }

            mu = *remainder;
          }


          // wait for the workers
          async_merger.wait_notifications(v_individual_accumulators.size());

          
        }

      }

      return true;
    }

  };




}


#endif /* ROBUST_PCA_TRIMMED_HPP__ */