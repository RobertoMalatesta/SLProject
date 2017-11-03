//#############################################################################
//  File:      SLCVKeyframe.cpp
//  Author:    Michael Göttlicher
//  Date:      October 2017
//  Codestyle: https://github.com/cpvrlab/SLProject/wiki/Coding-Style-Guidelines
//  Copyright: Marcus Hudritsch
//             This software is provide under the GNU General Public License
//             Please visit: http://opensource.org/licenses/GPL-3.0
//#############################################################################

#include "stdafx.h"
#include "SLCVKeyframe.h"

//-----------------------------------------------------------------------------
SLCamera* SLCVKeyFrame::getSceneObject()
{
    if (!_camera)
    {
        _camera = new SLCamera("KeyFrame" + _id);
        //set camera position and orientation
        SLMat4f om;

        om.setMatrix(
            _wTc.at<float>(0, 0), _wTc.at<float>(0, 1), _wTc.at<float>(0, 2), _wTc.at<float>(0, 3),
            -_wTc.at<float>(1, 0), -_wTc.at<float>(1, 1), -_wTc.at<float>(1, 2), _wTc.at<float>(1, 3),
            -_wTc.at<float>(2, 0), -_wTc.at<float>(2, 1), -_wTc.at<float>(2, 2), _wTc.at<float>(2, 3),
            _wTc.at<float>(3, 0), _wTc.at<float>(3, 1), _wTc.at<float>(3, 2), _wTc.at<float>(3, 3));

        _camera->om(om);
    }

    return _camera;
}