//#############################################################################
//  File:      SLCVOrbTracking.cpp
//  Author:    Jan Dellsperger
//  Date:      Apr 2018
//  Codestyle: https://github.com/cpvrlab/SLProject/wiki/Coding-Style-Guidelines
//  Copyright: Marcus Hudritsch, Michael Goettlicher
//             This software is provide under the GNU General Public License
//             Please visit: http://opensource.org/licenses/GPL-3.0
//#############################################################################

#include <stdafx.h>
#include <SLCVOrbTracking.h>
#include <SLCVCapture.h>
#include <SLCVMapNode.h>
#include <SLCVMapStorage.h>
#include <SLCVOrbVocabulary.h>

SLCVOrbTracking::SLCVOrbTracking(SLCVStateEstimator* stateEstimator,
    SLCVMapNode* mapNode,
    bool serial)
    : SLCVMapTracking(mapNode, serial),
    _stateEstimator{ stateEstimator }
{
    //load visual vocabulary for relocalization
    mpVocabulary = SLCVOrbVocabulary::get();

    //instantiate and load slam map
    mpKeyFrameDatabase = new SLCVKeyFrameDB(*mpVocabulary);

    _map = new SLCVMap("Map");
    //set SLCVMap in SLCVMapNode and update SLCVMapNode with scene objects
    mapNode->setMap(*_map);

    //setup file system and check for existing files
    SLCVMapStorage::init();
    //make new map
    SLCVMapStorage::newMap();

    if (_map->KeyFramesInMap())
        _initialized = true;
    else
        _initialized = false;

    int nFeatures = 1000;
    float fScaleFactor = 1.2;
    int nLevels = 8;
    int fIniThFAST = 20;
    int fMinThFAST = 7;

    //instantiate Orb extractor
    _extractor = new ORBextractor(nFeatures, fScaleFactor, nLevels, fIniThFAST, fMinThFAST);

    //system is initialized, because we loaded an existing map, but we have to relocalize
    _bOK = false;

    if (!_serial)
    {
        _trackingThread = std::thread(&SLCVOrbTracking::trackOrbsContinuously, this);
    }
}

SLCVOrbTracking::~SLCVOrbTracking()
{
    running(false);

    if (!_serial)
    {
        _trackingThread.join();
    }

    if (_extractor)
        delete _extractor;
}

void SLCVOrbTracking::running(bool running)
{
    std::lock_guard<std::mutex> guard(_runLock);
    _running = running;
}

bool SLCVOrbTracking::running()
{
    std::lock_guard<std::mutex> guard(_runLock);
    bool result = _running;
    return result;
}

void SLCVOrbTracking::calib(SLCVCalibration* calib)
{
    std::lock_guard<std::mutex> guard(_calibLock);
    _calib = calib;
    _calibReady.notify_all();
}

//bool SLCVOrbTracking::serial()
//{
//    bool result = _serial;
//    return result;
//}

void SLCVOrbTracking::trackOrbsContinuously()
{
    std::unique_lock<std::mutex> lk(_calibLock);
    _calibReady.wait(lk, [this] {return _calib; });

    while (running()) {
        track();
    }
}

void SLCVOrbTracking::track3DPts()
{
    //Frame constructor call in ORB-SLAM:
    // Current Frame
    double timestamp = 0.0; //todo
    SLCVCapture::FrameAndTime frameAndTime;
    SLCVCapture::lastFrameAsync(&frameAndTime);
    mCurrentFrame = SLCVFrame(frameAndTime.frameGray, timestamp, _extractor,
        _calib->cameraMat(), _calib->distortion(), mpVocabulary, true);
    /************************************************************/

    // System is initialized. Track Frame.
    //mLastProcessedState = mState;
    //bool bOK = false;
    _bOK = false;
    trackingType = TrackingType_None;

    // Localization Mode: Local Mapping is deactivated
    if (sm.state() == SLCVTrackingStateMachine::TRACKING_LOST)
    {
        _bOK = Relocalization();
    }
    else
    {
        //if (_optFlowFrames % 3)
        //{
            _bOK = TrackWithOptFlow();
        //}

        if (!_bOK)
        {
            //if NOT visual odometry tracking
            if (!mbVO) // In last frame we tracked enough MapPoints from the Map
            {
                if (!mVelocity.empty()) 
                { 
                    //we have a valid motion model    
                    _bOK = TrackWithMotionModel();
                    trackingType = TrackingType_MotionModel;
                }
                else 
                {  
                    //we have NO valid motion model
                    // All keyframes that observe a map point are included in the local map.
                    // Every current frame gets a reference keyframe assigned which is the keyframe
                    // from the local map that shares most matches with the current frames local map points matches.
                    // It is updated in UpdateLocalKeyFrames().
                    _bOK = TrackReferenceKeyFrame();
                    trackingType = TrackingType_ORBSLAM;
                }
            }
            else // In last frame we tracked mainly "visual odometry" points.
            {
                // We compute two camera poses, one from motion model and one doing relocalization.
                // If relocalization is sucessfull we choose that solution, otherwise we retain
                // the "visual odometry" solution.

                bool bOKMM = false;
                bool bOKReloc = false;
                vector<SLCVMapPoint*> vpMPsMM;
                vector<bool> vbOutMM;
                cv::Mat TcwMM;
                if (!mVelocity.empty())
                {
                    bOKMM = TrackWithMotionModel();
                    vpMPsMM = mCurrentFrame.mvpMapPoints;
                    vbOutMM = mCurrentFrame.mvbOutlier;
                    TcwMM = mCurrentFrame.mTcw.clone();
                }
                bOKReloc = Relocalization();

                //relocalization method is not valid but the velocity model method
                if (bOKMM && !bOKReloc)
                {
                    mCurrentFrame.SetPose(TcwMM);
                    mCurrentFrame.mvpMapPoints = vpMPsMM;
                    mCurrentFrame.mvbOutlier = vbOutMM;

                    if (mbVO)
                    {
                        for (int i = 0; i < mCurrentFrame.N; i++)
                        {
                            if (mCurrentFrame.mvpMapPoints[i] && !mCurrentFrame.mvbOutlier[i])
                            {
                                mCurrentFrame.mvpMapPoints[i]->IncreaseFound();
                            }
                        }
                    }
                }
                else if (bOKReloc)
                {
                    mbVO = false;
                }

                _bOK = bOKReloc || bOKMM;
                trackingType = TrackingType_None;
            }
        }

        _optFlowFrames++;
    }


    // mbVO true means that there are few matches to MapPoints in the map. We cannot retrieve
    // a local map and therefore we do not perform TrackLocalMap(). Once the system relocalizes
    // the camera we will use the local map again.

    if (_bOK && !mbVO)
    {
        _bOK = TrackLocalMap();
    }

    //if (_bOK)
    //    mState = OK;
    //else
    //    mState = LOST;

    //add map points to scene and keypoints to video image
    decorateSceneAndVideo(frameAndTime.frame);

    // If tracking were good
    if (_bOK)
    {
        // Update motion model
        if (!mLastFrame.mTcw.empty())
        {
            cv::Mat LastTwc = cv::Mat::eye(4, 4, CV_32F);
            mLastFrame.GetRotationInverse().copyTo(LastTwc.rowRange(0, 3).colRange(0, 3)); //mRwc
            const auto& cc = mLastFrame.GetCameraCenter(); //this is the translation w.r.t the world of the frame (warum dann Twc??)
            cc.copyTo(LastTwc.rowRange(0, 3).col(3));
            mVelocity = mCurrentFrame.mTcw*LastTwc;
        }
        else
            mVelocity = cv::Mat();

        //set current pose
        {
            cv::Mat Rwc(3, 3, CV_32F);
            cv::Mat twc(3, 1, CV_32F);

            //inversion
            auto Tcw = mCurrentFrame.mTcw.clone();
            Rwc = Tcw.rowRange(0, 3).colRange(0, 3).t();
            twc = -Rwc*Tcw.rowRange(0, 3).col(3);

            //conversion to SLMat4f
            SLMat4f slMat((SLfloat)Rwc.at<float>(0, 0), (SLfloat)Rwc.at<float>(0, 1), (SLfloat)Rwc.at<float>(0, 2), (SLfloat)twc.at<float>(0, 0),
                (SLfloat)Rwc.at<float>(1, 0), (SLfloat)Rwc.at<float>(1, 1), (SLfloat)Rwc.at<float>(1, 2), (SLfloat)twc.at<float>(1, 0),
                (SLfloat)Rwc.at<float>(2, 0), (SLfloat)Rwc.at<float>(2, 1), (SLfloat)Rwc.at<float>(2, 2), (SLfloat)twc.at<float>(2, 0),
                0.0f, 0.0f, 0.0f, 1.0f);
            slMat.rotate(180, 1, 0, 0);

            _stateEstimator->updatePose(slMat, frameAndTime.time);
        }

        // Clean VO matches
        for (int i = 0; i < mCurrentFrame.N; i++)
        {
            SLCVMapPoint* pMP = mCurrentFrame.mvpMapPoints[i];
            if (pMP)
                if (pMP->Observations() < 1)
                {
                    mCurrentFrame.mvbOutlier[i] = false;
                    mCurrentFrame.mvpMapPoints[i] = static_cast<SLCVMapPoint*>(NULL);
                }
        }

        // We allow points with high innovation (considererd outliers by the Huber Function)
        // pass to the new keyframe, so that bundle adjustment will finally decide
        // if they are outliers or not. We don't want next frame to estimate its position
        // with those points so we discard them in the frame.
        for (int i = 0; i < mCurrentFrame.N; i++)
        {
            if (mCurrentFrame.mvpMapPoints[i] && mCurrentFrame.mvbOutlier[i])
                mCurrentFrame.mvpMapPoints[i] = static_cast<SLCVMapPoint*>(NULL);
        }
    }


    if (!mCurrentFrame.mpReferenceKF)
        mCurrentFrame.mpReferenceKF = mpReferenceKF;

    mLastFrame = SLCVFrame(mCurrentFrame);

    // Store frame pose information to retrieve the complete camera trajectory afterwards.
    if (mCurrentFrame.mpReferenceKF && !mCurrentFrame.mTcw.empty())
    {
        cv::Mat Tcr = mCurrentFrame.mTcw*mCurrentFrame.mpReferenceKF->GetPoseInverse(); //Tcr = Tcw * Twr (current wrt reference = world wrt current * reference wrt world
        //relative frame poses are used to refer a frame to reference frame
        mlRelativeFramePoses.push_back(Tcr);
        mlpReferences.push_back(mpReferenceKF);
        mlFrameTimes.push_back(mCurrentFrame.mTimeStamp);
        mlbLost.push_back(sm.state() == SLCVTrackingStateMachine::TRACKING_LOST);
    }
    else if (mlRelativeFramePoses.size() && mlpReferences.size() && mlFrameTimes.size())
    {
        // This can happen if tracking is lost
        mlRelativeFramePoses.push_back(mlRelativeFramePoses.back());
        mlpReferences.push_back(mlpReferences.back());
        mlFrameTimes.push_back(mlFrameTimes.back());
        mlbLost.push_back(sm.state() == SLCVTrackingStateMachine::TRACKING_LOST);
    }
}

bool SLCVOrbTracking::Relocalization()
{
    //ghm1:
    //The goal is to find a camera pose of the current frame with more than 50 matches of keypoints to mappoints
    //1. search for relocalization candidates by querying the keyframe database (with similarity score)
    //2. for every found candidate we search keypoint matches (from kf candidate) with orb in current frame (ORB that belong to the same vocabulary node (at a certain level))
    //3. if more than 15 matches are found we use a PnPSolver with RANSAC to estimate an initial camera pose
    //4. if the pose is valid (RANSAC has not reached max. iterations), pose and matches are inserted into current frame and the pose of the frame is optimized using optimizer
    //5. if more less than 10 good matches remain continue with next candidate
    //6. else if less than 50 good matched remained after optimization, mappoints associated with the keyframe candidate are projected in the current frame (which has an initial pose) and more matches are searched in a coarse window
    //7. if we now have found more than 50 matches the pose of the current frame is optimized again using the additional found matches
    //8. during the optimization matches may be rejected. so if after optimization more than 30 and less than 50 matches remain we search again by projection using a narrower search window
    //9. if now more than 50 matches exist after search by projection the pose is optimized again (for the last time)
    //10. if more than 50 good matches remain after optimization, relocalization was successful

    // Compute Bag of Words Vector
    SLAverageTiming::start("ComputeBoW");
    mCurrentFrame.ComputeBoW();
    SLAverageTiming::stop("ComputeBoW");

    // Relocalization is performed when tracking is lost
    // Track Lost: Query SLCVKeyFrame Database for keyframe candidates for relocalisation
    SLAverageTiming::start("DetectRelocalizationCandidates");
    vector<SLCVKeyFrame*> vpCandidateKFs = mpKeyFrameDatabase->DetectRelocalizationCandidates(&mCurrentFrame);
    SLAverageTiming::stop("DetectRelocalizationCandidates");

    if (vpCandidateKFs.empty())
        return false;

    const int nKFs = vpCandidateKFs.size();

    // We perform first an ORB matching with each candidate
    // If enough matches are found we setup a PnP solver

    SLAverageTiming::start("MatchCandsAndSolvePose");
    ORBmatcher matcher(0.75, true);

    vector<PnPsolver*> vpPnPsolvers;
    vpPnPsolvers.resize(nKFs);

    vector<vector<SLCVMapPoint*> > vvpMapPointMatches;
    vvpMapPointMatches.resize(nKFs);

    vector<bool> vbDiscarded;
    vbDiscarded.resize(nKFs);

    int nCandidates = 0;

    for (int i = 0; i < nKFs; i++)
    {
        SLCVKeyFrame* pKF = vpCandidateKFs[i];
        if (pKF->isBad())
            vbDiscarded[i] = true;
        else
        {
            int nmatches = matcher.SearchByBoW(pKF, mCurrentFrame, vvpMapPointMatches[i]);
            if (nmatches < 15)
            {
                vbDiscarded[i] = true;
                continue;
            }
            else
            {
                PnPsolver* pSolver = new PnPsolver(mCurrentFrame, vvpMapPointMatches[i]);
                pSolver->SetRansacParameters(0.99, 10, 300, 4, 0.5, 5.991);
                vpPnPsolvers[i] = pSolver;
                nCandidates++;
            }
        }
    }
    SLAverageTiming::stop("MatchCandsAndSolvePose");

    SLAverageTiming::start("SearchCandsUntil50Matches");
    // Alternatively perform some iterations of P4P RANSAC
    // Until we found a camera pose supported by enough inliers
    bool bMatch = false;
    ORBmatcher matcher2(0.9, true);

    while (nCandidates > 0 && !bMatch)
    {
        for (int i = 0; i < nKFs; i++)
        {
            if (vbDiscarded[i])
                continue;

            // Perform 5 Ransac Iterations
            vector<bool> vbInliers;
            int nInliers;
            bool bNoMore;

            PnPsolver* pSolver = vpPnPsolvers[i];
            cv::Mat Tcw = pSolver->iterate(5, bNoMore, vbInliers, nInliers);

            // If Ransac reachs max. iterations discard keyframe
            if (bNoMore)
            {
                vbDiscarded[i] = true;
                nCandidates--;
            }

            // If a Camera Pose is computed, optimize
            if (!Tcw.empty())
            {
                Tcw.copyTo(mCurrentFrame.mTcw);

                set<SLCVMapPoint*> sFound;

                const int np = vbInliers.size();

                for (int j = 0; j < np; j++)
                {
                    if (vbInliers[j])
                    {
                        mCurrentFrame.mvpMapPoints[j] = vvpMapPointMatches[i][j];
                        sFound.insert(vvpMapPointMatches[i][j]);
                    }
                    else
                        mCurrentFrame.mvpMapPoints[j] = NULL;
                }

                int nGood = Optimizer::PoseOptimization(&mCurrentFrame);

                if (nGood < 10)
                    continue;

                for (int io = 0; io < mCurrentFrame.N; io++)
                    if (mCurrentFrame.mvbOutlier[io])
                        mCurrentFrame.mvpMapPoints[io] = static_cast<SLCVMapPoint*>(NULL);

                // If few inliers, search by projection in a coarse window and optimize again:
                //ghm1: mappoints seen in the keyframe which was found as candidate via BoW-search are projected into
                //the current frame using the position that was calculated using the matches from BoW matcher
                if (nGood < 50)
                {
                    int nadditional = matcher2.SearchByProjection(mCurrentFrame, vpCandidateKFs[i], sFound, 10, 100);

                    if (nadditional + nGood >= 50)
                    {
                        nGood = Optimizer::PoseOptimization(&mCurrentFrame);

                        // If many inliers but still not enough, search by projection again in a narrower window
                        // the camera has been already optimized with many points
                        if (nGood > 30 && nGood < 50)
                        {
                            sFound.clear();
                            for (int ip = 0; ip < mCurrentFrame.N; ip++)
                                if (mCurrentFrame.mvpMapPoints[ip])
                                    sFound.insert(mCurrentFrame.mvpMapPoints[ip]);
                            nadditional = matcher2.SearchByProjection(mCurrentFrame, vpCandidateKFs[i], sFound, 3, 64);

                            // Final optimization
                            if (nGood + nadditional >= 50)
                            {
                                nGood = Optimizer::PoseOptimization(&mCurrentFrame);

                                for (int io = 0; io < mCurrentFrame.N; io++)
                                    if (mCurrentFrame.mvbOutlier[io])
                                        mCurrentFrame.mvpMapPoints[io] = NULL;
                            }
                        }
                    }
                }


                // If the pose is supported by enough inliers stop ransacs and continue
                if (nGood >= 50)
                {
                    bMatch = true;
                    break;
                }
            }
        }
    }
    SLAverageTiming::stop("SearchCandsUntil50Matches");

    if (!bMatch)
    {
        return false;
    }
    else
    {
        mnLastRelocFrameId = mCurrentFrame.mnId;
        return true;
    }
}

bool SLCVOrbTracking::TrackWithOptFlow()
{
    SLAverageTiming::start("TrackWithOptFlow");

    if (mLastFrame.mvKeys.size() < 100)
    {
        SLAverageTiming::stop("TrackWithOptFlow");
        return false;
    }

    SLCVMat rvec = SLCVMat::zeros(3, 1, CV_64FC1);
    SLCVMat tvec = SLCVMat::zeros(3, 1, CV_64FC1);
    cv::Mat om = mLastFrame.mTcw;
    Rodrigues(om.rowRange(0, 3).colRange(0, 3), rvec);
    tvec = om.colRange(3, 4).rowRange(0, 3);

    SLVuchar status;
    SLVfloat err;
    SLCVSize winSize(15, 15);

    cv::TermCriteria criteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS,
                              1,    // terminate after this many iterations, or
                              0.03); // when the search window moves by less than this

    SLCVVPoint2f keyPointCoordinatesLastFrame;
    vector<SLCVMapPoint*> matchedMapPoints;
    vector<cv::KeyPoint> matchedKeyPoints;
    for (int i = 0; i < mLastFrame.mvpMapPoints.size(); i++)
    {
        if (mLastFrame.mvpMapPoints[i] && !mLastFrame.mvbOutlier[i])
        {
            keyPointCoordinatesLastFrame.push_back(mLastFrame.mvKeys[i].pt);

            matchedMapPoints.push_back(mLastFrame.mvpMapPoints[i]);
            matchedKeyPoints.push_back(mLastFrame.mvKeys[i]);
        }
    }

    // Find closest possible feature points based on optical flow
    SLCVVPoint2f pred2DPoints(keyPointCoordinatesLastFrame.size());

    cv::calcOpticalFlowPyrLK(
        mLastFrame.imgGray,             // Previous frame
        mCurrentFrame.imgGray,          // Current frame
        keyPointCoordinatesLastFrame,   // Previous and current keypoints coordinates.The latter will be
        pred2DPoints,                   // expanded if more good coordinates are detected during OptFlow
        status,                         // Output vector for keypoint correspondences (1 = match found)
        err,                            // Error size for each flow
        winSize,                        // Search window for each pyramid level
        3,                              // Max levels of pyramid creation
        criteria,                       // Configuration from above
        0,                              // Additional flags
        0.01);                         // Minimal Eigen threshold

    // Only use points which are not wrong in any way during the optical flow calculation
    SLCVVPoint2f frame2DPoints;
    SLCVVPoint3f model3DPoints;

    vector<SLCVMapPoint*> trackedMapPoints;
    vector<cv::KeyPoint> trackedKeyPoints;

    mnMatchesInliers = 0;

    for (size_t i = 0; i < status.size(); i++)
    {   
        if (status[i])
        {   
            // TODO(jan): if pred2DPoints is really expanded during optflow, then the association
            // to 3D points is maybe broken?
            frame2DPoints.push_back(pred2DPoints[i]);
            SLVec3f v = matchedMapPoints[i]->worldPosVec();
            cv::Point3f p3(v.x, v.y, v.z);
            model3DPoints.push_back(p3);

            matchedKeyPoints[i].pt.x = pred2DPoints[i].x;
            matchedKeyPoints[i].pt.y = pred2DPoints[i].y;

            trackedMapPoints.push_back(matchedMapPoints[i]);
            trackedKeyPoints.push_back(matchedKeyPoints[i]);

            std::lock_guard<std::mutex> guard(_nMapMatchesLock);
            mnMatchesInliers++;
        }
    }

    if (trackedKeyPoints.size() < matchedKeyPoints.size() * 0.75)
    {
        SLAverageTiming::stop("TrackWithOptFlow");
        return false;
    }

    /////////////////////
    // Pose Estimation //
    /////////////////////

    bool foundPose = cv::solvePnP(model3DPoints,
                                  frame2DPoints,
                                  _calib->cameraMat(),
                                  _calib->distortion(),
                                  rvec, tvec,
                                  true);

    if (foundPose)
    {
        SLCVMat Tcw = cv::Mat::eye(4, 4, CV_32F);
        Tcw.at<float>(0, 3) = tvec.at<float>(0, 0);
        Tcw.at<float>(1, 3) = tvec.at<float>(1, 0);
        Tcw.at<float>(2, 3) = tvec.at<float>(2, 0);

        SLCVMat Rcw = cv::Mat::zeros(3, 3, CV_32F);
        cv::Rodrigues(rvec, Rcw);

        Tcw.at<float>(0, 0) = Rcw.at<float>(0, 0);
        Tcw.at<float>(1, 0) = Rcw.at<float>(1, 0);
        Tcw.at<float>(2, 0) = Rcw.at<float>(2, 0);
        Tcw.at<float>(0, 1) = Rcw.at<float>(0, 1);
        Tcw.at<float>(1, 1) = Rcw.at<float>(1, 1);
        Tcw.at<float>(2, 1) = Rcw.at<float>(2, 1);
        Tcw.at<float>(0, 2) = Rcw.at<float>(0, 2);
        Tcw.at<float>(1, 2) = Rcw.at<float>(1, 2);
        Tcw.at<float>(2, 2) = Rcw.at<float>(2, 2);

        mCurrentFrame.SetPose(Tcw);
        mCurrentFrame.mvKeys = trackedKeyPoints;
        mCurrentFrame.mvpMapPoints = trackedMapPoints;

        mbVO = true;
        trackingType = TrackingType_OptFlow;
    }

    SLAverageTiming::stop("TrackWithOptFlow");

    return foundPose;
}

bool SLCVOrbTracking::TrackWithMotionModel()
{
    //This method is called if tracking is OK and we have a valid motion model
    //1. UpdateLastFrame(): ...
    //2. We set an initial pose into current frame, which is the pose of the last frame corrected by the motion model (expected motion since last frame)
    //3. Reinitialization of the assotiated map points to key points in the current frame to NULL
    //4. We search for matches with associated mappoints from lastframe by projection to the current frame. A narrow window is used.
    //5. If we found less than 20 matches we search again as before but in a wider search window.
    //6. If we have still less than 20 matches tracking with motion model was unsuccessful
    //7. Else the pose is Optimized
    //8. Matches classified as outliers by the optimization routine are updated in the mvpMapPoints vector in the current frame and the valid matches are counted
    //9. If less than 10 matches to the local map remain the tracking with visual odometry is activated (mbVO = true) and that means no tracking with motion model or reference keyframe
    //10. The tracking with motion model was successful, if we found more than 20 matches to map points

    ORBmatcher matcher(0.9, true);

    // Update last frame pose according to its reference keyframe
    // Create "visual odometry" points if in Localization Mode
    UpdateLastFrame();

    mCurrentFrame.SetPose(mVelocity*mLastFrame.mTcw);

    fill(mCurrentFrame.mvpMapPoints.begin(), mCurrentFrame.mvpMapPoints.end(), static_cast<SLCVMapPoint*>(NULL));

    // Project points seen in previous frame
    SLAverageTiming::start("SearchByProjection7");
    int th = 15;
    int nmatches = matcher.SearchByProjection(mCurrentFrame, mLastFrame, th, true);
    SLAverageTiming::stop("SearchByProjection7");

    // If few matches, uses a wider window search
    SLAverageTiming::start("SearchByProjection14");
    if (nmatches < 20)
    {
        fill(mCurrentFrame.mvpMapPoints.begin(), mCurrentFrame.mvpMapPoints.end(), static_cast<SLCVMapPoint*>(NULL));
        nmatches = matcher.SearchByProjection(mCurrentFrame, mLastFrame, 2 * th, true);
    }
    SLAverageTiming::stop("SearchByProjection14");

    if (nmatches < 20)
        return false;

    SLAverageTiming::start("PoseOptimizationTWMM");
    // Optimize frame pose with all matches
    Optimizer::PoseOptimization(&mCurrentFrame);
    SLAverageTiming::stop("PoseOptimizationTWMM");

    SLAverageTiming::start("DiscardOutliers");
    // Discard outliers
    int nmatchesMap = 0;
    for (int i = 0; i < mCurrentFrame.N; i++)
    {
        if (mCurrentFrame.mvpMapPoints[i])
        {
            if (mCurrentFrame.mvbOutlier[i])
            {
                SLCVMapPoint* pMP = mCurrentFrame.mvpMapPoints[i];

                mCurrentFrame.mvpMapPoints[i] = static_cast<SLCVMapPoint*>(NULL);
                mCurrentFrame.mvbOutlier[i] = false;
                pMP->mbTrackInView = false;
                pMP->mnLastFrameSeen = mCurrentFrame.mnId;
                nmatches--;
            }
            else if (mCurrentFrame.mvpMapPoints[i]->Observations() > 0)
                nmatchesMap++;
        }
    }
    SLAverageTiming::stop("DiscardOutliers");

    //if (mbOnlyTracking)
    //{
    mbVO = nmatchesMap < 10;
    return nmatches > 20;
    //}


    //return nmatchesMap >= 10;
}

bool SLCVOrbTracking::TrackLocalMap()
{
    // We have an estimation of the camera pose and some map points tracked in the frame.
    // We retrieve the local map and try to find matches to points in the local map.

    //(UpdateLocalKeyFrames())
    //1. For all matches to mappoints we search for the keyframes in which these mappoints have been observed
    //2. We set the keyframe with the most common matches to mappoints as reference keyframe. Simultaniously a list of localKeyFrames is maintained (mvpLocalKeyFrames)
    //(UpdateLocalPoints())
    //3. Pointers to map points are added to mvpLocalMapPoints and the id of the current frame is stored into mappoint instance (mnTrackReferenceForFrame).
    //(SearchLocalPoints())
    //4. The so found local map is searched for additional matches. We check if it is not matched already, if it is in frustum and then the ORBMatcher is used to search feature matches by projection.
    //(ORBMatcher::searchByProjection())
    //5.
    //(this function)
    //6. The Pose is optimized using the found additional matches and the already found pose as initial guess
    SLAverageTiming::start("UpdateLocalMap");
    UpdateLocalMap();
    SLAverageTiming::stop("UpdateLocalMap");

    SLAverageTiming::start("SearchLocalPoints");
    SearchLocalPoints();
    SLAverageTiming::stop("SearchLocalPoints");

    // Optimize Pose
    SLAverageTiming::start("PoseOptimizationTLM");
    Optimizer::PoseOptimization(&mCurrentFrame);
    SLAverageTiming::stop("PoseOptimizationTLM");
    {
        std::lock_guard<std::mutex> guard(_nMapMatchesLock);
        mnMatchesInliers = 0;
    }

    //SLAverageTiming::start("UpdateMapPointsStat");
    // Update MapPoints Statistics
    for (int i = 0; i < mCurrentFrame.N; i++)
    {
        if (mCurrentFrame.mvpMapPoints[i])
        {
            if (!mCurrentFrame.mvbOutlier[i])
            {
                mCurrentFrame.mvpMapPoints[i]->IncreaseFound();
                if (mCurrentFrame.mvpMapPoints[i]->Observations() > 0)
                {
                    std::lock_guard<std::mutex> guard(_nMapMatchesLock);
                    mnMatchesInliers++;
                }
            }
            //else if (mSensor == System::STEREO)
            //    mCurrentFrame.mvpMapPoints[i] = static_cast<SLCVMapPoint*>(NULL);
        }
    }
    //SLAverageTiming::stop("UpdateMapPointsStat");

    // Decide if the tracking was succesful
    // More restrictive if there was a relocalization recently
    int mnMatchesInliersLocal;
    {
        std::lock_guard<std::mutex> guard(_nMapMatchesLock);
        mnMatchesInliersLocal = mnMatchesInliers;
    }
    if (mCurrentFrame.mnId < mnLastRelocFrameId + mMaxFrames && mnMatchesInliersLocal < 50)
        return false;

    if (mnMatchesInliersLocal < 30)
        return false;
    else
        return true;
}

void SLCVOrbTracking::SearchLocalPoints()
{
    // Do not search map points already matched
    for (vector<SLCVMapPoint*>::iterator vit = mCurrentFrame.mvpMapPoints.begin(), vend = mCurrentFrame.mvpMapPoints.end(); vit != vend; vit++)
    {
        SLCVMapPoint* pMP = *vit;
        if (pMP)
        {
            if (pMP->isBad())
            {
                *vit = static_cast<SLCVMapPoint*>(NULL);
            }
            else
            {
                pMP->IncreaseVisible();
                pMP->mnLastFrameSeen = mCurrentFrame.mnId;
                pMP->mbTrackInView = false;
            }
        }
    }

    int nToMatch = 0;

    // Project points in frame and check its visibility
    for (vector<SLCVMapPoint*>::iterator vit = mvpLocalMapPoints.begin(), vend = mvpLocalMapPoints.end(); vit != vend; vit++)
    {
        SLCVMapPoint* pMP = *vit;
        if (pMP->mnLastFrameSeen == mCurrentFrame.mnId)
            continue;
        if (pMP->isBad())
            continue;
        // Project (this fills SLCVMapPoint variables for matching)
        if (mCurrentFrame.isInFrustum(pMP, 0.5))
        {
            //ghm1 test:
            //if (!_image.empty())
            //{
            //    SLCVPoint2f ptProj(pMP->mTrackProjX, pMP->mTrackProjY);
            //    cv::rectangle(_image,
            //        cv::Rect(ptProj.x - 3, ptProj.y - 3, 7, 7),
            //        Scalar(0, 0, 255));
            //}
            pMP->IncreaseVisible();
            nToMatch++;
        }
    }

    if (nToMatch > 0)
    {
        ORBmatcher matcher(0.8);
        int th = 1;
        // If the camera has been relocalised recently, perform a coarser search
        if (mCurrentFrame.mnId < mnLastRelocFrameId + 2)
            th = 5;
        matcher.SearchByProjection(mCurrentFrame, mvpLocalMapPoints, th);
    }
}

bool SLCVOrbTracking::TrackReferenceKeyFrame()
{
    //This routine is called if current tracking state is OK but we have NO valid motion model
    //1. Berechnung des BoW-Vectors f�r den current frame
    //2. using BoW we search mappoint matches (from reference keyframe) with orb in current frame (ORB that belong to the same vocabulary node (at a certain level))
    //3. if there are less than 15 matches return.
    //4. we use the pose found for the last frame as initial pose for the current frame
    //5. This pose is optimized using the matches to map points found by BoW search with reference frame
    //6. Matches classified as outliers by the optimization routine are updated in the mvpMapPoints vector in the current frame and the valid matches are counted
    //7. If there are more than 10 valid matches the reference frame tracking was successful.

    // Compute Bag of Words vector
    mCurrentFrame.ComputeBoW();

    // We perform first an ORB matching with the reference keyframe
    // If enough matches are found we setup a PnP solver
    ORBmatcher matcher(0.7, true);
    vector<SLCVMapPoint*> vpMapPointMatches;

    int nmatches = matcher.SearchByBoW(mpReferenceKF, mCurrentFrame, vpMapPointMatches);

    if (nmatches < 15)
        return false;

    mCurrentFrame.mvpMapPoints = vpMapPointMatches;
    mCurrentFrame.SetPose(mLastFrame.mTcw);

    Optimizer::PoseOptimization(&mCurrentFrame);

    // Discard outliers
    int nmatchesMap = 0;
    for (int i = 0; i < mCurrentFrame.N; i++)
    {
        if (mCurrentFrame.mvpMapPoints[i])
        {
            if (mCurrentFrame.mvbOutlier[i])
            {
                SLCVMapPoint* pMP = mCurrentFrame.mvpMapPoints[i];

                mCurrentFrame.mvpMapPoints[i] = static_cast<SLCVMapPoint*>(NULL);
                mCurrentFrame.mvbOutlier[i] = false;
                pMP->mbTrackInView = false;
                pMP->mnLastFrameSeen = mCurrentFrame.mnId;
                nmatches--;
            }
            else if (mCurrentFrame.mvpMapPoints[i]->Observations() > 0)
                nmatchesMap++;
        }
    }

    return nmatchesMap >= 10;
}

void SLCVOrbTracking::UpdateLastFrame()
{
    // Update pose according to reference keyframe
    SLCVKeyFrame* pRef = mLastFrame.mpReferenceKF;
    //cout << "pRef pose: " << pRef->GetPose() << endl;
    cv::Mat Tlr = mlRelativeFramePoses.back();
    //GHM1:
    //l = last, w = world, r = reference
    //Tlr is the relative transformation for the last frame wrt to reference frame
    //(because the relative pose for the current frame is added at the end of tracking)
    //Refer last frame pose to world: Tlw = Tlr * Trw
    //So it seems, that the frames pose does not always refer to world frame...?
    mLastFrame.SetPose(Tlr*pRef->GetPose());
}

void SLCVOrbTracking::UpdateLocalMap()
{
    // This is for visualization
    //mpMap->SetReferenceMapPoints(mvpLocalMapPoints);

    // Update
    UpdateLocalKeyFrames();
    UpdateLocalPoints();
}

void SLCVOrbTracking::UpdateLocalPoints()
{
    mvpLocalMapPoints.clear();

    for (vector<SLCVKeyFrame*>::const_iterator itKF = mvpLocalKeyFrames.begin(), itEndKF = mvpLocalKeyFrames.end(); itKF != itEndKF; itKF++)
    {
        SLCVKeyFrame* pKF = *itKF;
        const vector<SLCVMapPoint*> vpMPs = pKF->GetMapPointMatches();

        for (vector<SLCVMapPoint*>::const_iterator itMP = vpMPs.begin(), itEndMP = vpMPs.end(); itMP != itEndMP; itMP++)
        {
            SLCVMapPoint* pMP = *itMP;
            if (!pMP)
                continue;
            if (pMP->mnTrackReferenceForFrame == mCurrentFrame.mnId)
                continue;
            if (!pMP->isBad())
            {
                mvpLocalMapPoints.push_back(pMP);
                pMP->mnTrackReferenceForFrame = mCurrentFrame.mnId;
            }
        }
    }
}

void SLCVOrbTracking::UpdateLocalKeyFrames()
{
    // Each map point vote for the keyframes in which it has been observed
    map<SLCVKeyFrame*, int> keyframeCounter;
    for (int i = 0; i < mCurrentFrame.N; i++)
    {
        if (mCurrentFrame.mvpMapPoints[i])
        {
            SLCVMapPoint* pMP = mCurrentFrame.mvpMapPoints[i];
            if (!pMP->isBad())
            {
                const map<SLCVKeyFrame*, size_t> observations = pMP->GetObservations();
                for (map<SLCVKeyFrame*, size_t>::const_iterator it = observations.begin(), itend = observations.end(); it != itend; it++)
                    keyframeCounter[it->first]++;
            }
            else
            {
                mCurrentFrame.mvpMapPoints[i] = NULL;
            }
        }
    }

    if (keyframeCounter.empty())
        return;

    int max = 0;
    SLCVKeyFrame* pKFmax = static_cast<SLCVKeyFrame*>(NULL);

    mvpLocalKeyFrames.clear();
    mvpLocalKeyFrames.reserve(3 * keyframeCounter.size());

    // All keyframes that observe a map point are included in the local map. Also check which keyframe shares most points
    for (map<SLCVKeyFrame*, int>::const_iterator it = keyframeCounter.begin(), itEnd = keyframeCounter.end(); it != itEnd; it++)
    {
        SLCVKeyFrame* pKF = it->first;

        if (pKF->isBad())
            continue;

        if (it->second > max)
        {
            max = it->second;
            pKFmax = pKF;
        }

        mvpLocalKeyFrames.push_back(it->first);
        pKF->mnTrackReferenceForFrame = mCurrentFrame.mnId;
    }


    // Include also some not-already-included keyframes that are neighbors to already-included keyframes
    for (vector<SLCVKeyFrame*>::const_iterator itKF = mvpLocalKeyFrames.begin(), itEndKF = mvpLocalKeyFrames.end(); itKF != itEndKF; itKF++)
    {
        // Limit the number of keyframes
        if (mvpLocalKeyFrames.size() > 80)
            break;

        SLCVKeyFrame* pKF = *itKF;

        const vector<SLCVKeyFrame*> vNeighs = pKF->GetBestCovisibilityKeyFrames(10);

        for (vector<SLCVKeyFrame*>::const_iterator itNeighKF = vNeighs.begin(), itEndNeighKF = vNeighs.end(); itNeighKF != itEndNeighKF; itNeighKF++)
        {
            SLCVKeyFrame* pNeighKF = *itNeighKF;
            if (!pNeighKF->isBad())
            {
                if (pNeighKF->mnTrackReferenceForFrame != mCurrentFrame.mnId)
                {
                    mvpLocalKeyFrames.push_back(pNeighKF);
                    pNeighKF->mnTrackReferenceForFrame = mCurrentFrame.mnId;
                    break;
                }
            }
        }

        const set<SLCVKeyFrame*> spChilds = pKF->GetChilds();
        for (set<SLCVKeyFrame*>::const_iterator sit = spChilds.begin(), send = spChilds.end(); sit != send; sit++)
        {
            SLCVKeyFrame* pChildKF = *sit;
            if (!pChildKF->isBad())
            {
                if (pChildKF->mnTrackReferenceForFrame != mCurrentFrame.mnId)
                {
                    mvpLocalKeyFrames.push_back(pChildKF);
                    pChildKF->mnTrackReferenceForFrame = mCurrentFrame.mnId;
                    break;
                }
            }
        }

        SLCVKeyFrame* pParent = pKF->GetParent();
        if (pParent)
        {
            if (pParent->mnTrackReferenceForFrame != mCurrentFrame.mnId)
            {
                mvpLocalKeyFrames.push_back(pParent);
                pParent->mnTrackReferenceForFrame = mCurrentFrame.mnId;
                break;
            }
        }

    }

    if (pKFmax)
    {
        mpReferenceKF = pKFmax;
        mCurrentFrame.mpReferenceKF = mpReferenceKF;
    }
}

void SLCVOrbTracking::Reset()
{
    cout << "System Reseting" << endl;

    // Clear BoW Database
    mpKeyFrameDatabase->clear();

    // Clear Map (this erase MapPoints and KeyFrames)
    _map->clear();

    SLCVKeyFrame::nNextId = 0;
    SLCVFrame::nNextId = 0;
    //mState = NO_IMAGES_YET;
    _bOK = false;
    _initialized = false;

    mlRelativeFramePoses.clear();
    mlpReferences.clear();
    mlFrameTimes.clear();
    mlbLost.clear();

    //mpLastKeyFrame = NULL;
    mpReferenceKF = NULL;
    mvpLocalMapPoints.clear();
    mvpLocalKeyFrames.clear();
    mnMatchesInliers = 0;

    //we also have to clear the mapNode because it may access
    //mappoints and keyframes while we are loading
    _mapNode->clearAll();
}

void SLCVOrbTracking::Pause()
{
    sm.requestStateIdle();
    while (!sm.hasStateIdle())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void SLCVOrbTracking::Resume()
{
    sm.requestResume();
}