//
// Copyright 2017 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "pointsWrapper.h"

#include "context.h"
#include "UT_Gf.h"
#include "USD_Proxy.h"
#include "GT_VtArray.h"

#include <GT/GT_DANumeric.h>
#include <GT/GT_PrimPointMesh.h>
#include <GT/GT_Refine.h>
#include <GT/GT_RefineParms.h>
#include <GT/GT_GEOPrimPacked.h>

PXR_NAMESPACE_OPEN_SCOPE

using std::cerr;
using std::endl;

GusdPointsWrapper::
GusdPointsWrapper(
        const UsdStagePtr& stage,
        const SdfPath& path,
        bool isOverride )
{
    initUsdPrim( stage, path, isOverride );
}

GusdPointsWrapper::
GusdPointsWrapper(
        const GusdUSD_StageProxyHandle& stage, 
        const UsdGeomPoints& usdPoints, 
        const UsdTimeCode& time,
        const GusdPurposeSet& purposes )
    : GusdPrimWrapper( time, purposes )
    , m_usdPointsForRead( usdPoints, stage->GetLock() )
{
}

GusdPointsWrapper::
~GusdPointsWrapper()
{}

bool GusdPointsWrapper::
initUsdPrim(const UsdStagePtr& stage,
            const SdfPath& path,
            bool asOverride)
{
    if( asOverride ) {
        m_usdPointsForWrite = UsdGeomPoints(stage->OverridePrim( path ));
    }
    else {
        m_usdPointsForWrite = UsdGeomPoints::Define(stage, path );
    }
    return bool( m_usdPointsForWrite );
}

GT_PrimitiveHandle GusdPointsWrapper::
defineForWrite(
        const GT_PrimitiveHandle& sourcePrim,
        const UsdStagePtr& stage,
        const SdfPath& path,
        const GusdContext& ctxt)
{
    return new GusdPointsWrapper( stage, path, ctxt.getOverGeo( sourcePrim ));
}

GT_PrimitiveHandle GusdPointsWrapper::
defineForRead(
        const GusdUSD_StageProxyHandle& stage,
        const UsdGeomImageable&         sourcePrim, 
        const UsdTimeCode&              time,
        const GusdPurposeSet&           purposes )
{
    return new GusdPointsWrapper( 
                        stage, 
                        UsdGeomPoints( sourcePrim.GetPrim() ),
                        time,
                        purposes );
}

bool GusdPointsWrapper::
redefine( const UsdStagePtr& stage,
          const SdfPath& path,
          const GusdContext& ctxt,
          const GT_PrimitiveHandle& sourcePrim )
{
    initUsdPrim( stage, path, ctxt.getOverGeo( sourcePrim ));
    clearCaches();
    return true;
}

const UsdGeomImageable 
GusdPointsWrapper::getUsdPrimForRead(
    GusdUSD_ImageableHolder::ScopedLock &lock) const
{
    // obtain first lock to get geomtry as UsdGeomPoint.
    GusdUSD_PointsHolder::ScopedReadLock innerLock;
    innerLock.Acquire( m_usdPointsForRead );

    // Build new holder after casting to imageable
    GusdUSD_ImageableHolder tmp( UsdGeomImageable( (*innerLock).GetPrim() ),
                                 m_usdPointsForRead.GetLock() );
    lock.Acquire(tmp, /*write*/false);
    return *lock;
}

bool GusdPointsWrapper::
refine(GT_Refine& refiner, const GT_RefineParms* parms) const
{
    if(!isValid()) return false;

    bool refineForViewport = GT_GEOPrimPacked::useViewportLOD(parms);

    GusdUSD_PointsHolder::ScopedReadLock lock;
    lock.Acquire( m_usdPointsForRead );
    UsdGeomPoints points = *lock;   

    VtFloatArray vtFloatArray;
    VtIntArray   vtIntArray;
    VtVec3fArray vtVec3Array;
    
    GT_AttributeListHandle gtPointAttrs = new GT_AttributeList( new GT_AttributeMap() );
    GT_AttributeListHandle gtDetailAttrs = new GT_AttributeList( new GT_AttributeMap() );

    // point positions
    UsdAttribute pointsAttr = points.GetPointsAttr();
    if(!pointsAttr) {
        TF_WARN( "Invalid point attribute" );
        return false;
    }
    VtVec3fArray usdPoints;
    pointsAttr.Get(&usdPoints, m_time);
    auto gtPoints = new GusdGT_VtArray<GfVec3f>(usdPoints,GT_TYPE_POINT);
    gtPointAttrs = gtPointAttrs->addAttribute("P", gtPoints, true);

    if( !refineForViewport ) {
        // normals
        UsdAttribute normalsAttr = points.GetNormalsAttr();
        if(normalsAttr && normalsAttr.HasAuthoredValueOpinion()) {
            normalsAttr.Get(&vtVec3Array, m_time);
            if( vtVec3Array.size() < usdPoints.size() ) {
                TF_WARN( "Not enough values found for normals in %s. Expected %zd, got %zd.",
                         points.GetPrim().GetPath().GetText(),
                         usdPoints.size(), vtVec3Array.size() );
            }
            else {
                GT_DataArrayHandle gtNormals = 
                    new GusdGT_VtArray<GfVec3f>(vtVec3Array, GT_TYPE_NORMAL);
                gtPointAttrs = gtPointAttrs->addAttribute("N", gtNormals, true);
            }
        }

        // velocities
        UsdAttribute velAttr = points.GetVelocitiesAttr();
        if (velAttr && velAttr.HasAuthoredValueOpinion()) {
            velAttr.Get(&vtVec3Array, m_time);
            if( vtVec3Array.size() < usdPoints.size() ) {
                TF_WARN( "Not enough values found for velocities in %s. Expected %zd, got %zd.",
                         points.GetPrim().GetPath().GetText(),
                         usdPoints.size(), vtVec3Array.size() );
            }
            else {
                GT_DataArrayHandle gtVel = 
                        new GusdGT_VtArray<GfVec3f>(vtVec3Array, GT_TYPE_VECTOR);
                gtPointAttrs = gtPointAttrs->addAttribute("v", gtVel, true);
            }
        }
        
        // widths
        UsdAttribute widthsAttr = points.GetWidthsAttr();
        if( widthsAttr && widthsAttr.HasAuthoredValueOpinion()) {
            widthsAttr.Get(&vtFloatArray, m_time);
            if( vtFloatArray.size() < usdPoints.size() ) {
                TF_WARN( "Not enough values found for widths in %s. Expected %zd, got %zd.",
                         points.GetPrim().GetPath().GetText(),
                         usdPoints.size(), vtFloatArray.size() );
            }
            else {
                auto s = vtFloatArray.size();
                auto gtWidths = new GT_Real32Array( s, 1 );
                fpreal32 *d = gtWidths->data();
                for( size_t i = 0; i < s; ++i ) {
                    *d++ = vtFloatArray[i] * .5;
                }
                gtPointAttrs = gtPointAttrs->addAttribute("pscale", gtWidths, true);
            }
        }

        loadPrimvars( m_time, parms, 
              0, 
              usdPoints.size(),
              0,
              points.GetPath().GetString(),
              NULL,
              &gtPointAttrs,
              NULL,
              &gtDetailAttrs );
    }

    GT_PrimitiveHandle refinedPrimHandle
        = new GT_PrimPointMesh(gtPointAttrs,
                               gtDetailAttrs );

    refiner.addPrimitive(refinedPrimHandle);
    return true;
}


bool GusdPointsWrapper::
getUniqueID(int64& id) const
{
    static const int s_id = GT_Primitive::createPrimitiveTypeId();
    id = s_id;
    return true;
}


const char* GusdPointsWrapper::
className() const
{
    return "GusdPointsWrapper";
}


void GusdPointsWrapper::
enlargeBounds(UT_BoundingBox boxes[], int nsegments) const
{
    cerr << "GusdPointsWrapper::enlargeBounds NOT YET IMPLEMENTED" << endl;
    // TODO
}


int GusdPointsWrapper::
getMotionSegments() const
{
    // TODO
    return 1;
}


int64 GusdPointsWrapper::
getMemoryUsage() const
{
    // TODO
    return 0;
}


GT_PrimitiveHandle GusdPointsWrapper::
doSoftCopy() const
{
    // TODO
    return GT_PrimitiveHandle(new GusdPointsWrapper( *this ));
}


bool GusdPointsWrapper::isValid() const
{
    return m_usdPointsForWrite || m_usdPointsForRead;
}

bool GusdPointsWrapper::
updateFromGTPrim(const GT_PrimitiveHandle& sourcePrim,
                 const UT_Matrix4D&        houXform,
                 const GusdContext&        ctxt,
                 GusdSimpleXformCache&     xformCache )
{
    if( !m_usdPointsForWrite ) {
        return false;
    }

    bool writeNewGeo = !ctxt.getOverGeo( sourcePrim );

    GfMatrix4d xform = computeTransform( 
                            m_usdPointsForWrite.GetPrim().GetParent(),
                            ctxt.time,
                            houXform,
                            xformCache );

    GT_Owner attrOwner = GT_OWNER_INVALID;
    GT_DataArrayHandle houAttr;
    UsdAttribute usdAttr;
    
    // extent ------------------------------------------------------------------
    
    houAttr = GusdGT_Utils::getExtentsArray(sourcePrim);
    usdAttr = m_usdPointsForWrite.GetExtentAttr();
    updateAttributeFromGTPrim( GT_OWNER_INVALID, "extents", houAttr, usdAttr, ctxt.time );

    // transform ---------------------------------------------------------------

    updateTransformFromGTPrim( xform, ctxt.time, 
                               ctxt.granularity == GusdContext::PER_FRAME );

    // intrinsic attributes ----------------------------------------------------

    if( writeNewGeo && ctxt.purpose != UsdGeomTokens->default_ ) {
        m_usdPointsForWrite.GetPurposeAttr().Set( ctxt.purpose );
    }

    // visibility
    if( ctxt.granularity == GusdContext::PER_FRAME ) { 
        updateVisibilityFromGTPrim(sourcePrim, ctxt.time);
    }

    // P
    houAttr = sourcePrim->findAttribute("P", attrOwner, 0);
    usdAttr = m_usdPointsForWrite.GetPointsAttr();
    updateAttributeFromGTPrim( attrOwner, "P", houAttr, usdAttr, ctxt.time );
    
    // N
    houAttr = sourcePrim->findAttribute("N", attrOwner, 0);
    usdAttr = m_usdPointsForWrite.GetNormalsAttr();
    updateAttributeFromGTPrim( attrOwner, "N", houAttr, usdAttr, ctxt.time );

    // v
    houAttr = sourcePrim->findAttribute("v", attrOwner, 0);
    usdAttr = m_usdPointsForWrite.GetVelocitiesAttr();
    updateAttributeFromGTPrim( attrOwner, "v", houAttr, usdAttr, ctxt.time );    
    
    // pscale & width
    houAttr = sourcePrim->findAttribute("widths", attrOwner, 0);
    if(!houAttr) {
        houAttr = sourcePrim->findAttribute("pscale", attrOwner, 0);
        
        // If we found pscale, multiply values by 2 before converting to width.
        if(houAttr && houAttr->getTupleSize() == 1) {
            const GT_Size numVals = houAttr->entries();
            std::vector<fpreal32> pscaleArray(numVals);
            houAttr->fillArray(pscaleArray.data(), 0, numVals, 1); 

            std::transform(
                pscaleArray.begin(),
                pscaleArray.end(),
                pscaleArray.begin(),
                std::bind1st(std::multiplies<fpreal32>(), 2.0));

            houAttr.reset(new GT_Real32Array(pscaleArray.data(), numVals, 1));
        }
    }
    usdAttr = m_usdPointsForWrite.GetWidthsAttr();
    updateAttributeFromGTPrim( attrOwner, "widths", houAttr, usdAttr, ctxt.time );    

    // -------------------------------------------------------------------------
    
    // primvars ----------------------------------------------------------------

    GusdGT_AttrFilter filter = ctxt.attributeFilter;
    filter.appendPattern(GT_OWNER_POINT, "^P ^N ^v ^widths ^pscale ^visible ^usdactive");
    filter.appendPattern(GT_OWNER_CONSTANT, "^visible ^usdactive");
    if(const GT_AttributeListHandle pointAttrs = sourcePrim->getPointAttributes()) {
        GusdGT_AttrFilter::OwnerArgs owners;
        owners << GT_OWNER_POINT;
        filter.setActiveOwners(owners);
        updatePrimvarFromGTPrim( pointAttrs, filter, UsdGeomTokens->vertex, ctxt.time );
    }
    if(const GT_AttributeListHandle constAttrs = sourcePrim->getDetailAttributes()) {
        GusdGT_AttrFilter::OwnerArgs owners;
        owners << GT_OWNER_CONSTANT;
        filter.setActiveOwners(owners);
        updatePrimvarFromGTPrim( constAttrs, filter, UsdGeomTokens->constant, ctxt.time );
    }

    GT_Owner own;
    if(GT_DataArrayHandle Cd = sourcePrim->findAttribute( "Cd", own, 0 )) {
        GT_AttributeMapHandle attrMap = new GT_AttributeMap();
        GT_AttributeListHandle attrList = new GT_AttributeList( attrMap );
        attrList = attrList->addAttribute( "displayColor", Cd, true );
        GusdGT_AttrFilter filter( "*" );
        GusdGT_AttrFilter::OwnerArgs owners;
        owners << own;
        filter.setActiveOwners(owners);
        updatePrimvarFromGTPrim( attrList, filter, s_ownerToUsdInterp[own], ctxt.time );
    }

    // -------------------------------------------------------------------------
    return GusdPrimWrapper::updateFromGTPrim(sourcePrim, houXform, ctxt, xformCache);
}

PXR_NAMESPACE_CLOSE_SCOPE

