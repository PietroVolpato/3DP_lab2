#include <iostream>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>

void resizeImages(const std::string& folderPath, const std::string& outputPath, const cv::Size& newSize) {
    // Validate dimensions
    if (newSize.width <= 0 || newSize.height <= 0) {
        std::cerr << "Error: Invalid image dimensions. Width and height must be positive." << std::endl;
        return;
    }
    
    // Create output directory if it doesn't exist
    std::string command = "mkdir -p " + outputPath;
    system(command.c_str());
    
    std::vector<cv::String> filenames;
    
    // Get all image files in the folder
    std::string pattern = folderPath + "/*.*";
    cv::glob(pattern, filenames, false);
    
    std::cout << "Found " << filenames.size() << " files in " << folderPath << std::endl;
    
    for (const auto& filename : filenames) {
        // Read the image
        cv::Mat image = cv::imread(filename);
        
        // Skip if the image couldn't be loaded
        if (image.empty()) {
            std::cout << "Could not open or find " << filename << std::endl;
            continue;
        }
        
        // Check if dimensions are appropriate for this image
        if (image.cols <= 0 || image.rows <= 0) {
            std::cerr << "Skipping " << filename << ": Invalid image dimensions (" 
                      << image.cols << "x" << image.rows << ")" << std::endl;
            continue;
        }
        
        try {
            // Resize the image
            cv::Mat resizedImage;
            cv::resize(image, resizedImage, newSize, 0, 0, cv::INTER_LINEAR);
            
            // Extract just the filename without path
            std::string baseFilename = filename.substr(filename.find_last_of('/') + 1);
            std::string filenameWithoutExt = baseFilename.substr(0, baseFilename.find_last_of('.'));
            
            // Create output filename in the specified output directory
            std::string outputFilename = outputPath + "/" + filenameWithoutExt + "_resized.jpg";
            
            // Save the resized image
            cv::imwrite(outputFilename, resizedImage);
            
            std::cout << "Resized " << baseFilename << " from " << image.cols << "x" << image.rows
                      << " to " << resizedImage.cols << "x" << resizedImage.rows << std::endl;
        }
        catch (const cv::Exception& e) {
            std::cerr << "Error resizing " << filename << ": " << e.what() << std::endl;
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <input_folder> [output_folder] [width] [height]" << std::endl;
        return -1;
    }
    
    std::string folderPath = argv[1];
    
    // Default output folder is input folder + "_resized"
    std::string outputPath = folderPath + "_resized";
    if (argc >= 3) outputPath = argv[2];
    
    // Default resize dimensions
    int width = 640;
    int height = 480;
    
    // Allow user to specify dimensions
    if (argc >= 4) width = std::atoi(argv[3]);
    if (argc >= 5) height = std::atoi(argv[4]);
    
    if (width <= 0 || height <= 0) {
        std::cerr << "Error: Width and height must be positive values." << std::endl;
        std::cout << "Using default values: 640x480" << std::endl;
        width = 640;
        height = 480;
    }
    
    cv::Size newSize(width, height);
    std::cout << "Resizing images to " << width << "x" << height << std::endl;
    std::cout << "Saving results to " << outputPath << std::endl;
    
    resizeImages(folderPath, outputPath, newSize);
    
    return 0;
}