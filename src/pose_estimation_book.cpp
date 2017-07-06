#include "opencv_all.hpp"

int main(void)
{
    bool assume_plane = true, calib_camera = true, show_match = true;
    size_t min_inlier_num = 100;
    double camera_f_min = 400, camera_f_max = 4000, camera_f_default = 1000, camera_cx_default = 320, camera_cy_default = 240;

    // Load the object image and extract features
    cv::Mat obj_image = cv::imread("data/blais.jpg", cv::ImreadModes::IMREAD_GRAYSCALE);
    if (obj_image.empty()) return -1;

    cv::Ptr<cv::FeatureDetector> fdetector = cv::ORB::create();
    cv::Ptr<cv::DescriptorMatcher> fmatcher = cv::DescriptorMatcher::create("BruteForce-Hamming");
    std::vector<cv::KeyPoint> obj_keypoint;
    cv::Mat obj_descriptor;
    fdetector->detectAndCompute(obj_image, cv::Mat(), obj_keypoint, obj_descriptor);
    if (obj_keypoint.empty() || obj_descriptor.empty()) return -1;
    fmatcher->add(obj_descriptor);

    // Open a video
    cv::VideoCapture video;
    if (!video.open("data/blais.mp4")) return -1;

    // Prepare a box for simple AR
    std::vector<cv::Point3f> box_lower, box_upper;
    box_lower.push_back(cv::Point3f(30, 145, 0)); box_lower.push_back(cv::Point3f(30, 200, 0)); box_lower.push_back(cv::Point3f(200, 200, 0)); box_lower.push_back(cv::Point3f(200, 145, 0));
    box_upper.push_back(cv::Point3f(30, 145, -50)); box_upper.push_back(cv::Point3f(30, 200, -50)); box_upper.push_back(cv::Point3f(200, 200, -50)); box_upper.push_back(cv::Point3f(200, 145, -50));

    // Run camera calibration and pose estimation together
    cv::Mat K = (cv::Mat_<double>(3, 3) << camera_f_default, 0, camera_cx_default, 0, camera_f_default, camera_cy_default, 0, 0, 1);
    cv::Mat dist_coeff = cv::Mat::zeros(5, 1, CV_64F), rvec, tvec;
    while (true)
    {
        // Grab an image from the video
        cv::Mat image, gray;
        video >> image;
        if (image.empty()) break;
        if (image.channels() > 1) cv::cvtColor(image, gray, cv::COLOR_RGB2GRAY);
        else                      gray = image.clone();

        // Extract features and match them to the object features
        std::vector<cv::KeyPoint> img_keypoint;
        cv::Mat img_descriptor;
        fdetector->detectAndCompute(gray, cv::Mat(), img_keypoint, img_descriptor);
        if (img_keypoint.empty() || img_descriptor.empty()) continue;
        std::vector<cv::DMatch> match;
        fmatcher->match(img_descriptor, match);
        if (match.size() < min_inlier_num) continue;
        std::vector<cv::Point3f> obj_points;
        std::vector<cv::Point2f> obj_project, img_points;
        for (auto m = match.begin(); m < match.end(); m++)
        {
            obj_points.push_back(cv::Point3f(obj_keypoint[m->trainIdx].pt));
            obj_project.push_back(obj_keypoint[m->trainIdx].pt);
            img_points.push_back(img_keypoint[m->queryIdx].pt);
        }

        // Determine whether each matched feature is an inlier or not
        size_t inlier_num = 0;
        cv::Mat inlier_mask = cv::Mat::zeros(match.size(), 1, CV_8U);
        if (assume_plane)
        {
            cv::Mat H = cv::findHomography(img_points, obj_project, inlier_mask, cv::RANSAC, 2);
            if (!H.empty()) inlier_num = static_cast<size_t>(cv::sum(inlier_mask)[0]);
        }
        else
        {
            std::vector<int> inlier;
            if (cv::solvePnPRansac(obj_points, img_points, K, dist_coeff, rvec, tvec, false, 500, 2, 0.99, inlier))
            {
                inlier_num = static_cast<int>(inlier.size());
                for (size_t i = 0; i < inlier.size(); i++) inlier_mask.at<uchar>(inlier[i]) = 1;
            }
        }
        cv::Mat image_result = image;
        if (show_match) cv::drawMatches(image, img_keypoint, obj_image, obj_keypoint, match, image_result, cv::Scalar(0, 0, 255), cv::Scalar(0, 127, 0), inlier_mask);

        // Calibrate the camera and estimate its pose
        if (inlier_num > min_inlier_num)
        {
            std::vector<cv::Point3f> obj_inlier;
            std::vector<cv::Point2f> img_inlier;
            for (int idx = 0; idx < inlier_mask.rows; idx++)
            {
                if (inlier_mask.at<uchar>(idx))
                {
                    obj_inlier.push_back(obj_points[idx]);
                    img_inlier.push_back(img_points[idx]);
                }
            }
            if (calib_camera)
            {
                std::vector<cv::Mat> rvecs, tvecs;
                cv::calibrateCamera(std::vector<std::vector<cv::Point3f> >(1, obj_inlier), std::vector<std::vector<cv::Point2f> >(1, img_inlier), gray.size(), K, dist_coeff, rvecs, tvecs,
                    cv::CALIB_FIX_ASPECT_RATIO | cv::CALIB_FIX_PRINCIPAL_POINT | cv::CALIB_ZERO_TANGENT_DIST | cv::CALIB_FIX_K1 | cv::CALIB_FIX_K2 | cv::CALIB_FIX_K3 | cv::CALIB_FIX_K4 | cv::CALIB_FIX_K5 | cv::CALIB_FIX_K6 | cv::CALIB_FIX_S1_S2_S3_S4 | cv::CALIB_FIX_TAUX_TAUY);
                rvec = rvecs[0].clone();
                tvec = tvecs[0].clone();
            }
            else cv::solvePnP(obj_points, img_points, K, dist_coeff, rvec, tvec);

            if (K.at<double>(0) > camera_f_min && K.at<double>(0) < camera_f_max)
            {
                // Draw the box on the image
                cv::Mat line_lower, line_upper;
                cv::projectPoints(box_lower, rvec, tvec, K, dist_coeff, line_lower);
                cv::projectPoints(box_upper, rvec, tvec, K, dist_coeff, line_upper);
                line_lower.reshape(1).convertTo(line_lower, CV_32S); // Change 4 x 1 matrix (CV_64FC2) to 4 x 2 matrix (CV_32SC1)
                line_upper.reshape(1).convertTo(line_upper, CV_32S); // Because 'cv::polylines()' only accepts 'CV_32S' depth.
                cv::polylines(image_result, line_lower, true, cv::Scalar(255, 0, 0), 2);
                for (int i = 0; i < line_lower.rows; i++)
                    cv::line(image_result, cv::Point(line_lower.row(i)), cv::Point(line_upper.row(i)), cv::Scalar(0, 255, 0), 2);
                cv::polylines(image_result, line_upper, true, cv::Scalar(0, 0, 255), 2);
            }
            else K = (cv::Mat_<double>(3, 3) << camera_f_default, 0, camera_cx_default, 0, camera_f_default, camera_cy_default, 0, 0, 1);
        }

        // Show the image
        cv::String info = cv::format("Inliers: %d (%d%%), Focal Length: %.0f", inlier_num, 100 * inlier_num / match.size(), K.at<double>(0));
        cv::putText(image_result, info, cv::Point(5, 15), cv::FONT_HERSHEY_PLAIN, 1, cv::Scalar(0, 255, 0));
        cv::imshow("3DV Tutorial: Pose Estimation (Book)", image_result);
        int key = cv::waitKey(1);
        if (key == 27) break;                                   // 'ESC' key: Exit
        else if (key == 32)                                     // 'Space' key: Pause
        {
            key = cv::waitKey();
            if (key == 27) break;                               // 'ESC' key: Exit
        }
    }

    video.release();
    return 0;
}
