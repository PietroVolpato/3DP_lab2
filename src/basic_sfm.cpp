#include "basic_sfm.h"

#include <iostream>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include <ceres/ceres.h>
#include <ceres/rotation.h>

using namespace std;

struct ReprojectionError
{
  //////////////////////////// Code to be completed (5/7) //////////////////////////////////
  // This class should include an auto-differentiable cost function (see Ceres Solver docs).
  // Remember that we are dealing with a normalized, canonical camera:
  // point projection is easy! To rotete a point given an axis-angle rotation, use
  // the Ceres function:
  // AngleAxisRotatePoint(...) (see ceres/rotation.h)
  // WARNING: When dealing with the AutoDiffCostFunction template parameters,
  // pay attention to the order of the template parameters
  //////////////////////////////////////////////////////////////////////////////////////////

 
  ReprojectionError(double observed_x, double observed_y)
      : observed_x_(observed_x), observed_y_(observed_y) {}


  template <typename T>
  bool operator()(const T *const camera, const T *const point, T *residuals) const
  {
    // Camera is a 6D vector [angle_axis, translation]
    // point is a 3D vector

    // angle-axis rotation to the point
    T p[3];
    ceres::AngleAxisRotatePoint(camera, point, p);

    // Add the translation
    p[0] += camera[3];
    p[1] += camera[4];
    p[2] += camera[5];

    // this was introduced to avoid some numerical problems
    if (p[2] <= T(1e-6))
    {
      // point is behind the camera, large error
      residuals[0] = T(100.0); //1000
      residuals[1] = T(100.0);
      return true;
    }

    // no need to deal with distorsion 

    // Compute the projection ( no - sign, different from tutorial)
    T predicted_x = p[0] / p[2];
    T predicted_y = p[1] / p[2];

    // The error is the difference between the predicted and observed position
    residuals[0] = predicted_x - T(observed_x_);
    residuals[1] = predicted_y - T(observed_y_);

    return true;
  }

  // Factory to hide the construction of the CostFunction object from the client code (from tutorial)
  static ceres::CostFunction *Create(const double observed_x, const double observed_y)
  {
    return (new ceres::AutoDiffCostFunction<ReprojectionError, 2, 6, 3>(
        new ReprojectionError(observed_x, observed_y)));
  }

  double observed_x_;
  double observed_y_;

  /////////////////////////////////////////////////////////////////////////////////////////
};

namespace
{
  typedef Eigen::Map<Eigen::VectorXd> VectorRef;
  typedef Eigen::Map<const Eigen::VectorXd> ConstVectorRef;

  template <typename T>
  void FscanfOrDie(FILE *fptr, const char *format, T *value)
  {
    int num_scanned = fscanf(fptr, format, value);
    if (num_scanned != 1)
    {
      cerr << "Invalid UW data file.";
      exit(-1);
    }
  }

} // namespace

BasicSfM::~BasicSfM()
{
  reset();
}

void BasicSfM::reset()
{
  point_index_.clear();
  cam_pose_index_.clear();
  observations_.clear();
  colors_.clear();
  parameters_.clear();

  num_cam_poses_ = num_points_ = num_observations_ = num_parameters_ = 0;
}

void BasicSfM::readFromFile ( const std::string& filename, bool load_initial_guess, bool load_colors  )
{
  reset();

  FILE* fptr = fopen(filename.c_str(), "r");

  if (fptr == NULL)
  {
    cerr << "Error: unable to open file " << filename;
    return;
  };

  // This wil die horribly on invalid files. Them's the breaks.
  FscanfOrDie(fptr, "%d", &num_cam_poses_);
  FscanfOrDie(fptr, "%d", &num_points_);
  FscanfOrDie(fptr, "%d", &num_observations_);

  cout << "Header: " << num_cam_poses_
       << " " << num_points_
       << " " << num_observations_<<std::endl;

  point_index_.resize(num_observations_);
  cam_pose_index_.resize(num_observations_);
  observations_.resize(2 * num_observations_);

  num_parameters_ = camera_block_size_ * num_cam_poses_ + point_block_size_ * num_points_;
  parameters_.resize(num_parameters_);

  for (int i = 0; i < num_observations_; ++i)
  {
    FscanfOrDie(fptr, "%d", cam_pose_index_.data() + i);
    FscanfOrDie(fptr, "%d", point_index_.data() + i);
    for (int j = 0; j < 2; ++j)
    {
      FscanfOrDie(fptr, "%lf", observations_.data() + 2*i + j);
    }
  }

  if( load_colors )
  {
    colors_.resize(3*num_points_);
    for (int i = 0; i < num_points_; ++i)
    {
      int r,g,b;
      FscanfOrDie(fptr, "%d", &r );
      FscanfOrDie(fptr, "%d", &g);
      FscanfOrDie(fptr, "%d", &b );
      colors_[i*3] = r;
      colors_[i*3 + 1] = g;
      colors_[i*3 + 2] = b;
    }
  }

  if( load_initial_guess )
  {
    cam_pose_optim_iter_.resize(num_cam_poses_, 1 );
    pts_optim_iter_.resize( num_points_, 1 );

    for (int i = 0; i < num_parameters_; ++i)
    {
      FscanfOrDie(fptr, "%lf", parameters_.data() + i);
    }
  }
  else
  {
    memset(parameters_.data(), 0, num_parameters_*sizeof(double));
    // Masks used to indicate which cameras and points have been optimized so far
    cam_pose_optim_iter_.resize(num_cam_poses_, 0 );
    pts_optim_iter_.resize( num_points_, 0 );
  }

  fclose(fptr);
}


void BasicSfM::writeToFile (const string& filename, bool write_unoptimized ) const
{
  FILE* fptr = fopen(filename.c_str(), "w");

  if (fptr == NULL) {
    cerr << "Error: unable to open file " << filename;
    return;
  };

  if( write_unoptimized )
  {
    fprintf(fptr, "%d %d %d\n", num_cam_poses_, num_points_, num_observations_);

    for (int i = 0; i < num_observations_; ++i)
    {
      fprintf(fptr, "%d %d", cam_pose_index_[i], point_index_[i]);
      for (int j = 0; j < 2; ++j) {
        fprintf(fptr, " %g", observations_[2 * i + j]);
      }
      fprintf(fptr, "\n");
    }

    if( colors_.size() == num_points_*3 )
    {
      for (int i = 0; i < num_points_; ++i)
        fprintf(fptr, "%d %d %d\n", colors_[i*3], colors_[i*3 + 1], colors_[i*3 + 2]);
    }

    for (int i = 0; i < num_cam_poses_; ++i)
    {
      const double *camera = parameters_.data() + camera_block_size_ * i;
      for (int j = 0; j < camera_block_size_; ++j) {
        fprintf(fptr, "%.16g\n", camera[j]);
      }
    }

    const double* points = pointBlockPtr();
    for (int i = 0; i < num_points_; ++i)
    {
      const double* point = points + i * point_block_size_;
      for (int j = 0; j < point_block_size_; ++j) {
        fprintf(fptr, "%.16g\n", point[j]);
      }
    }
  }
  else
  {
    int num_cameras = 0, num_points = 0, num_observations = 0;

    for (int i = 0; i < num_cam_poses_; ++i)
      if( cam_pose_optim_iter_[i] > 0 ) num_cameras++;

    for (int i = 0; i < num_points_; ++i)
      if( pts_optim_iter_[i] > 0 ) num_points++;

    for (int i = 0; i < num_observations_; ++i)
      if( cam_pose_optim_iter_[cam_pose_index_[i]] > 0  && pts_optim_iter_[point_index_[i]] > 0 ) num_observations++;

    fprintf(fptr, "%d %d %d\n", num_cameras, num_points, num_observations);

    for (int i = 0; i < num_observations_; ++i)
    {
      if( cam_pose_optim_iter_[cam_pose_index_[i]] > 0  && pts_optim_iter_[point_index_[i]] > 0 )
      {
        fprintf(fptr, "%d %d", cam_pose_index_[i], point_index_[i]);
        for (int j = 0; j < 2; ++j) {
          fprintf(fptr, " %g", observations_[2 * i + j]);
        }
        fprintf(fptr, "\n");
      }
    }

    if( colors_.size() == num_points_*3 )
    {
      for (int i = 0; i < num_points_; ++i)
      {
        if(pts_optim_iter_[i] > 0)
          fprintf(fptr, "%d %d %d\n", colors_[i*3], colors_[i*3 + 1], colors_[i*3 + 2]);
      }
    }

    for (int i = 0; i < num_cam_poses_; ++i)
    {
      if( cam_pose_optim_iter_[i] > 0 )
      {
        const double *camera = parameters_.data() + camera_block_size_ * i;
        for (int j = 0; j < camera_block_size_; ++j)
        {
          fprintf(fptr, "%.16g\n", camera[j]);
        }
      }
    }

    const double* points = pointBlockPtr();
    for (int i = 0; i < num_points_; ++i)
    {
      if( pts_optim_iter_[i] > 0 )
      {
        const double* point = points + i * point_block_size_;
        for (int j = 0; j < point_block_size_; ++j)
        {
          fprintf(fptr, "%.16g\n", point[j]);
        }
      }
    }
  }

  fclose(fptr);
}

// Write the problem to a PLY file for inspection in Meshlab or CloudCompare.
void BasicSfM::writeToPLYFile (const string& filename, bool write_unoptimized ) const
{
  ofstream of(filename.c_str());

  int num_cameras, num_points;

  if( write_unoptimized )
  {
    num_cameras = num_cam_poses_;
    num_points = num_points_;
  }
  else
  {
    num_cameras = 0;
    num_points = 0;
    for (int i = 0; i < num_cam_poses_; ++i)
      if( cam_pose_optim_iter_[i] > 0 ) num_cameras++;

    for (int i = 0; i < num_points_; ++i)
      if( pts_optim_iter_[i] > 0 ) num_points++;
  }

  of << "ply"
     << '\n' << "format ascii 1.0"
     << '\n' << "element vertex " << num_cameras + num_points
     << '\n' << "property float x"
     << '\n' << "property float y"
     << '\n' << "property float z"
     << '\n' << "property uchar red"
     << '\n' << "property uchar green"
     << '\n' << "property uchar blue"
     << '\n' << "end_header" << endl;

  bool write_colors = ( colors_.size() == num_points_*3 );
  if( write_unoptimized )
  {
    // Export extrinsic data (i.e. camera centers) as green points.
    double center[3];
    for (int i = 0; i < num_cam_poses_; ++i)
    {
      const double* camera = cameraBlockPtr(i);
      cam2center (camera, center);
      of << center[0] << ' ' << center[1] << ' ' << center[2]
         << " 0 255 0" << '\n';
    }

    // Export the structure (i.e. 3D Points) as white points.
    const double* points = pointBlockPtr();
    for (int i = 0; i < num_points_; ++i)
    {
      const double* point = points + i * point_block_size_;
      for (int j = 0; j < point_block_size_; ++j)
      {
        of << point[j] << ' ';
      }
      if (write_colors )
        of << int(colors_[3*i])<<" " << int(colors_[3*i + 1])<<" "<< int(colors_[3*i + 2])<<"\n";
      else
        of << "255 255 255\n";
    }
  }
  else
  {
    // Export extrinsic data (i.e. camera centers) as green points.
    double center[3];
    for (int i = 0; i < num_cam_poses_; ++i)
    {
      if( cam_pose_optim_iter_[i] > 0 )
      {
        const double* camera = cameraBlockPtr(i);
        cam2center (camera, center);
        of << center[0] << ' ' << center[1] << ' ' << center[2]
           << " 0 255 0" << '\n';
      }
    }

    // Export the structure (i.e. 3D Points) as white points.
    const double* points = pointBlockPtr();;
    for (int i = 0; i < num_points_; ++i)
    {
      if( pts_optim_iter_[i] > 0 )
      {
        const double* point = points + i * point_block_size_;
        for (int j = 0; j < point_block_size_; ++j)
        {
          of << point[j] << ' ';
        }
        if (write_colors )
          of << int(colors_[3*i])<<" " << int(colors_[3*i + 1])<<" "<< int(colors_[3*i + 2])<<"\n";
        else
          of << "255 255 255\n";
      }
    }
  }
  of.close();
}

/* c_{w,cam} = R_{cam}'*[0 0 0]' - R_{cam}'*t_{cam} -> c_{w,cam} = - R_{cam}'*t_{cam} */
void BasicSfM::cam2center (const double* camera, double* center) const
{
  ConstVectorRef angle_axis_ref(camera, 3);

  Eigen::VectorXd inverse_rotation = -angle_axis_ref;
  ceres::AngleAxisRotatePoint(inverse_rotation.data(), camera + 3, center);
  VectorRef(center, 3) *= -1.0;
}

/* [0 0 0]' = R_{cam}*c_{w,cam} + t_{cam} -> t_{cam} = - R_{cam}*c_{w,cam} */
void BasicSfM::center2cam (const double* center, double* camera) const
{
  ceres::AngleAxisRotatePoint(camera, center, camera + 3);
  VectorRef(camera + 3, 3) *= -1.0;
}


bool BasicSfM::checkCheiralityConstraint (int pos_idx, int pt_idx )
{
  double *camera = cameraBlockPtr(pos_idx),
         *point = pointBlockPtr(pt_idx);

  double p[3];
  ceres::AngleAxisRotatePoint(camera, point, p);

  // camera[5] is the z cooordinate wrt the camera at pose pose_idx
  p[2] += camera[5];
  return p[2] > 0;
}

void BasicSfM::printPose ( int idx )  const
{
  const double *cam = cameraBlockPtr(idx);
  std::cout<<"camera["<<idx<<"]"<<std::endl
           <<"{"<<std::endl
           <<"\t r_vec : ("<<cam[0]<<", "<<cam[1]<<", "<<cam[2]<<")"<<std::endl
           <<"\t t_vec : ("<<cam[3]<<", "<<cam[4]<<", "<<cam[5]<<")"<<std::endl;

  std::cout<<"}"<<std::endl;
}


void BasicSfM::printPointParams ( int idx ) const
{
  const double *pt = pointBlockPtr(idx);
  std::cout<<"point["<<idx<<"] : ("<<pt[0]<<", "<<pt[1]<<", "<<pt[2]<<")"<<std::endl;
}


void BasicSfM::solve()
{
  // For each camera pose, prepare a map that reports the pairs [point index, observation index]
  // This map is used to quickly retrieve the observation index given a 3D point index
  // For instance, to query if the camera pose with index i_cam observed the
  // 3D point with index i_pt, check if cam_observation_[i_cam].find( i_pt ) is not cam_observation_[i_cam].end(), i.e.,:
  // if(cam_observation_[i_cam].find( i_pt ) != cam_observation_[i_cam].end())  { .... }
  // In case of success, you can retrieve the observation index obs_id simply with:
  // obs_id = cam_observation_[i_cam][i_pt]
  cam_observation_ = vector< map<int,int> > (num_cam_poses_ );
  for( int i_obs = 0; i_obs < num_observations_; i_obs++ )
  {
    int i_cam = cam_pose_index_[i_obs], i_pt = point_index_[i_obs];
    cam_observation_[i_cam][i_pt] = i_obs;
  }

  // Compute a (symmetric) num_cam_poses_ X num_cam_poses_ matrix
  // that counts the number of correspondences between pairs of camera poses
  Eigen::MatrixXi corr = Eigen::MatrixXi::Zero(num_cam_poses_, num_cam_poses_);

  for(int r = 0; r < num_cam_poses_; r++ )
  {
    for(int c = r + 1; c < num_cam_poses_; c++ )
    {
      int nc = 0;
      for( auto const& co_iter : cam_observation_[r] )
      {
        if( cam_observation_[c].find(co_iter.first ) != cam_observation_[c].end() )
          nc++;
      }
      corr(r,c) = nc;
    }
  }

  // num_cam_poses_ X num_cam_poses_ matrix to mask already tested seed pairs
  // already_tested_pair(r,c) == 0 -> not tested pair
  // already_tested_pair(r,c) != 0 -> already tested pair
  Eigen::MatrixXi already_tested_pair = Eigen::MatrixXi::Zero(num_cam_poses_, num_cam_poses_);


  // Indices of the two camera poses that define the initial seed pair
  int seed_pair_idx0, seed_pair_idx1;

  // Look for a suitable seed pair....
  while( true )
  {
    int max_corr = -1;
    for(int r = 0; r < num_cam_poses_; r++ )
    {
      for (int c = r + 1; c < num_cam_poses_; c++)
      {
        if( !already_tested_pair(r,c) && corr(r,c) > max_corr )
        {
          max_corr = corr(r,c);
          seed_pair_idx0 = r;
          seed_pair_idx1 = c;
        }
      }
    }

    if( max_corr < 0 )
    {
      std::cout<<"No seed pair found, exiting"<<std::endl;
      return;
    }
    already_tested_pair(seed_pair_idx0, seed_pair_idx1) = 1;

    if (incrementalReconstruction( seed_pair_idx0, seed_pair_idx1 ))
    {
      std::cout<<"Recostruction completed, exiting"<<std::endl;
      return;
    }
    else
    {
      std::cout<<"Try to look for a better seed pair"<<std::endl;
    }
  }
}

bool BasicSfM::incrementalReconstruction( int seed_pair_idx0, int seed_pair_idx1 )
{
  // Reset all parameters: we are starting a brand new reconstruction from a new seed pair
  memset(parameters_.data(), 0, num_parameters_*sizeof(double));
  // Masks used to indicate which cameras and points have been optimized so far
  cam_pose_optim_iter_.resize(num_cam_poses_, 0 );
  pts_optim_iter_.resize( num_points_, 0 );


  // Init R,t between the seed pair
  cv::Mat init_r_mat, init_r_vec, init_t_vec;

  std::vector<cv::Point2d> points0, points1;
  cv::Mat inlier_mask_E, inlier_mask_H;

  // Collect matches between the two images of the seed pair, to be used to extract the models E and H
  for (auto const &co_iter: cam_observation_[seed_pair_idx0])
  {
    if (cam_observation_[seed_pair_idx1].find(co_iter.first) != cam_observation_[seed_pair_idx1].end())
    {
      points0.emplace_back(observations_[2*co_iter.second],observations_[2*co_iter.second + 1]);
      points1.emplace_back(observations_[2*cam_observation_[seed_pair_idx1][co_iter.first]],
                           observations_[2*cam_observation_[seed_pair_idx1][co_iter.first] + 1]);
    }
  }

  // Canonical camera so identity K
  cv::Mat_<double> intrinsics_matrix = cv::Mat_<double>::eye(3,3);

  //////////////////////////// Code to be completed (3/7) /////////////////////////////////
  // Extract both Essential matrix E and Homograph matrix H.
  // As threshold in the functions to estimate both models, you may use 0.001 or similar.
  // Check that the number of inliers for the model E is higher than the number of
  // inliers for the model H (-> use inlier_mask_E and inlier_mask_H defined above <-), otherwise, return false
  //  (we will try a new seed pair),
  // If true, recover from E the initial rigid body transformation between seed_pair_idx0
  // and seed_pair_idx1 by using the cv::recoverPose() OpenCV function (use inlier_mask_E as input/output param)
  // Check if the recovered transformation is mainly given by a sideward motion, which is better than forward one.
  // Otherwise, return false (we will try a new seed pair)
  // In case of "good" sideward motion, store the transformation into init_r_mat and  init_t_vec; defined above
  /////////////////////////////////////////////////////////////////////////////////////////

  // estimate the Essential matrix 
  double threshold = 0.001;
  cv::Mat E = cv::findEssentialMat(points0, points1, intrinsics_matrix, cv::RANSAC, 0.999, threshold, inlier_mask_E);

  // estimate the Homography matrix 
  cv::Mat H = cv::findHomography(points0, points1, cv::RANSAC, threshold, inlier_mask_H);

  // count inliers for both models
  int num_inliers_E = cv::countNonZero(inlier_mask_E);
  int num_inliers_H = cv::countNonZero(inlier_mask_H);

  std::cout << "Inliers E: " << num_inliers_E << ", Inliers H: " << num_inliers_H << std::endl;

  // Check if inliers for E are more than inliers for H 
  if (num_inliers_E <= num_inliers_H)
  {
    std::cout << "H has more inliers than E. Will try a new seed pair" << std::endl;
    return false;
  }

  // recover rotation and translation from essential matrix
  int num_good_pts = cv::recoverPose(E, points0, points1, intrinsics_matrix, init_r_mat, init_t_vec, inlier_mask_E);

  // check if we have enough inliers after pose recovery
  if (num_good_pts < 10) //at least 5
  {
    std::cout << "Not enough points survived pose recovery. Will try a new seed pair" << std::endl;
    return false;
  }

  // check if the motion is mainly sideward rather than forward
  double tx = std::abs(init_t_vec.at<double>(0));
  double ty = std::abs(init_t_vec.at<double>(1));
  double tz = std::abs(init_t_vec.at<double>(2));

  double lateral_motion = std::sqrt(tx * tx + ty * ty);

  // if forward motion dominates z-component is larger than lateral components
  if (tz > lateral_motion)
  {
    std::cout << "Motion appears to be mainly forward. Will try a new seed pair" << std::endl;
    return false;
  }

  std::cout << "Found good seed pair with sideward motion." << std::endl;

  // the matrices init_r_mat and init_t_vec are already set by recoverPose,
  // so we don't need to do anything else here (if results are bad, they will anyway not be used)

  /////////////////////////////////////////////////////////////////////////////////////////

  int ref_cam_pose_idx = seed_pair_idx0, new_cam_pose_idx = seed_pair_idx1;

  // Initialize the first optimized poses, by integrating them into the registration
  // cam_pose_optim_iter_ and pts_optim_iter_ are simple mask vectors that define which camera poses and
  // which point positions have been already registered, specifically
  // if cam_pose_optim_iter_[pos_id] or pts_optim_iter_[id] are:
  // > 0 ---> The corresponding pose or point position has been already been estimated
  // == 0 ---> The corresponding pose or point position has not yet been estimated
  // == -1 ---> The corresponding pose or point position has been rejected due to e.g. outliers, etc...
  cam_pose_optim_iter_[ref_cam_pose_idx] = cam_pose_optim_iter_[new_cam_pose_idx] = 1;

  //Initialize the first RT wrt the reference position
  cv::Mat r_vec;
  // Recover the axis-angle rotation from init_r_mat
  cv::Rodrigues(init_r_mat, r_vec);

  // And update the parameters_ vector
  // First camera pose of the seed pair: just the identity transformation for now
  cv::Mat_<double> ref_rt_vec = (cv::Mat_<double>(3,1) << 0,0,0);
  initCamParams(ref_cam_pose_idx, ref_rt_vec, ref_rt_vec );
  // Second camera pose of the seed pair: the just recovered transformation
  initCamParams(new_cam_pose_idx, r_vec, init_t_vec );

  printPose(ref_cam_pose_idx);
  printPose(new_cam_pose_idx);

  // Triangulate the 3D points observed by both cameras
  cv::Mat_<double> proj_mat0 = cv::Mat_<double>::zeros(3, 4), proj_mat1(3, 4), hpoints4D;
  // First camera pose of the seed pair: just the identity transformation for now
  proj_mat0(0,0) = proj_mat0(1,1) = proj_mat0(2,2) = 1.0;
  // Second camera pose of the seed pair: the just recovered transformation
  init_r_mat.copyTo(proj_mat1(cv::Rect(0, 0, 3, 3)));
  init_t_vec.copyTo(proj_mat1(cv::Rect(3, 0, 1, 3)));

  cv::triangulatePoints(	proj_mat0, proj_mat1, points0, points1, hpoints4D );

  int r = 0;
  // Initialize the first optimized points
  for( auto const& co_iter : cam_observation_[ref_cam_pose_idx] )
  {
    auto &pt_idx = co_iter.first;
    if( cam_observation_[new_cam_pose_idx].find(pt_idx ) !=
        cam_observation_[new_cam_pose_idx].end() )
    {
      if( inlier_mask_E.at<unsigned char>(r) )
      {
        // Initialize the new point into the optimization
        double *pt = pointBlockPtr(pt_idx);

        // H-normalize the point
        pt[0] = hpoints4D.at<double>(0,r)/hpoints4D.at<double>(3,r);
        pt[1] = hpoints4D.at<double>(1,r)/hpoints4D.at<double>(3,r);
        pt[2] = hpoints4D.at<double>(2,r)/hpoints4D.at<double>(3,r);

        // Check the cheirality constraint
        if(pt[2] > 0.0 )
        {
          // Try to reproject the estimated 3D point in both cameras
          cv::Mat_<double> pt_3d = (cv::Mat_<double>(3,1) << pt[0],pt[1],pt[2]);

          pt_3d = init_r_mat*pt_3d + init_t_vec;

          cv::Point2d p0(pt[0]/pt[2], pt[1]/pt[2]),
              p1(pt_3d(0,0)/pt_3d(2,0), pt_3d(1,0)/pt_3d(2,0));

          // If the reprojection error is small, add the point to the reconstruction
          if(cv::norm(p0 - points0[r]) < max_reproj_err_ && cv::norm(p1 - points1[r]) < max_reproj_err_)
            pts_optim_iter_[pt_idx] = 1;
          else
            pts_optim_iter_[pt_idx] = -1;
        }
      }
    }
    r++;
  }

  // First bundle adjustment iteration: here we have only two camera poses, i.e., the seed pair
  bundleAdjustmentIter(new_cam_pose_idx );

  // Start to register new poses and observations...
  for(int iter = 1; iter < num_cam_poses_ - 1; iter++ )
  {
    // The vector n_init_pts stores the number of points already being optimized
    // that are projected in a new camera pose when is optimized for the first time
    std::vector<int> n_init_pts(num_cam_poses_, 0);
    int max_init_pts = -1;

    // Basic next best view selection strategy.
    // Select the new camera (new_cam_pose_idx) to be included in the optimization as the one that has
    // more projected points in common with the cameras already included in the optimization
    // for( int i_p = 0; i_p < num_points_; i_p++ )
    // {
    //   if( pts_optim_iter_[i_p] > 0 ) // Point already added
    //   {
    //     for(int i_c = 0; i_c < num_cam_poses_; i_c++ )
    //     {
    //       if( cam_pose_optim_iter_[i_c] == 0 && // New camera pose not yet registered
    //           cam_observation_[i_c].find( i_p ) != cam_observation_[i_c].end() ) // Dees camera i_c see this 3D point?
    //         n_init_pts[i_c]++;
    //     }
    //   }
    // }

    // for(int i_c = 0; i_c < num_cam_poses_; i_c++ )
    // {
    //   if( cam_pose_optim_iter_[i_c] == 0 && n_init_pts[i_c] > max_init_pts )
    //   {
    //     max_init_pts = n_init_pts[i_c];
    //     new_cam_pose_idx = i_c;
    //   }
    // }

    //////////////////////////// Code to be completed (OPTIONAL) ////////////////////////////////
    // Implement an alternative next best view selection strategy, e.g., the one presented
    // in class(see Structure From Motion Revisited paper, sec. 4.2). Just comment the basic next
    // best view selection strategy implemented above and replace it with yours.
    /////////////////////////////////////////////////////////////////////////////////////////
    const int K_t = 3;
    // Score for each candidate camera
    std::vector<double> scores(num_cam_poses_, -1.0);
    for(int i_c = 0; i_c < num_cam_poses_; i_c++) {
      // Skip already registered or rejected cameras
      if(cam_pose_optim_iter_[i_c] != 0) continue;
      // Collect indices of registered 3D points seen by camera i_c
      std::vector<int> pts;
      for(const auto& obs : cam_observation_[i_c]) {
        int pt_idx = obs.first;
        if(pts_optim_iter_[pt_idx] > 0)
          pts.push_back(pt_idx);
      }
      if(pts.empty()) continue;

      double score = 0.0;
      // Multi-resolution occupancy grids
      for(int l = 0; l < K_t; l++) {
        int K = 1 << l;
        // Occupancy map for this level
        std::vector<std::vector<bool>> occupied(K, std::vector<bool>(K, false));
        double weight = static_cast<double>(K) * static_cast<double>(K);
        for(int pt_idx : pts) {
          int obs_idx = cam_observation_[i_c][pt_idx];
          // Normalized image coordinates
          double x = observations_[2*obs_idx];
          double y = observations_[2*obs_idx + 1];
          // Map from [-1,1] to [0,1]
          double u = (x + 1.0) * 0.5;
          double v = (y + 1.0) * 0.5;
          int ix = std::min(std::max(static_cast<int>(u * K), 0), K - 1);
          int iy = std::min(std::max(static_cast<int>(v * K), 0), K - 1);
          if(!occupied[ix][iy]) {
            occupied[ix][iy] = true;
            score += weight;
          }
        }
      }
      scores[i_c] = score;
    }

    // Select the camera with the highest multi-resolution score
    new_cam_pose_idx = static_cast<int>(std::max_element(scores.begin(), scores.end()) - scores.begin());

    /////////////////////////////////////////////////////////////////////////////////////////


    // // Now new_cam_pose_idx is the index of the next camera pose to be registered
    // // Extract the 3D points that are projected in the new_cam_pose_idx-th pose and that are already registered
    std::vector<cv::Point3d> scene_pts;
    std::vector<cv::Point2d> img_pts;
    for( int i_p = 0; i_p < num_points_; i_p++ )
    {
      if (pts_optim_iter_[i_p] > 0 &&
          cam_observation_[new_cam_pose_idx].find(i_p) != cam_observation_[new_cam_pose_idx].end())
      {
        double *pt = pointBlockPtr(i_p);
        scene_pts.emplace_back(pt[0], pt[1], pt[2]);
        img_pts.emplace_back(observations_[cam_observation_[new_cam_pose_idx][i_p] * 2],
                             observations_[cam_observation_[new_cam_pose_idx][i_p] * 2 + 1]);
      }
    }
    if( scene_pts.size() <= 3 )
    {
      std::cout<<"No other positions can be optimized, exiting"<<std::endl;
      return false;
    }

    // Estimate an initial R,t by using PnP + RANSAC
    cv::solvePnPRansac(scene_pts, img_pts, intrinsics_matrix, cv::Mat(),
                       init_r_vec, init_t_vec, false, 100, max_reproj_err_ );
    // ... and add to the pool of optimized camera positions
    initCamParams(new_cam_pose_idx, init_r_vec, init_t_vec);
    cam_pose_optim_iter_[new_cam_pose_idx] = 1;

    // Extract the new points that, thanks to the new camera, are going to be optimized
    int n_new_pts = 0;
    std::vector<cv::Point2d> points0(1), points1(1);
    cv::Mat_<double> proj_mat0(3, 4), proj_mat1(3, 4), hpoints4D;
    for(int cam_idx = 0; cam_idx < num_cam_poses_; cam_idx++ )
    {
      if( cam_pose_optim_iter_[cam_idx] > 0 )
      {
        for( auto const& co_iter : cam_observation_[cam_idx] )
        {
          auto &pt_idx = co_iter.first;
          if( pts_optim_iter_[pt_idx] == 0 &&
              cam_observation_[new_cam_pose_idx].find(pt_idx ) != cam_observation_[new_cam_pose_idx].end() )
          {
            double *cam0_data = cameraBlockPtr(new_cam_pose_idx),
                *cam1_data = cameraBlockPtr(cam_idx);

            //////////////////////////// Code to be completed (4/7) /////////////////////////////////
            // Triangulate the 3D point with index pt_idx by using the observation of this point in the
            // camera poses with indices new_cam_pose_idx and cam_idx. The pointers cam0_data and cam1_data
            // point to the 6D pose blocks for these inside the parameters vector (e.g.,
            // cam0_data[0], cam0_data[1], cam0_data[2] hold the axis-angle representation fo the rotation of the
            // camera with index new_cam_pose_idx.
            // Use the OpenCV cv::triangulatePoints() function, remembering to check the cheirality constraint
            // for both cameras
            // In case of success (cheirality constrant satisfied) execute the following instructions (decomment e
            // cut&paste):

            // n_new_pts++;
            // pts_optim_iter_[pt_idx] = 1;
            // double *pt = pointBlockPtr(pt_idx);
            // pt[0] = /*X coordinate of the estimated point */;
            // pt[1] = /*X coordinate of the estimated point */;
            // pt[2] = /*X coordinate of the estimated point */;
            /////////////////////////////////////////////////////////////////////////////////////////

            // get the 2D image points observed in both cameras
            points0[0] = cv::Point2d(observations_[2 * cam_observation_[new_cam_pose_idx][pt_idx]],
                                     observations_[2 * cam_observation_[new_cam_pose_idx][pt_idx] + 1]);
            points1[0] = cv::Point2d(observations_[2 * co_iter.second],
                                     observations_[2 * co_iter.second + 1]);

            // build the projection matrices for both cameras
            // convert the axis-angle representation to rotation matrices
            cv::Mat_<double> R0(3, 3), R1(3, 3);
            cv::Mat_<double> rvec0 = (cv::Mat_<double>(3, 1) << cam0_data[0], cam0_data[1], cam0_data[2]);
            cv::Mat_<double> rvec1 = (cv::Mat_<double>(3, 1) << cam1_data[0], cam1_data[1], cam1_data[2]);
            cv::Rodrigues(rvec0, R0);
            cv::Rodrigues(rvec1, R1);

            // reate the projection matrices 
            proj_mat0 = cv::Mat_<double>::zeros(3, 4);
            proj_mat1 = cv::Mat_<double>::zeros(3, 4);
            R0.copyTo(proj_mat0(cv::Rect(0, 0, 3, 3)));
            R1.copyTo(proj_mat1(cv::Rect(0, 0, 3, 3)));
            proj_mat0(0, 3) = cam0_data[3];
            proj_mat0(1, 3) = cam0_data[4];
            proj_mat0(2, 3) = cam0_data[5];
            proj_mat1(0, 3) = cam1_data[3];
            proj_mat1(1, 3) = cam1_data[4];
            proj_mat1(2, 3) = cam1_data[5];

            
            cv::triangulatePoints(proj_mat0, proj_mat1, points0, points1, hpoints4D);

            // convert homogeneous coordinates to 3D point
            double X = hpoints4D.at<double>(0, 0) / hpoints4D.at<double>(3, 0);
            double Y = hpoints4D.at<double>(1, 0) / hpoints4D.at<double>(3, 0);
            double Z = hpoints4D.at<double>(2, 0) / hpoints4D.at<double>(3, 0);

            // check the cheirality constraint for both cameras
            bool cheirality0 = checkCheiralityConstraint(new_cam_pose_idx, pt_idx);

            // temporary  variables
            double *pt_temp = pointBlockPtr(pt_idx);
            double backup[3] = {pt_temp[0], pt_temp[1], pt_temp[2]};
            pt_temp[0] = X;
            pt_temp[1] = Y;
            pt_temp[2] = Z;

            bool cheirality1 = checkCheiralityConstraint(cam_idx, pt_idx);

            // if the point passes the cheirality constraint for both cameras keep it
            if (cheirality0 && cheirality1 && Z > 0)
            {
              n_new_pts++;
              pts_optim_iter_[pt_idx] = 1;
              double *pt = pointBlockPtr(pt_idx);
              pt[0] = X;
              pt[1] = Y;
              pt[2] = Z;
            }
            else
            {
              // restore the backup (will be zeroed out anyway)
              pt_temp[0] = backup[0];
              pt_temp[1] = backup[1];
              pt_temp[2] = backup[2];
            }

            /////////////////////////////////////////////////////////////////////////////////////////
          }
        }
      }
    }

    cout << "ADDED " << n_new_pts << " new points" << endl;

    cout << "Using " << iter + 2 << " over " << num_cam_poses_ << " cameras" << endl;
    for (int i = 0; i < int(cam_pose_optim_iter_.size()); i++)
      cout << int(cam_pose_optim_iter_[i]) << " ";
    cout << endl;
    //added this line
    std::vector<double> bck_parameters = parameters_;

    // Execute an iteration of bundle adjustment
    bundleAdjustmentIter(new_cam_pose_idx);

    Eigen::Vector3d vol_min = Eigen::Vector3d::Constant((std::numeric_limits<double>::max())),
                    vol_max = Eigen::Vector3d::Constant((-std::numeric_limits<double>::max()));
    for (int i_c = 0; i_c < num_cam_poses_; i_c++)
    {
      if (cam_pose_optim_iter_[i_c])
      {
        double *camera = cameraBlockPtr(i_c);
        if (camera[3] > vol_max(0))
          vol_max(0) = camera[3];
        if (camera[4] > vol_max(1))
          vol_max(1) = camera[4];
        if (camera[5] > vol_max(2))
          vol_max(2) = camera[5];
        if (camera[3] < vol_min(0))
          vol_min(0) = camera[3];
        if (camera[4] < vol_min(1))
          vol_min(1) = camera[4];
        if (camera[5] < vol_min(2))
          vol_min(2) = camera[5];
      }
    }

    double max_dist = 5 * (vol_max - vol_min).norm();
    if (max_dist < 10.0)
      max_dist = 10.0;

    double *pts = parameters_.data() + num_cam_poses_ * camera_block_size_;
    for (int i = 0; i < num_points_; i++)
    {
      if (pts_optim_iter_[i] > 0 &&
          (fabs(pts[i * point_block_size_]) > max_dist ||
           fabs(pts[i * point_block_size_ + 1]) > max_dist ||
           fabs(pts[i * point_block_size_ + 2]) > max_dist))
      {
        pts_optim_iter_[i] = -1;
      }
    }
    //////////////////////////// Code to be completed (7/7) //////////////////////////////////
    // The reconstruction may diverge, for example due to incorrect triangulations or
    // incorrect local minima found during the bundle adjustment. For example, this can lead
    // the points to be scattered very far from the origin of the coordinate system, producing
    // a totally incorrect reconstruction. In this case it might be a good idea to reset
    // the reconstruction and start from scratch with a new pair of seeds (i.e., this function
    // must return false). To check if there was a divergence you could for example check how
    // the previous camera and point positions were updated during this iteration.
    /////////////////////////////////////////////////////////////////////////////////////////

    //OLD CODE
   /* double scene_scale = (vol_max - vol_min).norm();
    const double POINT_CHANGE_THRESHOLD = 0.3 * scene_scale;  // avg threshold for points
    const double CAMERA_CHANGE_THRESHOLD = 0.5 * scene_scale; // avg threshold for cameras

    double total_cam_change = 0.0;
    int cam_count = 0;

    // Camera change average
    for (int i_c = 0; i_c < num_cam_poses_; i_c++)
    {
      if (cam_pose_optim_iter_[i_c] > 0)
      {
        double change = 0.0;
        for (int j = 0; j < camera_block_size_; j++)
        {
          double curr = parameters_[i_c * camera_block_size_ + j];
          double prev = bck_parameters[i_c * camera_block_size_ + j];
          change += (curr - prev) * (curr - prev);
        }
        total_cam_change += std::sqrt(change);
        cam_count++;
      }
    }

    if (cam_count > 0)
    {
      double avg_cam_change = total_cam_change / cam_count;
      if (avg_cam_change > CAMERA_CHANGE_THRESHOLD)
      {
        std::cout << "+++++ Reconstruction diverging (avg camera change). Restarting with new seed." << std::endl;
        return false;
      }
    }

    // Point change average
    const double *points = pointBlockPtr();
    const double *bck_points = bck_parameters.data() + num_cam_poses_ * camera_block_size_;
    double total_point_change = 0.0;
    int point_count = 0;

    for (int i_pt = 0; i_pt < num_points_; i_pt++)
    {
      if (pts_optim_iter_[i_pt] > 0)
      {
        double change = 0.0;
        for (int j = 0; j < point_block_size_; j++)
        {
          double curr = points[i_pt * point_block_size_ + j];
          double prev = bck_points[i_pt * point_block_size_ + j];
          change += (curr - prev) * (curr - prev);
        }
        total_point_change += std::sqrt(change);
        point_count++;
      }
    }

    if (point_count > 0)
    {
      double avg_point_change = total_point_change / point_count;
      if (avg_point_change > POINT_CHANGE_THRESHOLD)
      {
        std::cout << "+++++ Reconstruction diverging (avg point change). Restarting with new seed." << std::endl;
        return false;
      }
    }*/

    // Check the average distance of points
    double avg_point_distance = 0.0;
    int valid_points = 0;
    const double* points = pointBlockPtr();

    for (int i = 0; i < num_points_; i++) {
      if (pts_optim_iter_[i] > 0) {
        const double* point = points + i * point_block_size_;
        double dist = sqrt(point[0]*point[0] + point[1]*point[1] + point[2]*point[2]);
        avg_point_distance += dist;
        valid_points++;
      }
    }

    if (valid_points > 0) {
      avg_point_distance /= valid_points;
    }

    // Check the average distance of cameras
    double avg_camera_distance = 0.0;
    int valid_cameras = 0;
    for (int i = 0; i < num_cam_poses_; i++) {
      if (cam_pose_optim_iter_[i] > 0) {
        double* camera = cameraBlockPtr(i);
        double dist = sqrt(camera[3]*camera[3] + camera[4]*camera[4] + camera[5]*camera[5]);
        avg_camera_distance += dist;
        valid_cameras++;
      }
    }

    if (valid_cameras > 0) {
      avg_camera_distance /= valid_cameras;
    }

    // Thresholds for divergence
    const double MAX_POINT_DISTANCE_THRESHOLD = 100.0;
    const double MAX_CAMERA_DISTANCE_THRESHOLD = 100.0;

    // Check if the average distances exceed the thresholds
    // If they do, the reconstruction might be diverging
    if (avg_point_distance > MAX_POINT_DISTANCE_THRESHOLD || 
        avg_camera_distance > MAX_CAMERA_DISTANCE_THRESHOLD) {
      std::cout << "Reconstruction appears to be diverging. Restarting with a new seed pair." << std::endl;
      return false;
    }

    // If the number of valid points are few the reconstruction might be diverging
    if (valid_points < 20 && iter > 3) {
      std::cout << "Too few valid points remaining. Restarting with a new seed pair." << std::endl;
      return false;
    }

    // If the number of rejected points is high, the reconstruction is difficult
    int rejected_points = 0;
    for (int i = 0; i < num_points_; i++) {
      if (pts_optim_iter_[i] == -1) {
        rejected_points++;
      }
    }

    if (rejected_points > 0 && valid_points > 0 && 
        static_cast<double>(rejected_points) / (rejected_points + valid_points) > 0.5) {
      std::cout << "Too many points rejected. Restarting with a new seed pair." << std::endl;
      return false;
    }

    /////////////////////////////////////////////////////////////////////////////////////////
  }

  return true;
}

void BasicSfM::initCamParams(int new_pose_idx, cv::Mat r_vec, cv::Mat t_vec)
{
  double *camera = cameraBlockPtr(new_pose_idx);

  cv::Mat_<double> r_vec_d(r_vec), t_vec_d(t_vec);
  for (int r = 0; r < 3; r++)
  {
    camera[r] = r_vec_d(r, 0);
    camera[r + 3] = t_vec_d(r, 0);
  }
}

void BasicSfM::bundleAdjustmentIter(int new_cam_idx)
{
  ceres::Solver::Options options;
  options.linear_solver_type = ceres::DENSE_SCHUR;
  options.minimizer_progress_to_stdout = false;
  options.num_threads = 4; 
  options.max_num_iterations = 200;

  std::vector<double> bck_parameters;

  bool keep_optimize = true;

  // Global optimization
  while (keep_optimize)
  {
    bck_parameters = parameters_;
    ceres::Problem problem;
    ceres::Solver::Summary summary;

    // For each observation....
    for( int i_obs = 0; i_obs < num_observations_; i_obs++ )
    {
      //.. check if this observation has bem already registered (both checking camera pose and point pose)
      if( cam_pose_optim_iter_[cam_pose_index_[i_obs]] > 0 && pts_optim_iter_[point_index_[i_obs]] > 0 )
      {
        //////////////////////////// Code to be completed (6/7) /////////////////////////////////
        //... in case, add a residual block inside the Ceres solver problem.
        // You should define a suitable functor (i.e., see the ReprojectionError struct at the
        // beginning of this file)
        // You may try a Cauchy loss function with parameters, say, 2*max_reproj_err_
        // Remember that the parameter blocks are stored starting from the
        // parameters_.data() double* pointer.
        // The camera position blocks have size (camera_block_size_) of 6 elements,
        // while the point position blocks have size (point_block_size_) of 3 elements.
        //////////////////////////////////////////////////////////////////////////////////

        // get the observation values 
        double *observation = observations_.data() + (i_obs * 2);
        double observed_x = observation[0];
        double observed_y = observation[1];

        double *camera = cameraBlockPtr(cam_pose_index_[i_obs]);
        double *point = pointBlockPtr(point_index_[i_obs]);

        // cost function based on the ReprojectionError struct
        ceres::CostFunction *cost_function = ReprojectionError::Create(observed_x, observed_y);

        // residual block 
        problem.AddResidualBlock(
            cost_function,                            
            new ceres::CauchyLoss( 2*max_reproj_err_), //*2 prima 
            camera,                                     
            point                                       
        );

        // the first camera pose is fixed to avoid gauge freedom
        if (cam_pose_index_[i_obs] == 0)
        {
          problem.SetParameterBlockConstant(camera);
        }
        
        /////////////////////////////////////////////////////////////////////////////////////////

      }
    }

    Solve(options, &problem, &summary);

    // WARNING Here poor optimization ... :(
    // CHeck the cheirality constraint
    int n_cheirality_violation = 0;
    for( int i_obs = 0; i_obs < num_observations_; i_obs++ )
    {
      if( cam_pose_optim_iter_[cam_pose_index_[i_obs]] > 0 &&
          pts_optim_iter_[point_index_[i_obs]] == 1 &&
          !checkCheiralityConstraint(cam_pose_index_[i_obs], point_index_[i_obs]))
      {
        // Penalize the point..
        pts_optim_iter_[point_index_[i_obs]] -= 2;
        n_cheirality_violation++;
      }
    }

    int n_outliers;
    if( n_cheirality_violation > max_outliers_ )
    {
      std::cout << "****************** OPTIM CHEIRALITY VIOLATION for " << n_cheirality_violation << " points : redoing optim!!" << std::endl;
      parameters_ = bck_parameters;
    }
    else if ( (n_outliers = rejectOuliers()) > max_outliers_ )
    {
      std::cout<<"****************** OPTIM FOUND "<<n_outliers<<" OUTLIERS : redoing optim!!"<<std::endl;
      parameters_ = bck_parameters;
    }
    else
      keep_optimize = false;
  }

  printPose ( new_cam_idx );
}

int BasicSfM:: rejectOuliers()
{
  int num_ouliers = 0;
  for( int i_obs = 0; i_obs < num_observations_; i_obs++ )
  {
    if( cam_pose_optim_iter_[cam_pose_index_[i_obs]] > 0 && pts_optim_iter_[point_index_[i_obs]] > 0 )
    {
      double *camera = cameraBlockPtr (cam_pose_index_[i_obs]),
             *point = pointBlockPtr (point_index_[i_obs]),
             *observation = observations_.data() + (i_obs * 2);

      double p[3];
      ceres::AngleAxisRotatePoint(camera, point, p);

      // camera[3,4,5] are the translation.
      p[0] += camera[3];
      p[1] += camera[4];
      p[2] += camera[5];

      double predicted_x = p[0] / p[2];
      double predicted_y = p[1] / p[2];

      if ( fabs(predicted_x - observation[0]) > max_reproj_err_ ||
           fabs(predicted_y - observation[1]) > max_reproj_err_ )
      {
        // Penalize the point
        pts_optim_iter_[point_index_[i_obs]]-=2;
        num_ouliers ++;
      }
    }
  }
  return num_ouliers;
}
