#pragma once

#include <opencv2/core.hpp>

namespace cv {

CV_EXPORTS_W RotatedRect minAreaRect(InputArray points);
CV_EXPORTS Mat getPerspectiveTransform(
    const Point2f source[],
    const Point2f destination[],
    int solve_method);

}  // namespace cv
