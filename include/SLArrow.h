//#############################################################################
//  File:      SLArrow.h
//  Author:    Marcus Hudritsch
//  Date:      July 2014
//  Codestyle: https://github.com/cpvrlab/SLProject/wiki/Coding-Style-Guidelines
//  Copyright: Marcus Hudritsch
//             This software is provide under the GNU General Public License
//             Please visit: http://opensource.org/licenses/GPL-3.0
//#############################################################################

#ifndef SLARROW_H
#define SLARROW_H

#include <SLRevolver.h>

//-----------------------------------------------------------------------------
//! SLArrow is creates an arrow mesh based on its SLRevolver methods     
class SLArrow: public SLRevolver 
{   public:                     
    SLArrow(SLfloat  arrowCylinderRadius,
            SLfloat  length,
            SLfloat  headLength,
            SLfloat  headWidth,
            SLint    slices,
            SLstring name = "arrow mesh",
            SLMaterial* mat = nullptr)
    {  
        assert(slices >= 3 && "Error: Not enough slices.");
        assert(headLength < length);
        assert(headWidth > arrowCylinderRadius);
   
        _radius     = arrowCylinderRadius;
        _length     = length;
        _headLength = headLength;
        _headWidth  = headWidth;
        _slices     = slices;
        _revAxis.set(0,0,1);

        // Add revolving polyline points with double points for sharp edges
        _revPoints.reserve(8);
        _revPoints.push_back(SLVec3f(0,          0, 0));
        _revPoints.push_back(SLVec3f(_headWidth, 0, _headLength));
        _revPoints.push_back(SLVec3f(_headWidth, 0, _headLength));
        _revPoints.push_back(SLVec3f(_radius,    0, _headLength));
        _revPoints.push_back(SLVec3f(_radius,    0, _headLength));
        _revPoints.push_back(SLVec3f(_radius,    0, _length));
        _revPoints.push_back(SLVec3f(_radius,    0, _length));
        _revPoints.push_back(SLVec3f(0      ,    0, _length));

        buildMesh(mat);
    }
                      ~SLArrow  (){;}
               
   private:    
            SLfloat    _radius;     //!< radius of arrow cylinder
            SLfloat    _length;     //!< length of arrow
            SLfloat    _headLength; //!< length of arrow head
            SLfloat    _headWidth;  //!< width of arrow head
};
//-----------------------------------------------------------------------------
#endif //SLCYLINDER_H

