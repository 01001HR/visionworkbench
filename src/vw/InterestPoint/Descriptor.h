// __BEGIN_LICENSE__
//  Copyright (c) 2006-2012, United States Government as represented by the
//  Administrator of the National Aeronautics and Space Administration. All
//  rights reserved.
//
//  The NASA Vision Workbench is licensed under the Apache License,
//  Version 2.0 (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
// __END_LICENSE__


/// \file Descriptor.h
///
/// Basic classes and functions for generating interest point descriptors.
///
#ifndef __VW_INTERESTPOINT_DESCRIPTOR_H__
#define __VW_INTERESTPOINT_DESCRIPTOR_H__

#include <vw/Core/Debugging.h>
#include <vw/Image/ImageView.h>
#include <vw/Image/Transform.h>
#include <vw/FileIO/DiskImageResource.h>
#include <vw/InterestPoint/InterestData.h>
#include <vw/InterestPoint/MatrixIO.h>
#include <vw/InterestPoint/VectorIO.h>
#include <vw/InterestPoint/IntegralImage.h>

namespace vw {
namespace ip {

  template <class ImplT>
  class DescriptorGeneratorBase {

  public:

    /// \cond INTERNAL
    // Methods to access the derived type
    inline ImplT& impl() { return static_cast<ImplT&>(*this); }
    inline ImplT const& impl() const { return static_cast<ImplT const&>(*this); }
    /// \endcond

    // Given an image and a list of interest points, set the
    // descriptor field of the interest points using the
    // compute_descriptor() method provided by the subclass.
    template <class ViewT>
    void operator() ( ImageViewBase<ViewT> const& image,
                      InterestPointList& points ) {
      (*this)( image, points.begin(), points.end() );
    }

    template <class ViewT, class IterT>
    void operator() ( ImageViewBase<ViewT> const& image,
                      IterT start, IterT end ) {
      // Timing
      Timer total("\tTotal elapsed time", DebugMessage, "interest_point");

      for (InterestPointList::iterator i = start; i != end; ++i) {

        // First we compute the support region based on the interest point
        ImageView<PixelGray<float> > support =
          get_support(*i, pixel_cast<PixelGray<float> >(channel_cast_rescale<float>(image.impl())));

        // Next, we pass the support region and the interest point to
        // the descriptor generator ( compute_descriptor() ) supplied
        // by the subclass.
        i->descriptor.set_size( impl().descriptor_size() );
        impl().compute_descriptor( support, i->begin(), i->end() );
      }
    }

    // Default suport size ( i.e. descriptor window)
    int support_size() { return 41; }
    // Default descriptor(vector) length
    int descriptor_size() { return 128; }

    /// Get the size x size support region around an interest point,
    /// rescaled by the scale factor and rotated by the specified
    /// angle. Also, delay raster until assigment.
    template <class ViewT>
    inline TransformView<InterpolationView<EdgeExtensionView<ViewT, ZeroEdgeExtension>, BilinearInterpolation>, AffineTransform>
    get_support( InterestPoint const& pt,
                 ImageViewBase<ViewT> const& source) {

      float half_size = ((float)(impl().support_size() - 1)) / 2.0f;
      float scaling = 1.0f / pt.scale;
      double c=cos(-pt.orientation), s=sin(-pt.orientation);

      return transform(source.impl(),
                       AffineTransform( Matrix2x2(scaling*c, -scaling*s,
                                                  scaling*s, scaling*c),
                                        Vector2(scaling*(s*pt.y-c*pt.x)+half_size,
                                                -scaling*(s*pt.x+c*pt.y)+half_size) ),
                       impl().support_size(), impl().support_size() );
    }

  };

  template <class ViewT, class DescriptorT>
  class InterestPointDescriptionTask : public Task, private boost::noncopyable {
    ViewT m_view;
    DescriptorT& m_descriptor;
    int m_id, m_max_id;
    typename InterestPointList::iterator m_start, m_stop;

  public:
    InterestPointDescriptionTask( ImageViewBase<ViewT> const& view, DescriptorT& descriptor,
                                  int id, int max_id,
                                  typename InterestPointList::iterator start,
                                  typename InterestPointList::iterator stop ) :
      m_view( view.impl() ), m_descriptor( descriptor ), m_id( id ),
      m_max_id( max_id ), m_start(start), m_stop( stop ) {}

    virtual ~InterestPointDescriptionTask(){}

    void operator()() {
      BBox2i image_crop_bounds;
      const float half_size = ((float)( m_descriptor.support_size() - 1)) / 2.0f;
      BBox2i support_size( 0, 0, m_descriptor.support_size(),
                           m_descriptor.support_size() );
      for ( typename InterestPointList::iterator it = m_start;
            it != m_stop; it++ ) {
        float scaling = 1.0f / it->scale;
        double c=cos(-it->orientation), s=sin(-it->orientation);

        AffineTransform tx( Matrix2x2(scaling*c, -scaling*s,
                                      scaling*s, scaling*c),
                            Vector2(scaling*(s * it->y - c * it->x) + half_size,
                                    -scaling*(s * it->x + c * it->y) + half_size) );
        image_crop_bounds.grow( tx.reverse_bbox( support_size ) );
      }
      image_crop_bounds.expand( 1 );
      vw_out(InfoMessage, "interest_point") << "Describing interest points in block "
                                            << m_id + 1 << "/" << m_max_id << "   [ "
                                            << image_crop_bounds << " ]\n";

      // Reindex all the points to use image_crop_bounds
      for ( typename InterestPointList::iterator it = m_start;
            it != m_stop; it++ ) {
        it->x -= image_crop_bounds.min().x();
        it->y -= image_crop_bounds.min().y();
      }

      // Generate descriptors base on this little crop
      ImageView<PixelGray<float> > image =
        crop( edge_extend(m_view.impl(), ZeroEdgeExtension()), image_crop_bounds );
      m_descriptor( image, m_start, m_stop );

      // Reindex all the points back to the global origin
      for ( typename InterestPointList::iterator it = m_start;
            it != m_stop; it++ ) {
        it->x += image_crop_bounds.min().x();
        it->y += image_crop_bounds.min().y();
      }
    }
  };

  struct IsInBBox {
    BBox2i m_bbox;

    IsInBBox( BBox2i const& bbox ) : m_bbox( bbox ) {}
    bool operator()( InterestPoint const& ip ) {
      return m_bbox.contains( Vector2i( ip.x, ip.y ) );
    }
  };

  // There is a lot of memory allocation created on task generation. I
  // couldn't figure it out in a reasonable time frame. Thus now we
  // generate tasks on demand which should lower the instantaneous
  // memory requirement.
  template <class ViewT, class DescriptorT>
  class InterestDescriptionQueue : public WorkQueue {
    ViewT m_view;
    DescriptorT& m_descriptor;
    std::vector<BBox2i> m_bboxes;
    std::vector<typename InterestPointList::iterator> m_section_start, m_section_stop;
    Mutex m_mutex;
    size_t m_index;

    typedef InterestPointDescriptionTask<ViewT, DescriptorT> task_type;

  public:

    InterestDescriptionQueue( ImageViewBase<ViewT> const& view, DescriptorT& descriptor,
                              std::vector<BBox2i> const& bboxes,
                              std::vector<typename InterestPointList::iterator> const& section_start,
                              std::vector<typename InterestPointList::iterator> const& section_stop ) :
      m_view(view.impl()), m_descriptor(descriptor), m_bboxes(bboxes),
      m_section_start(section_start), m_section_stop(section_stop), m_index(0)  {
      this->notify();
    }

    size_t size() {
      return m_bboxes.size();
    }

    virtual boost::shared_ptr<Task> get_next_task() {
      Mutex::Lock lock(m_mutex);
      if ( m_index == m_bboxes.size() )
        return boost::shared_ptr<Task>();

      m_index++;
      typename InterestPointList::iterator sstart = m_section_start[m_index-1];
      typename InterestPointList::iterator sstop  = m_section_stop[m_index-1];

      // Generate a task to build descriptors for these interest points that exist only in this bbox
      return boost::shared_ptr<Task> ( new task_type( m_view, m_descriptor, m_index-1, m_bboxes.size(),
                                                      sstart, sstop ) );
    }
  };

  /// This function implements multithreaded interest point
  /// description. Threads are spun off to process the image in 1024 x
  /// 1024 pixel block plus some padding.
  template <class ViewT, class DescriptorT>
  void describe_interest_points( ImageViewBase<ViewT> const& view, DescriptorT& descriptor,
                                 InterestPointList& list ) {
    typedef InterestPointDescriptionTask<ViewT, DescriptorT> task_type;

    VW_OUT(DebugMessage, "interest_point")
      << "Running MT interest point descriptor.  Input image: [ "
      << view.impl().cols() << " x " << view.impl().rows() << " ]\n";

    // Process the image in 1024x1024 pixel blocks.
    int tile_size = vw_settings().default_tile_size();
    if (tile_size < 1024) tile_size = 1024;
    std::vector<BBox2i> bboxes = image_blocks(view.impl(), tile_size, tile_size);
    std::vector<typename InterestPointList::iterator> section_start, section_stop;
    typename InterestPointList::iterator sstop = list.begin();
    for ( size_t i = 0; i < bboxes.size(); ++i ) {
      typename InterestPointList::iterator sstart = sstop;
      sstop = std::partition( sstart, list.end(), IsInBBox( bboxes[i] ) );
      section_start.push_back(sstart);
      section_stop.push_back(sstop);
    }

    InterestDescriptionQueue<ViewT, DescriptorT>descriptor_que(view, descriptor,
                                                               bboxes,
                                                               section_start, section_stop);

    VW_OUT(DebugMessage, "interest_point") << "Waiting for threads to terminate.\n";
    descriptor_que.join_all();

    VW_OUT(DebugMessage, "interest_point") << "MT interest point description complete.\n";
    return;
  }

  /// A basic example descriptor class. The descriptor for an interest
  /// point is simply the pixel values in the support region around
  /// the point. It is normalized to provide some tolerance to changes
  /// in illumination.
  struct PatchDescriptorGenerator : public DescriptorGeneratorBase<PatchDescriptorGenerator> {

    template <class ViewT, class IterT>
    void compute_descriptor( ImageViewBase<ViewT> const& support,
                             IterT first, IterT last ) const {
      double sqr_length = 0;
      IterT fill = first;
      for (int j = 0; j < support.impl().rows(); j++) {
        for (int i = 0; i < support.impl().cols(); i++) {
          PixelGray<float> pix(support.impl()(i,j));
          //result(j*support.impl().cols() + i) = pix.v();
          *fill = pix.v();
          sqr_length += (*fill)*(*fill);
          fill++;
        }
      }
      // Normalizing
      sqr_length = sqrt(sqr_length);
      for ( ; first != last; first++ )
        *first /= sqr_length;
    }

    int descriptor_size() { return 41*41; }
  };

  // An implementation of PCA-SIFT
  struct PCASIFTDescriptorGenerator : public DescriptorGeneratorBase<PCASIFTDescriptorGenerator> {

    std::string basis_filename, avg_filename;
    Matrix<float> pca_basis;
    Vector<float> pca_avg;

    PCASIFTDescriptorGenerator(const std::string& pcabasis_filename,
                               const std::string& pcaavg_filename)
      : basis_filename(pcabasis_filename), avg_filename(pcaavg_filename) {

      // Read the PCA basis matrix and average vector
      read_matrix(pca_basis, basis_filename);
      read_vector(pca_avg, avg_filename);
    }


    template <class ViewT, class IterT>
    void compute_descriptor( ImageViewBase<ViewT> const& support,
                             IterT first, IterT last) const {

      for ( IterT fill = first; fill != last; fill++ )
        *fill = 0;

      // compute normalization constant (sum squares)
      double norm_const = 0;
      for (int j = 0; j < support.impl().rows(); j++) {
        for (int i = 0; i < support.impl().cols(); i++) {
          norm_const += support.impl()(i,j) * support.impl()(i,j);
        }
      }
      norm_const = sqrt(norm_const);

      double norm_pixel = 0;
      // project image patch onto PCA basis to get descriptor
      unsigned int index = 0;
      for (int j = 0; j < support.impl().rows(); j++) {
        for (int i = 0; i < support.impl().cols(); i++) {
          norm_pixel = support.impl()(i,j).v()/norm_const - pca_avg(index);

          IterT fill = first;
          for (unsigned k = 0; k < pca_basis.cols(); k++) {
            *fill += norm_pixel * pca_basis(index,k);
            fill++;
          }
          ++index;
        }
      }
    }

    int descriptor_size() { return pca_basis.cols(); }
  };

  // A Simple Scaled Gradient descriptor that reduces the number of elements
  // used in the descriptor and is hopefully more robust against
  // illumination changes.
  struct SGradDescriptorGenerator : public DescriptorGeneratorBase<SGradDescriptorGenerator> {

    static const uint32 box_strt[5];
    static const uint32 box_size[5];
    static const uint32 box_half[5];

    template <class ViewT, class IterT>
    void compute_descriptor(ImageViewBase<ViewT> const& support,
                            IterT first, IterT last) const {

      typedef typename PixelChannelType<typename ViewT::pixel_type>::type channel_type;
      ImageView<channel_type> iimage = IntegralImage(support);

      channel_type sqr_length = 0;

      // Iterate through scales
      IterT fill = first;
      for ( uint8 s = 0; s < 5; s++ ) {
        float inv_bh2 = 1 / float(box_half[s]*box_half[s]);

        // Iterate though quadrants
        for ( uint8 i = 0; i < 3; i++ ) {
          for ( uint8 j = 0; j < 3; j++ ) {
            Vector2i top_left( box_strt[s]+i*box_size[s],
                               box_strt[s]+j*box_size[s] );

            float minor_quad[4];

            // 1.) Top Left in local
            minor_quad[0] = IntegralBlock( iimage,
                                           top_left,
                                           top_left+Vector2i(box_half[s],
                                                             box_half[s]) );
            // 2.) Top Right in local
            minor_quad[1] = IntegralBlock( iimage,
                                           top_left+Vector2i(box_half[s],0),
                                           top_left+Vector2i(2*box_half[s],
                                                             box_half[s]) );
            // 3.) Bot Left in local
            minor_quad[2] = IntegralBlock( iimage,
                                           top_left+Vector2i(0,box_half[s]),
                                           top_left+Vector2i(box_half[s],
                                                             2*box_half[s]) );
            // 4.) Bot Right in local
            minor_quad[3] = IntegralBlock( iimage,
                                           top_left+Vector2i(box_half[s],
                                                             box_half[s]),
                                           top_left+Vector2i(2*box_half[s],
                                                             2*box_half[s]) );

            // 5.) Pulling out gradients
            *fill = (minor_quad[0] - minor_quad[2])*inv_bh2;
            sqr_length += (*fill)*(*fill); fill++;
            *fill = (minor_quad[1] - minor_quad[3])*inv_bh2;
            sqr_length += (*fill)*(*fill); fill++;
            *fill = (minor_quad[0] - minor_quad[1])*inv_bh2;
            sqr_length += (*fill)*(*fill); fill++;
            *fill = (minor_quad[2] - minor_quad[3])*inv_bh2;
            sqr_length += (*fill)*(*fill); fill++;
          } // end j
        } // end i
      } // end s
      VW_DEBUG_ASSERT( fill == last, LogicErr() << "Allocated Vector size does not appear to match code's expectations." );

      // Normalizing
      float sqr_length_inv = 1.0f / sqrt(float(sqr_length));
      if ( !std::isnormal(sqr_length_inv) )
        sqr_length_inv = 0.0f;
      for ( ; first != last; first++ )
        (*first) *= sqr_length_inv;
    }

    int support_size() { return 42; }
    int descriptor_size() { return 180; }
  };

}} // namespace vw::ip


#endif //__VW_INTERESTPOINT_DESCRIPTOR_H__
