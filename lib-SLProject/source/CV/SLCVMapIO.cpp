﻿//#############################################################################
//  File:      SLCVMapIO.cpp
//  Author:    Michael Goettlicher
//  Date:      October 2017
//  Codestyle: https://github.com/cpvrlab/SLProject/wiki/Coding-Style-Guidelines
//  Copyright: Marcus Hudritsch
//             This software is provide under the GNU General Public License
//             Please visit: http://opensource.org/licenses/GPL-3.0
//#############################################################################

#include "stdafx.h"

#include "SLCVMapIO.h"
#include <SLCVKeyFrameDB.h>
#include <SLCVMapNode.h>

using namespace std;

//-----------------------------------------------------------------------------
SLCVMapIO::SLCVMapIO(const string& filename, ORBVocabulary* orbVoc, bool kfImgsIO, string currImgPath)
  : _orbVoc(orbVoc),
    _kfImgsIO(kfImgsIO),
    _currImgPath(currImgPath)
{
    _fs.open(filename, cv::FileStorage::READ);
    if (!_fs.isOpened())
    {
        string msg = "Failed to open filestorage: " + filename + "\n";
        SL_WARN_MSG(msg.c_str());
        throw(std::runtime_error(msg));
    }
}
//-----------------------------------------------------------------------------
SLCVMapIO::~SLCVMapIO()
{
    _fs.release();
}
//-----------------------------------------------------------------------------
//! add map point
void SLCVMapIO::load(SLCVMap& map, SLCVKeyFrameDB& kfDB)
{
    //load map node object matrix
    if (!_fs["mapNodeOm"].empty())
    {
        cv::Mat cvOM; //has to be here!
        _fs["mapNodeOm"] >> cvOM;
        SLMat4f om;
        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                om(i, j) = cvOM.at<float>(i, j);
            }
        }
        map.getMapNode()->om(om);
    }

    //load keyframes
    loadKeyFrames(map, kfDB);
    //load map points
    loadMapPoints(map);

    //update the covisibility graph, when all keyframes and mappoints are loaded
    std::vector<SLCVKeyFrame*> kfs               = map.GetAllKeyFrames();
    SLCVKeyFrame*              firstKF           = nullptr;
    bool                       buildSpanningTree = false;
    for (int i = 0; i < kfs.size(); i++)
    {
        // Update links in the Covisibility Graph, do not build the spanning tree yet
        SLCVKeyFrame* kf = kfs[i];
        kf->UpdateConnections(false);
        if (kf->mnId == 0)
        {
            firstKF = kf;
        }
        else if (kf->GetParent() == NULL)
        {
            buildSpanningTree = true;
        }
    }

    assert(firstKF && "Could not find keyframe with id 0\n");

    // Build spanning tree if keyframes have no parents (legacy support)
    if (buildSpanningTree)
    {
        //QueueElem: <unconnected_kf, graph_kf, weight>
        using QueueElem                  = std::tuple<SLCVKeyFrame*, SLCVKeyFrame*, int>;
        auto                    cmpQueue = [](const QueueElem& left, const QueueElem& right) { return (std::get<2>(left) < std::get<2>(right)); };
        auto                    cmpMap   = [](const pair<SLCVKeyFrame*, int>& left, const pair<SLCVKeyFrame*, int>& right) { return left.second < right.second; };
        std::set<SLCVKeyFrame*> graph;
        std::set<SLCVKeyFrame*> unconKfs;
        for (auto& kf : kfs)
            unconKfs.insert(kf);

        //pick first kf
        graph.insert(firstKF);
        unconKfs.erase(firstKF);

        while (unconKfs.size())
        {
            std::priority_queue<QueueElem, std::vector<QueueElem>, decltype(cmpQueue)> q(cmpQueue);
            //update queue with keyframes with neighbous in the graph
            for (auto& unconKf : unconKfs)
            {
                const std::map<SLCVKeyFrame*, int>& weights = unconKf->GetConnectedKfWeights();
                for (auto& graphKf : graph)
                {
                    auto it = weights.find(graphKf);
                    if (it != weights.end())
                    {
                        QueueElem newElem = std::make_tuple(unconKf, it->first, it->second);
                        q.push(newElem);
                    }
                }
            }
            //extract keyframe with shortest connection
            QueueElem topElem = q.top();
            //remove it from unconKfs and add it to graph
            SLCVKeyFrame* newGraphKf = std::get<0>(topElem);
            unconKfs.erase(newGraphKf);
            newGraphKf->ChangeParent(std::get<1>(topElem));
            std::cout << "Added kf " << newGraphKf->mnId << " with parent " << std::get<1>(topElem)->mnId << std::endl;
            //update parent
            graph.insert(newGraphKf);
        }
    }

    // Build spanning tree
    /*
    std::vector<SLCVKeyFrame*> spanningTreeFringe;
    spanningTreeFringe.resize(kfs.size());
    spanningTreeFringe[0] = firstKF;
    uint32_t fringeCount = 1;
    uint32_t fringeTakeIndex = 0;
    uint32_t fringePutIndex = 1;
    while (fringeCount)
    {
        fringeCount--;
        SLCVKeyFrame* kf = spanningTreeFringe[fringeTakeIndex];
        std::set<SLCVKeyFrame*> connectedKeyFrames = kf->GetConnectedKeyFrames();
        for (std::set<SLCVKeyFrame*>::iterator connectedKeyFramesIt = connectedKeyFrames.begin(); connectedKeyFramesIt != connectedKeyFrames.end(); connectedKeyFramesIt++)
        {
            SLCVKeyFrame* connectedKF = *connectedKeyFramesIt;
            if (connectedKF->mnId != 0 && !connectedKF->GetParent())
            {
                connectedKF->ChangeParent(kf);
                spanningTreeFringe[fringePutIndex] = connectedKF;
                fringePutIndex++;
                fringeCount++;
            }
        }
        fringeTakeIndex++;
    }
    */

    //compute resulting values for map points
    auto mapPts = map.GetAllMapPoints();
    for (auto& mp : mapPts)
    {
        //mean viewing direction and depth
        mp->UpdateNormalAndDepth();
        mp->ComputeDistinctiveDescriptors();
    }

    SL_LOG("Slam map loading successful.");
}
//-----------------------------------------------------------------------------
void SLCVMapIO::save(const string& filename, SLCVMap& map, bool kfImgsIO, const string& pathImgs)
{
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);

    //save keyframes (without graph/neigbourhood information)
    auto kfs = map.GetAllKeyFrames();
    if (!kfs.size())
        return;

    SLCVMapNode* mapNode = map.getMapNode();
    if (mapNode)
    {
        auto    om = mapNode->om();
        cv::Mat cvOM(4, 4, CV_32F);
        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                cvOM.at<float>(i, j) = om(i, j);
            }
        }

        fs << "mapNodeOm" << cvOM;
    }

    //start sequence keyframes
    fs << "KeyFrames"
       << "[";
    for (int i = 0; i < kfs.size(); ++i)
    {
        SLCVKeyFrame* kf = kfs[i];
        if (kf->isBad())
            continue;

        fs << "{"; //new map keyFrame
                   //add id
        fs << "id" << (int)kf->mnId;
        if (kf->mnId != 0) //kf with id 0 has no parent
            fs << "parentId" << (int)kf->GetParent()->mnId;
        else
            fs << "parentId" << -1;
        //loop edges: we store the id of the connected kf
        auto loopEdges = kf->GetLoopEdges();
        if (loopEdges.size())
        {
            std::vector<int> loopEdgeIds;
            for (auto loopEdgeKf : loopEdges)
            {
                loopEdgeIds.push_back(loopEdgeKf->mnId);
            }
            fs << "loopEdges" << loopEdgeIds;
        }

        // world w.r.t camera
        fs << "Tcw" << kf->GetPose();
        fs << "featureDescriptors" << kf->mDescriptors;
        fs << "keyPtsUndist" << kf->mvKeysUn;

        //scale factor
        fs << "scaleFactor" << kf->mfScaleFactor;
        //number of pyriamid scale levels
        fs << "nScaleLevels" << kf->mnScaleLevels;
        //fs << "fx" << kf->fx;
        //fs << "fy" << kf->fy;
        //fs << "cx" << kf->cx;
        //fs << "cy" << kf->cy;
        fs << "K" << kf->mK;

        //debug print
        //std::cout << "fx" << kf->fx << std::endl;
        //std::cout << "fy" << kf->fy << std::endl;
        //std::cout << "cx" << kf->cx << std::endl;
        //std::cout << "cy" << kf->cy << std::endl;
        //std::cout << "K" << kf->mK << std::endl;

        fs << "nMinX" << kf->mnMinX;
        fs << "nMinY" << kf->mnMinY;
        fs << "nMaxX" << kf->mnMaxX;
        fs << "nMaxY" << kf->mnMaxY;

        fs << "}"; //close map

        //save the original frame image for this keyframe
        if (kfImgsIO)
        {
            cv::Mat imgColor;
            if (!kf->imgGray.empty())
            {
                std::stringstream ss;
                ss << pathImgs << "kf" << (int)kf->mnId << ".jpg";

                cv::cvtColor(kf->imgGray, imgColor, cv::COLOR_GRAY2BGR);
                cv::imwrite(ss.str(), imgColor);

                //if this kf was never loaded, we still have to set the texture path
                kf->setTexturePath(ss.str());
            }
        }
    }
    fs << "]"; //close sequence keyframes

    auto mpts = map.GetAllMapPoints();
    //start map points sequence
    fs << "MapPoints"
       << "[";
    for (int i = 0; i < mpts.size(); ++i)
    {
        SLCVMapPoint* mpt = mpts[i];
        if (mpt->isBad())
            continue;

        fs << "{"; //new map for MapPoint
                   //add id
        fs << "id" << (int)mpt->mnId;
        //add position
        fs << "mWorldPos" << mpt->GetWorldPos();
        //save keyframe observations
        auto        observations = mpt->GetObservations();
        vector<int> observingKfIds;
        vector<int> corrKpIndices; //corresponding keypoint indices in observing keyframe
        for (auto it : observations)
        {
            if (!it.first->isBad())
            {
                observingKfIds.push_back(it.first->mnId);
                corrKpIndices.push_back(it.second);
            }
        }
        fs << "observingKfIds" << observingKfIds;
        fs << "corrKpIndices" << corrKpIndices;
        //(we calculate mean descriptor and mean deviation after loading)

        //reference key frame (I think this is the keyframe from which this
        //map point was generated -> first reference?)
        if (!map.isKeyFrameInMap(mpt->refKf()))
        {
            cout << "Reference keyframe not in map!" << endl;
        }
        else if (mpt->refKf()->isBad())
        {
            cout << "Reference keyframe is bad!" << endl;
        }
        fs << "refKfId" << (int)mpt->refKf()->mnId;

        fs << "}"; //close map
    }
    fs << "]";

    // explicit close
    fs.release();
    SL_LOG("Slam map storage successful.");
}
//-----------------------------------------------------------------------------
//calculation of scaleFactors , levelsigma2, invScaleFactors and invLevelSigma2
void SLCVMapIO::calculateScaleFactors(float scaleFactor, int nlevels)
{
    //(copied from ORBextractor ctor)
    _vScaleFactor.clear();
    _vLevelSigma2.clear();
    _vScaleFactor.resize(nlevels);
    _vLevelSigma2.resize(nlevels);
    _vScaleFactor[0] = 1.0f;
    _vLevelSigma2[0] = 1.0f;
    for (int i = 1; i < nlevels; i++)
    {
        _vScaleFactor[i] = _vScaleFactor[i - 1] * scaleFactor;
        _vLevelSigma2[i] = _vScaleFactor[i] * _vScaleFactor[i];
    }

    _vInvScaleFactor.resize(nlevels);
    _vInvLevelSigma2.resize(nlevels);
    for (int i = 0; i < nlevels; i++)
    {
        _vInvScaleFactor[i] = 1.0f / _vScaleFactor[i];
        _vInvLevelSigma2[i] = 1.0f / _vLevelSigma2[i];
    }
}
//-----------------------------------------------------------------------------
void SLCVMapIO::loadKeyFrames(SLCVMap& map, SLCVKeyFrameDB& kfDB)
{
    cv::FileNode n = _fs["KeyFrames"];
    if (n.type() != cv::FileNode::SEQ)
    {
        cerr << "strings is not a sequence! FAIL" << endl;
    }

    //mapping of keyframe pointer by their id (used during map points loading)
    _kfsMap.clear();

    //the id of the parent is mapped to the kf id because we can assign it not before all keyframes are loaded
    std::map<int, int> parentIdMap;
    //vector of keyframe ids of connected loop edge candidates mapped to kf id that they are connected to
    std::map<int, std::vector<int>> loopEdgesMap;
    //reserve space in kfs
    for (auto it = n.begin(); it != n.end(); ++it)
    {
        int id = (*it)["id"];
        //load parent id
        if (!(*it)["parentId"].empty())
        {
            int parentId    = (*it)["parentId"];
            parentIdMap[id] = parentId;
        }
        //load ids of connected loop edge candidates
        if (!(*it)["loopEdges"].empty() && (*it)["loopEdges"].isSeq())
        {
            cv::FileNode     les = (*it)["loopEdges"];
            std::vector<int> loopEdges;
            for (auto itLes = les.begin(); itLes != les.end(); ++itLes)
            {
                loopEdges.push_back((int)*itLes);
            }
            loopEdgesMap[id] = loopEdges;
        }
        // Infos about the pose: https://github.com/raulmur/ORB_SLAM2/issues/249
        // world w.r.t. camera pose -> wTc
        cv::Mat Tcw; //has to be here!
        (*it)["Tcw"] >> Tcw;

        cv::Mat featureDescriptors; //has to be here!
        (*it)["featureDescriptors"] >> featureDescriptors;

        //load undistorted keypoints in frame
        //todo: braucht man diese wirklich oder kann man das umgehen, indem zus�tzliche daten im MapPoint abgelegt werden (z.B. octave/level siehe UpdateNormalAndDepth)
        std::vector<cv::KeyPoint> keyPtsUndist;
        (*it)["keyPtsUndist"] >> keyPtsUndist;

        //ORB extractor information
        float scaleFactor;
        (*it)["scaleFactor"] >> scaleFactor;
        //number of pyriamid scale levels
        int nScaleLevels = -1;
        (*it)["nScaleLevels"] >> nScaleLevels;
        //calculation of scaleFactors , levelsigma2, invScaleFactors and invLevelSigma2
        calculateScaleFactors(scaleFactor, nScaleLevels);

        //calibration information
        //load camera matrix
        cv::Mat K;
        (*it)["K"] >> K;
        float fx, fy, cx, cy;
        fx = K.at<float>(0, 0);
        fy = K.at<float>(1, 1);
        cx = K.at<float>(0, 2);
        cy = K.at<float>(1, 2);

        //image bounds
        float nMinX, nMinY, nMaxX, nMaxY;
        (*it)["nMinX"] >> nMinX;
        (*it)["nMinY"] >> nMinY;
        (*it)["nMaxX"] >> nMaxX;
        (*it)["nMaxY"] >> nMaxY;

        //SLCVKeyFrame* newKf = new SLCVKeyFrame(keyPtsUndist.size());
        SLCVKeyFrame* newKf = new SLCVKeyFrame(Tcw, id, fx, fy, cx, cy, keyPtsUndist.size(), keyPtsUndist, featureDescriptors, _orbVoc, nScaleLevels, scaleFactor, _vScaleFactor, _vLevelSigma2, _vInvLevelSigma2, nMinX, nMinY, nMaxX, nMaxY, K, &kfDB, &map);

        if (_kfImgsIO)
        {
            stringstream ss;
            ss << _currImgPath << "kf" << id << ".jpg";
            //newKf->imgGray = kfImg;
            if (SLFileSystem::fileExists(ss.str()))
            {
                newKf->setTexturePath(ss.str());
                cv::Mat imgColor = cv::imread(ss.str());
                ;
                cv::cvtColor(imgColor, newKf->imgGray, cv::COLOR_BGR2GRAY);
            }
        }
        //kfs.push_back(newKf);
        map.AddKeyFrame(newKf);

        //Update keyframe database:
        //add to keyframe database
        kfDB.add(newKf);

        //pointer goes out of scope und wird invalid!!!!!!
        //map pointer by id for look-up
        _kfsMap[newKf->mnId] = newKf;
    }

    //set parent keyframe pointers into keyframes
    auto kfs = map.GetAllKeyFrames();
    for (SLCVKeyFrame* kf : kfs)
    {
        if (kf->mnId != 0)
        {
            auto itParentId = parentIdMap.find(kf->mnId);
            if (itParentId != parentIdMap.end())
            {
                int  parentId   = itParentId->second;
                auto itParentKf = _kfsMap.find(parentId);
                if (itParentKf != _kfsMap.end())
                    kf->ChangeParent(itParentKf->second);
                else
                    cerr << "[SLCVMapIO] loadKeyFrames: Parent does not exist! FAIL" << endl;
            }
            else
                cerr << "[SLCVMapIO] loadKeyFrames: Parent does not exist! FAIL" << endl;
        }
    }

    int numberOfLoopClosings = 0;
    //set loop edge pointer into keyframes
    for (SLCVKeyFrame* kf : kfs)
    {
        auto it = loopEdgesMap.find(kf->mnId);
        if (it != loopEdgesMap.end())
        {
            const auto& loopEdgeIds = it->second;
            for (int loopKfId : loopEdgeIds)
            {
                auto loopKfIt = _kfsMap.find(loopKfId);
                if (loopKfIt != _kfsMap.end())
                {
                    kf->AddLoopEdge(loopKfIt->second);
                    numberOfLoopClosings++;
                }
                else
                    cerr << "[SLCVMapIO] loadKeyFrames: Loop keyframe id does not exist! FAIL" << endl;
            }
        }
    }
    //there is a loop edge in the keyframe and the matched keyframe -> division by 2
    map.setNumLoopClosings(numberOfLoopClosings / 2);
}
//-----------------------------------------------------------------------------
void SLCVMapIO::loadMapPoints(SLCVMap& map)
{
    cv::FileNode n = _fs["MapPoints"];
    if (n.type() != cv::FileNode::SEQ)
    {
        cerr << "strings is not a sequence! FAIL" << endl;
    }

    //reserve space in mapPts
    //mapPts.reserve(n.size());
    //read and add map points
    for (auto it = n.begin(); it != n.end(); ++it)
    {
        //newPt->id( (int)(*it)["id"]);
        int id = (int)(*it)["id"];

        cv::Mat mWorldPos; //has to be here!
        (*it)["mWorldPos"] >> mWorldPos;

        SLCVMapPoint* newPt = new SLCVMapPoint(id, mWorldPos, &map);
        //get observing keyframes
        vector<int> observingKfIds;
        (*it)["observingKfIds"] >> observingKfIds;
        //get corresponding keypoint indices in observing keyframe
        vector<int> corrKpIndices;
        (*it)["corrKpIndices"] >> corrKpIndices;

        map.AddMapPoint(newPt);

        //get reference keyframe id
        int refKfId = (int)(*it)["refKfId"];

        //find and add pointers of observing keyframes to map point
        {
            //SLCVMapPoint* mapPt = mapPts.back();
            SLCVMapPoint* mapPt = newPt;
            for (int i = 0; i < observingKfIds.size(); ++i)
            {
                const int kfId = observingKfIds[i];
                if (_kfsMap.find(kfId) != _kfsMap.end())
                {
                    SLCVKeyFrame* kf = _kfsMap[kfId];
                    kf->AddMapPoint(mapPt, corrKpIndices[i]);
                    mapPt->AddObservation(kf, corrKpIndices[i]);
                }
                else
                {
                    cout << "keyframe with id " << i << " not found!";
                }
            }

            //todo: is the reference keyframe only a currently valid variable or has every keyframe a reference keyframe?? Is it necessary for tracking?
            //map reference key frame pointer
            if (_kfsMap.find(refKfId) != _kfsMap.end())
                mapPt->refKf(_kfsMap[refKfId]);
            else
            {
                cout << "no reference keyframe found!" << endl;
                if (observingKfIds.size())
                {
                    //we use the first of the observing keyframes
                    int kfId = observingKfIds[0];
                    if (_kfsMap.find(kfId) != _kfsMap.end())
                        mapPt->refKf(_kfsMap[kfId]);
                }
                else
                    int stop = 0;
            }
        }
    }
    //cv::FileNode n = _fs["MapPoints"];
    //if (n.type() != cv::FileNode::SEQ)
    //{
    //    cerr << "strings is not a sequence! FAIL" << endl;
    //}

    ////reserve space in mapPts
    ////mapPts.reserve(n.size());
    ////read and add map points
    //for (auto it = n.begin(); it != n.end(); ++it)
    //{
    //    //newPt->id( (int)(*it)["id"]);
    //    int id = (int)(*it)["id"];

    //    cv::Mat mWorldPos; //has to be here!
    //    (*it)["mWorldPos"] >> mWorldPos;

    //    SLCVMapPoint* newPt = new SLCVMapPoint(id, mWorldPos, &map);

    //    //level
    //    //int level;
    //    //(*it)["level"] >> level;
    //    //newPt->level(level);

    //    //get observing keyframes
    //    vector<int> observingKfIds;
    //    (*it)["observingKfIds"] >> observingKfIds;
    //    //get corresponding keypoint indices in observing keyframe
    //    vector<int> corrKpIndices;
    //    (*it)["corrKpIndices"] >> corrKpIndices;

    //    //mapPts.push_back(newPt);
    //    //mapPts.insert(newPt);
    //    map.AddMapPoint(newPt);

    //    //get reference keyframe id
    //    int refKfId = (int)(*it)["refKfId"];

    //    //find and add pointers of observing keyframes to map point
    //    {
    //        //SLCVMapPoint* mapPt = mapPts.back();
    //        SLCVMapPoint* mapPt = newPt;
    //        for (int i=0; i<observingKfIds.size(); ++i)
    //        {
    //            const int kfId = observingKfIds[i];
    //            if (_kfsMap.find(kfId) != _kfsMap.end()) {
    //                SLCVKeyFrame* kf = _kfsMap[kfId];
    //                mapPt->AddObservation(kf, corrKpIndices[i]);
    //                kf->AddMapPoint(mapPt, corrKpIndices[i]);
    //            }
    //            else {
    //                cout << "keyframe with id " << i << " not found!";
    //            }
    //        }

    //        //todo: is the reference keyframe only a currently valid variable or has every keyframe a reference keyframe?? Is it necessary for tracking?
    //        //map reference key frame pointer
    //        if (_kfsMap.find(refKfId) != _kfsMap.end())
    //            mapPt->refKf(_kfsMap[refKfId]);
    //        else {
    //            cout << "no reference keyframe found!" << endl;
    //            if (observingKfIds.size()) {
    //                //we use the first of the observing keyframes
    //                int kfId = observingKfIds[0];
    //                if (_kfsMap.find(kfId) != _kfsMap.end())
    //                    mapPt->refKf(_kfsMap[kfId]);
    //            }
    //            else
    //                int stop = 0;
    //        }
    //    }
    //}
}
//-----------------------------------------------------------------------------