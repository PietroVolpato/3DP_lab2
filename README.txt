Prerequisites (in debian-based distro, not required in the course Virtual Machine):

sudo apt install build-essential cmake libboost-filesystem-dev libopencv-dev libomp-dev
sudo apt install libceres-dev libyaml-cpp-dev libgtest-dev libeigen3-dev

Build and run the executable:

mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make

Test the two applications (located inside the bin/ folder)

./matcher <calibration parameters filename> <images folder filename> <output data file> [focal length scale]
./basic_sfm <input data file> <output ply file>

Datasets

The dataset/ folder contains two simple datasets, each including a set of images and the corresponding camera calibration file. For convenience, and to facilitate parallel development of the two applications, preprocessed data files with detection and feature matching results are also provided for both datasets. These can be used directly with basic_sfm. However, your submission will be evaluated using the original input images, not the preprocessed files.

Examples

For the provided datasets, set the focal lenght scale to 1.1, e.g.:

./matcher ../datasets/3dp_cam.yml ../datasets/images_1 ../data1.txt 1.1
./matcher ../datasets/3dp_cam.yml ../datasets/images_2 ../data2.txt 1.1

./basic_sfm ../data1.txt ../cloud1.ply
./basic_sfm ../data2.txt ../cloud2.ply

# Our datasets

Dataset 1: gnome

Commands to run from the bin folder 
./matcher ../datasets/gnome_cam.yml ../datasets/gnome_dataset ../gnome_data.txt 1.1
./basic_sfm ../gnome_data.txt ../cloud_gnome.ply 

Then to visualize the reconstruction, run from the root folder:
meshlab cloud_gnome.ply

Dataset 2: angel
./matcher ../datasets/Pietro_cam.yml ../datasets/angel_dataset ../angel_data.txt 1.1
./basic_sfm ../angel_data.txt ../cloud_angel.ply 

To visualize:
meshlab cloud_angel.ply
